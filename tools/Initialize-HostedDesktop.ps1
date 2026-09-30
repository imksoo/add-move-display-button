[CmdletBinding()]
param([string]$OutputDirectory='real-app-evidence/desktop-setup')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
# This is environment preparation, deliberately separate from the read-only pixel gate.
# Never run against a personal desktop, a persistent runner, or a non-Windows-11 image.
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted' -or $env:ImageOS -notin @('win11-arm64','win11-vs2026-arm64')) {
    throw 'Desktop setup is restricted to disposable GitHub-hosted Windows 11 Arm runners.'
}
$os=Get-CimInstance Win32_OperatingSystem
if ($os.ProductType -ne 1 -or [int]$os.BuildNumber -lt 22000) { throw 'Windows 11 client required.' }
$out=[IO.Path]::GetFullPath($OutputDirectory)
$null=New-Item -ItemType Directory -Force $out
$bootstrap=[ordered]@{ stage='loading'; image=$env:ImageOS; imageVersion=$env:ImageVersion; commit=$env:GITHUB_SHA; error=$null }
$bootstrap | ConvertTo-Json | Set-Content (Join-Path $out 'bootstrap.json') -Encoding UTF8
try {
    Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,WindowsBase
    $references=@('System.dll','System.Core.dll','System.Web.Extensions.dll',
        [System.Windows.Automation.AutomationElement].Assembly.Location,
        [System.Windows.Automation.AutomationPattern].Assembly.Location,
        [System.Windows.Rect].Assembly.Location)
    Add-Type -Path (Join-Path $PSScriptRoot '../tests/desktop/HostedDesktopSetup.cs') -ReferencedAssemblies $references
    $bootstrap.stage='loaded'
} catch { $bootstrap.error=$_.Exception.ToString(); throw }
finally { $bootstrap | ConvertTo-Json | Set-Content (Join-Path $out 'bootstrap.json') -Encoding UTF8 }
$result=[HostedDesktopSetup]::CompletePrivacyPage($out)
$result | ConvertTo-Json -Depth 8
if (-not $result.Success) { throw $result.Error }
