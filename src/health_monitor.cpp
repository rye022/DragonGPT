#include "health_monitor.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <algorithm>
#include <bit>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#endif
namespace dragon {
HealthMonitor::HealthMonitor(QObject *parent):QObject(parent){timer_.setInterval(5000);connect(&timer_,&QTimer::timeout,this,&HealthMonitor::sample);connect(&watcher_,&QFutureWatcher<HealthSample>::finished,this,[this]{ingest(watcher_.result());});}
HealthMonitor::~HealthMonitor(){stop();watcher_.waitForFinished();}
void HealthMonitor::start(){if(timer_.isActive())return;timer_.start();sample();}
void HealthMonitor::stop(){timer_.stop();}
void HealthMonitor::sample(){if(!watcher_.isRunning())watcher_.setFuture(QtConcurrent::run(&HealthMonitor::readSystem));}
HealthSample HealthMonitor::readSystem(){
    HealthSample s;s.time=QDateTime::currentDateTimeUtc();
#ifdef Q_OS_WIN
    FILETIME idle{},kernel{},user{};
    const auto value=[](FILETIME t){return quint64(t.dwLowDateTime)|(quint64(t.dwHighDateTime)<<32);};
    if(GetSystemTimes(&idle,&kernel,&user)){s.totalTicks=value(kernel)+value(user);s.idleTicks=value(idle);}
    MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);if(GlobalMemoryStatusEx(&memory)){s.memoryTotal=memory.ullTotalPhys;s.memoryUsed=memory.ullTotalPhys-memory.ullAvailPhys;s.memory=double(s.memoryUsed)*100/double(s.memoryTotal);}
    LASTINPUTINFO input{};input.cbSize=sizeof(input);if(GetLastInputInfo(&input))s.idleSeconds=(GetTickCount()-input.dwTime)/1000;
    struct GpuCounter{
        PDH_HQUERY query=nullptr;PDH_HCOUNTER counter=nullptr;QMutex mutex;
        GpuCounter(){if(PdhOpenQueryW(nullptr,0,&query)==ERROR_SUCCESS && PdhAddEnglishCounterW(query,L"\\GPU Engine(*)\\Utilization Percentage",0,&counter)!=ERROR_SUCCESS){PdhCloseQuery(query);query=nullptr;}}
        ~GpuCounter(){if(query)PdhCloseQuery(query);}
    };static GpuCounter counters;
    {QMutexLocker lock(&counters.mutex);
        if(counters.query && PdhCollectQueryData(counters.query)==ERROR_SUCCESS){
            DWORD bytes=0,count=0;PdhGetFormattedCounterArrayW(counters.counter,PDH_FMT_DOUBLE,&bytes,&count,nullptr);
            if(bytes>0 && bytes<8*1024*1024){QByteArray storage(int(bytes),Qt::Uninitialized);auto *items=reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(storage.data());
                if(PdhGetFormattedCounterArrayW(counters.counter,PDH_FMT_DOUBLE,&bytes,&count,items)==ERROR_SUCCESS){
                    QHash<QString,double> engines;
                    for(DWORD i=0;i<count;++i)if(items[i].FmtValue.CStatus==PDH_CSTATUS_VALID_DATA || items[i].FmtValue.CStatus==PDH_CSTATUS_NEW_DATA){
                        const auto name=QString::fromWCharArray(items[i].szName);const int luid=name.indexOf("luid_");if(luid>=0)engines[name.mid(luid)]+=std::max(0.0,items[i].FmtValue.doubleValue);
                    }
                    for(auto it=engines.begin();it!=engines.end();++it)s.gpu=std::max(s.gpu,std::min(100.0,it.value()));
                }
            }
        }
    }
    // Optional NVIDIA telemetry, loaded only from the Windows system directory.
    struct Nvml{
        HMODULE library=LoadLibraryExW(L"nvml.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        using Init=int(*)();using Handle=int(*)(unsigned int,void **);using Temperature=int(*)(void *,unsigned int,unsigned int *);using Shutdown=int(*)();
        Init init=nullptr;Handle handle=nullptr;Temperature temperature=nullptr;Shutdown shutdown=nullptr;bool ready=false;
        struct Rates{unsigned int gpu=0,memory=0;};using Utilization=int(*)(void *,Rates *);Utilization utilization=nullptr;
        Nvml(){if(library){init=std::bit_cast<Init>(GetProcAddress(library,"nvmlInit_v2"));handle=std::bit_cast<Handle>(GetProcAddress(library,"nvmlDeviceGetHandleByIndex_v2"));temperature=std::bit_cast<Temperature>(GetProcAddress(library,"nvmlDeviceGetTemperature"));shutdown=std::bit_cast<Shutdown>(GetProcAddress(library,"nvmlShutdown"));ready=init && handle && temperature && shutdown && init()==0;}}
        ~Nvml(){if(ready)shutdown();if(library)FreeLibrary(library);}
    };static Nvml nvml;
    if(nvml.ready){void *device=nullptr;unsigned int temperature=0;if(nvml.handle(0,&device)==0){
        if(nvml.temperature(device,0,&temperature)==0 && temperature<130)s.gpuTemperature=temperature;
        const auto utilization=std::bit_cast<Nvml::Utilization>(GetProcAddress(nvml.library,"nvmlDeviceGetUtilizationRates"));Nvml::Rates rates;
        if(s.gpu<0 && utilization && utilization(device,&rates)==0 && rates.gpu<=100){s.gpu=rates.gpu;s.gpuSource="GPU 0（驱动）";}
    }}
#endif
    return s;
}
void HealthMonitor::ingest(HealthSample s){
    if(!s.time.isValid())return;
    const auto gap=latest_.time.isValid()?latest_.time.secsTo(s.time):0;
    if(s.cpu<0 && previousRaw_.totalTicks>0 && s.totalTicks>previousRaw_.totalTicks && s.idleTicks>=previousRaw_.idleTicks){const double total=double(s.totalTicks-previousRaw_.totalTicks),idle=double(s.idleTicks-previousRaw_.idleTicks);s.cpu=std::clamp(100.0*(1-idle/total),0.0,100.0);}
    previousRaw_=s;
    if(gap>0 && gap<=10 && s.idleSeconds>=0 && s.idleSeconds<60)emit activeSeconds(int(gap),s.time.toLocalTime().date());
    const bool high=s.cpu>=90 || s.memory>=90 || s.gpu>=95;
    if(!high || gap<0 || gap>15)highSince_={};else if(!highSince_.isValid())highSince_=s.time;
    if(highSince_.isValid() && highSince_.secsTo(s.time)>=120 && (!lastAlert_.isValid() || lastAlert_.secsTo(s.time)>=1800)){
        lastAlert_=s.time;emit sustainedLoad("资源占用已持续较高两分钟。可以查看任务管理器，检查后台任务、可用内存或正在运行的游戏；这不能单独说明硬件故障。");
    }
    latest_=s;trend_.append(s);if(trend_.size()>120)trend_.removeFirst();emit changed();
}
QString HealthMonitor::overview() const{
    if(!latest_.time.isValid())return "正在读取电脑状态，请稍候。";
    const auto percent=[](double v){return v<0?QString("暂不可用"):QString::number(v,'f',1)+"%";};
    return QString("CPU：%1\n内存：%2（%3 / %4 GB）\nGPU（%9）：%5\nGPU 温度：%6\nCPU 温度：当前接口不支持\n%7\n采样：%8").arg(percent(latest_.cpu),percent(latest_.memory)).arg(latest_.memoryUsed/1073741824.0,0,'f',1).arg(latest_.memoryTotal/1073741824.0,0,'f',1).arg(percent(latest_.gpu),latest_.gpuTemperature<0?"硬件或驱动未提供":QString::number(latest_.gpuTemperature,'f',0)+" °C",highSince_.isValid()?"近期负载较高；请结合趋势和正在进行的任务判断。":"近期采样未发现持续高负载，实际卡顿还可能与磁盘或网络有关。",latest_.time.toLocalTime().toString("HH:mm:ss"),latest_.gpuSource);
}
}
