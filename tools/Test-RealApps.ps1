[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ExecutablePath,
    [string]$OutputDirectory = 'real-app-evidence',
    [switch]$AllowDesktopCapture,
    [switch]$RequireWindows11Coverage
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
# Only capture a disposable hosted desktop by default. Never silently capture a user's session.
if (-not $AllowDesktopCapture -and $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'Use a disposable desktop; non-hosted runs require explicit -AllowDesktopCapture.'
}
$exe = (Resolve-Path -LiteralPath $ExecutablePath).Path
$out = [IO.Path]::GetFullPath($OutputDirectory)
$null = New-Item -ItemType Directory -Force $out
if (@(Get-Process -Name MoveToMonitorButton -ErrorAction SilentlyContinue).Count) {
    throw 'A pre-existing MoveToMonitorButton is running. Refusing to stop or interfere with it.'
}
$probe = Join-Path $PSScriptRoot '../tests/desktop/DesktopProbe.cs'
Add-Type -Path $probe -ReferencedAssemblies System.Drawing.dll
if ($RequireWindows11Coverage) {
    if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') { throw 'Hosted input setup is restricted to disposable GitHub runners.' }
    Add-Type -Path (Join-Path $PSScriptRoot '../tests/desktop/HostedDesktopSetup.cs') -ReferencedAssemblies System.dll,System.Drawing.dll,System.Windows.Forms.dll
}
[DesktopProbe]::PhysicalCoordinates()
$work = [DesktopProbe]::WorkArea()
$chromeProfile = Join-Path ([IO.Path]::GetTempPath()) ('mtmb-chrome-'+[guid]::NewGuid().ToString('N'))
$oldCursor = New-Object DesktopProbe+Point
$null = [DesktopProbe]::GetCursorPos([ref]$oldCursor)
$os = Get-CimInstance Win32_OperatingSystem
$packages = @(Get-AppxPackage | Where-Object { $_.Name -match 'WindowsNotepad|WindowsStore|WindowsCalculator' })
$evidence = [ordered]@{
    schemaVersion = 1
    commit = $env:GITHUB_SHA
    executableSha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    os = $os.Caption; build = $os.BuildNumber
    hostArchitecture = $env:PROCESSOR_ARCHITECTURE
    executableArchitecture = 'x64'
    imageVersion = $env:ImageVersion
    sessionId = [Diagnostics.Process]::GetCurrentProcess().SessionId
    userInteractive = [Environment]::UserInteractive
    chromeVersion = $null
    workArea = $work
    packages = @($packages | Select-Object Name,@{n='Version';e={$_.Version.ToString()}},PackageFullName)
    scope = 'Actual EXE; real apps and explicitly labelled framework fixtures; one monitor override; no physical monitor movement.'
    cases = New-Object System.Collections.Generic.List[object]
    fatalError = $null
}
function Require([bool]$condition,[string]$message) { if (-not $condition) { throw $message } }
function Save-Evidence {
    $evidence | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath (Join-Path $out 'results.json') -Encoding UTF8
}
function Stop-Utility($process) {
    if ($null -eq $process) { return }
    if (-not $process.HasExited) {
        $hostWindow = @([DesktopProbe]::Windows() | Where-Object { $_.Pid -eq $process.Id -and $_.Class -eq 'MoveToMonitorButton.Host.v1' })
        if ($hostWindow.Count) { $null = [DesktopProbe]::PostMessage([IntPtr]$hostWindow[0].Handle,0x10,[IntPtr]::Zero,[IntPtr]::Zero) }
        if (-not $process.WaitForExit(5000)) { $process.Kill(); $process.WaitForExit(); throw 'Test-owned utility failed to exit normally.' }
    }
    $process.WaitForExit()
    Require ($process.ExitCode -eq 0) "Utility exited with code $($process.ExitCode)"
    $process.Dispose()
}
function Get-NewTarget($before,$spec,$launched) {
    $deadline = [DateTime]::UtcNow.AddSeconds(35)
    $last = 0L; $stable = 0
    do {
        $selected = $null
        $candidates = @([DesktopProbe]::Windows() | Where-Object { $_.Visible -and $_.Cloaked -eq 0 -and $_.Bounds.Width -gt 250 -and $_.Bounds.Height -gt 100 -and $before -notcontains $_.Handle })
        foreach ($w in $candidates) {
            $match = $false
            if ($spec.Kind -eq 'Framework' -and $w.Pid -eq $launched.Id) {
                $match = ($spec.Framework -eq 'WPF' -and $w.Class -like 'HwndWrapper*') -or ($spec.Framework -eq 'WinForms' -and $w.Class -like 'WindowsForms10.Window*')
            }
            elseif ($spec.Kind -eq 'Explorer') { $match = $w.Class -eq 'CabinetWClass' }
            elseif ($spec.Kind -eq 'Notepad') { $match = $w.Process -match '^notepad$' }
            elseif ($spec.Kind -eq 'Chrome') { $match = $w.Process -eq 'chrome' -and $w.Class -like 'Chrome_WidgetWin*' }
            elseif ($spec.Kind -eq 'Mmc') { $match = $w.Process -eq 'mmc' }
            elseif ($spec.Kind -eq 'Package') {
                $match = $w.Package -like "$($spec.Package)_*"
                if (-not $match -and $w.Class -eq 'ApplicationFrameWindow') {
                    $caption = [DesktopProbe]::Caption([IntPtr]$w.Handle)
                    $match = @($caption.Children | Where-Object { $_.Package -like "$($spec.Package)_*" }).Count -gt 0
                }
            }
            if ($match) {
                # CoreWindow can initially appear top-level and then be reparented.
                # Never move/test the child content as though it were the app frame.
                $caption = [DesktopProbe]::Caption([IntPtr]$w.Handle)
                if (($caption.Style -band 0x40000000) -ne 0) { continue }
                $selected = $w; break
            }
        }
        if ($null -ne $selected -and $selected.Handle -eq $last) { $stable++ } else { $stable=0 }
        if ($null -ne $selected) { $last = $selected.Handle } else { $last=0 }
        if ($stable -ge 6) { return $selected }
        Start-Sleep -Milliseconds 300
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'The requested application did not create a stable new identifiable top-level window.'
}
function Test-WindowState($spec,$target,[string]$mode) {
    $hwnd = [IntPtr]$target.Handle
    $result = [ordered]@{ app=$spec.Name; kind=$spec.Kind; state=$mode; status='failed'; reason=$null; window=$target; caption=$null; overlay=$null; nativeSizeMatched=$null; pixelGlyph=$null; pixelHover=$null; stableSamples=0; hitPoints=0; screenshots=@(); focusSetup=$null; foreground=$null; hitObservations=@() }
    $app = $null
    try {
        Require ([DesktopProbe]::IsWindow($hwnd)) 'Identified target window no longer exists.'
        if ($mode -eq 'maximized') { $null = [DesktopProbe]::ShowWindowAsync($hwnd,3) }
        else {
            $null = [DesktopProbe]::ShowWindowAsync($hwnd,9)
            Start-Sleep -Milliseconds 350
            $width = [Math]::Min($(if ($mode -eq 'restored-narrow') { 660 } else { 950 }),$work.Width-80)
            $null = [DesktopProbe]::SetWindowPos($hwnd,[IntPtr]::Zero,$work.Left+35,$work.Top+40,$width,[Math]::Min(520,$work.Height-100),0x4014)
        }
        Start-Sleep -Milliseconds 650
        $null = [DesktopProbe]::SetCursorPos($work.Left+10,$work.Bottom-50)
        if ($RequireWindows11Coverage) { $result.focusSetup=[HostedDesktopSetup]::InitializeInput() }
        for ($focusAttempt=0;$focusAttempt -lt 5;$focusAttempt++) {
            $null = [DesktopProbe]::SetForegroundWindow($hwnd)
            Start-Sleep -Milliseconds 350
            if ([DesktopProbe]::GetForegroundWindow() -eq $hwnd) { break }
        }
        $result.foreground=[DesktopProbe]::Describe([DesktopProbe]::GetForegroundWindow())
        Require ([DesktopProbe]::GetForegroundWindow() -eq $hwnd) 'Test cannot obtain target foreground; not an overlay verdict.'
        Require ([DesktopProbe]::IsZoomed($hwnd) -eq ($mode -eq 'maximized')) 'Target did not reach the requested window state.'
        $caption = [DesktopProbe]::Caption($hwnd)
        $result.caption = $caption
        $prefix = "$($spec.Name)-$mode"
        $baseline = Join-Path $out "$prefix-baseline.png"
        $normal = Join-Path $out "$prefix-normal.png"
        $hover = Join-Path $out "$prefix-hover.png"
        $capture = [DesktopProbe]::CaptureCaption($hwnd,$baseline)
        $result.screenshots += [IO.Path]::GetFileName($baseline)
        $app = Start-Process -FilePath $exe -ArgumentList '--show-on-single-monitor' -PassThru
        $deadline = [DateTime]::UtcNow.AddSeconds(12)
        $overlay = $null; $candidate = $null; $settled = 0
        do {
            Start-Sleep -Milliseconds 250
            $app.Refresh()
            Require (-not $app.HasExited) 'Utility exited while waiting for the overlay.'
            $found = @([DesktopProbe]::Windows() | Where-Object { $_.Pid -eq $app.Id -and $_.Class -eq 'MoveToMonitorButton.Overlay.v1' -and $_.Visible })
            if ($found.Count) {
                $current=$found[0]
                if ($null -ne $candidate -and $candidate.Handle -eq $current.Handle -and $candidate.Bounds.Same($current.Bounds)) { $settled++ } else { $settled=0 }
                $candidate=$current
                if ($settled -ge 2) { $overlay=$current; break }
            } else { $candidate=$null; $settled=0 }
        } while ([DateTime]::UtcNow -lt $deadline)
        $null = [DesktopProbe]::CaptureCaption($hwnd,$normal)
        $result.screenshots += [IO.Path]::GetFileName($normal)
        Require ($null -ne $overlay) 'Overlay not visible on the real application.'
        $bounds = [DesktopProbe]::Bounds([IntPtr]$overlay.Handle)
        $result.overlay = $bounds
        Require ($caption.Window.Contains($bounds)) 'Overlay outside target bounds.'
        Require ($work.Contains($bounds)) 'Overlay outside monitor work area.'
        if ($caption.DwmControlsOk) { Require (-not $bounds.Overlaps($caption.Controls)) 'Overlay overlaps the native caption controls.' }
        if ($spec.RequireNativeMatch) { Require $caption.TitleQueryOk 'Required native caption measurement failed.' }
        if ($caption.TitleQueryOk) {
            $validButtons = @(2,3,5 | Where-Object { $caption.Title.Buttons[$_].Width -gt 0 -and $caption.Title.Buttons[$_].Height -gt 0 -and (($caption.Title.States[$_] -band 0x18000) -eq 0) })
            if ($spec.RequireNativeMatch) { Require ($validButtons.Count -gt 0) 'Required native caption measurement is empty.' }
            if ($validButtons.Count) {
                $reference = $caption.Title.Buttons[$validButtons[0]]
                $matched = $bounds.Width -eq $reference.Width -and $bounds.Top -eq $reference.Top -and $bounds.Bottom -eq $reference.Bottom
                $result.nativeSizeMatched = $matched
                if ($spec.RequireNativeMatch) { Require $matched 'Overlay did not match the required native caption-button width and vertical alignment.' }
            }
        }
        $oh = [IntPtr]$overlay.Handle
        $points = @(
            [DesktopProbe+Point]::new(($bounds.Left+3),($bounds.Top+3)),
            [DesktopProbe+Point]::new(($bounds.Right-4),($bounds.Top+3)),
            [DesktopProbe+Point]::new(($bounds.Left+3),($bounds.Bottom-4)),
            [DesktopProbe+Point]::new(($bounds.Right-4),($bounds.Bottom-4)),
            [DesktopProbe+Point]::new(($bounds.Left+[int]($bounds.Width/2)),($bounds.Top+[int]($bounds.Height/2)))
        )
        foreach ($point in $points) {
            $hitWindow=[DesktopProbe]::WindowFromPoint($point)
            $result.hitObservations += [pscustomobject]@{ point=$point; window=[DesktopProbe]::Describe($hitWindow); overlayBounds=[DesktopProbe]::Bounds($oh) }
            Require ($hitWindow -eq $oh) 'Overlay has a click-through hole or is occluded.'
            Require ([DesktopProbe]::Hit($oh,$point.X,$point.Y) -eq 1) 'Overlay does not return HTCLIENT.'
            $result.hitPoints++
        }
        for ($i=0;$i -lt 8;$i++) {
            Start-Sleep -Milliseconds 250
            Require ([DesktopProbe]::IsWindowVisible($oh) -and [DesktopProbe]::Bounds($oh).Same($bounds)) 'Overlay disappears or shifts during steady state.'
            Require ([DesktopProbe]::GetForegroundWindow() -eq $hwnd) 'Foreground changed during the observation.'
            $result.stableSamples++
        }
        $null = [DesktopProbe]::CaptureCaption($hwnd,$normal)
        $glyph = [DesktopProbe]::Compare($baseline,$normal,$bounds,$capture)
        $result.pixelGlyph = $glyph
        Require ($glyph.Changed -ge 6) 'No visible move glyph in the actual screen pixels.'
        $null = [DesktopProbe]::SetCursorPos($points[4].X,$points[4].Y)
        Start-Sleep -Milliseconds 450
        $null = [DesktopProbe]::CaptureCaption($hwnd,$hover)
        $result.screenshots += [IO.Path]::GetFileName($hover)
        $hot = [DesktopProbe]::Compare($normal,$hover,$bounds,$capture)
        $result.pixelHover = $hot
        Require ($hot.Changed -ge 6) 'Hover did not visibly change the overlay.'
        Require ([DesktopProbe]::Responsive($oh)) 'Overlay message loop is unresponsive.'
        $result.status = 'passed'
    } catch { $result.reason = $_.Exception.Message }
    finally {
        try { Stop-Utility $app } catch { $result.status='failed'; $result.reason="$($result.reason) Cleanup: $($_.Exception.Message)" }
        $null = [DesktopProbe]::SetCursorPos($work.Left+10,$work.Bottom-50)
        $evidence.cases.Add([pscustomobject]$result)
        Save-Evidence
        Write-Host "$($result.app) / $mode : $($result.status) $($result.reason)"
    }
}
$chromeCandidates = @(
    (Join-Path $env:ProgramFiles 'Google/Chrome/Application/chrome.exe'),
    (Join-Path ${env:ProgramFiles(x86)} 'Google/Chrome/Application/chrome.exe')
)
$chromePath = $chromeCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if ($chromePath) { $evidence.chromeVersion = (Get-Item -LiteralPath $chromePath).VersionInfo.FileVersion }
$hasModernNotepad = @($packages | Where-Object Name -eq 'Microsoft.WindowsNotepad').Count -gt 0
$specs = @(
    @{Name='Notepad'; Kind=$(if ($hasModernNotepad) {'Package'} else {'Notepad'}); Package='Microsoft.WindowsNotepad'; RequireNativeMatch=(-not $hasModernNotepad)},
    @{Name='Explorer'; Kind='Explorer'; Package=$null; RequireNativeMatch=$true},
    @{Name='Chrome'; Kind='Chrome'; Package=$null; RequireNativeMatch=$false},
    @{Name='TaskScheduler'; Kind='Mmc'; Package=$null; RequireNativeMatch=$true},
    @{Name='MicrosoftStore'; Kind='Package'; Package='Microsoft.WindowsStore'; RequireNativeMatch=$false},
    @{Name='Calculator'; Kind='Package'; Package='Microsoft.WindowsCalculator'; RequireNativeMatch=$false},
    @{Name='WPF-fixture'; Kind='Framework'; Framework='WPF'; Package=$null; RequireNativeMatch=$true},
    @{Name='WinForms-fixture'; Kind='Framework'; Framework='WinForms'; Package=$null; RequireNativeMatch=$true}
)
try {
    Require ([Environment]::UserInteractive) 'No interactive desktop.'
    Save-Evidence
    if ($RequireWindows11Coverage) {
        Require ($os.ProductType -eq 1 -and [int]$os.BuildNumber -ge 22000) 'Windows 11 client required for modern-app coverage.'
        foreach ($required in @('Microsoft.WindowsNotepad','Microsoft.WindowsStore','Microsoft.WindowsCalculator')) {
            Require (@($packages | Where-Object Name -eq $required).Count -gt 0) "Required modern package missing: $required"
        }
    }
    foreach ($spec in $specs) {
        $launched = $null; $target = $null
        if ($spec.Kind -eq 'Package' -and @($packages | Where-Object Name -eq $spec.Package).Count -eq 0) {
            $evidence.cases.Add([pscustomobject]@{app=$spec.Name; kind=$spec.Kind; state='all'; status='unavailable'; reason='Package not installed for this runner user; not counted as passed.'})
            Save-Evidence
            continue
        }
        try {
            $before = @([DesktopProbe]::Windows() | ForEach-Object Handle)
            switch ($spec.Kind) {
                'Chrome' {
                    Require ([bool]$chromePath) 'Google Chrome is required for this hosted desktop suite.'
                    $profile = $chromeProfile
                    $page = Join-Path $out 'chrome-test.html'
                    '<!doctype html><meta charset="utf-8"><title>MTMB isolated Chrome test</title><p>Synthetic local test page. No account or user data.</p>' | Set-Content -LiteralPath $page -Encoding UTF8
                    $uri = ([Uri]$page).AbsoluteUri
                    $launched = Start-Process $chromePath -ArgumentList ('--user-data-dir="'+$profile+'" --no-first-run --no-default-browser-check --disable-background-networking --new-window "'+$uri+'"') -PassThru
                }
                'Notepad' {
                    $sample = Join-Path $out 'caption-test-content.txt'
                    'Synthetic test file; no user content.' | Set-Content -LiteralPath $sample -Encoding UTF8
                    $launched = Start-Process notepad.exe -ArgumentList ('"'+$sample+'"') -PassThru
                }
                'Explorer' {
                    $folder = Join-Path $out 'empty-explorer-fixture'
                    $null = New-Item -ItemType Directory -Force $folder
                    $launched = Start-Process explorer.exe -ArgumentList ('"'+$folder+'"') -PassThru
                }
                'Mmc' { $launched = Start-Process mmc.exe -ArgumentList taskschd.msc -PassThru }
                'Package' {
                    $package = $packages | Where-Object Name -eq $spec.Package | Select-Object -First 1
                    $manifest = Get-AppxPackageManifest $package.PackageFullName
                    $appId = @($manifest.Package.Applications.Application)[0].Id
                    $launched = Start-Process explorer.exe -ArgumentList ('shell:AppsFolder\'+$package.PackageFamilyName+'!'+$appId) -PassThru
                }
                'Framework' {
                    $fixture = (Resolve-Path (Join-Path $PSScriptRoot '../tests/desktop/FrameworkFixture.ps1')).Path
                    $launched = Start-Process (Join-Path $PSHOME 'powershell.exe') -ArgumentList ('-NoProfile -STA -File "'+$fixture+'" -Framework '+$spec.Framework) -PassThru
                }
            }
            $target = Get-NewTarget $before $spec $launched
            foreach ($mode in @('normal','maximized','restored-narrow')) { Test-WindowState $spec $target $mode }
        } catch {
            [DesktopProbe]::Windows() | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $out "$($spec.Name)-launch-windows.json") -Encoding UTF8
            $evidence.cases.Add([pscustomobject]@{app=$spec.Name; kind=$spec.Kind; state='launch'; status='failed'; reason=$_.Exception.Message})
            Save-Evidence
            Write-Host "$($spec.Name) launch failed: $($_.Exception.Message)"
        } finally {
            # Close only a newly identified test window; never kill Explorer or unrelated app processes.
            if ($null -ne $target -and [DesktopProbe]::IsWindow([IntPtr]$target.Handle)) {
                $null = [DesktopProbe]::PostMessage([IntPtr]$target.Handle,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
                Start-Sleep -Milliseconds 350
            }
            if ($null -ne $launched) { $launched.Dispose() }
        }
    }
} catch { $evidence.fatalError = $_.Exception.Message; throw }
finally {
    $null = [DesktopProbe]::SetCursorPos($oldCursor.X,$oldCursor.Y)
    $profile = $chromeProfile
    if (Test-Path -LiteralPath $profile) { Remove-Item -LiteralPath $profile -Recurse -Force -ErrorAction SilentlyContinue }
    Save-Evidence
    $summary = @('# Real application caption results','',"OS: $($evidence.os), build $($evidence.build), host $($evidence.hostArchitecture); executable x64.",'', '| App | State | Result | Reason |','|---|---|---|---|')
    foreach ($case in $evidence.cases) { $summary += "| $($case.app) | $($case.state) | $($case.status) | $($case.reason) |" }
    $summary += @('', 'A geometry/pixel pass is not a claim of pixel-identical DWM styling or physical multi-monitor movement. Screenshots require review. Missing packages are unavailable, never passed.')
    $summary | Set-Content -LiteralPath (Join-Path $out 'SUMMARY.md') -Encoding UTF8
    if ($env:GITHUB_STEP_SUMMARY) { $summary | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8 }
}
$failures = @($evidence.cases | Where-Object status -eq 'failed')
Require ($failures.Count -eq 0) "$($failures.Count) real-app test cases failed. See results.json and screenshots."
if ($RequireWindows11Coverage) {
    foreach ($required in @('Notepad','Explorer','MicrosoftStore','Calculator')) {
        foreach ($mode in @('normal','maximized','restored-narrow')) {
            $matched=@($evidence.cases | Where-Object { $_.app -eq $required -and $_.state -eq $mode -and $_.status -eq 'passed' })
            Require ($matched.Count -eq 1) "Required Windows 11 coverage missing: $required / $mode"
        }
    }
}
