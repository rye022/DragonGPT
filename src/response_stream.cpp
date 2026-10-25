#include "response_stream.h"
#include "api_errors.h"
#include <QJsonArray>
#include <QJsonDocument>

namespace dragon {
void ResponseStream::feed(const QByteArray &bytes) {
    if(!error_.isEmpty()) return;
    received_+=bytes.size();
    if(received_>2*1024*1024) {error_="服务响应超过大小限制";return;}
    buffer_+=bytes;
    qsizetype newline;
    while((newline=buffer_.indexOf('\n'))>=0) {
        auto line=buffer_.left(newline); buffer_.remove(0,newline+1);
        if(line.endsWith('\r')) line.chop(1);
        if(line.isEmpty()) { dispatch(); data_.clear(); }
        else if(line.startsWith("data:")) {
            auto payload=line.mid(5); if(payload.startsWith(' ')) payload.remove(0,1);
            if(!data_.isEmpty()) data_+='\n';
            data_+=payload;
        }
        if(!error_.isEmpty()) return;
    }
    if(buffer_.size()>1024*1024 || data_.size()>1024*1024) error_="流事件超过大小限制";
}
void ResponseStream::dispatch() {
    if(data_.isEmpty() || data_=="[DONE]" || completed_) return;
    QJsonParseError parseError;
    const auto document=QJsonDocument::fromJson(data_,&parseError);
    if(parseError.error!=QJsonParseError::NoError || !document.isObject()) {error_="服务返回了无效流事件";return;}
    const auto event=document.object(); const auto type=event.value("type").toString();
    if(type=="response.output_text.delta" || type=="response.refusal.delta") text_+=event.value("delta").toString();
    if(type=="error" || type=="response.failed" || type=="response.incomplete") {const auto response=event.value("response").toObject();error_="模型响应未完成 · "+apiError("DeepSeek",200,response.isEmpty()?event:response);return;}
    if(type=="response.completed") {
        const auto response=event.value("response").toObject();
        if(response.value("status").toString()!="completed") {error_="模型响应未完成";return;}
        QString canonical;
        for(const auto &item:response.value("output").toArray()) {
            const auto object=item.toObject();
            if(object.value("type").toString()!="message") continue;
            for(const auto &part:object.value("content").toArray()) {
                const auto content=part.toObject();
                if(content.value("type").toString()=="output_text") canonical+=content.value("text").toString();
                if(content.value("type").toString()=="refusal") canonical+=content.value("refusal").toString();
            }
        }
        if(!canonical.isEmpty()) text_=canonical;
        usage_=response.value("usage").toObject(); completed_=true;
    }
    if(text_.size()>16000) error_="回复超过本地长度限制";
}
void ResponseStream::finish(){if(!completed_ && error_.isEmpty())feed("\n\n");}
}
