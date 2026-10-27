#include "offline_speech.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>

namespace dragon {
QString VoiceCache::path(const QString &voice,const QString &text) const{
    const auto key=QCryptographicHash::hash(QByteArray("qwen3-tts-flash/pcm24k/v1")+'\0'+voice.toUtf8()+'\0'+text.toUtf8(),QCryptographicHash::Sha256).toHex();
    return directory_+"/"+QString::fromLatin1(key)+".pcm";
}
QByteArray VoiceCache::read(const QString &voice,const QString &text) const{
    QFile file(path(voice,text));
    if(!file.open(QIODevice::ReadOnly) || file.size()<2 || file.size()>16*1024*1024 || file.size()%2)return {};
    return file.readAll();
}
void VoiceCache::write(const QString &voice,const QString &text,const QByteArray &pcm){
    if(pcm.isEmpty() || pcm.size()%2 || pcm.size()>16*1024*1024 || !QDir().mkpath(directory_))return;
    QDir directory(directory_);auto files=directory.entryInfoList({"*.pcm"},QDir::Files,QDir::Time|QDir::Reversed);
    qint64 bytes=0;for(const auto &file:files)bytes+=file.size();
    const QRegularExpression valid("^[a-f0-9]{64}\\.pcm$");
    for(const auto &file:files){
        if(bytes+pcm.size()<=200*1024*1024)break;
        if(valid.match(file.fileName()).hasMatch() && QFile::remove(file.absoluteFilePath()))bytes-=file.size();
    }
    QSaveFile file(path(voice,text));if(file.open(QIODevice::WriteOnly) && file.write(pcm)==pcm.size())file.commit();
}
void VoiceCache::clear(){
    const QRegularExpression valid("^[a-f0-9]{64}\\.pcm$");
    for(const auto &file:QDir(directory_).entryInfoList({"*.pcm"},QDir::Files))
        if(valid.match(file.fileName()).hasMatch())QFile::remove(file.absoluteFilePath());
}
OfflineSpeech::OfflineSpeech(QObject *parent,QString directory):QObject(parent),
    directory_(directory.isEmpty()?QCoreApplication::applicationDirPath():std::move(directory)){
    timeout_.setSingleShot(true);idle_.setSingleShot(true);
    connect(&timeout_,&QTimer::timeout,this,[this]{fail("离线苏瑶合成超时");});
    connect(&idle_,&QTimer::timeout,this,[this]{if(!pending_){loaded_=false;process_.kill();}});
    connect(&process_,&QProcess::readyReadStandardOutput,this,&OfflineSpeech::receive);
    connect(&process_,&QProcess::readyReadStandardError,this,[this]{process_.readAllStandardError();});
    connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){if(pending_)fail("无法运行离线苏瑶引擎");});
    connect(&process_,&QProcess::finished,this,[this](int,QProcess::ExitStatus){loaded_=false;if(pending_)fail("离线苏瑶进程提前退出");});
}
OfflineSpeech::~OfflineSpeech(){pending_=false;process_.kill();process_.waitForFinished(3000);}
bool OfflineSpeech::available() const{
    return QFileInfo::exists(interpreter())
        && QFileInfo::exists(directory_+"/offline-runtime/ready.json")
        && QFileInfo::exists(directory_+"/offline-runtime/model/model.safetensors")
        && QFileInfo::exists(directory_+"/offline-runtime/model/speech_tokenizer/model.safetensors")
        && QFileInfo::exists(directory_+"/offline/worker.py");
}
QString OfflineSpeech::interpreter() const{
    QFile file(directory_+"/offline-runtime/ready.json");
    if(!file.open(QIODevice::ReadOnly) || file.size()>16384)return {};
    const auto path=QJsonDocument::fromJson(file.readAll()).object().value("python").toString();
    const QFileInfo executable(path);
    return executable.isAbsolute() && executable.fileName().compare("python.exe",Qt::CaseInsensitive)==0 && !path.startsWith("\\\\")?path:QString{};
}
QString OfflineSpeech::start(const QString &text){
    if(!available())return "尚未准备离线苏瑶模型";
    if(text.trimmed().isEmpty() || text.toUcs4().size()>600)return "离线合成文本长度无效";
    if(pending_)return "上一段离线语音仍在生成";
    idle_.stop();pending_=true;pendingText_=text;++generation_;timeout_.start(180000);
    if(process_.state()==QProcess::Running && loaded_){send();return {};}
    if(process_.state()!=QProcess::NotRunning){pending_=false;timeout_.stop();return "离线引擎正在停止，请稍后重试";}
    buffer_.clear();loaded_=false;
    auto environment=QProcessEnvironment::systemEnvironment();
    environment.insert("HF_HUB_OFFLINE","1");environment.insert("TRANSFORMERS_OFFLINE","1");environment.insert("HF_HUB_DISABLE_TELEMETRY","1");
    environment.insert("PYTHONUTF8","1");environment.insert("PYTHONUNBUFFERED","1");
    environment.remove("DEEPSEEK_API_KEY");environment.remove("OPENAI_API_KEY");environment.remove("DASHSCOPE_API_KEY");
    process_.setProcessEnvironment(environment);process_.setWorkingDirectory(directory_+"/offline-runtime");
    process_.start(interpreter(),{"-u",directory_+"/offline/worker.py","--model",directory_+"/offline-runtime/model"});
    emit progress("正在加载离线苏瑶 · 首次需要一些时间");return {};
}
void OfflineSpeech::send(){
    const QJsonObject command{{"id",qint64(generation_)},{"text",pendingText_}};
    process_.write(QJsonDocument(command).toJson(QJsonDocument::Compact)+'\n');
}
void OfflineSpeech::receive(){
    buffer_+=process_.readAllStandardOutput();if(buffer_.size()>24*1024*1024){fail("离线音频超过限制");return;}
    qsizetype position;
    while((position=buffer_.indexOf('\n'))>=0){
        const auto line=buffer_.left(position);buffer_.remove(0,position+1);
        const auto value=QJsonDocument::fromJson(line).object();
        if(value.value("event")=="loaded"){loaded_=true;if(pending_)send();continue;}
        if(!pending_ || value.value("id").toInteger(-1)!=qint64(generation_))continue;
        if(value.contains("error")){fail("离线苏瑶合成失败");return;}
        const auto result=QByteArray::fromBase64Encoding(value.value("pcm").toString().toLatin1(),QByteArray::AbortOnBase64DecodingErrors);
        if(!result || result.decoded.isEmpty() || result.decoded.size()%2 || result.decoded.size()>16*1024*1024){fail("离线苏瑶返回了无效音频");return;}
        pending_=false;pendingText_.clear();timeout_.stop();idle_.start(60000);emit ready(result.decoded);
    }
}
void OfflineSpeech::cancel(){
    ++generation_;pendingText_.clear();
    if(pending_){pending_=false;loaded_=false;process_.kill();buffer_.clear();}
    timeout_.stop();
}
void OfflineSpeech::fail(const QString &message){cancel();emit failed(message);}
}
