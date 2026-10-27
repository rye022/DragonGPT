#include "speech_recognition.h"
#include "chat_settings.h"
#include "agent_runtime.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMediaDevices>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
namespace dragon {
AsrSettings AsrSettings::load(const QString &path){
    QSettings s(path,QSettings::IniFormat);AsrSettings v;const auto ready=preparedAgentRuntime("asr");
    v.engine=s.value("engine","sapi").toString();v.language=s.value("language","zh").toString();v.microphone=s.value("microphone").toString();v.python=s.value("python",ready["python"].toString()).toString();v.modelPath=s.value("modelPath",ready["model_path"].toString()).toString();v.device=s.value("device","cpu").toString();v.endpoint=s.value("endpoint").toString();v.cloudModel=s.value("cloudModel").toString();v.hotwords=s.value("hotwords",QStringList{"C++","Python","GitHub","Codex","DeepSeek","DragonGPT","Live2D","STL","RAII","API","Agent","GPT"}).toStringList();v.silenceMs=qBound(600,s.value("silenceMs",1100).toInt(),3000);v.sensitivity=qBound(0,s.value("sensitivity",2).toInt(),3);v.autoSend=s.value("autoSend",false).toBool();return v;
}
void AsrSettings::save(const QString &path) const{
    QSettings s(path,QSettings::IniFormat);s.setValue("engine",engine);s.setValue("language",language);s.setValue("microphone",microphone);s.setValue("python",python);s.setValue("modelPath",modelPath);s.setValue("device",device);s.setValue("endpoint",endpoint);s.setValue("cloudModel",cloudModel);s.setValue("hotwords",hotwords.mid(0,80));s.setValue("silenceMs",qBound(600,silenceMs,3000));s.setValue("sensitivity",qBound(0,sensitivity,3));s.setValue("autoSend",autoSend);s.sync();
}
QJsonObject ASRContextCorrector::correct(const QString &raw,const QStringList &hotwords,bool uncertain){
    QString corrected=raw.trimmed();
    // Only exact ASCII case normalization. Never phonetic/semantic guessing.
    for(const auto &entry:hotwords){
        const auto mapping=entry.split("=>");
        if(mapping.size()==2 && !mapping[0].trimmed().isEmpty() && !mapping[1].trimmed().isEmpty() && mapping[0].size()<=60 && mapping[1].size()<=60){corrected.replace(mapping[0].trimmed(),mapping[1].trimmed());continue;}
        const auto word=entry.trimmed();if(word.isEmpty() || word.size()>60 || !QRegularExpression("^[A-Za-z][A-Za-z0-9+#._-]*$").match(word).hasMatch())continue;
        QRegularExpression re("(?<![A-Za-z0-9_])"+QRegularExpression::escape(word)+"(?![A-Za-z0-9_])",QRegularExpression::CaseInsensitiveOption);corrected.replace(re,word);
    }
    const bool changed=corrected!=raw.trimmed();
    const bool operation=QRegularExpression("删除|清理|整理|覆盖|替换|修改|上传|发送|付款|支付|购买|安装|运行|执行|移动|编译|测试|打开|读取|扫描|搜索|帮我|创建|delete|remove|upload|send|run|write|move",QRegularExpression::CaseInsensitiveOption).match(corrected).hasMatch();
    return {{"raw",raw},{"corrected",corrected},{"changed",changed},{"uncertain",uncertain},{"operation",operation},{"requires_review",uncertain || changed || operation},{"confidence_available",false}};
}
SpeechRecognitionService::SpeechRecognitionService(LocalSpeech &legacy,const QString &directory,QObject *parent):QObject(parent),legacy_(legacy),directory_(directory),settings_(AsrSettings::load(directory+"/asr.ini")){
    deadline_.setSingleShot(true);connect(&deadline_,&QTimer::timeout,this,[this]{stop();emit failed("语音输入超时，未提交任何指令");});
    connect(&legacy_,&LocalSpeech::hypothesis,this,[this](const QString &s){if(listening_)emit partial(s);});
    connect(&legacy_,&LocalSpeech::recognitionDetail,this,[this](const QString &s,bool uncertain){if(listening_)publish(s,uncertain);});
    connect(&legacy_,&LocalSpeech::failed,this,[this](const QString &s){if(listening_){stop();emit failed(s);}});
    connect(&worker_,&QProcess::readyReadStandardOutput,this,&SpeechRecognitionService::consume);
    connect(&worker_,&QProcess::readyReadStandardError,this,[this]{worker_.readAllStandardError();});
    connect(&worker_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){if(listening_){stop();emit failed("无法启动 ASR 运行时，请检查 Python 路径及依赖");}});
    connect(&worker_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus){consume();if(listening_){stop();emit failed(code?"ASR 进程失败；没有提交指令":"ASR 未返回稳定文本；请重试");}});
}
SpeechRecognitionService::~SpeechRecognitionService(){stop();}
void SpeechRecognitionService::configure(const AsrSettings &s,const QByteArray &key){stop();settings_=s;settings_.save(directory_+"/asr.ini");if(!key.isEmpty())CredentialStore(directory_+"/asr-key.dpapi").save(key);}
void SpeechRecognitionService::setOutputActive(bool active){const bool previous=outputActive_;outputActive_=active;if(active)stop();else if(previous)outputTail_.restart();}
QString SpeechRecognitionService::start(){
    if(outputActive_ || (outputTail_.isValid() && outputTail_.elapsed()<650))return "扬声器回声保护中，请稍后重试";
    if(worker_.state()!=QProcess::NotRunning)return "上一段 ASR 正在释放资源，请稍后重试";
    stop();listening_=true;captureClock_.restart();deadline_.start(settings_.engine=="sapi"?35000:120000);emit status("准备语音识别 · "+settings_.engine);
    if(settings_.engine=="sapi"){
        if(settings_.language!="zh" && !settings_.language.isEmpty()){stop();return "SAPI 兼容模式仅配置中文；自动/其他语言请选择本地或云端引擎";}
        legacy_.configureRecognition(settings_.silenceMs,settings_.hotwords+projectWords_);const auto e=legacy_.listen();if(!e.isEmpty())stop();return e;
    }
    if(settings_.engine!="local" && settings_.engine!="cloud"){stop();return "未知 ASR 引擎";}
    if(!QFileInfo::exists(settings_.python)){stop();return "请选择已准备的 ASR Python 运行时";}
    const auto installed=QCoreApplication::applicationDirPath()+"/asr/worker.py";const auto script=QFileInfo::exists(installed)?installed:QString(DRAGON_SOURCE_DIR)+"/asr/worker.py";
    QStringList hints;for(const auto &word:settings_.hotwords+projectWords_)hints<<word.split("=>").last().trimmed();
    QJsonObject config{{"engine",settings_.engine},{"language",settings_.language},{"model_path",settings_.modelPath},{"device",settings_.device},{"hotwords",hints.join(", ")},{"silence_ms",settings_.silenceMs},{"sensitivity",settings_.sensitivity},{"endpoint",settings_.endpoint},{"cloud_model",settings_.cloudModel}};
    if(settings_.engine=="cloud"){try{auto key=CredentialStore(directory_+"/asr-key.dpapi").load();if(key.isEmpty()){stop();return "请先保存云端 ASR 凭据";}config.insert("key",QString::fromUtf8(key));key.fill('\0');}catch(const std::exception &){stop();return "ASR 凭据无法解密";}}
    output_.clear();ready_=false;worker_.setProgram(settings_.python);worker_.setArguments({"-u",script});worker_.start();worker_.write(QJsonDocument(config).toJson(QJsonDocument::Compact)+'\n');return {};
}
void SpeechRecognitionService::stop(){++epoch_;listening_=ready_=false;deadline_.stop();legacy_.stopListening();if(source_)source_->stop();source_.reset();resampler_.reset();worker_.kill();output_.clear();emit level(0);}
void SpeechRecognitionService::finishInput(){
    if(!listening_)return;
    if(settings_.engine=="sapi"){legacy_.finishListening();return;}
    if(!ready_){stop();emit failed("识别引擎尚未准备好，请等待监听状态后再说话");return;}
    ready_=false;if(source_)source_->suspend();deadline_.start(90000);worker_.write("{\"finish\":true}\n");emit status("手动结束录音 · 正在识别");
}
void SpeechRecognitionService::capture(){
    if(!listening_)return;
    auto device=QMediaDevices::defaultAudioInput();for(const auto &d:QMediaDevices::audioInputs())if(QString::fromUtf8(d.id().toBase64())==settings_.microphone)device=d;
    if(device.isNull()){stop();emit failed("未找到麦克风");return;}
    QAudioFormat wire;wire.setSampleRate(16000);wire.setChannelCount(1);wire.setSampleFormat(QAudioFormat::Int16);const auto actual=device.isFormatSupported(wire)?wire:device.preferredFormat();
    if(!actual.isValid()){stop();emit failed("麦克风音频格式无效");return;}
    resampler_=std::make_unique<PcmResampler>(actual,wire);source_=std::make_unique<QAudioSource>(device,actual);source_->setBufferSize(actual.bytesForDuration(200000));auto *io=source_->start();if(!io || source_->error()!=QAudio::NoError){stop();emit failed("打开麦克风失败");return;}
    const auto epoch=epoch_;
    connect(io,&QIODevice::readyRead,this,[this,io,epoch]{if(!listening_ || epoch!=epoch_ || !ready_)return;const auto data=resampler_->feed(io->readAll());if(data.isEmpty())return;emit level(pcmEnergy(data));if(worker_.bytesToWrite()>64000){stop();emit failed("ASR 输入积压，已停止以防截断或重复提交");return;}for(qsizetype i=0;i<data.size();i+=9600){const auto chunk=data.mid(i,9600);worker_.write(QJsonDocument(QJsonObject{{"pcm",QString::fromLatin1(chunk.toBase64())}}).toJson(QJsonDocument::Compact)+'\n');}});
    connect(source_.get(),&QAudioSource::stateChanged,this,[this,epoch](QAudio::State s){if(listening_ && epoch==epoch_ && s==QAudio::StoppedState && source_->error()!=QAudio::NoError){stop();emit failed("录音设备中断，未发送指令");}});
    deadline_.start(35000);emit status("麦克风监听中 · WebRTC VAD · 16kHz PCM16 · "+device.description());
}
void SpeechRecognitionService::consume(){
    output_+=worker_.readAllStandardOutput();if(output_.size()>65536){stop();emit failed("ASR 响应超出限制");return;}
    int i;while((i=output_.indexOf('\n'))>=0){const auto r=QJsonDocument::fromJson(output_.left(i)).object();output_.remove(0,i+1);if(!listening_)continue;
        const auto event=r["event"].toString();if(event=="ready"){ready_=true;capture();}
        else if(event=="processing"){ready_=false;if(source_)source_->suspend();deadline_.start(90000);emit status("正在识别稳定音频段 · 麦克风暂停");}
        else if(event=="error"){const auto error=r["error"].toString();stop();emit failed(error);}
        else if(event=="final"){QStringList reliability;for(const auto &s:r["signals"].toArray())reliability<<s.toString();publish(r["text"].toString(),!reliability.isEmpty(),reliability,r["latency_ms"].toInt(-1));}
    }
}
void SpeechRecognitionService::publish(const QString &text,bool uncertain,const QStringList &reliability,int latencyMs){
    if(text.trimmed().isEmpty()){stop();emit failed("没有识别出有效语音；未提交任何指令");return;}
    const auto elapsed=captureClock_.elapsed();auto result=ASRContextCorrector::correct(text,settings_.hotwords+projectWords_,uncertain);result.insert("signals",QJsonArray::fromStringList(reliability));result.insert("engine",settings_.engine);result.insert("inference_ms",latencyMs);result.insert("elapsed_ms",elapsed);stop();emit finalResult(result);
}
}
