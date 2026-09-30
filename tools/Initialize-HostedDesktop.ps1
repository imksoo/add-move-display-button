[CmdletBinding()]
param([string]$OutputDirectory='real-app-evidence/desktop-setup', [switch]$PrepareApplicationTests)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
# Environment provisioning is separate from the unmodified read-only pixel gate.
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted' -or $env:ImageOS -notin @('win11-arm64','win11-vs2026-arm64')) {
    throw 'Desktop setup is restricted to disposable GitHub-hosted Windows 11 Arm runners.'
}
$os=Get-CimInstance Win32_OperatingSystem
if ($os.ProductType -ne 1 -or [int]$os.BuildNumber -lt 22000) { throw 'Windows 11 client required.' }
$out=[IO.Path]::GetFullPath($OutputDirectory)
$null=New-Item -ItemType Directory -Force $out
$report=[ordered]@{ stage='starting'; success=$false; image=$env:ImageOS; imageVersion=$env:ImageVersion; commit=$env:GITHUB_SHA; before=$null; after=$null; policies=@(); closeAction=$null; focusAction=$null; wsl=$null; error=$null }
function Save-Setup { $report | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $out 'desktop-setup.json') -Encoding UTF8 }
function Set-PrivacyPolicy([string]$path,[string]$name,[int]$value) {
    $old=Get-ItemProperty -LiteralPath $path -Name $name -ErrorAction SilentlyContinue
    $previous=if ($null -eq $old) { $null } else { $old.$name }
    $null=New-Item -Path $path -Force
    $null=New-ItemProperty -LiteralPath $path -Name $name -Value $value -PropertyType DWord -Force
    $actual=(Get-ItemProperty -LiteralPath $path -Name $name).$name
    if ($actual -ne $value) { throw "Policy write did not persist: $path / $name" }
    $report.policies+=@{path=$path;name=$name;before=$previous;after=$actual}
    Save-Setup
}
Save-Setup
try {
    Add-Type -Path (Join-Path $PSScriptRoot '../tests/desktop/HostedDesktopSetup.cs') -ReferencedAssemblies System.dll,System.Drawing.dll,System.Windows.Forms.dll
    $report.before=[HostedDesktopSetup]::Observe()
    Save-Setup
    [HostedDesktopSetup]::Capture((Join-Path $out 'before.png'))
    if ($null -ne $report.before) { [HostedDesktopSetup]::Validate($report.before) }
    # Microsoft policy mappings: Privacy, Experience, TextInput and System CSPs.
    # Disable optional collection explicitly before suppressing the setup UI.
    Set-PrivacyPolicy 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\DataCollection' 'AllowTelemetry' 1
    Set-PrivacyPolicy 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\TextInput' 'AllowLinguisticDataCollection' 0
    Set-PrivacyPolicy 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\AppPrivacy' 'LetAppsAccessLocation' 2
    Set-PrivacyPolicy 'HKLM:\SOFTWARE\Policies\Microsoft\FindMyDevice' 'AllowFindMyDevice' 0
    Set-PrivacyPolicy 'HKCU:\SOFTWARE\Policies\Microsoft\Windows\CloudContent' 'DisableTailoredExperiencesWithDiagnosticData' 1
    Set-PrivacyPolicy 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\AdvertisingInfo' 'DisabledByGroupPolicy' 1
    Set-PrivacyPolicy 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\OOBE' 'DisablePrivacyExperience' 1
    Set-PrivacyPolicy 'HKCU:\SOFTWARE\Policies\Microsoft\Windows\OOBE' 'DisablePrivacyExperience' 1
    $report.stage='policy-applied'; Save-Setup
    if ($null -ne $report.before) {
        $report.closeAction=[HostedDesktopSetup]::CloseVerified($report.before)
        Save-Setup
    }
    for ($i=0;$i -lt 30;$i++) {
        Start-Sleep -Milliseconds 100
        if ($null -ne [HostedDesktopSetup]::Observe()) { throw 'Setup UI reappeared after policy application.' }
    }
    if ($PrepareApplicationTests) {
        # This image's WSL bootstrap opens unsolicited update-prompt terminals.
        # Repair the prerequisite before any product observation, never dismiss
        # foreground windows or retry failed cases during the pixel tests.
        $report.stage='wsl-prerequisite'; Save-Setup
        $report.wsl=[ordered]@{ before=@(Get-CimInstance Win32_Process -Filter "Name='wsl.exe' OR Name='WindowsTerminal.exe'" | Select-Object Name,ProcessId,ParentProcessId,ExecutablePath,CommandLine); updateExit=$null; versionExit=$null }
        Save-Setup
        $wsl=Join-Path $env:SystemRoot 'System32/wsl.exe'
        $update=Start-Process -FilePath $wsl -ArgumentList '--update --web-download' -NoNewWindow -PassThru -RedirectStandardOutput (Join-Path $out 'wsl-update.stdout.txt') -RedirectStandardError (Join-Path $out 'wsl-update.stderr.txt')
        if (-not $update.WaitForExit(240000)) { $update.Kill(); throw 'Test-owned WSL prerequisite update exceeded four minutes.' }
        $update.WaitForExit(); $report.wsl.updateExit=$update.ExitCode; $update.Dispose(); Save-Setup
        if ($report.wsl.updateExit -ne 0) { throw 'WSL prerequisite update failed; see captured output.' }
        $version=Start-Process -FilePath $wsl -ArgumentList '--version' -NoNewWindow -PassThru -RedirectStandardOutput (Join-Path $out 'wsl-version.stdout.txt') -RedirectStandardError (Join-Path $out 'wsl-version.stderr.txt')
        if (-not $version.WaitForExit(10000)) { $version.Kill(); throw 'Updated WSL version query was not noninteractive.' }
        $version.WaitForExit(); $report.wsl.versionExit=$version.ExitCode; $version.Dispose(); Save-Setup
        if ($report.wsl.versionExit -ne 0) { throw 'Updated WSL failed its version query.' }
        # Existing bootstrap prompts expire themselves after 60 seconds. Do not
        # close unrelated terminals or change the product observation duration.
        $deadline=[DateTime]::UtcNow.AddSeconds(70)
        while (@(Get-Process -Name wsl -ErrorAction SilentlyContinue).Count) {
            if ([DateTime]::UtcNow -ge $deadline) { throw 'A WSL bootstrap prompt remains after prerequisite update.' }
            Start-Sleep -Milliseconds 500
        }
    }
    $report.after=[HostedDesktopSetup]::Observe()
    $report.focusAction=[HostedDesktopSetup]::InitializeInput()
    [HostedDesktopSetup]::Capture((Join-Path $out 'after.png'))
    $report.success=$true
} catch { $report.error=$_.Exception.ToString(); throw }
finally { $report.stage='complete'; Save-Setup; $report | ConvertTo-Json -Depth 8 }
# Success here means provisioning completed, never that screen capture passed.
# Every caller MUST run Test-DesktopReadiness.ps1 afterwards in this same job.
