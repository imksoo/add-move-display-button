[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ExecutablePath,
    [Parameter(Mandatory=$true)][string]$DisplaySetupPath,
    [string]$OutputDirectory = 'real-app-evidence'
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
    foreach ($percent in @(150,200)) {
        $dpi = [int](96*$percent/100)
        $out = Join-Path $OutputDirectory "settings-dpi-$dpi"
        $null = New-Item -ItemType Directory -Force $out
        # Keep at least 720 effective vertical pixels at both scales.
        $width = if ($percent -eq 200) { 2560 } else { 1920 }
        $height = if ($percent -eq 200) { 1440 } else { 1080 }
        & $setup $percent $width $height | Tee-Object -FilePath (Join-Path $out 'display-scale.json')
        if ($LASTEXITCODE -ne 0) { throw "Cannot configure $percent percent display scale." }
        & powershell.exe -NoProfile -File (Join-Path $PSScriptRoot 'Test-DesktopReadiness.ps1') -OutputDirectory $out
        if ($LASTEXITCODE -ne 0) { throw "Desktop preflight failed at $percent percent." }
        & powershell.exe -NoProfile -File (Join-Path $PSScriptRoot 'Test-RealApps.ps1') -ExecutablePath $exe -OutputDirectory $out -RequireWindows11Coverage -SettingsOnly -ExpectedDpi $dpi
        if ($LASTEXITCODE -ne 0) { throw "Settings alignment failed at $percent percent." }
    }
} finally {
    & $setup $original.percent $original.width $original.height
    if ($LASTEXITCODE -ne 0) { throw 'Cannot restore original display configuration.' }
}
