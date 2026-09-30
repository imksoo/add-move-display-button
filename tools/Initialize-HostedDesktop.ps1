[CmdletBinding()]
param([string]$OutputDirectory='real-app-evidence/desktop-setup')
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
$report=[ordered]@{ stage='starting'; success=$false; image=$env:ImageOS; imageVersion=$env:ImageVersion; commit=$env:GITHUB_SHA; before=$null; after=$null; policies=@(); closeAction=$null; focusAction=$null; error=$null }
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
    $report.after=[HostedDesktopSetup]::Observe()
    $report.focusAction=[HostedDesktopSetup]::InitializeInput()
    [HostedDesktopSetup]::Capture((Join-Path $out 'after.png'))
    $report.success=$true
} catch { $report.error=$_.Exception.ToString(); throw }
finally { $report.stage='complete'; Save-Setup; $report | ConvertTo-Json -Depth 8 }
# Success here means provisioning completed, never that screen capture passed.
# Every caller MUST run Test-DesktopReadiness.ps1 afterwards in this same job.
