#include "screen_capture.h"
#include <QApplication>
#include <QScreen>
#include <QSettings>
#include <QFileInfo>
#include <QCoreApplication>
#include <QRegularExpression>
#include <algorithm>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <uiautomation.h>
#endif
namespace dragon {
namespace {
#ifdef Q_OS_WIN
bool own(HWND h){DWORD pid=0;GetWindowThreadProcessId(h,&pid);return pid==GetCurrentProcessId();}
bool secureDesktop(){
    const auto desk=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);if(!desk)return true;
    wchar_t name[64]{};DWORD size=0;const bool safe=GetUserObjectInformationW(desk,UOI_NAME,name,sizeof(name),&size) && QString::fromWCharArray(name)=="Default";
    CloseDesktop(desk);return !safe;
}
bool passwordFocused(){
    const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    IUIAutomation *automation=nullptr;IUIAutomationElement *element=nullptr;BOOL password=FALSE;
    const auto result=CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_IUIAutomation,reinterpret_cast<void**>(&automation));
    if(SUCCEEDED(result) && SUCCEEDED(automation->GetFocusedElement(&element)) && element){element->get_CurrentIsPassword(&password);element->Release();}
    if(automation)automation->Release();if(SUCCEEDED(initialized))CoUninitialize();return password;
}
QImage readPixels(const QRect &r){
    if(r.isEmpty() || r.width()>16384 || r.height()>16384)return {};
    const auto desktop=GetDC(nullptr);const auto memory=CreateCompatibleDC(desktop);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=r.width();info.bmiHeader.biHeight=-r.height();info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void *pixels=nullptr;const auto bitmap=CreateDIBSection(desktop,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    QImage image;
    if(bitmap && memory){const auto previous=SelectObject(memory,bitmap);
        if(BitBlt(memory,0,0,r.width(),r.height(),desktop,r.x(),r.y(),SRCCOPY|CAPTUREBLT))image=QImage(static_cast<uchar*>(pixels),r.width(),r.height(),QImage::Format_RGB32).copy();
        SelectObject(memory,previous);
    }
    if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);ReleaseDC(nullptr,desktop);return image;
}
#endif
}
CaptureTarget ScreenCapture::inspect(quintptr handle){
    CaptureTarget result;result.handle=handle;
#ifdef Q_OS_WIN
    const auto h=reinterpret_cast<HWND>(handle);if(!IsWindow(h) || !IsWindowVisible(h) || IsIconic(h) || own(h))return {};
    wchar_t title[1024]{};GetWindowTextW(h,title,1024);result.title=QString::fromWCharArray(title);
    DWORD pid=0;GetWindowThreadProcessId(h,&pid);const auto process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(process){wchar_t path[32768]{};DWORD count=32768;if(QueryFullProcessImageNameW(process,0,path,&count))result.process=QFileInfo(QString::fromWCharArray(path)).fileName();CloseHandle(process);}
    RECT bounds{};if(GetWindowRect(h,&bounds))result.bounds=QRect(bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top);
    MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);if(GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&monitor))result.fullscreen=bounds.left<=monitor.rcMonitor.left && bounds.top<=monitor.rcMonitor.top && bounds.right>=monitor.rcMonitor.right && bounds.bottom>=monitor.rcMonitor.bottom;
