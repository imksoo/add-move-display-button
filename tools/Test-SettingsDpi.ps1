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
    foreach ($percent in @(200,150)) {
        $dpi = [int](96*$percent/100)
        $out = Join-Path $OutputDirectory "settings-dpi-$dpi"
        $null = New-Item -ItemType Directory -Force $out
        # Try only modes observed on this host. Different orientations can
        # expose different scale ranges; the actual window DPI is checked below.
        $modes = if ($percent -eq 200) { @(@(1600,1200,0),@(1080,1920,1),@(1200,1600,1)) } else { ,@(1920,1080,0) }
        $configured = $false
        foreach ($mode in $modes) {
            & $setup $percent $mode[0] $mode[1] $mode[2] | Tee-Object -FilePath (Join-Path $out 'display-scale.json')
            if ($LASTEXITCODE -eq 0) { $configured = $true; break }
        }
        if (-not $configured) { throw "Cannot configure $percent percent display scale in any supported mode." }
        & powershell.exe -NoProfile -File (Join-Path $PSScriptRoot 'Test-DesktopReadiness.ps1') -OutputDirectory $out
        if ($LASTEXITCODE -ne 0) { throw "Desktop preflight failed at $percent percent." }
        & powershell.exe -NoProfile -File (Join-Path $PSScriptRoot 'Test-RealApps.ps1') -ExecutablePath $exe -OutputDirectory $out -RequireWindows11Coverage -SettingsOnly -ExpectedDpi $dpi
        if ($LASTEXITCODE -ne 0) { throw "Settings alignment failed at $percent percent." }
    }
} finally {
    & $setup $original.percent $original.width $original.height $original.orientation
    if ($LASTEXITCODE -ne 0) { throw 'Cannot restore original display configuration.' }
}
