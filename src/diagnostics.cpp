#include "diagnostics.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
namespace dragon {
namespace {
QString logFile;
std::wstring crashFile;
QMutex logMutex;
void messages(QtMsgType type,const QMessageLogContext &,const QString &message){
    QMutexLocker lock(&logMutex);
    QString safe=message;
    safe.replace(QRegularExpression("Bearer\\s+[^\\s]+",QRegularExpression::CaseInsensitiveOption),"Bearer [redacted]");
    safe.replace(QRegularExpression("sk-[A-Za-z0-9_-]{8,}"),"[redacted-key]");
    QFile file(logFile);
    if(file.size()>1024*1024){file.close();QFile::remove(logFile+".previous");QFile::rename(logFile,logFile+".previous");}
    if(file.open(QIODevice::WriteOnly|QIODevice::Append))
        file.write((QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)+" ["+QString::number(type)+"] "+safe.left(3000)+"\n").toUtf8());
}
LONG WINAPI crash(EXCEPTION_POINTERS *exception){
    const auto handle=CreateFileW(crashFile.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle!=INVALID_HANDLE_VALUE){char line[80];const auto size=wsprintfA(line,"Unhandled exception: 0x%08lX\r\n",exception->ExceptionRecord->ExceptionCode);DWORD written=0;WriteFile(handle,line,DWORD(size),&written,nullptr);CloseHandle(handle);}
    return EXCEPTION_CONTINUE_SEARCH;
}
}
void installDiagnostics(const QString &directory){
    QDir().mkpath(directory);
    logFile=directory+"/app.log";crashFile=QDir::toNativeSeparators(directory+"/crash.log").toStdWString();
    qInstallMessageHandler(messages);SetUnhandledExceptionFilter(crash);
}
}
