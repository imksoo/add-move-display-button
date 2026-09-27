[CmdletBinding()]
param([string]$OutputDirectory='real-app-evidence')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if ($env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'Desktop preparation is restricted to disposable GitHub-hosted VMs, never a personal/self-hosted machine.'
}
$out=[IO.Path]::GetFullPath($OutputDirectory)
$null=New-Item -ItemType Directory -Force $out
Start-Transcript -Path (Join-Path $out 'desktop-preparation.txt')
try {
    $session=[Diagnostics.Process]::GetCurrentProcess().SessionId
    $os=Get-CimInstance Win32_OperatingSystem
    Write-Host "Preparing $($os.Caption); session $session; image $env:ImageVersion"
    if ([int]$os.ProductType -eq 1) {
        # Official policy: suppress first-logon privacy UI. Does not enable consent,
        # change existing privacy preferences, disable Defender/UAC, or sign in.
        # https://learn.microsoft.com/windows/client-management/mdm/policy-csp-privacy#disableprivacyexperience
        foreach ($hive in @('HKCU:','HKLM:')) {
            $key="$hive\Software\Policies\Microsoft\Windows\OOBE"
            $null=New-Item -Path $key -Force
            $null=New-ItemProperty -Path $key -Name DisablePrivacyExperience -Value 1 -PropertyType DWord -Force
            Write-Host "Disposable VM only: $key\DisablePrivacyExperience=1"
        }
        # Close ONLY first-logon UI hosts from the Windows installation in this session.
        # Never terminate Explorer, ApplicationFrameHost, RuntimeBroker, or arbitrary apps.
        for ($attempt=0;$attempt -lt 3;$attempt++) {
            $brokers=@(Get-Process -Name UserOOBEBroker,CloudExperienceHostBroker -ErrorAction SilentlyContinue | Where-Object SessionId -eq $session)
            foreach ($process in $brokers) {
                $path=$process.Path
                if (-not $path.StartsWith($env:SystemRoot+'\',[StringComparison]::OrdinalIgnoreCase)) {
                    throw "Unexpected first-logon process path: $path"
                }
                Write-Host "Closing first-logon UI: $($process.ProcessName) PID=$($process.Id), $path"
                $process | Stop-Process -Force
            }
            Start-Sleep -Seconds 1
        }
    }
    Add-Type -ReferencedAssemblies System.Windows.Forms.dll,System.Drawing.dll -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;
public static class HostedDesktopReady {
    [DllImport("user32.dll")] static extern IntPtr FindWindow(string cls,string title);
    [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr hwnd);
    [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr hwnd,IntPtr dc);
    [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("gdi32.dll")] static extern uint GetPixel(IntPtr dc,int x,int y);
    public static void Check() {
        SetThreadDpiAwarenessContext(new IntPtr(-4));
        if (FindWindow("Shell_TrayWnd",null)==IntPtr.Zero) throw new InvalidOperationException("No shell taskbar in the test desktop.");
        using (var form=new Form()) {
            form.Text="MTMB desktop readiness sentinel";
            form.BackColor=Color.FromArgb(17,97,173);
            form.StartPosition=FormStartPosition.Manual;
            form.Location=new Point(40,40); form.ClientSize=new Size(240,120);
            form.TopMost=true; form.ShowInTaskbar=false;
            form.Show(); form.Activate(); form.Refresh();
            Application.DoEvents(); Thread.Sleep(400); Application.DoEvents();
            Point point=form.PointToScreen(new Point(80,60));
            IntPtr dc=GetDC(IntPtr.Zero);
            if (dc==IntPtr.Zero) throw new Win32Exception();
            uint color;
            try { color=GetPixel(dc,point.X,point.Y); } finally { ReleaseDC(IntPtr.Zero,dc); }
            if (color!=0x00ad6111) throw new InvalidOperationException("Visible desktop is obscured/not the test desktop. Sentinel RGB mismatch: 0x"+color.ToString("X8"));
            form.Close(); Application.DoEvents();
        }
    }
}
'@
    [HostedDesktopReady]::Check()
    Write-Host 'PASS: shell taskbar exists and sentinel pixels are actually visible on screen.'
} finally { Stop-Transcript }
