#include "qwen_speech.h"
#include "audio_duplex.h"
#include "api_errors.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QMediaDevices>
#include <QSettings>
#include <QSslSocket>
#include <QRegularExpression>
#include <QUuid>
#include <QtConcurrent>
#include <stdexcept>

namespace dragon {
QVector<VoiceChoice> voiceChoices(){return {{"System","系统中文（离线）"},{"Serena","苏瑶"},{"Momo","茉兔"},{"Chelsie","千雪"},{"Bella","萌宝"},{"Katerina","卡捷琳娜"}};}
QVector<VoiceChoice> selectableLegacyVoices(){return {{"System","系统中文（离线）"},{"Serena","苏瑶（可离线）"}};}
QVector<VoiceChoice> audioVoiceChoices(const QString &model,const QString &region){
    if(model!="qwen-audio-3.1-tts-flash")return {};
    QVector<VoiceChoice> voices{{"longanhuan_v3.1","龙安欢 · 多语种女声"}};
    if(region=="beijing")voices+=QVector<VoiceChoice>{
        {"longanlingxi_v3.1","龙安灵希 · 可爱甜美"},
        {"longhua_v3.1","龙华 · 元气甜美"},
        {"qiaoxiaojiao_v3.1","乔小娇 · 俏丽可爱"},
        {"xiaxiaochen_v3.1","夏小晨 · 元气明亮"},
        {"xuxiaoqiao_v3.1","徐小俏 · 自然俏皮"},
        {"anyuqing_v3.1","安语晴 · 甜妹"},
        {"longanyuanfei_v3.1","龙安元妃 · 高傲角色音"}};
    return voices;
}
namespace {
bool validVoice(const QString &id){for(const auto &v:voiceChoices())if(v.id==id)return true;return false;}
}
VoicePreferences VoicePreferences::load(const QString &file){
    QSettings ini(file,QSettings::IniFormat);VoicePreferences v;
    v.voice=ini.value("voice/id","Serena").toString();if(!validVoice(v.voice))v.voice="Serena";
    v.region=ini.value("voice/region","beijing").toString();
    v.apiFamily=ini.value("voice/apiFamily",v.apiFamily).toString();
    v.audioWorkspace=ini.value("voice/audioWorkspace",v.audioWorkspace).toString();
    v.audioTransport=ini.value("voice/audioTransport",v.audioTransport).toString();
    v.audioModel=ini.value("voice/audioModel",v.audioModel).toString();
    v.audioVoice=ini.value("voice/audioVoice",v.audioVoice).toString();
    v.systemProxy=ini.value("voice/systemProxy",false).toBool();
    v.preferOffline=ini.value("voice/preferOffline",true).toBool();v.cacheAudio=ini.value("voice/cacheAudio",true).toBool();
    v.readAloud=ini.value("voice/readAloud",false).toBool();v.fastResponse=ini.value("voice/fastResponse",true).toBool();return v;
}
void VoicePreferences::save(const QString &file) const{
    if(!validVoice(voice))throw std::runtime_error("Unknown voice");
    if(QwenSpeech::endpointForRegion(region).isEmpty())throw std::runtime_error("Unsupported Bailian region");
    if(apiFamily!="qwen-tts" && apiFamily!="qwen-audio-tts")throw std::runtime_error("Unknown TTS API family");
    if(audioTts()){const auto error=QwenSpeech::configurationError(*this);if(!error.isEmpty())throw std::runtime_error(error.toUtf8().constData());}
    QSettings ini(file,QSettings::IniFormat);ini.setValue("voice/id",voice);ini.setValue("voice/systemProxy",systemProxy);
    ini.setValue("voice/preferOffline",preferOffline);ini.setValue("voice/cacheAudio",cacheAudio);
    ini.setValue("voice/readAloud",readAloud);ini.setValue("voice/fastResponse",fastResponse);ini.sync();
    ini.setValue("voice/region",region);ini.sync();
    ini.setValue("voice/apiFamily",apiFamily);ini.setValue("voice/audioWorkspace",audioWorkspace);
    ini.setValue("voice/audioTransport",audioTransport);
    ini.setValue("voice/audioModel",audioModel);ini.setValue("voice/audioVoice",audioVoice);ini.sync();
    if(ini.status()!=QSettings::NoError)throw std::runtime_error("Cannot save voice preferences");
}
QUrl VoicePreferences::audioEndpoint() const{
    static const QRegularExpression workspace("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$");
    if(!workspace.match(audioWorkspace).hasMatch())return {};
    const QString zone=region=="beijing"?"cn-beijing":region=="singapore"?"ap-southeast-1":QString();
    if(zone.isEmpty())return {};
    return QUrl("https://"+audioWorkspace+"."+zone+".maas.aliyuncs.com/api/v1/services/audio/tts/SpeechSynthesizer");
}
QString VoicePreferences::cacheVoiceId() const{
    return audioTts()?"qwen-audio-tts/"+audioTransport+"/"+region+"/"+audioWorkspace+"/"+audioModel+"/"+audioVoice:voice;
}
QString VoicePreferences::expressiveCacheId() const{
    return audioTts()?cacheVoiceId()+"/expression-v1/"+expression.cacheKey():cacheVoiceId();
}
QUrl VoicePreferences::audioWebSocketEndpoint() const{
    QUrl url=audioTransport=="websocket-public"?QwenSpeech::endpointForRegion(region):audioEndpoint();
    if(url.isEmpty())return {};
    url.setScheme("wss");url.setPath("/api-ws/v1/inference");return url;
}
QString QwenSpeech::configurationError(const VoicePreferences &p){
    if(p.apiFamily!="qwen-tts" && !p.audioTts())return "未知语音接口系列";
    if(endpointForRegion(p.region).isEmpty())return "不支持此百炼地域";
    if(!p.audioTts())return !validVoice(p.voice) || p.voice=="System"?QString("请选择百炼 Qwen-TTS 音色"):QString();
    if(p.audioTransport!="http" && p.audioTransport!="websocket-workspace" && p.audioTransport!="websocket-public")return "不支持此 Audio TTS 连接方式";
    if(p.audioTransport!="websocket-public" && p.audioEndpoint().isEmpty())return "Audio TTS 需要有效的 Workspace ID；请在百炼控制台右上角业务空间查看";
    static const QRegularExpression model("^qwen-audio-3\\.[01]-tts-(flash|plus)$"),voice("^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$");
    if(!model.match(p.audioModel).hasMatch())return "Audio TTS 模型需为已支持的 qwen-audio-3.0/3.1-tts-flash/plus；不能混用 qwen3-tts 或 tts-next 协议";
    if(!voice.match(p.audioVoice).hasMatch() || validVoice(p.audioVoice))return "Audio TTS 需使用该模型的音色 ID；Chelsie/Serena 等旧系列 ID 不能混用";
    return {};
}
QwenSpeech::QwenSpeech(QObject *parent,QUrl endpoint):QObject(parent),manager_(this),endpoint_(std::move(endpoint)){
    timeout_.setSingleShot(true);connect(&timeout_,&QTimer::timeout,this,[this]{fail("百炼语音超时；未自动重发合成请求");});
}
QUrl QwenSpeech::endpointForRegion(const QString &region){
    const QString path="/api/v1/services/aigc/multimodal-generation/generation";
    if(region=="beijing")return QUrl("https://dashscope.aliyuncs.com"+path);
    if(region=="singapore")return QUrl("https://dashscope-intl.aliyuncs.com"+path);
    return {};
}
QString QwenSpeech::start(const QString &text,const VoicePreferences &preferences,const QByteArray &key){
    cancel();
    if(key.trimmed().isEmpty())return "尚未配置百炼 API key";
    if(key.size()>4096 || key.contains('\n') || key.contains('\r'))return "百炼 API key 格式无效";
    const auto configError=configurationError(preferences);if(!configError.isEmpty())return configError;
    if(text.trimmed().isEmpty() || text.toUcs4().size()>maxCharacters)return "每段百炼语音需在 1–600 字符内";
    if(preferences.audioTts() && preferences.audioTransport!="http")return startSocket(text,preferences,key);
    const auto regional=endpointForRegion(preferences.region);if(regional.isEmpty())return "不支持此百炼地域；请选择与密钥一致的地域";
    const bool official=endpoint_.scheme()=="https" && endpoint_.port(443)==443 &&
        (((endpoint_.host()=="dashscope.aliyuncs.com" || endpoint_.host()=="dashscope-intl.aliyuncs.com") && endpoint_.path()==regional.path()) ||
         (preferences.audioTts() && endpoint_==preferences.audioEndpoint()));
    const bool local=endpoint_.scheme()=="http" && (endpoint_.host()=="127.0.0.1" || endpoint_.host()=="::1");
    if((!official && !local) || !endpoint_.userInfo().isEmpty() || endpoint_.hasQuery() || endpoint_.hasFragment())return "不支持此百炼语音地址";
    if(official && !QSslSocket::supportsSsl())return "百炼语音 TLS 运行库不可用";
    apiUrl_=local?endpoint_:preferences.audioTts()?preferences.audioEndpoint():regional;
    audioTts_=preferences.audioTts();
    QNetworkProxy proxy(QNetworkProxy::NoProxy);
    if(preferences.systemProxy){const auto error=resolveSystemProxy(proxy,apiUrl_);if(!error.isEmpty())return error;}
    manager_.setProxy(proxy);manager_.clearConnectionCache();
    QNetworkRequest request(apiUrl_);request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    request.setRawHeader("Authorization","Bearer "+key.trimmed());
    if(!audioTts_)request.setRawHeader("X-DashScope-SSE","enable");
    request.setRawHeader("Accept",audioTts_?"application/json":"text/event-stream");request.setTransferTimeout(qMin(timeoutMs_,audioTts_?60000:20000));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    QJsonObject audioInput{{"text",text},{"voice",preferences.audioVoice},{"format","wav"},{"sample_rate",24000}};
    if(!preferences.expression.instruction.isEmpty()){const auto fields=preferences.expression.parameters();for(auto it=fields.begin();it!=fields.end();++it)audioInput.insert(it.key(),it.value());}
    const QJsonObject body=audioTts_?
        QJsonObject{{"model",preferences.audioModel},{"input",audioInput}}:
        QJsonObject{{"model","qwen3-tts-flash"},{"input",QJsonObject{{"text",text},{"voice",preferences.voice},{"language_type","Chinese"}}}};
    emit timing("tts_http_request");reply_=manager_.post(request,QJsonDocument(body).toJson(QJsonDocument::Compact));
    attach(generation_);timeout_.start(timeoutMs_);return {};
}
void QwenSpeech::attach(quint64 generation){
    reply_->setReadBufferSize(2*1024*1024);
    connect(reply_,&QIODevice::readyRead,this,[this,generation]{if(generation==generation_)read();});
    connect(reply_,&QNetworkReply::finished,this,[this,generation]{if(generation==generation_)finished(generation);});
}
void QwenSpeech::read(){
    if(!reply_)return;
    const auto generation=generation_;const auto bytes=reply_->readAll();
    const int code=reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if(code!=200){errorBody_+=bytes.left(qMax(qsizetype(0),qsizetype(65536)-errorBody_.size()));return;}
    if(!received_ && !bytes.isEmpty())emit timing(downloading_?"tts_download_first_response":"tts_first_response");
    received_+=bytes.size();
    if(received_>24*1024*1024){fail("百炼语音响应超过限制");return;}
    events_+=bytes;
    if(downloading_){if(events_.size()>16*1024*1024+4096)fail("下载音频超过限制");return;}
    if(reply_->header(QNetworkRequest::ContentTypeHeader).toString().contains("application/json",Qt::CaseInsensitive) || events_.trimmed().startsWith('{')){
        if(events_.size()>2*1024*1024)fail("百炼 JSON 响应超过限制");
        return;
    }
    events_.replace("\r\n","\n");
    while(reply_ && generation==generation_){
        const auto boundary=events_.indexOf("\n\n");if(boundary<0)break;
        const auto packet=events_.left(boundary);events_.remove(0,boundary+2);event(packet);
    }
    if(generation==generation_ && events_.size()>2*1024*1024)fail("百炼语音事件超过限制");
}
void QwenSpeech::event(const QByteArray &packet){
    QByteArray data;
    for(const auto &line:packet.split('\n'))if(line.startsWith("data:")){if(!data.isEmpty())data+='\n';data+=line.mid(5).trimmed();}
    if(data.isEmpty() || data=="[DONE]")return;
    QJsonParseError error;const auto doc=QJsonDocument::fromJson(data,&error);
    if(error.error!=QJsonParseError::NoError || !doc.isObject()){fail("百炼语音事件无效");return;}
    payload(doc.object());
}
void QwenSpeech::payload(const QJsonObject &value){
    if(!value.value("code").toString().isEmpty() || value.contains("error") || (value.contains("status_code") && value.value("status_code").toInt()!=200)){
        fail(apiError(audioTts_?"百炼 Audio TTS":"百炼 Qwen-TTS / qwen3-tts-flash",value.value("status_code").toInt(200),value));return;
    }
    const auto output=value.value("output").toObject();
    const auto audio=output.value("audio").toObject();
    const auto encoded=audio.value("data").toString().toLatin1();
    if(audioTts_ && !encoded.isEmpty()){fail("Audio TTS 非流式 WAV 请求应返回完整音频 URL，未将容器数据作为 PCM 播放");return;}
    if(complete_ && !encoded.isEmpty()){fail("百炼在完成事件后返回了额外音频");return;}
    if(!encoded.isEmpty()){
        const auto decoded=QByteArray::fromBase64Encoding(encoded,QByteArray::AbortOnBase64DecodingErrors);
        if(!decoded || pcm_.size()+pendingPcm_.size()+decoded.decoded.size()>16*1024*1024){fail("百炼音频数据无效或过长");return;}
        pendingPcm_+=decoded.decoded;const auto even=pendingPcm_.size()/2*2;
        if(even){const auto chunk=pendingPcm_.left(even);pendingPcm_.remove(0,even);if(pcm_.isEmpty())emit timing("tts_first_pcm");pcm_+=chunk;
            const auto generation=generation_;emit chunkReady(chunk);if(generation!=generation_)return;
        }
    }
    if(!audio.value("url").toString().isEmpty())audioUrl_=QUrl(audio.value("url").toString());
    if(output.value("finish_reason").toString()=="stop")complete_=true;
}
void QwenSpeech::finished(quint64 generation){
    if(!reply_)return;
    read();if(generation!=generation_ || !reply_)return;
    const int code=reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if(code!=200){fail(apiError(audioTts_?"百炼 Audio TTS":"百炼 Qwen-TTS / qwen3-tts-flash",code,errorBody_,reply_->rawHeader("x-request-id")));return;}
    if(reply_->error()!=QNetworkReply::NoError){fail("百炼语音连接中断；未自动重发合成请求");return;}
    if(downloading_){
        QByteArray result;const auto error=decodePcm24kWav(events_,result);
        if(!error.isEmpty()){fail(error);return;}
        emit timing("tts_first_pcm");emit chunkReady(result);if(generation!=generation_)return;
        cancel();emit ready(result);return;
    }
    if(!events_.trimmed().isEmpty()){
        if(events_.trimmed().startsWith('{')){
            QJsonParseError error;const auto doc=QJsonDocument::fromJson(events_,&error);
            if(error.error!=QJsonParseError::NoError || !doc.isObject()){fail("百炼 JSON 响应无效");return;}
            payload(doc.object());
        }else event(events_);
    }
    if(generation!=generation_ || !reply_)return;
    if(!complete_ || !pendingPcm_.isEmpty()){fail("百炼语音流未完整结束");return;}
    if(pcm_.isEmpty()){
        if(!audioUrl_.isEmpty()){download();return;}
        fail("百炼返回的语音为空");return;
    }
    const auto result=pcm_;cancel();emit ready(result);
}
void QwenSpeech::download(){
    auto url=audioUrl_;
    const bool local=apiUrl_.scheme()=="http" && url.scheme()=="http" && url.host()==apiUrl_.host() && url.port(80)==apiUrl_.port(80);
    static const QRegularExpression trusted("^[a-z0-9][a-z0-9-]*\\.oss(?:-[a-z0-9-]+)?\\.aliyuncs\\.com$");
    if(!local){
        if(!trusted.match(url.host()).hasMatch() || (url.scheme()!="http" && url.scheme()!="https")){fail("百炼完整音频地址不受信任");return;}
        url.setScheme("https");
        if(url.port(443)!=443){fail("百炼完整音频端口无效");return;}
    }
    if(!url.isValid() || !url.userInfo().isEmpty() || url.hasFragment() || url.toString().size()>8192){fail("百炼完整音频地址无效");return;}
    auto *old=reply_.data();reply_.clear();old->disconnect(this);old->deleteLater();
    downloading_=true;events_.clear();errorBody_.clear();received_=0;
    QNetworkRequest request(url);request.setTransferTimeout(qMin(timeoutMs_,20000));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    // OSS signed URLs authenticate themselves. Never forward the service API key.
    reply_=manager_.get(request);attach(generation_);
}
void QwenSpeech::cancel(){
    ++generation_;timeout_.stop();
    if(socket_){auto *socket=socket_.release();socket->disconnect(this);socket->abort();socket->deleteLater();}
    socketTaskStarted_=false;socketTaskId_.clear();
    if(reply_){auto *reply=reply_.data();reply_.clear();reply->disconnect(this);reply->abort();reply->deleteLater();}
    events_.clear();pcm_.clear();pendingPcm_.clear();errorBody_.clear();audioUrl_.clear();complete_=downloading_=false;received_=0;
}
void QwenSpeech::fail(const QString &reason){cancel();emit failed(reason);}

QString QwenSpeech::startSocket(const QString &text,const VoicePreferences &p,const QByteArray &key){
    const bool local=endpoint_.scheme()=="ws" && (endpoint_.host()=="127.0.0.1" || endpoint_.host()=="::1");
    const auto url=local?endpoint_:p.audioWebSocketEndpoint();
    if(url.isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment())return "Audio TTS WebSocket 地址无效";
    if(!local && !QSslSocket::supportsSsl())return "Audio TTS TLS 运行库不可用";
    QNetworkProxy proxy(QNetworkProxy::NoProxy);if(p.systemProxy){const auto error=resolveSystemProxy(proxy,url);if(!error.isEmpty())return error;}
    socket_=std::make_unique<QWebSocket>();socket_->setProxy(proxy);socket_->setMaxAllowedIncomingMessageSize(16*1024*1024);
    socketTaskId_=QUuid::createUuid().toString(QUuid::Id128);const auto generation=generation_;
    connect(socket_.get(),&QWebSocket::connected,this,[this,generation,p]{
        if(generation!=generation_ || !socket_)return;
        emit timing("tts_socket_connected");
        const QJsonObject header{{"action","run-task"},{"task_id",socketTaskId_},{"streaming","duplex"}};
        QJsonObject parameters{{"voice",p.audioVoice},{"format","pcm"},{"sample_rate",24000},{"text_type","PlainText"}};
        if(!p.expression.instruction.isEmpty()){const auto fields=p.expression.parameters();for(auto it=fields.begin();it!=fields.end();++it)parameters.insert(it.key(),it.value());}
        const QJsonObject payload{{"task_group","audio"},{"task","tts"},{"function","SpeechSynthesizer"},{"model",p.audioModel},{"parameters",parameters},{"input",QJsonObject{}}};
        emit timing("tts_task_requested");socket_->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"header",header},{"payload",payload}}).toJson(QJsonDocument::Compact)));
    });
    connect(socket_.get(),&QWebSocket::textMessageReceived,this,[this,generation,text](const QString &message){
        if(generation!=generation_)return;
        if(message.size()>1024*1024){fail("Audio TTS 事件超过限制");return;}
        QJsonParseError error;const auto doc=QJsonDocument::fromJson(message.toUtf8(),&error);
        if(error.error!=QJsonParseError::NoError || !doc.isObject()){fail("Audio TTS WebSocket 事件无效");return;}socketEvent(doc.object(),text);
    });
    connect(socket_.get(),&QWebSocket::binaryMessageReceived,this,[this,generation](const QByteArray &bytes){
        if(generation!=generation_)return;
        if(!socketTaskStarted_ || pcm_.size()+pendingPcm_.size()+bytes.size()>16*1024*1024){fail("Audio TTS 音频顺序或大小无效");return;}
        pendingPcm_+=bytes;const auto size=pendingPcm_.size()/2*2;
        if(size){const auto chunk=pendingPcm_.left(size);pendingPcm_.remove(0,size);if(pcm_.isEmpty())emit timing("tts_first_pcm");pcm_+=chunk;emit chunkReady(chunk);}
    });
    connect(socket_.get(),&QWebSocket::errorOccurred,this,[this,generation](QAbstractSocket::SocketError){
        if(generation!=generation_ || !socket_)return;
        const int status=socket_->errorString().contains("403")?403:socket_->errorString().contains("401")?401:0;fail(apiError("Audio TTS WebSocket",status));
    });
    connect(socket_.get(),&QWebSocket::disconnected,this,[this,generation]{if(generation==generation_)fail("Audio TTS 连接在任务结束前断开；未自动重发");});
    QNetworkRequest request(url);request.setRawHeader("Authorization","Bearer "+key.trimmed());emit timing("tts_socket_open");socket_->open(request);timeout_.start(timeoutMs_);return {};
}
void QwenSpeech::socketEvent(const QJsonObject &value,const QString &text){
    const auto header=value.value("header").toObject();if(header.value("task_id").toString()!=socketTaskId_)return;
    const auto type=header.value("event").toString();
    if(type=="task-failed"){
        const QJsonObject error{{"code",header.value("error_code")},{"request_id",header.value("request_id")}};
        fail("Audio TTS 任务失败 · "+apiError("百炼",200,error));return;
    }
    if(type=="task-started"){
        if(socketTaskStarted_)return;
        socketTaskStarted_=true;
        emit timing("tts_task_started");
        auto send=[this](const QString &action,const QJsonObject &input){
            const QJsonObject message{{"header",QJsonObject{{"action",action},{"task_id",socketTaskId_},{"streaming","duplex"}}},{"payload",QJsonObject{{"input",input}}}};
            socket_->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
        };
        emit timing("tts_text_sent");send("continue-task",QJsonObject{{"text",text}});send("finish-task",{});return;
    }
    if(type=="task-finished"){
        if(!socketTaskStarted_ || pcm_.isEmpty() || !pendingPcm_.isEmpty()){fail("Audio TTS 任务结束但音频为空或不完整");return;}
        const auto result=pcm_;cancel();emit ready(result);
    }
}

