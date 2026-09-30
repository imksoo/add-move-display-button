// Independent black-box observer. Never links the application's placement code.
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;

public static class DesktopProbe
{
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect
    {
        public int Left, Top, Right, Bottom;
        public int Width { get { return Right - Left; } }
        public int Height { get { return Bottom - Top; } }
        public bool Contains(Rect r) { return r.Width > 0 && r.Height > 0 && r.Left >= Left && r.Top >= Top && r.Right <= Right && r.Bottom <= Bottom; }
        public bool Overlaps(Rect r) { return r.Width > 0 && r.Height > 0 && Left < r.Right && Right > r.Left && Top < r.Bottom && Bottom > r.Top; }
        public bool Same(Rect r) { return Left == r.Left && Top == r.Top && Right == r.Right && Bottom == r.Bottom; }
    }
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; public Point(int x, int y) { X = x; Y = y; } }
    [StructLayout(LayoutKind.Sequential)] private struct MonitorInfo { public int Size; public Rect Monitor, Work; public uint Flags; }
    [StructLayout(LayoutKind.Sequential)] public struct TitleInfo
    {
        public uint Size; public Rect Title;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst=6)] public uint[] States;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst=6)] public Rect[] Buttons;
    }
    public sealed class WindowInfo
    {
        public long Handle; public int Pid; public string Class, Process, Version, Package, Title; public uint Cloaked;
        public Rect Bounds; public bool Visible;
    }
    public sealed class CaptionInfo
    {
        public Rect Window, Frame, Controls;
        public TitleInfo Title; public bool TitleQueryOk, DwmControlsOk;
        public int TitleQueryError; public uint Dpi; public long Style, ExStyle;
        public bool Maximized; public List<WindowInfo> Children;
    }
    public sealed class PixelChange { public int Changed, Pixels; public double MeanDifference; }
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int capacity);
    [DllImport("dwmapi.dll", EntryPoint="DwmGetWindowAttribute")] private static extern int DwmValue(IntPtr hwnd, int attribute, out uint value, int size);
    [DllImport("user32.dll")] private static extern bool SystemParametersInfo(uint action,uint param,out uint value,uint flags);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr hwnd);
    [DllImport("user32.dll", SetLastError=true)] public static extern bool GetWindowRect(IntPtr hwnd, out Rect bounds);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetClassName(IntPtr hwnd, StringBuilder name, int capacity);
    [DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")] private static extern IntPtr GetWindowLongPtr(IntPtr hwnd, int index);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr hwnd, int command);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point point);
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(Point point);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern IntPtr MonitorFromPoint(Point point, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern bool GetMonitorInfo(IntPtr monitor, ref MonitorInfo info);
    [DllImport("user32.dll")] private static extern int GetSystemMetrics(int index);
    [DllImport("user32.dll")] private static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] private static extern IntPtr GetDC(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern int ReleaseDC(IntPtr hwnd, IntPtr dc);
    [DllImport("gdi32.dll", SetLastError=true)] private static extern bool BitBlt(IntPtr target, int x, int y, int width, int height, IntPtr source, int sourceX, int sourceY, uint operation);
    [DllImport("dwmapi.dll", EntryPoint="DwmGetWindowAttribute")] private static extern int DwmRect(IntPtr hwnd, int attribute, out Rect bounds, int size);
    [DllImport("user32.dll", EntryPoint="SendMessageTimeoutW", SetLastError=true)] private static extern IntPtr QueryTitle(IntPtr hwnd, uint message, IntPtr wp, ref TitleInfo info, uint flags, uint timeout, out UIntPtr result);
    [DllImport("user32.dll", EntryPoint="SendMessageTimeoutW", SetLastError=true)] private static extern IntPtr Query(IntPtr hwnd, uint message, IntPtr wp, IntPtr lp, uint flags, uint timeout, out UIntPtr result);
    [DllImport("kernel32.dll")] private static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] private static extern int GetPackageFullName(IntPtr process, ref uint length, StringBuilder name);

    public static void PhysicalCoordinates() { SetThreadDpiAwarenessContext(new IntPtr(-4)); }
    public static Rect WorkArea() { var m = new MonitorInfo(); m.Size = Marshal.SizeOf(m); if (!GetMonitorInfo(MonitorFromPoint(new Point(0,0),1), ref m)) throw new Win32Exception(); return m.Work; }
    public static Rect Bounds(IntPtr hwnd) { Rect r; if (!GetWindowRect(hwnd, out r)) throw new Win32Exception(); return r; }
    private static string ClassName(IntPtr hwnd) { var s = new StringBuilder(256); GetClassName(hwnd, s, s.Capacity); return s.ToString(); }
    public static WindowInfo Describe(IntPtr hwnd)
    {
        uint pid; GetWindowThreadProcessId(hwnd, out pid);
        var r = new WindowInfo(); r.Handle = hwnd.ToInt64(); r.Pid = (int)pid;
        r.Class = ClassName(hwnd); r.Visible = IsWindowVisible(hwnd); GetWindowRect(hwnd, out r.Bounds);
        var title=new StringBuilder(512); GetWindowText(hwnd,title,title.Capacity); r.Title=title.ToString();
        DwmValue(hwnd,14,out r.Cloaked,4);
        try { using (var p = Process.GetProcessById((int)pid)) { r.Process = p.ProcessName; try { r.Version = p.MainModule.FileVersionInfo.FileVersion; } catch { } } } catch { r.Process = "unavailable"; }
        var handle = OpenProcess(0x1000, false, pid);
        if (handle != IntPtr.Zero) {
            try { uint length = 0; if (GetPackageFullName(handle, ref length, null) == 122) { var name = new StringBuilder((int)length); if (GetPackageFullName(handle, ref length, name) == 0) r.Package = name.ToString(); } }
            finally { CloseHandle(handle); }
        }
        return r;
    }
    public static WindowInfo[] Windows()
    {
        var found = new List<WindowInfo>();
        // EnumWindows excludes non-desktop CoreWindows on Windows 8+.
        // Include packaged top-level windows without accepting child content as a frame.
        var seen=new HashSet<IntPtr>(); IntPtr hwnd=IntPtr.Zero;
        while ((hwnd=FindWindowEx(IntPtr.Zero,hwnd,null,null))!=IntPtr.Zero && seen.Add(hwnd) && seen.Count<=4096) {
            if (ClassName(hwnd)!="ConsoleWindowClass") found.Add(Describe(hwnd));
        }
        return found.ToArray();
    }
    public static CaptionInfo Caption(IntPtr hwnd)
    {
        var c = new CaptionInfo(); c.Window = Bounds(hwnd); c.Frame = c.Window;
        Rect r; if (DwmRect(hwnd,9,out r,16) >= 0) c.Frame = r;
        c.DwmControlsOk = DwmRect(hwnd,5,out r,16) >= 0 && r.Width > 0 && r.Height > 0;
        if (c.DwmControlsOk) { r.Left += c.Window.Left; r.Right += c.Window.Left; r.Top += c.Window.Top; r.Bottom += c.Window.Top; c.Controls = r; }
        var t = new TitleInfo(); t.Size = (uint)Marshal.SizeOf(t); t.States = new uint[6]; t.Buttons = new Rect[6];
        UIntPtr ignored; c.TitleQueryOk = QueryTitle(hwnd,0x33f,IntPtr.Zero,ref t,0x23,500,out ignored) != IntPtr.Zero;
        c.TitleQueryError = c.TitleQueryOk ? 0 : Marshal.GetLastWin32Error(); c.Title = t;
        c.Style = GetWindowLongPtr(hwnd,-16).ToInt64(); c.ExStyle = GetWindowLongPtr(hwnd,-20).ToInt64();
        c.Dpi = GetDpiForWindow(hwnd); c.Maximized = IsZoomed(hwnd);
        c.Children = new List<WindowInfo>();
        var pending=new Queue<IntPtr>(); var seen=new HashSet<IntPtr>(); pending.Enqueue(hwnd);
        while (pending.Count>0 && c.Children.Count<64) {
            var parent=pending.Dequeue(); IntPtr child=IntPtr.Zero;
            while ((child=FindWindowEx(parent,child,null,null))!=IntPtr.Zero && seen.Add(child) && c.Children.Count<64) {
                c.Children.Add(Describe(child)); pending.Enqueue(child);
            }
        }
        return c;
    }
    public static long Hit(IntPtr hwnd, int x, int y)
    {
        UIntPtr result; uint packed = (uint)(ushort)x | ((uint)(ushort)y << 16);
        return Query(hwnd,0x84,IntPtr.Zero,new IntPtr((long)packed),0x23,500,out result) == IntPtr.Zero ? -9999 : unchecked((long)result.ToUInt64());
    }
    public static bool Responsive(IntPtr hwnd) { UIntPtr result; return Query(hwnd,0,IntPtr.Zero,IntPtr.Zero,0x23,2000,out result) != IntPtr.Zero; }
    public static object InputSettings() {
        uint tracking,zorder,timeout;
        bool a=SystemParametersInfo(0x1000,0,out tracking,0);
        bool b=SystemParametersInfo(0x100c,0,out zorder,0);
        bool c=SystemParametersInfo(0x2002,0,out timeout,0);
        Point cursor; GetCursorPos(out cursor);
        return new { TrackingOk=a, Tracking=tracking, ZOrderOk=b, ZOrder=zorder, TimeoutOk=c, Timeout=timeout,
            Cursor=cursor, AtCursor=Describe(WindowFromPoint(cursor)), Foreground=Describe(GetForegroundWindow()) };
    }
    public static void CaptureScreen(string path) {
        int x=GetSystemMetrics(76),y=GetSystemMetrics(77),w=GetSystemMetrics(78),h=GetSystemMetrics(79);
        using (var image=new Bitmap(w,h,PixelFormat.Format24bppRgb)) {
            using (var graphics=Graphics.FromImage(image)) graphics.CopyFromScreen(x,y,0,0,new Size(w,h));
            image.Save(path,ImageFormat.Png);
        }
    }
    public static Rect CaptureCaption(IntPtr target, string path)
    {
        if (GetForegroundWindow() != target) throw new InvalidOperationException("Capture blocked: test target is not foreground.");
        Rect r = Bounds(target);
        int left = GetSystemMetrics(76), top = GetSystemMetrics(77);
        int right = left + GetSystemMetrics(78), bottom = top + GetSystemMetrics(79);
        r.Bottom = Math.Min(r.Bottom,r.Top + Math.Max(140, (int)GetDpiForWindow(target) * 140 / 96));
        r.Left = Math.Max(r.Left,left); r.Top = Math.Max(r.Top,top); r.Right = Math.Min(r.Right,right); r.Bottom = Math.Min(r.Bottom,bottom);
        if (r.Width < 100 || r.Height < 20) throw new InvalidOperationException("Capture rectangle is offscreen.");
        // Framework Graphics.CopyFromScreen rejects combined raster-operation flags.
        // Use real GDI SRCCOPY | CAPTUREBLT so the product's layered overlay is included.
        // RGB output deliberately has no alpha channel that could hide captured pixels.
        using (var image = new Bitmap(r.Width,r.Height,PixelFormat.Format24bppRgb)) {
            using (var g = Graphics.FromImage(image)) {
                IntPtr screen = GetDC(IntPtr.Zero);
                if (screen == IntPtr.Zero) throw new Win32Exception();
                try {
                    IntPtr destination = g.GetHdc();
                    try { if (!BitBlt(destination,0,0,r.Width,r.Height,screen,r.Left,r.Top,0x40cc0020)) throw new Win32Exception(); }
                    finally { g.ReleaseHdc(destination); }
                } finally { ReleaseDC(IntPtr.Zero,screen); }
            }
            image.Save(path,ImageFormat.Png);
        }
        return r;
    }
    public static PixelChange Compare(string first, string second, Rect overlay, Rect capture)
    {
        if (!capture.Contains(overlay)) throw new InvalidOperationException("Overlay outside captured caption.");
        var result = new PixelChange(); long total = 0;
        using (var a = new Bitmap(first)) using (var b = new Bitmap(second)) {
            if (a.Size != b.Size) throw new InvalidOperationException("Screenshot dimensions changed.");
            for (int y=overlay.Top-capture.Top; y<overlay.Bottom-capture.Top; ++y)
                for (int x=overlay.Left-capture.Left; x<overlay.Right-capture.Left; ++x) {
                    var p=a.GetPixel(x,y); var q=b.GetPixel(x,y);
                    int difference=Math.Max(Math.Abs(p.R-q.R),Math.Max(Math.Abs(p.G-q.G),Math.Abs(p.B-q.B)));
                    if (difference >= 16) ++result.Changed;
                    total += difference; ++result.Pixels;
                }
        }
        result.MeanDifference = result.Pixels == 0 ? 0 : (double)total/result.Pixels;
        return result;
    }
}

