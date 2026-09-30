// Read-only desktop diagnostics. A capture failure is never a product verdict.
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;
using System.Windows.Forms;

public static class DesktopReadiness
{
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct NativePoint { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] struct UserFlags { public int Inherit, Reserved; public uint Flags; }
    delegate bool EnumProc(IntPtr hwnd, IntPtr data);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr hwnd);
    [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr hwnd, IntPtr dc);
    [DllImport("user32.dll", SetLastError=true)] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] static extern IntPtr GetThreadDpiAwarenessContext();
    [DllImport("user32.dll")] static extern int GetAwarenessFromDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr hwnd);
    [DllImport("gdi32.dll")] static extern uint GetPixel(IntPtr dc, int x, int y);
    [DllImport("gdi32.dll", SetLastError=true)] static extern bool BitBlt(IntPtr to, int x, int y, int w, int h, IntPtr from, int sx, int sy, uint operation);
    [DllImport("user32.dll", SetLastError=true)] static extern IntPtr GetProcessWindowStation();
    [DllImport("user32.dll", SetLastError=true)] static extern IntPtr GetThreadDesktop(uint tid);
    [DllImport("user32.dll", SetLastError=true)] static extern IntPtr OpenInputDesktop(uint flags, bool inherit, uint access);
    [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr desktop);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool GetUserObjectInformationW(IntPtr handle, int index, IntPtr data, uint bytes, out uint needed);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool IsWow64Process2(IntPtr process, out ushort processMachine, out ushort nativeMachine);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr hwnd, StringBuilder text, int length);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowTextW(IntPtr hwnd, StringBuilder text, int length);
    [DllImport("user32.dll", SetLastError=true)] static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] static extern bool IsIconic(IntPtr hwnd);
    [DllImport("user32.dll")] static extern IntPtr WindowFromPoint(NativePoint point);
    [DllImport("user32.dll")] static extern IntPtr GetAncestor(IntPtr hwnd, uint flags);
    [DllImport("user32.dll", SetLastError=true)] static extern bool EnumDesktopWindows(IntPtr desktop, EnumProc callback, IntPtr data);
    [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr hwnd, int attribute, out uint value, int bytes);
    [DllImport("dwmapi.dll")] static extern int DwmIsCompositionEnabled(out bool enabled);
    [DllImport("wtsapi32.dll", SetLastError=true)] static extern bool WTSQuerySessionInformationW(IntPtr server, int session, int info, out IntPtr data, out int bytes);
    [DllImport("wtsapi32.dll")] static extern void WTSFreeMemory(IntPtr data);

    public sealed class ObjectInfo {
        public string Name; public int NameError; public bool QueryOk, Input; public int QueryError; public uint Flags;
    }
    public sealed class WindowInfo {
        public long Hwnd; public uint Pid, Tid, Dpi, Cloaked; public int SessionId = -1, RectError, CloakHresult;
        public string Class, Title, Process; public Rect Bounds; public bool Visible, Iconic;
    }
    public sealed class Context {
        public string Utc; public uint Tid; public int Pid, SessionId, WtsState = -1, WtsError, InputOpenError;
        public ObjectInfo Station, ThreadDesktop, InputDesktop; public bool Matches;
    }
    public sealed class Sample {
        public string Expected, Actual, BitmapActual, Screenshot, Utc, Error;
        public uint RawColor; public bool PixelMatches, ForegroundMatches, BitmapMatches, Ready;
        public long StartedTicks, FinishedTicks; public int Phase, PaintCount, EraseCount, LastPaintPhase;
        public NativePoint ScreenPoint; public Rect CaptureRect; public int BitmapX, BitmapY, DpiAwareness;
        public Context Before, After; public WindowInfo Sentinel, ForegroundBefore, ForegroundAfter, AtPoint;
    }
    public sealed class Report {
        public int SchemaVersion = 1; public string Stage, Error, StartedUtc, FinishedUtc, Apartment, FullScreen;
        public long DpiPreviousContext; public int DpiSetError, DwmHresult; public bool DwmEnabled;
        public string ProcessMachine, NativeMachine; public int MachineError;
        public List<Sample> Original = new List<Sample>(), Late = new List<Sample>();
        public Dictionary<string, object> Windows = new Dictionary<string, object>();
        public List<object> Processes = new List<object>();
        public bool LegacyPixelPass, Ready;
    }
    sealed class Sentinel : Form {
        public int Phase, PaintCount, EraseCount, LastPaintPhase = -1;
        protected override void WndProc(ref Message m) {
            int message = m.Msg;
            base.WndProc(ref m);
            if (message == 0x000f) { PaintCount++; LastPaintPhase = Phase; }
            if (message == 0x0014) EraseCount++;
        }
    }
    static string Rgb(Color color) { return String.Format("#{0:X2}{1:X2}{2:X2}", color.R, color.G, color.B); }
    static string Hex(uint n) { return String.Format("0x{0:X8}", n); }
    static Rect RectangleValue(Rectangle r) { return new Rect { Left=r.Left, Top=r.Top, Right=r.Right, Bottom=r.Bottom }; }
    static void Save(Report report, string output, string stage) {
        report.Stage = stage;
        var serializer = new JavaScriptSerializer(); serializer.MaxJsonLength = 8 * 1024 * 1024;
        File.WriteAllText(Path.Combine(output, "desktop-diagnostic.json"), serializer.Serialize(report), new UTF8Encoding(false));
    }
    static ObjectInfo UserObject(IntPtr handle, bool desktop) {
        var r = new ObjectInfo();
        IntPtr buffer = Marshal.AllocHGlobal(1024);
        try {
            uint needed;
            if (GetUserObjectInformationW(handle, 2, buffer, 1024, out needed)) r.Name = Marshal.PtrToStringUni(buffer);
            else r.NameError = Marshal.GetLastWin32Error();
            int index = desktop ? 6 : 1;
            r.QueryOk = GetUserObjectInformationW(handle, index, buffer, 1024, out needed);
            if (!r.QueryOk) r.QueryError = Marshal.GetLastWin32Error();
            else if (desktop) r.Input = Marshal.ReadInt32(buffer) != 0;
            else r.Flags = ((UserFlags)Marshal.PtrToStructure(buffer, typeof(UserFlags))).Flags;
        } finally { Marshal.FreeHGlobal(buffer); }
        return r;
    }
    static Context Snapshot() {
        var r = new Context { Utc=DateTime.UtcNow.ToString("o"), Tid=GetCurrentThreadId() };
        using (var p = Process.GetCurrentProcess()) { r.Pid=p.Id; r.SessionId=p.SessionId; }
        r.Station = UserObject(GetProcessWindowStation(), false);
        r.ThreadDesktop = UserObject(GetThreadDesktop(r.Tid), true);
        IntPtr input = OpenInputDesktop(0, false, 0x0001); // DESKTOP_READOBJECTS only
        if (input == IntPtr.Zero) r.InputOpenError = Marshal.GetLastWin32Error();
        else { try { r.InputDesktop=UserObject(input, true); } finally { CloseDesktop(input); } }
        IntPtr data; int bytes;
        if (WTSQuerySessionInformationW(IntPtr.Zero, r.SessionId, 8, out data, out bytes)) {
            try { if (bytes >= 4) r.WtsState=Marshal.ReadInt32(data); } finally { WTSFreeMemory(data); }
        } else r.WtsError=Marshal.GetLastWin32Error();
        r.Matches = r.WtsState == 0 && r.Station.Name == "WinSta0" && r.Station.QueryOk && (r.Station.Flags & 1) != 0
            && r.ThreadDesktop.QueryOk && r.ThreadDesktop.Input && r.InputDesktop != null && r.InputDesktop.QueryOk
            && r.InputDesktop.Input && r.ThreadDesktop.Name != null && r.ThreadDesktop.Name == r.InputDesktop.Name;
        return r;
    }
    static WindowInfo Window(IntPtr hwnd) {
        var r = new WindowInfo { Hwnd=hwnd.ToInt64() };
        if (hwnd == IntPtr.Zero) return r;
        r.Tid=GetWindowThreadProcessId(hwnd, out r.Pid);
        var text = new StringBuilder(512); GetClassNameW(hwnd, text, text.Capacity); r.Class=text.ToString();
        text.Clear(); GetWindowTextW(hwnd, text, text.Capacity); r.Title=text.ToString();
        if (!GetWindowRect(hwnd, out r.Bounds)) r.RectError=Marshal.GetLastWin32Error();
        r.Visible=IsWindowVisible(hwnd); r.Iconic=IsIconic(hwnd); r.Dpi=GetDpiForWindow(hwnd);
        r.CloakHresult=DwmGetWindowAttribute(hwnd, 14, out r.Cloaked, 4);
        try { using (var p = Process.GetProcessById((int)r.Pid)) { r.Process=p.ProcessName; r.SessionId=p.SessionId; } } catch { r.Process="unavailable"; }
        return r;
    }
    static Bitmap Capture(IntPtr screen, Rectangle r) {
        var image = new Bitmap(r.Width, r.Height, PixelFormat.Format24bppRgb);
        try {
            using (var g = Graphics.FromImage(image)) {
                var dest=g.GetHdc();
                try { if (!BitBlt(dest, 0, 0, r.Width, r.Height, screen, r.Left, r.Top, 0x40cc0020)) throw new Win32Exception(); }
                finally { g.ReleaseHdc(dest); }
            }
            return image;
        } catch { image.Dispose(); throw; }
    }
    static Sample ObserveColor(Sentinel form, Color color, string output, string filename) {
        var s = new Sample { Expected=Rgb(color), Screenshot=filename, Phase=form.Phase, Utc=DateTime.UtcNow.ToString("o"), StartedTicks=Stopwatch.GetTimestamp() };
        s.Before=Snapshot();
        Point p=form.PointToScreen(new Point(80,60));
        s.ScreenPoint=new NativePoint { X=p.X, Y=p.Y };
        s.DpiAwareness=GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext());
        s.Sentinel=Window(form.Handle); s.ForegroundBefore=Window(GetForegroundWindow());
        s.ForegroundMatches=s.ForegroundBefore.Hwnd == form.Handle.ToInt64();
        s.AtPoint=Window(GetAncestor(WindowFromPoint(s.ScreenPoint), 2));
        s.PaintCount=form.PaintCount; s.EraseCount=form.EraseCount; s.LastPaintPhase=form.LastPaintPhase;
        IntPtr screen=GetDC(IntPtr.Zero);
        if (screen == IntPtr.Zero) throw new InvalidOperationException("GetDC(NULL) returned NULL.");
        try {
            s.RawColor=GetPixel(screen, p.X, p.Y);
            s.Actual=String.Format("#{0:X2}{1:X2}{2:X2}", s.RawColor & 255, (s.RawColor >> 8) & 255, (s.RawColor >> 16) & 255);
            s.PixelMatches=s.RawColor != 0xffffffff && s.Actual == s.Expected;
            Rectangle r=form.Bounds; s.CaptureRect=RectangleValue(r); s.BitmapX=p.X-r.Left; s.BitmapY=p.Y-r.Top;
            using (var image=Capture(screen, r)) {
                image.Save(Path.Combine(output, filename), ImageFormat.Png);
                if (s.BitmapX < 0 || s.BitmapY < 0 || s.BitmapX >= image.Width || s.BitmapY >= image.Height) s.Error="Sample is outside capture rectangle.";
                else { s.BitmapActual=Rgb(image.GetPixel(s.BitmapX, s.BitmapY)); s.BitmapMatches=s.BitmapActual == s.Expected; }
            }
        } finally { ReleaseDC(IntPtr.Zero, screen); }
        s.ForegroundAfter=Window(GetForegroundWindow()); s.After=Snapshot(); s.FinishedTicks=Stopwatch.GetTimestamp();
        s.Ready=s.PixelMatches && s.BitmapMatches && s.ForegroundMatches && s.ForegroundAfter.Hwnd == form.Handle.ToInt64()
            && s.Before.Matches && s.After.Matches && s.Before.SessionId == s.After.SessionId
            && s.Before.ThreadDesktop.Name == s.After.ThreadDesktop.Name && s.Sentinel.Visible && !s.Sentinel.Iconic
            && s.Sentinel.CloakHresult >= 0 && s.Sentinel.Cloaked == 0 && s.Sentinel.SessionId == s.Before.SessionId
            && s.LastPaintPhase == s.Phase && s.PaintCount > 0 && s.DpiAwareness == 2;
        return s;
    }
    static object WindowsOn(IntPtr desktop) {
        var list = new List<WindowInfo>();
        bool ok=EnumDesktopWindows(desktop, delegate(IntPtr hwnd, IntPtr unused) { list.Add(Window(hwnd)); return true; }, IntPtr.Zero);
        int error=ok ? 0 : Marshal.GetLastWin32Error();
        return new { Ok=ok, Error=error, Windows=list };
    }
    static void FailureEvidence(Report r, string output) {
        r.Windows["threadDesktop"]=WindowsOn(GetThreadDesktop(GetCurrentThreadId()));
        IntPtr input=OpenInputDesktop(0, false, 0x0001);
        if (input == IntPtr.Zero) r.Windows["inputDesktopError"]=Marshal.GetLastWin32Error();
        else { try { r.Windows["inputDesktop"]=WindowsOn(input); } finally { CloseDesktop(input); } }
        foreach (string name in new[] {"explorer", "dwm", "CloudExperienceHost", "UserOOBEBroker", "LogonUI"}) {
            foreach (var p in Process.GetProcessesByName(name)) {
                using (p) { try { r.Processes.Add(new { Name=name, Pid=p.Id, SessionId=p.SessionId }); } catch { } }
            }
        }
        Save(r, output, "failure-window-inventory");
        Rectangle bounds=SystemInformation.VirtualScreen;
        if (bounds.Width <= 0 || bounds.Height <= 0 || bounds.Width > 8192 || bounds.Height > 8192) throw new InvalidOperationException("Virtual desktop dimensions outside diagnostic capture limit.");
        IntPtr screen=GetDC(IntPtr.Zero);
        if (screen == IntPtr.Zero) throw new InvalidOperationException("GetDC(NULL) returned NULL for failure capture.");
        try { using (var image=Capture(screen, bounds)) { r.FullScreen="desktop-failure-full.png"; image.Save(Path.Combine(output,r.FullScreen), ImageFormat.Png); } }
        finally { ReleaseDC(IntPtr.Zero, screen); }
    }
    public static Report Observe(string output) {
        var r = new Report { StartedUtc=DateTime.UtcNow.ToString("o"), Apartment=Thread.CurrentThread.GetApartmentState().ToString() };
        Save(r, output, "starting");
        try {
            r.DpiPreviousContext=SetThreadDpiAwarenessContext(new IntPtr(-4)).ToInt64();
            if (r.DpiPreviousContext == 0) r.DpiSetError=Marshal.GetLastWin32Error();
            ushort processMachine, nativeMachine;
            if (IsWow64Process2(new IntPtr(-1), out processMachine, out nativeMachine)) { r.ProcessMachine=Hex(processMachine); r.NativeMachine=Hex(nativeMachine); }
            else r.MachineError=Marshal.GetLastWin32Error();
            r.DwmHresult=DwmIsCompositionEnabled(out r.DwmEnabled);
            if (FindWindow("Shell_TrayWnd", null) == IntPtr.Zero) throw new InvalidOperationException("No shell taskbar in the test desktop.");
            using (var form = new Sentinel()) {
                form.Text="MTMB desktop pixel readiness check"; form.StartPosition=FormStartPosition.Manual;
                var work=Screen.PrimaryScreen.WorkingArea;
                form.Location=new Point(work.Left+40, work.Top+40); form.ClientSize=new Size(320,140);
                form.TopMost=true; form.ShowInTaskbar=false; form.Show(); form.Activate();
                Color[] colors={Color.FromArgb(17,97,173), Color.FromArgb(173,97,17)};
                for (int phase=0; phase<2; phase++) {
                    form.Phase=phase; form.BackColor=colors[phase]; form.Refresh();
                    // Preserve the original two colors and 20 x 50ms message-pump wait.
                    for (int i=0; i<20; i++) { Application.DoEvents(); Thread.Sleep(50); }
                    Save(r, output, "original-capture-"+phase);
                    r.Original.Add(ObserveColor(form, colors[phase], output, "desktop-sentinel-"+phase+".png"));
                    Save(r, output, "original-recorded-"+phase);
                }
                r.LegacyPixelPass=r.Original.Count == 2 && r.Original.TrueForAll(delegate(Sample s) { return s.PixelMatches; });
                r.Ready=r.LegacyPixelPass && r.DpiPreviousContext != 0 && r.Original.TrueForAll(delegate(Sample s) { return s.Ready; });
                Save(r, output, "original-verdict-fixed");
                if (!r.Ready) {
                    try { FailureEvidence(r, output); } catch (Exception e) { r.Error="Failure evidence: "+e.Message; }
                    // Bounded diagnostic-only observations NEVER replace the original verdict.
                    var clock=Stopwatch.StartNew();
                    for (int i=0; i<10 && clock.ElapsedMilliseconds<5000; i++) {
                        int colorIndex=i%2; form.Phase=i+2; form.BackColor=colors[colorIndex]; form.Refresh();
                        for (int j=0; j<5; j++) { Application.DoEvents(); Thread.Sleep(50); }
                        r.Late.Add(ObserveColor(form, colors[colorIndex], output, "desktop-late-"+i+".png"));
                        Save(r, output, "late-recorded-"+i);
                    }
                }
                form.Close(); Application.DoEvents();
            }
        } catch (Exception e) { r.Ready=false; r.Error=e.ToString(); }
        finally { r.FinishedUtc=DateTime.UtcNow.ToString("o"); Save(r, output, "complete"); }
        return r;
    }
}