SpeechPlayer::SpeechPlayer(QObject *parent,std::function<QAudioDevice()> resolver):QObject(parent),outputResolver_(resolver?std::move(resolver):[]{return QMediaDevices::defaultAudioOutput();}){
    timer_.setInterval(10);connect(&timer_,&QTimer::timeout,this,&SpeechPlayer::flush);
    connect(&deviceQuery_,&QFutureWatcher<QAudioDevice>::finished,this,[this]{
        outputDevice_=deviceQuery_.result();deviceReady_=true;deviceLoading_=false;deviceTrace_.mark("audio_device_query_end");
        if(!deviceMonitor_){deviceMonitor_=std::make_unique<QMediaDevices>();connect(deviceMonitor_.get(),&QMediaDevices::audioOutputsChanged,this,&SpeechPlayer::prepareDevice);}
        if(!waitingDevice_)return;
        waitingDevice_=false;emit timing("audio_device_wait_end");
        const auto error=openDevice();if(!error.isEmpty()){stop();emit failed(error);return;}
        queue_+=converter_->feed(wire_);
        if(!streamOpen_ && !wire_.isEmpty())queue_+=converter_->feed(wire_.right(2));
        flush();
    });
    prepareDevice();
}
SpeechPlayer::~SpeechPlayer(){stop();deviceQuery_.waitForFinished();}
void SpeechPlayer::prepareDevice(){
    if(deviceLoading_)return;
    deviceReady_=false;deviceLoading_=true;deviceTrace_.begin();deviceTrace_.mark("audio_device_query_begin");
    // Qt documents QMediaDevices getters as thread-safe. Do not capture this:
    // the worker only enumerates outputs, never opens a sink or microphone.
    deviceQuery_.setFuture(QtConcurrent::run(outputResolver_));
}
QString SpeechPlayer::play(const QByteArray &pcm){
    if(pcm.isEmpty() || pcm.size()%2){stop();return "音频数据为空或不完整";}
    auto error=beginStream();if(!error.isEmpty())return error;
    error=appendPcm(pcm);if(!error.isEmpty()){stop();return error;}
    finishStream();return {};
}
QString SpeechPlayer::beginStream(){
    stop();
    streamOpen_=true;
    if(!deviceReady_){waitingDevice_=true;emit timing("audio_device_wait_begin");return {};}
    const auto error=openDevice();if(!error.isEmpty())stop();return error;
}
QString SpeechPlayer::openDevice(){
    const auto device=outputDevice_;if(device.isNull())return "未找到扬声器";
    const auto wire=voicePcmFormat();const auto format=device.isFormatSupported(wire)?wire:device.preferredFormat();
    if(!format.isValid())return "扬声器格式不可用";
    converter_=std::make_unique<PcmResampler>(wire,format);
    emit timing("audio_sink_create_begin");sink_=std::make_unique<QAudioSink>(device,format);emit timing("audio_sink_create_end");sink_->setVolume(0.8);sink_->setBufferSize(format.bytesForDuration(120000));
    emit timing("audio_sink_start_begin");
    device_=sink_->start();
    emit timing("audio_sink_start_end");
    if(!device_ || sink_->error()!=QAudio::NoError){stop();return "无法开启语音播放";}
    timer_.start();return {};
}
QString SpeechPlayer::appendPcm(const QByteArray &pcm){
    if((!sink_ && !waitingDevice_) || !streamOpen_ || pcm.isEmpty() || pcm.size()%2)return "流式音频数据无效";
    if(wire_.size()+pcm.size()>16*1024*1024)return "流式音频超过限制";
    wire_+=pcm;if(!waitingDevice_){queue_+=converter_->feed(pcm);flush();}return {};
}
void SpeechPlayer::finishStream(){
    if(waitingDevice_){streamOpen_=false;return;}
    if(!sink_ || !streamOpen_)return;
    if(!wire_.isEmpty())queue_+=converter_->feed(wire_.right(2));
    streamOpen_=false;flush();
}
void SpeechPlayer::flush(){
    if(!sink_ || !device_)return;
    const auto generation=generation_;
    // Underrun while waiting for the next network chunk is recoverable.
    if(sink_->error()!=QAudio::NoError && sink_->error()!=QAudio::UnderrunError){stop();emit failed("播放语音失败");return;}
    if(!queue_.isEmpty()){
        const auto count=qMin(qint64(sink_->bytesFree()),qint64(queue_.size()));
        if(count>0){
            const auto written=device_->write(queue_.constData(),count);
            if(written<0){stop();emit failed("写入扬声器失败");return;}
            if(written>0){queue_.remove(0,written);if(!started_){started_=true;emit timing("audio_first_write");emit started();}}
        }
    }
    if(generation!=generation_ || !sink_)return;
    const auto offset=qBound(qint64(0),(sink_->processedUSecs()*48/1000)/2*2,qint64(wire_.size()));
    emit mouthChanged(pcmEnergy(wire_.mid(offset,1200)));
    if(generation!=generation_ || !sink_)return;
    if(!streamOpen_ && queue_.isEmpty() && sink_->state()==QAudio::IdleState){stop();emit finished();}
}
void SpeechPlayer::stop(){++generation_;timer_.stop();waitingDevice_=false;streamOpen_=started_=false;device_=nullptr;if(sink_)sink_->stop();sink_.reset();converter_.reset();queue_.clear();wire_.clear();emit mouthChanged(0);}
}
