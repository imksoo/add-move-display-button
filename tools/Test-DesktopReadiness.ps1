[CmdletBinding()]
param([string]$OutputDirectory='real-app-evidence', [switch]$AllowDesktopCapture)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if (-not $AllowDesktopCapture -and $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'Use a disposable desktop; non-hosted capture requires explicit -AllowDesktopCapture.'
}
# Diagnostic only: no registry changes, process termination, desktop switching,
# authentication changes, or attempts to dismiss Windows setup/security screens.
$out=[IO.Path]::GetFullPath($OutputDirectory)
$null=New-Item -ItemType Directory -Force $out
$os=Get-CimInstance Win32_OperatingSystem
$result=[ordered]@{
    status='environment-blocked'; reason=$null; commit=$env:GITHUB_SHA
    os=$os.Caption; build=$os.BuildNumber; architecture=$env:PROCESSOR_ARCHITECTURE
    imageVersion=$env:ImageVersion
    sessionId=[Diagnostics.Process]::GetCurrentProcess().SessionId
    userInteractive=[Environment]::UserInteractive
    observations=$null
    packages=@(Get-AppxPackage | Where-Object { $_.Name -match 'WindowsNotepad|WindowsStore|WindowsCalculator' } | Select-Object Name,Version,PackageFullName)
    scope='Observes an owned test window and screen pixels. Does not modify Windows configuration.'
}
try {
    Add-Type -ReferencedAssemblies System.Windows.Forms.dll,System.Drawing.dll -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;
public static class DesktopReadiness {
    public sealed class Sample {
        public string Expected, Actual;
        public bool PixelMatches, ForegroundMatches;
        public string Screenshot;
    }
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr FindWindow(string cls,string title);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr hwnd);
    [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr hwnd,IntPtr dc);
    [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("gdi32.dll")] static extern uint GetPixel(IntPtr dc,int x,int y);
    [DllImport("gdi32.dll",SetLastError=true)] static extern bool BitBlt(IntPtr to,int x,int y,int w,int h,IntPtr from,int sx,int sy,uint operation);
    public static Sample[] Observe(string output) {
        SetThreadDpiAwarenessContext(new IntPtr(-4));
        if (FindWindow("Shell_TrayWnd",null)==IntPtr.Zero) throw new InvalidOperationException("No shell taskbar in the test desktop.");
        var samples=new List<Sample>();
        using (var form=new Form()) {
            form.Text="MTMB desktop pixel readiness check";
            form.StartPosition=FormStartPosition.Manual;
            var work=Screen.PrimaryScreen.WorkingArea;
            form.Location=new Point(work.Left+40,work.Top+40);
            form.ClientSize=new Size(320,140);
            form.TopMost=true; form.ShowInTaskbar=false;
            form.Show(); form.Activate();
            foreach (var color in new[]{Color.FromArgb(17,97,173),Color.FromArgb(173,97,17)}) {
                form.BackColor=color; form.Refresh();
                for (int i=0;i<20;i++) { Application.DoEvents(); Thread.Sleep(50); }
                Point point=form.PointToScreen(new Point(80,60));
                IntPtr screen=GetDC(IntPtr.Zero);
                if (screen==IntPtr.Zero) throw new Win32Exception();
                var sample=new Sample();
                sample.Expected=String.Format("#{0:X2}{1:X2}{2:X2}",color.R,color.G,color.B);
                sample.ForegroundMatches=GetForegroundWindow()==form.Handle;
                sample.Screenshot="desktop-sentinel-"+samples.Count+".png";
                try {
                    uint actual=GetPixel(screen,point.X,point.Y);
                    sample.Actual=String.Format("#{0:X2}{1:X2}{2:X2}",actual&255,(actual>>8)&255,(actual>>16)&255);
                    sample.PixelMatches=sample.Actual==sample.Expected;
                    Rectangle r=form.Bounds;
                    using (var image=new Bitmap(r.Width,r.Height,PixelFormat.Format24bppRgb)) {
                        using(var g=Graphics.FromImage(image)) {
                            var dest=g.GetHdc();
                            try { if(!BitBlt(dest,0,0,r.Width,r.Height,screen,r.Left,r.Top,0x40cc0020)) throw new Win32Exception(); }
                            finally { g.ReleaseHdc(dest); }
                        }
                        image.Save(System.IO.Path.Combine(output,sample.Screenshot),ImageFormat.Png);
                    }
                } finally { ReleaseDC(IntPtr.Zero,screen); }
                samples.Add(sample);
            }
            form.Close(); Application.DoEvents();
        }
        return samples.ToArray();
    }
}
'@
    if (-not [Environment]::UserInteractive) { throw 'No interactive desktop.' }
    $samples=@([DesktopReadiness]::Observe($out))
    $result.observations=$samples
    if ($samples.Count -ne 2 -or @($samples | Where-Object { -not $_.PixelMatches }).Count) {
        throw 'The visible screen does not show both colors of the owned sentinel. Real-app rendering cannot be judged in this environment.'
    }
    $result.status='ready'
} catch { $result.reason=$_.Exception.Message }
finally {
    $result | ConvertTo-Json -Depth 6 | Tee-Object -FilePath (Join-Path $out 'desktop-readiness.json')
    if ($env:GITHUB_STEP_SUMMARY) {
        "### Desktop preflight: $($result.status)`n$($result.reason)`nAn environment block is not a product pass or failure." | Out-File $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
    }
}
if ($result.status -ne 'ready') { throw $result.reason }
