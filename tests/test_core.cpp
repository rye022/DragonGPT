#include "companion.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>
#include <cmath>
#include <stdexcept>

using namespace dragon;
namespace {
int assertions=0;
void expect(bool condition,const char *message) {
    ++assertions;
    if(!condition) throw std::runtime_error(message);
}
void persistenceAndDeletion(const QString &file) {
    qint64 id=0;
    {
        MemoryStore memory(file);
        memory.setNickname("小林");
        id=memory.remember("我喜欢安静的陪伴");
        expect(id>0,"memory insertion failed");
        expect(memory.remember("  我喜欢安静的陪伴  ")==id,"duplicate was not deduplicated");
        expect(memory.remember("  ")==0,"empty memory was retained");
        memory.remember("我正在学习 C++ 多线程");
        memory.appendExchange("你好","你来了");
        expect(memory.exchanges()==1,"exchange count is wrong");
    }
    {
        MemoryStore reopened(file);
        expect(reopened.nickname()=="小林","nickname did not survive restart");
        expect(reopened.memories().size()==2,"memories did not survive restart");
        expect(reopened.recentMessages().size()==2,"chat history did not survive restart");
        const auto related=reopened.relevant("多线程怎么学习");
        expect(!related.isEmpty() && related.first().content.contains("多线程"),"Chinese retrieval missed relevant memory");
        expect(reopened.relevant("无关的香蕉火山").isEmpty(),"retrieval injected irrelevant facts");
        expect(reopened.relevant("",3).isEmpty(),"empty query retrieved facts");
        expect(reopened.relevant("学习",0).isEmpty(),"zero limit ignored");
        reopened.forget(id);
        expect(reopened.memories().size()==1,"selected memory was not deleted");
        reopened.clearAll();
        expect(reopened.nickname().isEmpty() && reopened.memories().isEmpty() && reopened.recentMessages().isEmpty() && reopened.exchanges()==0,"clearAll left retained user data");
    }
    {
        MemoryStore reopened(file);
        expect(reopened.memories().isEmpty() && reopened.nickname().isEmpty(),"deleted data reappeared after restart");
        for(int i=0;i<115;++i) reopened.appendExchange(QString::number(i),"reply");
        expect(reopened.recentMessages(999).size()==200,"chat retention is unbounded");
        expect(reopened.recentMessages(2).first().content=="114","recent history order is incorrect");
        expect(reopened.exchanges()==115 && reopened.relationship()=="初识期" && reopened.life().dailyExperience==2,"rapid conversation farming bypassed chat cooldown");
        reopened.remember("'; DROP TABLE memories; --");
        expect(reopened.memories().size()==1,"SQL content was not safely bound");
    }
}
void emotions() {
    EmotionEngine engine;
    const auto now=QDateTime::fromString("2026-10-21T12:00:00Z",Qt::ISODate);
    expect(engine.request(Emotion::Worried,2.0,1000,10,now),"emotion request rejected");
    expect(engine.state().intensity==1.0,"emotion intensity not clamped");
    expect(!engine.request(Emotion::Happy,0.8,5000,1,now.addMSecs(100)),"idle gesture overrode important emotion");
    engine.tick(now.addMSecs(999));
    expect(engine.state().emotion==Emotion::Worried,"emotion expired early");
    engine.tick(now.addMSecs(1000));
    expect(engine.state().emotion==Emotion::Calm,"emotion failed to expire");
    expect(engine.request(Emotion::Shy,0.5,1000,1,now.addMSecs(1001)),"expired priority still blocked emotion");
    expect(std::abs(engine.avatarParameters().blush-0.5)<0.001,"Live2D parameter mapping incorrect");
    expect(EmotionEngine::classify("我成功了但是好累")==Emotion::Worried,"distress should take priority over celebration");
}
void interruptSafety() {
    VoiceSession voice;
    expect(!voice.beginOutput(0),"output accepted before session began");
    expect(voice.begin(),"voice session did not start");
    const auto turn=voice.epoch();
    expect(voice.finishInput(turn) && voice.beginOutput(turn),"normal turn transitions failed");
    quint64 cancelled=99999;
    QObject::connect(&voice,&VoiceSession::cancelOutput,[&](quint64 e){cancelled=e;});
    voice.interrupt();
    expect(cancelled==turn && voice.state()==VoiceSession::State::Interrupted,"interrupt did not cancel the active turn");
    expect(!voice.finishOutput(turn),"stale audio completion accepted after interrupt");
    expect(voice.resume(),"interrupted session failed to resume");
    const auto newTurn=voice.epoch();
    expect(newTurn!=turn && !voice.finishInput(turn),"stale callback accepted in resumed turn");
    expect(voice.finishInput(newTurn) && voice.beginOutput(newTurn) && voice.finishOutput(newTurn),"new voice turn failed");
    expect(!voice.beginOutput(newTurn),"completed output replayed");
    voice.stop();
    expect(voice.state()==VoiceSession::State::Idle && !voice.resume(),"stopped session resumed without begin");
}
void companionFlow(const QString &file) {
    Companion companion(file);
    expect(companion.respond("   ").text.isEmpty() && companion.memory().exchanges()==0,"blank message modified state");
    companion.respond("我喜欢白色");
    expect(companion.memory().memories().isEmpty(),"unrequested fact stored as long-term memory");
    companion.respond("叫我小雨");
    expect(companion.memory().nickname()=="小雨","explicit nickname command failed");
    companion.respond("记住：我正在做 DragonGPT");
    expect(companion.memory().memories().size()==1,"explicit memory command failed");
    expect(companion.respond("你记得 DragonGPT 吗？").text.contains("我正在做 DragonGPT"),"memory recall failed");
    expect(companion.promptContext("DragonGPT").contains("related_memories"),"structured prompt context missing");
    expect(companion.respond("你好").text.contains("小雨"),"saved nickname was not used");
    expect(companion.respond("我有点累").emotion==Emotion::Worried,"reply emotion failed");
}
void profileContext(const QString &file) {
    qint64 profileId=0;
    {
        Companion companion(file);auto &memory=companion.memory();
        profileId=memory.remember("用户是 C++ 游戏开发者，生日为 2 月 22 日，未提供出生年份。");
        memory.updateMemory(profileId,memory.memories().first().content,"用户资料，职业",1);
        const auto style=memory.remember("日常交流多体贴，按语境少量使用哦等语气词；技术解释保持准确。");
        memory.updateMemory(style,memory.memories().first().content,"偏好;交流偏好",1);
        memory.remember("一条普通的无关记忆");
        auto context=QJsonDocument::fromJson(companion.promptContext("早安").toUtf8()).object();
        expect(context.value("related_memories").toArray().isEmpty(),"ordinary keyword retrieval changed");
        expect(context.value("profile_memories").toArray().size()==1 && context.value("profile_memories").toArray().first().toString().contains("2 月 22"),"explicit profile missing without query keyword");
        expect(context.value("communication_preferences").toArray().size()==1,"communication preference missing without query keyword");
        memory.forget(style);
        context=QJsonDocument::fromJson(companion.promptContext("早安").toUtf8()).object();
        expect(context.value("communication_preferences").toArray().isEmpty(),"deleted preference still injected");
    }
    Companion reopened(file);auto &memory=reopened.memory();
    expect(QJsonDocument::fromJson(reopened.promptContext("你好").toUtf8()).object().value("profile_memories").toArray().size()==1,"profile did not survive restart");
    memory.updateMemory(profileId,"已过期资料","用户资料",1,QDateTime::currentDateTimeUtc().addSecs(-10));
    expect(QJsonDocument::fromJson(reopened.promptContext("你好").toUtf8()).object().value("profile_memories").toArray().isEmpty(),"expired profile still injected");
    for(int i=0;i<12;++i){const auto id=memory.remember(QString("测试资料 %1").arg(i));memory.updateMemory(id,QString("测试资料 %1").arg(i),"用户资料,交流偏好",1);}
    const auto bounded=QJsonDocument::fromJson(reopened.promptContext("你好").toUtf8()).object();
    expect(bounded.value("profile_memories").toArray().size()==8 && bounded.value("communication_preferences").toArray().size()==4,"persistent context is unbounded");
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir dir(QDir::currentPath()+"/dragon-tests-XXXXXX");
    if(!dir.isValid()) return 2;
    try {
        persistenceAndDeletion(dir.path()+"/store.sqlite"); emotions(); interruptSafety();
        companionFlow(dir.path()+"/companion.sqlite");
        profileContext(dir.path()+"/profile.sqlite");
        QTextStream(stdout)<<"PASS: "<<assertions<<" assertions (persistence, retrieval, deletion, emotion expiry, interruption, offline flow).\n";
        return 0;
    } catch(const std::exception &e) {QTextStream(stderr)<<"FAIL: "<<e.what()<<"\n";return 1;}
}
