[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ExecutablePath,
    [Parameter(Mandatory=$true)][string]$DisplaySetupPath,
    [string]$OutputDirectory = 'real-app-evidence',
    [switch]$ChatGPTOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'Display changes are restricted to disposable GitHub-hosted runners.'
}
$setup = (Resolve-Path -LiteralPath $DisplaySetupPath).Path
$exe = (Resolve-Path -LiteralPath $ExecutablePath).Path
$original = & $setup | ConvertFrom-Json
if ($LASTEXITCODE -ne 0) { throw 'Cannot read original display configuration.' }
try {
    foreach ($percent in @(200,150)) {
        $dpi = [int](96*$percent/100)
        $out = Join-Path $OutputDirectory "settings-dpi-$dpi"
        $null = New-Item -ItemType Directory -Force $out
        # Try only modes observed on this host. Different orientations can
        # expose different scale ranges; the actual window DPI is checked below.
        $modes = @(@{Width=1920; Height=1080; Orientation=0})
        if ($percent -eq 200) {
            $modes = @(@{Width=1600; Height=1200; Orientation=0}, @{Width=1080; Height=1920; Orientation=1}, @{Width=1200; Height=1600; Orientation=1})
        }
        $configured = $false
        foreach ($mode in $modes) {
            & $setup $percent $mode.Width $mode.Height $mode.Orientation | Tee-Object -FilePath (Join-Path $out 'display-scale.json')
            if ($LASTEXITCODE -eq 0) { $configured = $true; break }
        }
        if (-not $configured) { throw "Cannot configure $percent percent display scale in any supported mode." }
        & powershell.exe -NoProfile -File (Join-Path $PSScriptRoot 'Test-DesktopReadiness.ps1') -OutputDirectory $out
        if ($LASTEXITCODE -ne 0) { throw "Desktop preflight failed at $percent percent." }
        $selection = if ($ChatGPTOnly) { '-ChatGPTOnly' } else { '-SettingsOnly' }
        & powershell.exe -NoProfile -File (Join-Path $PSScriptRoot 'Test-RealApps.ps1') -ExecutablePath $exe -OutputDirectory $out -RequireWindows11Coverage $selection -ExpectedDpi $dpi
        if ($LASTEXITCODE -ne 0) { throw "Caption alignment failed at $percent percent ($selection)." }
    }
} finally {
    & $setup $original.percent $original.width $original.height $original.orientation
    if ($LASTEXITCODE -ne 0) { throw 'Cannot restore original display configuration.' }
}
