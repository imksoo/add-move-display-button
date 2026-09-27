[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ExecutablePath,
    [string]$OutputPath = 'startup-result.json'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$exe = (Resolve-Path -LiteralPath $ExecutablePath).Path
$output = [IO.Path]::GetFullPath($OutputPath)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($output)) | Out-Null

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class StartupWindows {
    private delegate bool EnumProc(IntPtr hwnd, IntPtr data);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassName(IntPtr hwnd, StringBuilder name, int capacity);
    [DllImport("user32.dll", SetLastError = true)] private static extern IntPtr SendMessageTimeout(IntPtr hwnd, uint message, UIntPtr wp, IntPtr lp, uint flags, uint timeout, out UIntPtr result);
    public static Dictionary<string, IntPtr> ForProcess(int pid) {
        var found = new Dictionary<string, IntPtr>();
        EnumWindows((hwnd, data) => {
            uint owner;
            GetWindowThreadProcessId(hwnd, out owner);
            if (owner == (uint)pid) {
                var name = new StringBuilder(256);
                GetClassName(hwnd, name, name.Capacity);
                found[name.ToString()] = hwnd;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static bool Responds(IntPtr hwnd) {
        UIntPtr result;
        return SendMessageTimeout(hwnd, 0, UIntPtr.Zero, IntPtr.Zero, 0x23, 2000, out result) != IntPtr.Zero;
    }
}
'@

$result = [ordered]@{
    executableSha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    osVersion = [Environment]::OSVersion.VersionString
    sessionId = [Diagnostics.Process]::GetCurrentProcess().SessionId
    loaderExitCode = $null
    normalStartup = $false
    hostWindowResponded = $false
    overlayWindowCreated = $false
    quitExitCode = $null
    applicationExitCode = $null
    passed = $false
    error = $null
}
$loader = $null
$app = $null
$quit = $null
try {
    $loader = Start-Process -FilePath $exe -ArgumentList '--startup-check' -PassThru
    if (-not $loader.WaitForExit(15000)) { throw 'Loader/startup-check timed out.' }
    $loader.WaitForExit()
    $result.loaderExitCode = $loader.ExitCode
    if ($loader.ExitCode -ne 0) { throw "Loader/startup-check failed: $($loader.ExitCode)" }

    # Launch the real application with NO test flag: tray/window initialization,
    # normal event loop, bounded WM_NULL response, and its normal --quit path.
    $app = Start-Process -FilePath $exe -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    $ready = $false
    do {
        Start-Sleep -Milliseconds 200
        $app.Refresh()
        if ($app.HasExited) { throw "Normal startup exited early: $($app.ExitCode)" }
        $windows = [StartupWindows]::ForProcess($app.Id)
        if ($windows.ContainsKey('#32770')) { throw 'Normal startup displayed an unexpected modal dialog.' }
        $ready = $windows.ContainsKey('MoveToMonitorButton.Host.v1') -and $windows.ContainsKey('MoveToMonitorButton.Overlay.v1')
    } while (-not $ready -and [DateTime]::UtcNow -lt $deadline)
    if (-not $ready) { throw 'Normal startup did not create the host and overlay windows.' }
    # Let initialization/timers run before claiming a healthy resident process.
    Start-Sleep -Seconds 3
    $app.Refresh()
    if ($app.HasExited) { throw "Application exited during observation: $($app.ExitCode)" }
    $windows = [StartupWindows]::ForProcess($app.Id)
    if ($windows.ContainsKey('#32770')) { throw 'Initialization failed with a modal dialog.' }
    if (-not $windows.ContainsKey('MoveToMonitorButton.Host.v1') -or -not $windows.ContainsKey('MoveToMonitorButton.Overlay.v1')) {
        throw 'Application windows disappeared during observation.'
    }
    $result.overlayWindowCreated = $true
    $result.hostWindowResponded = [StartupWindows]::Responds($windows['MoveToMonitorButton.Host.v1'])
    if (-not $result.hostWindowResponded) { throw 'The normal UI event loop did not respond.' }
    $result.normalStartup = $true

    $quit = Start-Process -FilePath $exe -ArgumentList '--quit' -PassThru
    if (-not $quit.WaitForExit(10000)) { throw 'The --quit command timed out.' }
    $quit.WaitForExit()
    $result.quitExitCode = $quit.ExitCode
    if ($quit.ExitCode -ne 0) { throw "The --quit command failed: $($quit.ExitCode)" }
    if (-not $app.WaitForExit(10000)) { throw 'Application did not exit after --quit.' }
    $app.WaitForExit()
    $result.applicationExitCode = $app.ExitCode
    if ($app.ExitCode -ne 0) { throw "Application exit failed: $($app.ExitCode)" }
    $result.passed = $true
}
catch {
    $result.error = $_.Exception.Message
    throw
}
finally {
    foreach ($process in @($quit, $app, $loader)) {
        if ($null -ne $process) {
            if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
            $process.Dispose()
        }
    }
    $json = $result | ConvertTo-Json -Depth 4
    $json | Set-Content -LiteralPath $output -Encoding utf8
    Write-Host $json
}
