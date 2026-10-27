$ErrorActionPreference = 'Stop'
[Console]::InputEncoding = [Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type @'
using System; using System.Runtime.InteropServices;
public class DragonDesktop {
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
 [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a,uint b,bool attach);
 [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint x,uint y,uint d,UIntPtr e);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr h,System.Text.StringBuilder b,int n);
 [DllImport("user32.dll")] static extern int GetWindowLongW(IntPtr h,int n);
 [DllImport("user32.dll",SetLastError=true)] static extern IntPtr SendMessageTimeoutW(IntPtr h,uint m,IntPtr w,IntPtr l,uint flags,uint timeout,out UIntPtr result);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageTimeoutW")] static extern IntPtr SendText(IntPtr h,uint m,IntPtr w,string text,uint flags,uint timeout,out UIntPtr result);
 public static string ClassName(IntPtr h){var name=new System.Text.StringBuilder(256);GetClassNameW(h,name,256);return name.ToString();}
 public static bool SetText(IntPtr h,string text){if(Password(h)||ClassName(h).IndexOf("edit",StringComparison.OrdinalIgnoreCase)<0)return false;UIntPtr r;return SendText(h,0xC,IntPtr.Zero,text,2,1000,out r)!=IntPtr.Zero&&r.ToUInt64()!=0;}
 public static bool ClickButton(IntPtr h){if(ClassName(h).IndexOf("button",StringComparison.OrdinalIgnoreCase)<0)return false;UIntPtr r;return SendMessageTimeoutW(h,0xF5,IntPtr.Zero,IntPtr.Zero,2,1000,out r)!=IntPtr.Zero;}
 public static bool Password(IntPtr h) {if(h==IntPtr.Zero)return false;var name=new System.Text.StringBuilder(256);GetClassNameW(h,name,256);if(name.ToString().IndexOf("edit",StringComparison.OrdinalIgnoreCase)<0)return false;if((GetWindowLongW(h,-16)&0x20)!=0)return true;UIntPtr c;return SendMessageTimeoutW(h,0xD2,IntPtr.Zero,IntPtr.Zero,2,100,out c)!=IntPtr.Zero&&c.ToUInt64()!=0;}
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint a,bool b,uint p);
 [DllImport("advapi32.dll",SetLastError=true)] public static extern bool OpenProcessToken(IntPtr p,uint a,out IntPtr t);
 [DllImport("advapi32.dll",SetLastError=true)] public static extern bool GetTokenInformation(IntPtr t,int c,out int v,int n,out int r);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
 public static bool Elevated(IntPtr h) {uint p; GetWindowThreadProcessId(h,out p); IntPtr ph=OpenProcess(0x1000,false,p),t; if(ph==IntPtr.Zero) return true; try {if(!OpenProcessToken(ph,8,out t))return true; try {int v,r;return !GetTokenInformation(t,20,out v,4,out r)||v!=0;} finally{CloseHandle(t);}}finally{CloseHandle(ph);}}
}
'@
$request = [Console]::In.ReadToEnd() | ConvertFrom-Json
$argsObject = $request.args
if ($request.tool -eq 'desktop_launch') {
    $apps = @{notepad='notepad.exe';calculator='calc.exe';explorer='explorer.exe'}
    if (!$apps.ContainsKey([string]$argsObject.app)) { throw 'Application not allowed' }
    Start-Process -FilePath $apps[[string]$argsObject.app] -WindowStyle Hidden
    @{launched=$true;app=$argsObject.app} | ConvertTo-Json -Compress
    exit
}
$handle = [DragonDesktop]::GetForegroundWindow()
if ($request.tool -eq 'desktop_focus') { $handle = [IntPtr]::new([long]$argsObject.handle) }
if ($argsObject.focus -eq $true) { $handle = [IntPtr]::new([long]$argsObject.handle) }
if ($request.tool -eq 'desktop_inspect' -and $argsObject.handle) { $handle = [IntPtr]::new([long]$argsObject.handle) }
if ($handle -eq [IntPtr]::Zero -or [DragonDesktop]::Elevated($handle)) { throw 'Elevated or unavailable window denied' }
$window = [Windows.Automation.AutomationElement]::FromHandle($handle)
if ($window.Current.Name -match '(?i)密码|登录|支付|password|sign.?in|login|payment|incognito|无痕') { throw 'Sensitive window denied' }
function Test-PrivateElement($element) {
    $walk=$element
    for ($depth=0; $walk -and $depth -lt 24; $depth++) {
        try {
            if ([DragonDesktop]::Password([IntPtr]::new($walk.Current.NativeWindowHandle)) -or $walk.Current.IsPassword -or $walk.Current.Name -match '(?i)密码|口令|验证码|password|passwd|token|secret|credential' -or $walk.Current.AutomationId -match '(?i)password|passwd|token|secret|credential') { return $true }
            if ($walk -eq $window) { break }
            $walk=[Windows.Automation.TreeWalker]::RawViewWalker.GetParent($walk)
        } catch { return $true }
    }
    return $false
}
if ($request.tool -eq 'desktop_focus' -or $argsObject.focus -eq $true) {
    [DragonDesktop]::ShowWindow($handle,9) | Out-Null
    [uint32]$foregroundProcess=0
    $foregroundThread=[DragonDesktop]::GetWindowThreadProcessId([DragonDesktop]::GetForegroundWindow(),[ref]$foregroundProcess)
    $currentThread=[DragonDesktop]::GetCurrentThreadId()
    $attached=$false
    try {
        if ($foregroundThread -ne $currentThread) { $attached=[DragonDesktop]::AttachThreadInput($currentThread,$foregroundThread,$true) }
        [DragonDesktop]::BringWindowToTop($handle) | Out-Null
        [DragonDesktop]::SetForegroundWindow($handle) | Out-Null
    } finally { if ($attached) { [DragonDesktop]::AttachThreadInput($currentThread,$foregroundThread,$false) | Out-Null } }
    $ok=[DragonDesktop]::GetForegroundWindow() -eq $handle
    if ($request.tool -eq 'desktop_focus') { @{focused=$ok;title=$window.Current.Name} | ConvertTo-Json -Compress; exit }
    if (!$ok) { throw 'Unable to activate requested window' }
}
if ($request.tool -eq 'desktop_key') {
    if ([long]$argsObject.handle -ne [DragonDesktop]::GetForegroundWindow().ToInt64()) { throw 'Foreground window changed' }
    $focused = [Windows.Automation.AutomationElement]::FocusedElement
    if (!$focused -or (Test-PrivateElement $focused)) { throw 'Focused password/sensitive control denied' }
    $keys = @{tab='{TAB}';enter='{ENTER}';escape='{ESC}';up='{UP}';down='{DOWN}';left='{LEFT}';right='{RIGHT}'}
    if (!$keys.ContainsKey([string]$argsObject.key)) { throw 'Key not allowed' }
    Add-Type -AssemblyName System.Windows.Forms
    [Windows.Forms.SendKeys]::SendWait($keys[[string]$argsObject.key])
    @{performed=$true;key=$argsObject.key} | ConvertTo-Json -Compress
    exit
}
if ($request.tool -in @('desktop_invoke','desktop_fill','desktop_click')) {
    if ([long]$argsObject.handle -ne [DragonDesktop]::GetForegroundWindow().ToInt64()) { throw 'Foreground window changed; inspect again' }
    $condition = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,[string]$argsObject.automation_id)
    $matches = $window.FindAll([Windows.Automation.TreeScope]::Descendants,$condition)
    if ($matches.Count -ne 1) { throw 'AutomationId must identify exactly one control' }
    $target = $matches.Item(0)
    if ((Test-PrivateElement $target) -or !$target.Current.IsEnabled) { throw 'Sensitive or disabled control denied' }
    if ($request.tool -eq 'desktop_click') {
        $bounds = $target.Current.BoundingRectangle
        if ($target.Current.IsOffscreen -or $bounds.IsEmpty) { throw 'Control not visible' }
        [DragonDesktop]::SetCursorPos([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2)) | Out-Null
        [DragonDesktop]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
        [DragonDesktop]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
    } elseif ($request.tool -eq 'desktop_invoke') {
        $pattern=$null
        if ($target.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern,[ref]$pattern)) { $pattern.Invoke() }
        elseif (![DragonDesktop]::ClickButton([IntPtr]::new($target.Current.NativeWindowHandle))) { throw 'Control has no supported InvokePattern or native button action' }
    } else {
        $pattern=$null
        if ($target.TryGetCurrentPattern([Windows.Automation.ValuePattern]::Pattern,[ref]$pattern)) { $pattern.SetValue([string]$argsObject.text) }
        elseif (![DragonDesktop]::SetText([IntPtr]::new($target.Current.NativeWindowHandle),[string]$argsObject.text)) { throw 'Control has no supported ValuePattern or native editable text action' }
    }
    @{performed=$true;automation_id=$argsObject.automation_id} | ConvertTo-Json -Compress
    exit
}
if ($request.tool -ne 'desktop_inspect') { throw 'Unknown desktop operation' }
$nodes = $window.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)
$privateNodes=@{}
for ($i=0; $i -lt [Math]::Min(150,$nodes.Count); $i++) {
    $candidate=$nodes.Item($i)
    if ([DragonDesktop]::Password([IntPtr]::new($candidate.Current.NativeWindowHandle)) -or $candidate.Current.IsPassword -or $candidate.Current.Name -match '(?i)password|passwd|token|secret|credential|密码|口令|验证码' -or $candidate.Current.AutomationId -match '(?i)password|passwd|token|secret|credential') {
        $privateNodes[($candidate.GetRuntimeId() -join ',')]=$true
        $children=$candidate.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)
        for ($j=0;$j -lt $children.Count;$j++) { $privateNodes[($children.Item($j).GetRuntimeId() -join ',')]=$true }
    }
}
$items = @()
for ($i=0; $i -lt [Math]::Min(150,$nodes.Count); $i++) {
    $node = $nodes.Item($i)
    if ($privateNodes.ContainsKey(($node.GetRuntimeId() -join ','))) { continue }
    # Return labels and locators, never ValuePattern contents or password values.
    $items += @{name=$node.Current.Name;automation_id=$node.Current.AutomationId;type=$node.Current.ControlType.ProgrammaticName;enabled=$node.Current.IsEnabled}
}
@{handle=$handle.ToInt64();title=$window.Current.Name;controls=$items;untrusted=$true} | ConvertTo-Json -Depth 5 -Compress
