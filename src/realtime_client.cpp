#include "realtime_client.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QSslSocket>
#include "api_errors.h"

namespace dragon {
RealtimeClient::RealtimeClient(QObject *parent,QUrl endpoint):QObject(parent),endpoint_(std::move(endpoint)){
    timeout_.setSingleShot(true);connect(&timeout_,&QTimer::timeout,this,[this]{fail("在线语音连接超时，已回退本地语音");});
}
QString RealtimeClient::start(const ChatSettings &settings,const QByteArray &key,const QString &model,const QString &instructions){
    stop();if(key.trimmed().isEmpty() || key.size()>4096 || key.contains('\n') || key.contains('\r'))return "未配置有效的 OpenAI API key";
    if(model.trimmed().isEmpty())return "尚未配置实时语音模型 ID";
    auto config=settings;config.model=model;QNetworkProxy proxy;
    const auto error=config.resolveProxy(proxy,endpoint_);if(!error.isEmpty())return error;
    const bool official=endpoint_.scheme()=="wss" && endpoint_.host()=="api.openai.com" && endpoint_.port(443)==443 && endpoint_.path()=="/v1/realtime" && endpoint_.userInfo().isEmpty() && !endpoint_.hasQuery() && !endpoint_.hasFragment();
    const bool local=endpoint_.scheme()=="ws" && endpoint_.host()=="127.0.0.1";
    if(!official && !local)return "不支持此实时语音地址";
    if(official && !QSslSocket::supportsSsl())return "实时语音 TLS 运行库不可用";
    socket_=std::make_unique<QWebSocket>();socket_->setProxy(proxy);socket_->setMaxAllowedIncomingMessageSize(2*1024*1024);
    const auto generation=++generation_;
    connect(socket_.get(),&QWebSocket::connected,this,[this,generation,instructions]{
        if(generation!=generation_)return;
        const QJsonObject format{{"type","audio/pcm"},{"rate",24000}};
        const QJsonObject vad{{"type","server_vad"},{"threshold",0.5},{"prefix_padding_ms",300},{"silence_duration_ms",600},{"create_response",true},{"interrupt_response",true}};
        const QJsonObject audio{{"input",QJsonObject{{"format",format},{"turn_detection",vad},{"noise_reduction",QJsonObject{{"type","near_field"}}}}},
                                {"output",QJsonObject{{"format",format},{"voice","marin"}}}};
        send(QJsonObject{{"type","session.update"},{"session",QJsonObject{{"type","realtime"},{"instructions",instructions},{"output_modalities",QJsonArray{"audio"}},{"audio",audio}}}});
    });
    connect(socket_.get(),&QWebSocket::textMessageReceived,this,[this,generation](const QString &message){if(generation==generation_){QJsonParseError error;const auto doc=QJsonDocument::fromJson(message.toUtf8(),&error);if(error.error!=QJsonParseError::NoError || !doc.isObject())fail("实时语音返回了无效事件");else event(doc.object());}});
    connect(socket_.get(),&QWebSocket::errorOccurred,this,[this,generation](QAbstractSocket::SocketError){if(generation==generation_)fail("实时语音网络或授权失败，已回退本地语音");});
    connect(socket_.get(),&QWebSocket::disconnected,this,[this,generation]{if(generation==generation_)fail("实时语音连接已断开，已回退本地语音");});
    auto url=endpoint_;QUrlQuery query;query.addQueryItem("model",model.trimmed());url.setQuery(query);
    QNetworkRequest request(url);request.setRawHeader("Authorization","Bearer "+key.trimmed());socket_->open(request);timeout_.start(15000);return {};
}
void RealtimeClient::send(const QJsonObject &event){if(socket_ && socket_->state()==QAbstractSocket::ConnectedState)socket_->sendTextMessage(QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact)));}
void RealtimeClient::appendAudio(const QByteArray &pcm){
    if(!ready_ || !socket_ || pcm.isEmpty() || pcm.size()%2)return;
    if(socket_->bytesToWrite()>=1024*1024){
        ready_=false;
        QTimer::singleShot(0,this,[this,generation=generation_]{if(generation==generation_)fail("语音上传积压，已回退本地语音");});
        return;
    }
    send({{"type","input_audio_buffer.append"},{"audio",QString::fromLatin1(pcm.toBase64())}});
}
void RealtimeClient::clearInput(){send({{"type","input_audio_buffer.clear"}});}
void RealtimeClient::interrupt(int playedMs){
    if(!responseId_.isEmpty()) {cancelled_.insert(responseId_);send({{"type","response.cancel"},{"response_id",responseId_}});}
    if(!itemId_.isEmpty())send({{"type","conversation.item.truncate"},{"item_id",itemId_},{"content_index",0},{"audio_end_ms",qMax(0,playedMs)}});
    responseId_.clear();itemId_.clear();outputText_.clear();emit interrupted();
    if(cancelled_.size()>100)cancelled_.clear();
}
void RealtimeClient::event(const QJsonObject &value){
    const auto type=value.value("type").toString();
    if(type=="session.updated"){if(!ready_){ready_=true;timeout_.stop();emit sessionReady();}return;}
    if(type=="error"){
        const auto code=value.value("error").toObject().value("code").toString();
        if(code=="response_cancel_not_active" || code=="conversation_already_has_active_response")return;
        fail(apiError("实时语音",200,value));return;
    }
    if(type=="rate_limits.updated"){emit rateLimits(value.value("rate_limits").toArray());return;}
    if(type=="input_audio_buffer.speech_started"){emit userSpeechStarted();return;}
    if(type=="conversation.item.input_audio_transcription.completed"){inputText_=value.value("transcript").toString();emit userText(inputText_);return;}
    if(type=="response.created"){responseId_=value.value("response").toObject().value("id").toString();itemId_.clear();outputText_.clear();return;}
    if(type=="conversation.item.input_audio_transcription.failed"){fail(apiError("实时语音识别",200,value));return;}
    const auto response=value.value("response_id").toString();
    if((type=="response.output_audio.delta" || type=="response.output_audio_transcript.delta") && (response.isEmpty() || responseId_.isEmpty()))return;
    if(!response.isEmpty() && (cancelled_.contains(response) || response!=responseId_))return;
    if(type=="response.output_audio.delta"){
        itemId_=value.value("item_id").toString();const auto decoded=QByteArray::fromBase64Encoding(value.value("delta").toString().toLatin1(),QByteArray::AbortOnBase64DecodingErrors);
        const auto pcm=decoded.decoded;
        if(!decoded || pcm.isEmpty() || pcm.size()>192000 || pcm.size()%2){fail("实时音频数据格式无效");return;}emit audio(pcm);
    }
    if(type=="response.output_audio_transcript.delta"){outputText_+=value.value("delta").toString();if(outputText_.size()>16000){fail("语音字幕超过限制");return;}emit assistantText(outputText_);}
    if(type=="response.done"){
        const auto result=value.value("response").toObject();const auto id=result.value("id").toString();
        if(cancelled_.contains(id) || id!=responseId_)return;
        const auto status=result.value("status").toString();
        if(status!="completed" && status!="cancelled"){fail(apiError("实时语音响应",200,result.value("status_details").toObject()));return;}
        if(status=="completed" && !outputText_.isEmpty())emit completed(inputText_,outputText_,result.value("usage").toObject());
        responseId_.clear();inputText_.clear();outputText_.clear();
    }
}
void RealtimeClient::stop(){++generation_;ready_=false;timeout_.stop();if(socket_){auto *socket=socket_.release();socket->disconnect(this);socket->abort();socket->deleteLater();}responseId_.clear();itemId_.clear();inputText_.clear();outputText_.clear();cancelled_.clear();}
void RealtimeClient::fail(const QString &reason){stop();emit failed(reason);}
}