#endif
    return result;
}
QString ScreenCapture::denied(const CaptureTarget &t,const QStringList &blocked){
    if(!t.handle || t.title.trimmed().isEmpty() || t.process.isEmpty())return "无法验证目标窗口；已暂停读取";
    const auto data=t.title+" "+t.process;
    if(QRegularExpression("password|密码|支付|付款|收银|银行卡|隐私|无痕|inprivate|incognito|private browsing|credential|keepass|1password|bitwarden|银行|banking|登录|sign in|log in",QRegularExpression::CaseInsensitiveOption).match(data).hasMatch())return "敏感窗口已屏蔽，无法截图";
    for(const auto &word:blocked)if(!word.trimmed().isEmpty() && data.contains(word.trimmed(),Qt::CaseInsensitive))return "目标在用户屏蔽列表中";
    return {};
}
ScreenCapture::ScreenCapture(const QString &settingsFile,QObject *parent):QObject(parent),file_(settingsFile),context_(this){
    QSettings settings(file_,QSettings::IniFormat);enabled_=settings.value("screen/enabled",false).toBool();blocked_=settings.value("screen/blocked").toStringList();context_.setEnabled(enabled_ && settings.value("screen/context",false).toBool());
    tracker_.setInterval(1000);tracker_.setTimerType(Qt::CoarseTimer);connect(&tracker_,&QTimer::timeout,this,&ScreenCapture::poll);tracker_.start();poll();
    nativeDeadline_.setSingleShot(true);
    connect(&nativeDeadline_,&QTimer::timeout,this,[this]{cancel();emit failed("原生窗口捕获超时，已终止捕获进程；未读取其他窗口");});
    connect(&nativeCapture_,&QProcess::readyReadStandardOutput,this,[this]{const auto bytes=nativeCapture_.readAllStandardOutput();if(!busy_)return;nativeOutput_+=bytes;if(nativeOutput_.size()>12000000){cancel();emit failed("窗口图像超过内存限制");}});
}
ScreenCapture::~ScreenCapture(){cancel();if(nativeCapture_.state()!=QProcess::NotRunning)nativeCapture_.waitForFinished(1000);}
void ScreenCapture::configure(bool enabled,bool contextEnabled,const QStringList &blocked){
    const auto previous=last_.handle;cancel();enabled_=enabled;blocked_=blocked;last_={};last_.handle=previous;context_.setEnabled(enabled && contextEnabled);
    QSettings settings(file_,QSettings::IniFormat);settings.setValue("screen/enabled",enabled);settings.setValue("screen/context",contextEnabled);settings.setValue("screen/blocked",blocked);settings.sync();poll();
}
void ScreenCapture::poll(){
#ifdef Q_OS_WIN
    const auto foreground=GetForegroundWindow();if(!foreground || own(foreground))return;
    // While disabled only a handle is remembered; no title/content collection.
    if(!enabled_){last_={};last_.handle=reinterpret_cast<quintptr>(foreground);return;}
    if(secureDesktop()){last_={};context_.observe({}, {},true,QDateTime::currentDateTimeUtc());return;}
    last_=inspect(reinterpret_cast<quintptr>(foreground));
    if(!denied(last_,blocked_).isEmpty()){context_.observe({}, {},true,QDateTime::currentDateTimeUtc());return;}
    context_.observe(last_.title,{},last_.fullscreen,QDateTime::currentDateTimeUtc());
#endif
}
QVector<CaptureTarget> ScreenCapture::windows() const{
    QVector<CaptureTarget> result;if(!enabled_)return result;
#ifdef Q_OS_WIN
    EnumWindows([](HWND h,LPARAM data)->BOOL{auto &rows=*reinterpret_cast<QVector<CaptureTarget>*>(data);const auto t=ScreenCapture::inspect(reinterpret_cast<quintptr>(h));if(t.handle && !t.title.isEmpty())rows.append(t);return TRUE;},reinterpret_cast<LPARAM>(&result));
#endif
    return result;
}
void ScreenCapture::restore(){for(auto &widget:hidden_)if(widget)widget->show();hidden_.clear();}
void ScreenCapture::cancel(){++epoch_;nativeDeadline_.stop();nativeCapture_.kill();nativeOutput_.clear();busy_=false;restore();}
void ScreenCapture::capture(quintptr handle,int monitor){
    if(!enabled_){emit failed("屏幕识别尚未授权，请在「智能」页开启总开关");return;}
    if(busy_){emit status("截图正在进行，已合并重复请求");return;}
    if(nativeCapture_.state()!=QProcess::NotRunning){emit failed("上次捕获进程仍在退出，请稍后重试");return;}
    const auto target=inspect(handle?handle:last_.handle);
    const auto error=denied(target,blocked_);if(!error.isEmpty()){emit failed(error);return;}
#ifdef Q_OS_WIN
    if(secureDesktop() || passwordFocused()){emit failed("锁屏、安全桌面或密码输入期间禁止读取");return;}
    if(monitor>=QGuiApplication::screens().size()){emit failed("所选显示器已断开");return;}
    if(monitor>=0){for(const auto &t:windows())if(!denied(t,blocked_).isEmpty()){emit failed("存在敏感或不可验证窗口；请改用指定窗口识别");return;}}
    busy_=true;const auto epoch=++epoch_;emit status("正在获取聊天窗口出现前的目标…");
    for(auto *widget:QApplication::topLevelWidgets())if(widget->isVisible()){hidden_.append(widget);widget->hide();}
    const auto hwnd=reinterpret_cast<HWND>(target.handle);if(monitor>=0)SetForegroundWindow(hwnd);
    QTimer::singleShot(160,this,[this,target,monitor,epoch,hwnd]{
        if(epoch!=epoch_)return;
        QString error;QImage image;
        const auto actual=inspect(target.handle);
        const auto currentWindows=monitor>=0?windows():QVector<CaptureTarget>{};
        if(!enabled_ || secureDesktop() || passwordFocused())error="屏幕权限已撤销，或目标处于安全/密码输入状态";
        else if(actual.process!=target.process || !denied(actual,blocked_).isEmpty())error="目标已改变或变为敏感窗口";
        else if(monitor>=0 && GetForegroundWindow()!=hwnd)error="无法确认显示器捕获目标；请先点击目标，或改用窗口捕获";
        else if(monitor>=0 && monitor>=QGuiApplication::screens().size())error="显示器已断开";
        else if(monitor>=0 && std::any_of(currentWindows.cbegin(),currentWindows.cend(),[this](const CaptureTarget &t){return !denied(t,blocked_).isEmpty();}))error="截图前出现敏感窗口，已停止";
        else {
            if(monitor<0){
                nativeOutput_.clear();disconnect(&nativeCapture_,&QProcess::finished,this,nullptr);disconnect(&nativeCapture_,&QProcess::errorOccurred,this,nullptr);
                auto finish=[this,epoch,target](bool ok){
                    if(epoch!=epoch_)return;nativeDeadline_.stop();nativeOutput_+=nativeCapture_.readAllStandardOutput();QImage image;if(ok && nativeOutput_.size()<=12000000)image.loadFromData(nativeOutput_,"PNG");nativeOutput_.clear();
                    const auto current=inspect(target.handle);const auto policy=denied(current,blocked_);
                    QString error;if(!enabled_ || secureDesktop() || passwordFocused() || !policy.isEmpty() || current.process!=target.process)error="捕获期间目标或隐私状态改变，已丢弃图像";
                    else if(image.isNull()){
                        if(GetForegroundWindow()==reinterpret_cast<HWND>(target.handle))image=readPixels(current.bounds);
                        if(image.isNull())error="目标不支持原生窗口捕获；请将目标放到前台重试，未读取其他窗口";
                    }
                    busy_=false;restore();if(!error.isEmpty()){emit failed(error);return;}emit status("原生窗口捕获完成，仅保存在内存");emit ready(image,{{"source","on-demand-native"},{"scope","window"},{"captured_at",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
                };
                connect(&nativeCapture_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[finish](int code,QProcess::ExitStatus status){finish(code==0 && status==QProcess::NormalExit);});
                connect(&nativeCapture_,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError error){if(error==QProcess::FailedToStart)finish(false);});
#ifdef Q_OS_WIN
                nativeCapture_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args){args->flags|=0x08000000;});
#endif
                nativeCapture_.setProgram(QCoreApplication::applicationDirPath()+"/DragonCapture.exe");nativeCapture_.setArguments({QString::number(target.handle)});nativeDeadline_.start(3500);nativeCapture_.start();return;
            }
            QRect bounds=actual.bounds;
            if(monitor>=0){const auto *screen=QGuiApplication::screens()[monitor];const auto g=screen->geometry();const auto d=screen->devicePixelRatio();bounds=QRect(g.topLeft(),QSize(qRound(g.width()*d),qRound(g.height()*d)));}
            // GetWindowRect is in native physical coordinates under Qt's DPI awareness.
            image=readPixels(bounds);
            if(image.isNull())error="Windows 无法捕获该窗口（可能受保护），未尝试其他窗口";
        }
        busy_=false;restore();
        if(!error.isEmpty()){emit failed(error);return;}
        emit status("截图仅保存在内存中，等待本地 OCR 或视觉分析");
        emit ready(image,{{"source","on-demand"},{"scope",monitor<0?"window":"monitor"},{"captured_at",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
    });
#else
    Q_UNUSED(monitor) emit failed("当前平台不支持 Windows 捕获");
#endif
}
}
