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
    $install=Join-Path $env:RUNNER_TEMP 'mtmb-signed-driver'
    $null=New-Item -ItemType Directory -Force $install
    $zip=Join-Path $install 'driver.zip'
    Invoke-WebRequest 'https://github.com/VirtualDrivers/Virtual-Display-Driver/releases/download/25.7.23/VirtualDisplayDriver-x86.Driver.Only.zip' -OutFile $zip -UseBasicParsing -TimeoutSec 120
    if ((Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant() -ne 'e24210692b442b39af763536330ce78b423f19342b7a7792c26de3944e418b3a') { throw 'Pinned driver archive hash mismatch.' }
    Expand-Archive $zip (Join-Path $install 'driver')
    $driver=Join-Path $install 'driver/VirtualDisplayDriver'
    $signature=Get-AuthenticodeSignature (Join-Path $driver 'mttvdd.cat')
    $signature | Format-List * | Out-File (Join-Path $out 'driver-signature.txt')
    if ($signature.Status -ne 'Valid') { throw "Driver catalog signature is not trusted: $($signature.Status)" }
    # Consent to this validated publisher on this disposable machine, as the
    # vendor's silent installer requires. Do not import or alter trusted roots.
    if ($signature.SignerCertificate.Subject -notmatch 'CN=SignPath Foundation') { throw 'Unexpected pinned driver publisher.' }
    $publisherStore=New-Object Security.Cryptography.X509Certificates.X509Store('TrustedPublisher','LocalMachine')
    $publisherStore.Open('ReadWrite')
    try { $publisherStore.Add($signature.SignerCertificate) } finally { $publisherStore.Close() }
    # Use normal Windows signature validation. No test-signing, root certificate
    # import, Secure Boot change, or signature-enforcement bypass.
    $null=New-Item -ItemType Directory -Force 'C:\VirtualDisplayDriver'
    $config=Get-Content (Join-Path $driver 'vdd_settings.xml') -Raw
    $config=$config -replace '<count>1</count>','<count>2</count>'
    $config | Set-Content 'C:\VirtualDisplayDriver\vdd_settings.xml' -Encoding utf8
    $inf=Join-Path $driver 'MttVDD.inf'
    # Windows PowerShell Start-Process with redirected output can lose ExitCode
    # (PowerShell/PowerShell#5421); keep the native process object and drain pipes.
    $start=[Diagnostics.ProcessStartInfo]::new()
    $start.FileName=Join-Path $env:SystemRoot 'System32/pnputil.exe'
    $start.Arguments='/add-driver "'+$inf+'" /install'
    $start.UseShellExecute=$false; $start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
    $installer=[Diagnostics.Process]::new(); $installer.StartInfo=$start
    $null=$installer.Start()
    $stdout=$installer.StandardOutput.ReadToEndAsync(); $stderr=$installer.StandardError.ReadToEndAsync()
    if (-not $installer.WaitForExit(60000)) { $installer.Kill(); throw 'Driver installation timed out.' }
    $stdout.Result | Set-Content (Join-Path $out 'driver-install.txt')
    $stderr.Result | Set-Content (Join-Path $out 'driver-install-error.txt')
    if ($installer.ExitCode -ne 0 -and $installer.ExitCode -ne 3010) { throw "Driver installation failed: $($installer.ExitCode)" }
    $installer.Dispose()
    Push-Location $out
    try { & $test $exe 2>&1 | Tee-Object 'multi-monitor-test.txt'; $exit=$LASTEXITCODE } finally { Pop-Location }
    if ($exit -ne 0) { throw "Actual multi-monitor test failed: $exit" }
    $result['details']=Get-Content (Join-Path $out 'multi-monitor-details.json') -Raw | ConvertFrom-Json
    $result.passed=$true
} catch { $result.error=$_.Exception.Message; throw }
finally { $result | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $out 'multi-monitor-result.json') -Encoding utf8 }
