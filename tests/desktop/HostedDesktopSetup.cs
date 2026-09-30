// Native identity checks for the disposable runner's first-logon setup host.
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

public static class HostedDesktopSetup
{
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string title);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", SetLastError=true)] static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr hwnd, int attribute, out uint value, int bytes);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] static extern int GetPackageFullName(IntPtr process, ref uint length, StringBuilder name);
    public sealed class Host {
        public long Hwnd, StartTicks; public int Pid, SessionId; public string Process, Path, Package;
    }
    public static Host Observe() {
        Host result=null; IntPtr hwnd=IntPtr.Zero;
        int session=Process.GetCurrentProcess().SessionId;
        while ((hwnd=FindWindowEx(IntPtr.Zero,hwnd,"Windows.UI.Core.CoreWindow","Microsoft account"))!=IntPtr.Zero) {
            uint pid, cloaked;
            if (!IsWindowVisible(hwnd)) continue;
            if (DwmGetWindowAttribute(hwnd,14,out cloaked,4)<0 || cloaked!=0) continue;
            GetWindowThreadProcessId(hwnd,out pid);
            using (var p=Process.GetProcessById((int)pid)) {
                if (p.ProcessName!="WWAHost" || p.SessionId!=session) continue;
                if (result!=null) throw new InvalidOperationException("Ambiguous setup host.");
                uint length=0; string package=null;
                if (GetPackageFullName(p.Handle,ref length,null)==122) {
                    var text=new StringBuilder((int)length);
                    if (GetPackageFullName(p.Handle,ref length,text)==0) package=text.ToString();
                }
                result=new Host { Hwnd=hwnd.ToInt64(), Pid=p.Id, SessionId=p.SessionId,
                    Process=p.ProcessName, Path=p.MainModule.FileName, Package=package, StartTicks=p.StartTime.ToUniversalTime().Ticks };
            }
        }
        return result;
    }
    public static void Validate(Host host) {
        string systemHost=System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),"WWAHost.exe");
        if (host.SessionId==0 || !String.Equals(host.Path,systemHost,StringComparison.OrdinalIgnoreCase)
            || host.Package==null || !host.Package.StartsWith("Microsoft.Windows.CloudExperienceHost_",StringComparison.Ordinal))
            throw new InvalidOperationException("Window is not the verified Windows CloudExperienceHost; see desktop-setup.json. Refusing to close it.");
    }
    public static string CloseVerified(Host before) {
        var current=Observe();
        if (current==null) return "already-closed";
        Validate(current);
        if (before.Pid!=current.Pid || before.Hwnd!=current.Hwnd || before.StartTicks!=current.StartTicks)
            throw new InvalidOperationException("Setup host changed while applying policy.");
        if (!PostMessage(new IntPtr(current.Hwnd),0x0010,IntPtr.Zero,IntPtr.Zero)) throw new Win32Exception();
        for (int i=0;i<20;i++) { Thread.Sleep(100); if (Observe()==null) return "WM_CLOSE"; }
        // The old setup instance can ignore WM_CLOSE. End only the verified PID
        // after policy application, never every WWAHost or the user's Explorer.
        using (var p=Process.GetProcessById(current.Pid)) {
            if (p.StartTime.ToUniversalTime().Ticks!=current.StartTicks) throw new InvalidOperationException("PID reused.");
            p.Kill(); if (!p.WaitForExit(5000)) throw new InvalidOperationException("Setup host did not exit.");
        }
        return "terminated-verified-setup-pid";
    }
    public static void Capture(string path) {
        SetThreadDpiAwarenessContext(new IntPtr(-4));
        var bounds=SystemInformation.VirtualScreen;
        using (var image=new Bitmap(bounds.Width,bounds.Height)) {
            using (var graphics=Graphics.FromImage(image)) graphics.CopyFromScreen(bounds.Location,Point.Empty,bounds.Size);
            image.Save(path,ImageFormat.Png);
        }
    }
}
