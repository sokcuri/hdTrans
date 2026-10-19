using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

class Probe {
    delegate bool EnumCallback(IntPtr window, IntPtr arg);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumCallback callback, IntPtr arg);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumCallback callback, IntPtr arg);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr window, StringBuilder text, int length);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window, StringBuilder text, int length);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] static extern IntPtr GetMenu(IntPtr window);
    [DllImport("user32.dll")] static extern int GetMenuItemCount(IntPtr menu);
    [DllImport("user32.dll")] static extern IntPtr GetSubMenu(IntPtr menu, int position);
    [DllImport("user32.dll")] static extern uint GetMenuItemID(IntPtr menu, int position);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetMenuString(IntPtr menu, uint item, StringBuilder text, int length, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wparam, string lparam);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr LoadLibrary(string file);
    [DllImport("kernel32.dll", CharSet=CharSet.Ansi)] static extern IntPtr GetProcAddress(IntPtr module, string name);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate IntPtr TranslateWide(IntPtr text);
    static string root, app, evidence;
    static string Text(IntPtr h) { var s = new StringBuilder(32768); GetWindowText(h,s,s.Capacity); return s.ToString(); }
    static string Class(IntPtr h) { var s = new StringBuilder(512); GetClassName(h,s,s.Capacity); return s.ToString(); }
    static void Menus(IntPtr menu, string prefix) {
        if (menu == IntPtr.Zero) return;
        for (int i=0; i<GetMenuItemCount(menu); ++i) {
            var s = new StringBuilder(512); GetMenuString(menu,(uint)i,s,s.Capacity,0x400);
            Console.WriteLine("MENU " + prefix + i + " id=" + GetMenuItemID(menu,i) + " text=" + s);
            Menus(GetSubMenu(menu,i),prefix+i+"/");
        }
    }
    static List<IntPtr> Windows(int pid) {
        var result = new List<IntPtr>();
        EnumWindows(delegate(IntPtr h, IntPtr arg) { uint p; GetWindowThreadProcessId(h,out p); if (p==pid) result.Add(h); return true; },IntPtr.Zero);
        return result;
    }
    static void Dump(int pid, string stage) {
        Console.WriteLine("STAGE " + stage);
        foreach (var h in Windows(pid)) {
            Console.WriteLine("WINDOW " + h + " visible=" + IsWindowVisible(h) + " class=" + Class(h) + " text=" + Text(h));
            if (!IsWindowVisible(h)) continue;
            Menus(GetMenu(h),"");
            EnumChildWindows(h,delegate(IntPtr c, IntPtr arg) { Console.WriteLine("CONTROL " + c + " visible=" + IsWindowVisible(c) + " class=" + Class(c) + " text=" + Text(c)); return true; },IntPtr.Zero);
        }
        try {
            using (var bitmap = new Bitmap(Screen.PrimaryScreen.Bounds.Width,Screen.PrimaryScreen.Bounds.Height)) {
                using(var g=Graphics.FromImage(bitmap)) g.CopyFromScreen(0,0,0,0,bitmap.Size);
                bitmap.Save(Path.Combine(evidence,stage+".png"),ImageFormat.Png);
            }
        } catch(Exception ex) { Console.WriteLine("SCREENSHOT " + ex.GetType().Name + " " + ex.Message); }
    }
    static void Modules(int pid) {
        try { foreach (ProcessModule module in Process.GetProcessById(pid).Modules) {
            string name=module.ModuleName;
            if (name.IndexOf("ehnd",StringComparison.OrdinalIgnoreCase)>=0 || name.IndexOf("eztrans",StringComparison.OrdinalIgnoreCase)>=0)
                Console.WriteLine("LOADED " + name + " sha256=" + Hash(module.FileName));
        }} catch(Exception ex) { Console.WriteLine("MODULES " + ex.Message); }
    }
    static string Hash(string file) {
        using(var sha=System.Security.Cryptography.SHA256.Create()) using(var stream=File.OpenRead(file))
            return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-","").ToLowerInvariant();
    }
    static int Plugin() {
        Directory.SetCurrentDirectory(app);
        Console.WriteLine("PLUGIN actual bundled dll/CustomData/ezTransXP.dll, normal LoadLibrary initialization");
        Console.WriteLine("PLUGIN sha256=" + Hash(Path.Combine(app,"dll","CustomData","ezTransXP.dll")));
        IntPtr module=LoadLibrary(Path.Combine(app,"dll","CustomData","ezTransXP.dll"));
        Console.WriteLine("LoadLibrary=" + module + " error=" + Marshal.GetLastWin32Error());
        if(module==IntPtr.Zero) return 2;
        Modules(Process.GetCurrentProcess().Id);
        IntPtr address=GetProcAddress(module,"TranslateW");
        Console.WriteLine("Actual plugin TranslateW=" + address);
        if(address==IntPtr.Zero) return 3;
        var translate=(TranslateWide)Marshal.GetDelegateForFunctionPointer(address,typeof(TranslateWide));
        bool passed=true;
        foreach(string input in new[]{"猫", "水", "こんにちは。"}) {
            IntPtr buffer=Marshal.AllocHGlobal(65536);
            byte[] bytes=Encoding.Unicode.GetBytes(input+"\0"); Marshal.Copy(bytes,0,buffer,bytes.Length);
            Console.WriteLine("CALL actual-plugin TranslateW input=" + input);
            IntPtr returned=translate(buffer);
            string output=Marshal.PtrToStringUni(buffer);
            Console.WriteLine("RESULT return="+returned+" output="+output);
            if(input=="猫") { Console.WriteLine("EXPECTED 猫 -> 고양이"); passed &= output=="고양이" && returned!=IntPtr.Zero; }
            if(output==input || String.IsNullOrEmpty(output)) passed=false;
            Marshal.FreeHGlobal(buffer);
        }
        Modules(Process.GetCurrentProcess().Id);
        Console.WriteLine("PLUGIN_RESULT " + (passed?"PASS":"FAIL"));
        return passed?0:4;
    }
    static int Gui() {
        var start=new ProcessStartInfo(Path.Combine(app,"TwoControl.exe")); start.WorkingDirectory=app; start.UseShellExecute=false;
        var process=Process.Start(start);
        Console.WriteLine("GUI actual TwoControl.exe pid="+process.Id+" sha256="+Hash(start.FileName));
        Thread.Sleep(12000);
        if(process.HasExited) { Console.WriteLine("GUI exited="+process.ExitCode); return 5; }
        Dump(process.Id,"gui-start"); Modules(process.Id);
        Clipboard.SetText("猫");
        Console.WriteLine("GUI clipboard authored input=猫");
        Thread.Sleep(5000);
        Dump(process.Id,"gui-clipboard"); Modules(process.Id);
        process.Kill(); process.WaitForExit();
        Console.WriteLine("GUI_RESULT discovery only; translation not asserted");
        return 0;
    }
    [STAThread] static int Main(string[] args) {
        Console.OutputEncoding=new UTF8Encoding(false);
        root=Path.GetFullPath(args[1]); app=Path.Combine(root,"application"); evidence=Path.Combine(root,"evidence");
        try { if(args[0]=="inspect") { Dump(Int32.Parse(args[2]),"plugin-blocked"); Modules(Int32.Parse(args[2])); return 0; } return args[0]=="plugin"?Plugin():Gui(); }
        catch(Exception ex) { Console.WriteLine("ERROR "+ex); return 10; }
    }
}
