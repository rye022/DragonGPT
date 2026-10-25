#include "speech_expression.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <algorithm>

namespace dragon {
QString SpeechExpression::cacheKey() const {
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(parameters()).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex().left(20));
}
QJsonObject SpeechExpression::parameters() const{return {{"rate",rate},{"pitch",pitch},{"instruction",instruction}};}
SpeechExpression SpeechExpression::forVoice(const QString &voice,Emotion mood,int affinity){
    SpeechExpression p;p.emotion=mood;
    if(voice=="longanyuanfei_v3.1"){
        p.rate=.95;p.pitch=.96;p.style="矜持傲娇";p.instruction="保持原音色，语气略带傲娇但体贴，节奏自然，适度停顿，音调沉稳，克制自然，不尖叫。";
    }else if(voice=="longhua_v3.1"){
        p.rate=1.10;p.pitch=1.03;p.style="元气甜美";p.instruction="保持原音色，语气元气甜美，轻快有活力，吐字清楚，不夸张喊叫。";
    }else if(voice=="xiaxiaochen_v3.1"){
        p.rate=1.10;p.pitch=1.02;p.style="明亮活泼";p.instruction="保持原音色，语气明亮开朗，节奏轻快，精神饱满但不急促。";
    }else if(voice=="xuxiaoqiao_v3.1"){
        p.rate=1.10;p.pitch=1.01;p.style="自然俏皮";p.instruction="保持原音色，自然俏皮，轻快地交流，带一点笑意，不做作。";
    }else if(voice=="qiaoxiaojiao_v3.1"){
        p.rate=1.05;p.pitch=1.03;p.style="俏丽可爱";p.instruction="保持原音色，语气俏丽可爱，语速适中偏轻快，甜而自然。";
    }else if(voice=="anyuqing_v3.1"){
        p.rate=1.05;p.pitch=1.02;p.style="清甜亲近";p.instruction="保持原音色，清甜亲近，语气柔和，以正常日常聊天速度清晰交流，停顿简短自然。";
    }else if(voice=="longanlingxi_v3.1"){
        p.rate=1.05;p.pitch=1.02;p.style="软甜可爱";p.instruction="保持原音色，温柔可爱，轻柔甜美，不刻意夹嗓，留出自然停顿。";
    }else if(voice=="longanhuan_v3.1"){
        p.rate=1.06;p.pitch=1.0;p.style="亲切开朗";p.instruction="保持原音色，亲切开朗，节奏自然偏轻快，像日常聊天。";
    }else p.instruction="保持原音色，自然清晰地朗读，语气温柔真诚。";
    switch(mood){
    case Emotion::Worried:p.rate-=.04;p.pitch*=.98;p.instruction+="此时在关心对方，请温柔安慰，句尾柔和，语调平稳，不要使用兴奋语气。";break;
    case Emotion::Sleepy:p.rate-=.08;p.pitch*=.97;p.instruction+="此时是安静困倦的陪伴，节奏舒缓，语调轻柔低缓，不夸张打哈欠。";break;
    case Emotion::Shy:p.rate-=.025;p.instruction+="此时有一点害羞，语气含蓄，在转折处有短暂迟疑，停顿自然，不要尖细夸张。";break;
    case Emotion::Happy:p.rate+=.04;p.pitch*=1.02;p.instruction+="此时心情愉快，带自然笑意和明亮的语调，不发出额外笑声。";break;
    case Emotion::Sad:p.rate-=.045;p.pitch*=.98;p.instruction+="难过但不表演哭腔，声音柔和，认真倾听，句尾轻落。";break;
    case Emotion::Angry:p.rate+=.015;p.instruction+="有一点不满，重音明确，坚定但体贴，不吼叫、不尖锐、不责备对方。";break;
    case Emotion::Tsundere:p.rate-=.02;p.instruction+="有一点含蓄傲娇，轻微嘴硬但体贴，转折自然，不责备、不装生气、不尖声。";break;
    case Emotion::Relaxed:p.rate-=.05;p.pitch*=.99;p.instruction+="此时轻松陪伴，节奏从容，音色自然柔和，不催促对方。";break;
    case Emotion::Focused:p.rate-=.02;p.instruction+="此时认真专注，吐字清晰、语调平稳，短句鼓励，减少夸张语气和额外打扰。";break;
    case Emotion::Excited:p.rate+=.06;p.pitch*=1.025;p.instruction+="此时很开心和兴奋，声音有活力但控制音量，不尖叫、不额外发笑、不催促对方。";break;
    case Emotion::Smile:p.instruction+="此时温和微笑，保持轻松友善。";break;
    case Emotion::Thinking:p.rate-=.015;p.instruction+="此时在认真思考，语调清晰平稳，停顿从容。";break;
    case Emotion::Curious:p.pitch*=1.015;p.instruction+="此时有些好奇，轻微上扬语调，自然提问。";break;
    default:break;
    }
    if(affinity>=0){
        if(affinity<20)p.instruction+="关系初识，语气友善克制，保留礼貌距离。";
        else if(affinity<50)p.instruction+="关系熟悉，自然放松，可以轻轻俏皮，但尊重对方。";
        else if(affinity<80)p.instruction+="关系亲近，温柔自然，适度含蓄关心，不索取承诺。";
        else p.instruction+="深度羁绊，信任默契，亲切但尊重独处，不捏造共同经历。";
    }
    p.rate=std::clamp(p.rate,.90,1.18);p.pitch=std::clamp(p.pitch,.90,1.08);return p;
}
}
