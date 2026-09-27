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
# Do not attach a GUI-driving console test to the workflow shell's interactive
# console. Drain both output pipes asynchronously, and bound the child lifetime
# just as CTest does in the SDK job. Failure evidence must survive a hung child.
$process=New-Object System.Diagnostics.Process
$process.StartInfo.FileName=$test
$process.StartInfo.Arguments='"'+$exe+'"'
$process.StartInfo.WorkingDirectory=$out
$process.StartInfo.UseShellExecute=$false
$process.StartInfo.CreateNoWindow=$true
$process.StartInfo.RedirectStandardOutput=$true
$process.StartInfo.RedirectStandardError=$true
$stdout=$null; $stderr=$null; $started=$false
try {
    $started=$process.Start()
    if (-not $started) { throw 'Failed to start inactive integration test.' }
    $stdout=$process.StandardOutput.ReadToEndAsync()
    $stderr=$process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(90000)) {
        throw 'Inactive integration test timed out after 90 seconds. See output logs.'
    }
    if ($process.ExitCode -ne 0) { throw "Inactive integration test failed: $($process.ExitCode)" }
    $after=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($before -ne $after) { throw 'EXE changed during the test.' }
    $result=Get-Content -LiteralPath $resultFile -Raw | ConvertFrom-Json
    if ($result.passed -ne $true) { throw 'Missing successful inactive test result.' }
    $result | Add-Member -NotePropertyName executableSha256 -NotePropertyValue $before
    $result | Add-Member -NotePropertyName commit -NotePropertyValue $env:GITHUB_SHA
    $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $resultFile -Encoding utf8
    Get-Content -LiteralPath $resultFile
} finally {
    if ($started -and -not $process.HasExited) {
        $process.Kill() # Only the test process created above, never other desktop applications.
        $null=$process.WaitForExit(5000)
    }
    foreach ($entry in @(@{Task=$stdout;Name='stdout'},@{Task=$stderr;Name='stderr'})) {
        if ($null -ne $entry.Task -and $entry.Task.Wait(5000)) {
            $entry.Task.Result | Tee-Object -FilePath (Join-Path $out ('inactive-'+$entry.Name+'.txt'))
        }
    }
    $process.Dispose()
}
