@echo off
setlocal
set "MTMB_DIAG_EXE=%~dp0MoveToMonitorButton.exe"
set "MTMB_DIAG_LOG=%~dp0startup-check.txt"
echo Checking executable startup. No Windows settings are changed.
echo If Windows shows an error dialog, close it with OK.
powershell.exe -NoLogo -NoProfile -Command "$ErrorActionPreference='Stop'; $lines=@('MoveToMonitorButton startup check',('is64BitOS='+[Environment]::Is64BitOperatingSystem),('OS='+[Environment]::OSVersion.VersionString)); $p=$null; try { $exe=$env:MTMB_DIAG_EXE; $lines+=('sha256='+(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash); $p=New-Object System.Diagnostics.Process; $p.StartInfo.FileName=$exe; $p.StartInfo.Arguments='--startup-check'; $p.StartInfo.UseShellExecute=$false; if (-not $p.Start()) { throw 'Process.Start returned false' }; if (-not $p.WaitForExit(15000)) { $p.Kill(); $lines+='result=TIMEOUT (diagnostic child terminated)' } else { $code=$p.ExitCode; $lines+=('exit_decimal='+$code); $lines+=('exit_hex=0x{0:X8}' -f $code); if ($code -eq 0) { $lines+='result=STARTUP_OK (not a GUI/multi-monitor test)' } else { $lines+='result=STARTUP_FAILED' } } } catch { $lines+=('error='+$_.Exception.Message) } finally { if ($null -ne $p) { $p.Dispose() } }; $lines | Tee-Object -FilePath $env:MTMB_DIAG_LOG"
echo.
echo Diagnostic output: "%MTMB_DIAG_LOG%"
pause
endlocal
