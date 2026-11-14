[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ExecutablePath,
      [Parameter(Mandatory=$true)][string]$TestPath,
      [string]$OutputDirectory='multi-monitor-evidence')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted' -or $env:PROCESSOR_ARCHITECTURE -ne 'AMD64') {
    throw 'Driver installation is restricted to disposable hosted x64 desktops.'
}
$out=[IO.Path]::GetFullPath($OutputDirectory)
$null=New-Item -ItemType Directory -Force $out
$exe=(Resolve-Path $ExecutablePath).Path
$test=(Resolve-Path $TestPath).Path
$result=[ordered]@{ commit=$env:GITHUB_SHA; executableSha256=(Get-FileHash $exe -Algorithm SHA256).Hash.ToLowerInvariant(); passed=$false; scope='Actual Windows extended desktop with a signed indirect display driver, not physical monitor hardware.'; error=$null }
try {
    $zip=Join-Path $out 'driver.zip'
    Invoke-WebRequest 'https://github.com/VirtualDrivers/Virtual-Display-Driver/releases/download/25.7.23/VirtualDisplayDriver-x86.Driver.Only.zip' -OutFile $zip -UseBasicParsing
    if ((Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant() -ne 'e24210692b442b39af763536330ce78b423f19342b7a7792c26de3944e418b3a') { throw 'Pinned driver archive hash mismatch.' }
    Expand-Archive $zip (Join-Path $out 'driver')
    $driver=Join-Path $out 'driver/VirtualDisplayDriver'
    $signature=Get-AuthenticodeSignature (Join-Path $driver 'mttvdd.cat')
    $signature | Format-List * | Out-File (Join-Path $out 'driver-signature.txt')
    if ($signature.Status -ne 'Valid') { throw "Driver catalog signature is not trusted: $($signature.Status)" }
    # Use normal Windows signature validation. No test-signing, root certificate
    # import, Secure Boot change, or signature-enforcement bypass.
    $null=New-Item -ItemType Directory -Force 'C:\VirtualDisplayDriver'
    $config=Get-Content (Join-Path $driver 'vdd_settings.xml') -Raw
    $config=$config -replace '<count>1</count>','<count>2</count>'
    $config | Set-Content 'C:\VirtualDisplayDriver\vdd_settings.xml' -Encoding utf8
    & pnputil.exe /add-driver (Join-Path $driver 'MttVDD.inf') /install | Tee-Object (Join-Path $out 'driver-install.txt')
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 3010) { throw "Driver installation failed: $LASTEXITCODE" }
    Push-Location $out
    try { & $test $exe 2>&1 | Tee-Object 'multi-monitor-test.txt'; $exit=$LASTEXITCODE } finally { Pop-Location }
    if ($exit -ne 0) { throw "Actual multi-monitor test failed: $exit" }
    $result.passed=$true
} catch { $result.error=$_.Exception.Message; throw }
finally { $result | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $out 'multi-monitor-result.json') -Encoding utf8 }
