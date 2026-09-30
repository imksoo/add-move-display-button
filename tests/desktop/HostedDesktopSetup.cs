// Complete only the known first-logon privacy page on a disposable CI desktop.
// No registry edits, process termination, desktop switching, or account sign-in.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Web.Script.Serialization;
using System.Windows.Automation;

public static class HostedDesktopSetup
{
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string title);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr hwnd, int attribute, out uint value, int bytes);
    public sealed class Result {
        public bool Success; public string Error, Stage, StartedUtc, FinishedUtc;
        public int SessionId, Pid; public List<object> Observations=new List<object>();
        public List<object> Actions=new List<object>();
    }
    static string Name(AutomationElement e) { return Regex.Replace(e.Current.Name ?? "", @"\s+", " ").Trim(); }
    static void Save(Result r, string output, string stage) {
        r.Stage=stage;
        var json=new JavaScriptSerializer(); json.MaxJsonLength=8*1024*1024;
        File.WriteAllText(Path.Combine(output,"desktop-setup.json"),json.Serialize(r),new UTF8Encoding(false));
    }
    static List<AutomationElement> Descendants(AutomationElement root) {
        var result=new List<AutomationElement>();
        var all=root.FindAll(TreeScope.Descendants,Condition.TrueCondition);
        if (all.Count>2000) throw new InvalidOperationException("Unexpectedly large setup UI; refusing automation.");
        foreach (AutomationElement element in all) result.Add(element);
        return result;
    }
    static object Describe(AutomationElement e) {
        object pattern; string toggle=null;
        if (e.TryGetCurrentPattern(TogglePattern.Pattern,out pattern)) toggle=((TogglePattern)pattern).Current.ToggleState.ToString();
        var c=e.Current;
        return new { Name=Name(e), Id=c.AutomationId, Type=c.ControlType.ProgrammaticName,
            c.ClassName, c.ProcessId, c.IsEnabled, c.IsOffscreen, Toggle=toggle,
            Bounds=c.BoundingRectangle.ToString() };
    }
    static AutomationElement FindHost(Result r) {
        AutomationElement found=null;
        IntPtr hwnd=IntPtr.Zero;
        while ((hwnd=FindWindowEx(IntPtr.Zero,hwnd,"Windows.UI.Core.CoreWindow","Microsoft account"))!=IntPtr.Zero) {
            uint pid, cloaked;
            if (!IsWindowVisible(hwnd)) continue;
            if (DwmGetWindowAttribute(hwnd,14,out cloaked,4)<0 || cloaked!=0) continue;
            GetWindowThreadProcessId(hwnd,out pid);
            using (var process=Process.GetProcessById((int)pid)) {
                if (process.ProcessName!="WWAHost" || process.SessionId!=r.SessionId) continue;
                if (found!=null) throw new InvalidOperationException("More than one matching setup host.");
                // The UIA desktop children view omits this shell-band window on the Arm image.
                // Resolve the verified native HWND directly; never infer absence from UIA alone.
                found=AutomationElement.FromHandle(hwnd); r.Pid=process.Id;
            }
        }
        return found;
    }
    public static Result CompletePrivacyPage(string output) {
        var r=new Result { StartedUtc=DateTime.UtcNow.ToString("o"), SessionId=Process.GetCurrentProcess().SessionId };
        Save(r,output,"starting");
        try {
            if (r.SessionId==0 || !Environment.UserInteractive) throw new InvalidOperationException("Interactive user session required.");
            bool accepted=false; int choices=0;
            var seen=new HashSet<string>();
            var timer=Stopwatch.StartNew();
            for (int page=0; page<8 && timer.ElapsedMilliseconds<60000; page++) {
                var root=FindHost(r);
                if (root==null) { r.Success=true; break; } // The strict pixel preflight still decides readiness.
                var elements=Descendants(root);
                var snapshot=new List<object>(); foreach (var e in elements) snapshot.Add(Describe(e));
                r.Observations.Add(new { Utc=DateTime.UtcNow.ToString("o"), Page=page, Root=Describe(root), Elements=snapshot });
                Save(r,output,"observed-page-"+page);
                if (accepted) { Thread.Sleep(1000); continue; }
                bool privacy=elements.Exists(delegate(AutomationElement e) { return Name(e)=="Choose privacy settings for your device"; });
                if (!privacy) throw new InvalidOperationException("Matching host is not the known privacy page; see UI inventory. No action taken on this page.");
                int pageChoices=0;
                foreach (var e in elements) {
                    object pattern;
                    if (!e.TryGetCurrentPattern(TogglePattern.Pattern,out pattern)) continue;
                    if (!e.Current.IsEnabled || e.Current.IsOffscreen) continue;
                    var toggle=(TogglePattern)pattern;
                    string key=String.Join(",",e.GetRuntimeId());
                    if (seen.Add(key)) choices++;
                    if (choices>7) throw new InvalidOperationException("Unexpected number of privacy choices.");
                    string before=toggle.Current.ToggleState.ToString();
                    if (toggle.Current.ToggleState==ToggleState.Indeterminate) throw new InvalidOperationException("Indeterminate privacy choice.");
                    if (toggle.Current.ToggleState==ToggleState.On) { toggle.Toggle(); Thread.Sleep(200); }
                    if (toggle.Current.ToggleState!=ToggleState.Off) throw new InvalidOperationException("Privacy choice did not become Off.");
                    pageChoices++;
                    r.Actions.Add(new { Utc=DateTime.UtcNow.ToString("o"), Action="privacy-off", Name=Name(e), Id=e.Current.AutomationId, Before=before, After="Off" });
                    Save(r,output,"privacy-choice");
                }
                AutomationElement next=null, accept=null;
                foreach (var e in elements) {
                    if (e.Current.ControlType!=ControlType.Button || !e.Current.IsEnabled || e.Current.IsOffscreen) continue;
                    if (Name(e)=="Next") { if (next!=null) throw new InvalidOperationException("Ambiguous Next."); next=e; }
                    if (Name(e)=="Accept") { if (accept!=null) throw new InvalidOperationException("Ambiguous Accept."); accept=e; }
                }
                var button=accept ?? next;
                if (button==null || pageChoices==0) throw new InvalidOperationException("Expected privacy controls not exposed; see UI inventory.");
                if (accept!=null && choices<5) throw new InvalidOperationException("Not all expected privacy choices were observed before Accept.");
                object invoke;
                if (!button.TryGetCurrentPattern(InvokePattern.Pattern,out invoke)) throw new InvalidOperationException("Privacy button does not support Invoke.");
                string action=Name(button);
                r.Actions.Add(new { Utc=DateTime.UtcNow.ToString("o"), Action=action, ChoicesObserved=choices });
                Save(r,output,"invoking-"+action);
                ((InvokePattern)invoke).Invoke();
                if (accept!=null) accepted=true;
                Thread.Sleep(1000);
            }
            if (!r.Success) throw new InvalidOperationException("Privacy host did not complete within the setup deadline.");
        } catch (Exception e) { r.Success=false; r.Error=e.ToString(); }
        finally { r.FinishedUtc=DateTime.UtcNow.ToString("o"); Save(r,output,"complete"); }
        return r;
    }
}
