[CmdletBinding()]
param([string]$OutputDirectory = 'chatgpt-evidence')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'Test applications may only be installed on disposable GitHub-hosted runners.'
}
$null = New-Item -ItemType Directory -Force $OutputDirectory
foreach ($id in @('9PLM9XGG6VKS','9NT1R1C2HH7J')) {
    winget install --id $id --exact --source msstore --accept-package-agreements --accept-source-agreements --silent --disable-interactivity 2>&1 | Tee-Object (Join-Path $OutputDirectory "install-$id.txt")
    if ($LASTEXITCODE -ne 0) { throw "Store installation failed: $id ($LASTEXITCODE)" }
}
Get-AppxPackage | Where-Object Name -like 'OpenAI.*' | Select-Object Name,Version,Architecture,PackageFullName | ConvertTo-Json | Set-Content (Join-Path $OutputDirectory 'packages.json') -Encoding UTF8
