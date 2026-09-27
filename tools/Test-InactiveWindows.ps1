[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ExecutablePath,
    [Parameter(Mandatory=$true)][string]$TestPath,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$exe=(Resolve-Path -LiteralPath $ExecutablePath).Path
$test=(Resolve-Path -LiteralPath $TestPath).Path
$out=[IO.Path]::GetFullPath($OutputDirectory)
$null=New-Item -ItemType Directory -Force $out
$resultFile=Join-Path $out 'inactive-result.json'
if (Test-Path -LiteralPath $resultFile) { Remove-Item -LiteralPath $resultFile }
$before=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
Push-Location $out
try {
    & $test $exe
    if ($LASTEXITCODE -ne 0) { throw "Inactive integration test failed: $LASTEXITCODE" }
    $after=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($before -ne $after) { throw 'EXE changed during the test.' }
    $result=Get-Content -LiteralPath $resultFile -Raw | ConvertFrom-Json
    if ($result.passed -ne $true) { throw 'Missing successful inactive test result.' }
    $result | Add-Member -NotePropertyName executableSha256 -NotePropertyValue $before
    $result | Add-Member -NotePropertyName commit -NotePropertyValue $env:GITHUB_SHA
    $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $resultFile -Encoding utf8
    Get-Content -LiteralPath $resultFile
} finally { Pop-Location }
