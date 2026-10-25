#include "screen_ocr.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
namespace dragon {
ScreenOcr::ScreenOcr(QObject *parent):QObject(parent){
    deadline_.setSingleShot(true);
    connect(&deadline_,&QTimer::timeout,this,[this]{cancel();emit failed("Windows OCR 超时，已停止；未上传截图");});
    connect(&process_,&QProcess::readyReadStandardOutput,this,[this]{const auto bytes=process_.readAllStandardOutput();if(!deadline_.isActive())return;output_+=bytes;if(output_.size()>100000){cancel();emit failed("OCR 输出超限");}});
    connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){if(e==QProcess::FailedToStart){deadline_.stop();emit failed("无法启动 Windows OCR；可配置视觉模型后重试");}});
    connect(&process_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status){
        if(!deadline_.isActive())return;deadline_.stop();output_+=process_.readAllStandardOutput();const auto result=QJsonDocument::fromJson(output_).object();output_.fill('\0');output_.clear();
        if(code!=0 || status!=QProcess::NormalExit || !result["ok"].toBool()){emit failed("Windows OCR 不可用或没有安装识别语言；可选择支持图像的视觉模型");return;}
        const auto text=result["text"].toString().trimmed().left(12000);if(text.isEmpty()){emit failed("没有识别到文字；图形、公式或游戏画面建议使用视觉模型");return;}emit ready(text);
    });
}
ScreenOcr::~ScreenOcr(){cancel();if(process_.state()!=QProcess::NotRunning)process_.waitForFinished(1000);}
void ScreenOcr::cancel(){deadline_.stop();process_.kill();output_.fill('\0');output_.clear();}
void ScreenOcr::start(const QImage &image){
    if(busy()){emit failed("OCR 正在运行，请等待或取消");return;}
    const auto helper=QCoreApplication::applicationDirPath()+"/scripts/ocr.ps1";
    if(!QFile::exists(helper)){emit failed("安装包缺少本地 OCR 脚本");return;}
    auto scaled=image.scaled(2600,2600,Qt::KeepAspectRatio,Qt::SmoothTransformation);QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);scaled.save(&buffer,"PNG");
    const auto payload=QJsonDocument(QJsonObject{{"png",QString::fromLatin1(bytes.toBase64())}}).toJson(QJsonDocument::Compact);
    output_.clear();
#ifdef Q_OS_WIN
    process_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args){args->flags|=0x08000000;});
#endif
    process_.setProgram("powershell.exe");process_.setArguments({"-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-File",helper});
    disconnect(&process_,&QProcess::started,this,nullptr);
    connect(&process_,&QProcess::started,this,[this,payload]{process_.write(payload);process_.closeWriteChannel();},Qt::SingleShotConnection);
    deadline_.start(25000);process_.start();
}
}
