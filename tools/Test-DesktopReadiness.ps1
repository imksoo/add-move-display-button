[CmdletBinding()]
param([string]$OutputDirectory='real-app-evidence', [switch]$AllowDesktopCapture)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if (-not $AllowDesktopCapture -and $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'Use a disposable desktop; non-hosted capture requires explicit -AllowDesktopCapture.'
}
# Diagnostic only: no registry changes, process termination, desktop switching,
# authentication changes, or attempts to dismiss Windows setup/security screens.
$out=[IO.Path]::GetFullPath($OutputDirectory)
$null=New-Item -ItemType Directory -Force $out
$os=Get-CimInstance Win32_OperatingSystem
$result=[ordered]@{
    status='environment-blocked'; reason=$null; commit=$env:GITHUB_SHA
    os=$os.Caption; build=$os.BuildNumber; architecture=$env:PROCESSOR_ARCHITECTURE
    imageVersion=$env:ImageVersion; imageOS=$env:ImageOS; runId=$env:GITHUB_RUN_ID; runAttempt=$env:GITHUB_RUN_ATTEMPT
    sessionId=[Diagnostics.Process]::GetCurrentProcess().SessionId
    userInteractive=[Environment]::UserInteractive
    observations=$null; legacyPixelPass=$false; diagnosticFile='desktop-diagnostic.json'
    packages=@(Get-AppxPackage | Where-Object { $_.Name -match 'WindowsNotepad|WindowsStore|WindowsCalculator' } | Select-Object Name,Version,PackageFullName)
    scope='Observes an owned test window and screen pixels. Does not modify Windows configuration.'
}
try {
    $probe=Join-Path $PSScriptRoot '../tests/desktop/DesktopReadiness.cs'
    Add-Type -Path $probe -ReferencedAssemblies System.dll,System.Core.dll,System.Windows.Forms.dll,System.Drawing.dll,System.Web.Extensions.dll
    if (-not [Environment]::UserInteractive) { throw 'No interactive desktop.' }
    $diagnostic=[DesktopReadiness]::Observe($out)
    $samples=@($diagnostic.Original)
    $result.observations=$samples
    $result.legacyPixelPass=$diagnostic.LegacyPixelPass
    if ($samples.Count -ne 2 -or @($samples | Where-Object { -not $_.PixelMatches }).Count) {
        throw 'The visible screen does not show both colors of the owned sentinel. Real-app rendering cannot be judged in this environment.'
    }
    if (-not $diagnostic.Ready) {
        throw 'Original colors matched, but capture/context/foreground checks did not. See desktop-diagnostic.json.'
    }
    $result.status='ready'
} catch { $result.reason=$_.Exception.Message }
finally {
    $result | ConvertTo-Json -Depth 12 | Tee-Object -FilePath (Join-Path $out 'desktop-readiness.json')
    if ($env:GITHUB_STEP_SUMMARY) {
        "### Desktop preflight: $($result.status)`n$($result.reason)`nAn environment block is not a product pass or failure." | Out-File $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
    }
}
if ($result.status -ne 'ready') { throw $result.reason }

