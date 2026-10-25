#include "desktop_hotkey.h"
#include <QCoreApplication>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
namespace dragon {
namespace {
DesktopHotkey *active=nullptr;
#ifdef Q_OS_WIN
LRESULT CALLBACK keyboard(int code,WPARAM message,LPARAM data){
    if(code==HC_ACTION && active){
        const auto *key=reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
        if(key->vkCode=='Q' && !(key->flags&LLKHF_INJECTED)){
            const auto foreground=GetForegroundWindow();wchar_t cls[128]{};GetClassNameW(foreground,cls,128);
            const auto name=QString::fromWCharArray(cls);
            bool desktop=foreground && (foreground==GetShellWindow() || name=="Progman" || name=="WorkerW");
            GUITHREADINFO focus{};focus.cbSize=sizeof(focus);
            if(desktop && GetGUIThreadInfo(GetWindowThreadProcessId(foreground,nullptr),&focus) && focus.hwndFocus){
                wchar_t focusClass[128]{};GetClassNameW(focus.hwndFocus,focusClass,128);const auto type=QString::fromWCharArray(focusClass);
                desktop=type=="SysListView32" || type=="SHELLDLL_DefView" || type=="Progman" || type=="WorkerW";
            }
            const bool modifiers=(GetAsyncKeyState(VK_CONTROL)|GetAsyncKeyState(VK_MENU)|GetAsyncKeyState(VK_SHIFT)|GetAsyncKeyState(VK_LWIN)|GetAsyncKeyState(VK_RWIN))&0x8000;
            active->observeQ(message==WM_KEYDOWN || message==WM_SYSKEYDOWN,desktop && !modifiers,qint64(GetTickCount64()));
        }
    }
    return CallNextHookEx(nullptr,code,message,data);
}
#endif
}
DesktopHotkey::DesktopHotkey(QObject *parent):QObject(parent){static int serial=0;id_=0x4400+(++serial%1000);QCoreApplication::instance()->installNativeEventFilter(this);}
DesktopHotkey::~DesktopHotkey(){configure("off");QCoreApplication::instance()->removeNativeEventFilter(this);}
QString DesktopHotkey::configure(const QString &sequence){
    const auto value=sequence.trimmed();
    if(value!="off" && value!="Q" && value!="Ctrl+Q" && value!="Alt+Q" && value!="Ctrl+Alt+Q" && value!="Ctrl+Alt+S")return "请选择 Q、Ctrl+Q、Alt+Q、Ctrl+Alt+Q 或 off";
#ifdef Q_OS_WIN
    if(registered_){UnregisterHotKey(nullptr,id_);registered_=false;}
    if(hook_){UnhookWindowsHookEx(static_cast<HHOOK>(hook_));hook_=nullptr;}
    if(active==this)active=nullptr;
    sequence_="off";down_=false;lastTap_=-10000;
    if(value=="Q"){
        if(active && active!=this)return "Q 快捷键已被另一个实例占用";
        active=this;hook_=SetWindowsHookExW(WH_KEYBOARD_LL,keyboard,GetModuleHandleW(nullptr),0);
        if(!hook_){active=nullptr;return "Windows 未允许安装 Q 观察器；请使用组合键或托盘";}
    }else if(value!="off"){
        UINT modifiers=MOD_NOREPEAT;
        if(value.contains("Ctrl"))modifiers|=MOD_CONTROL;
        if(value.contains("Alt"))modifiers|=MOD_ALT;
        if(!RegisterHotKey(nullptr,id_,modifiers,value.endsWith('S')?'S':'Q'))return "快捷键注册失败，可能已被其他软件占用；请改用另一组合键";
        registered_=true;
    }
    sequence_=value;return {};
#else
    sequence_="off";return value=="off"?QString{}:QString("全局快捷键仅支持 Windows");
#endif
}
void DesktopHotkey::observeQ(bool down,bool allowed,qint64 ms){
    if(!allowed){down_=down;lastTap_=-10000;return;}
    if(down){if(down_)return;down_=true;return;}
    if(!down_)return;down_=false;
    if(ms-lastTap_>=40 && ms-lastTap_<=350){lastTap_=-10000;QMetaObject::invokeMethod(this,[this]{emit activated();},Qt::QueuedConnection);}
    else lastTap_=ms;
}
bool DesktopHotkey::nativeEventFilter(const QByteArray &,void *message,qintptr *){
#ifdef Q_OS_WIN
    const auto *event=static_cast<MSG*>(message);
    if(registered_ && event->message==WM_HOTKEY && event->wParam==quintptr(id_)){emit activated();return true;}
#else
    Q_UNUSED(message)
#endif
    return false;
}
}
