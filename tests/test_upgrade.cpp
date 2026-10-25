#include "companion_window.h"
#include "sprite_motion.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QTemporaryDir>
#include <QTextStream>
#include <cmath>
#include <stdexcept>
using namespace dragon;
namespace {
int assertions=0;
void check(bool condition,const char *reason){++assertions;if(!condition)throw std::runtime_error(reason);}
void growth(){
    LifeState s;check(s.growthPercent()==0,"Empty growth must show zero");
    s.experience=60;s.activeDays=1;check(s.growthPercent()==6,"Experience alone bypassed day gate");
    s.activeDays=3;check(s.stageIndex()==1 && s.growthPercent()==20,"Global intimacy reset at stage boundary");
    s.experience=130;s.activeDays=6;check(s.growthPercent()==32,"Within-stage percentage incorrect");
    s.legacyStage=2;s.experience=200;s.activeDays=0;check(s.stageIndex()==2 && s.growthPercent()==50,"Legacy stage invented active days");
    s.experience=600;s.activeDays=30;check(s.growthPercent()==100,"Previously completed intimacy was demoted");
}
void speech(const QString &directory){
    const auto slow=SpeechExpression::forVoice("longanyuanfei_v3.1",Emotion::Calm);
    const auto fast=SpeechExpression::forVoice("xiaxiaochen_v3.1",Emotion::Calm);
    check(slow.rate<1 && fast.rate>1 && slow.pitch<fast.pitch,"Voice archetypes lack slow/fast distinction");
    QSet<QString> expressions;
    for(const auto &v:audioVoiceChoices("qwen-audio-3.1-tts-flash","beijing")){
        const auto calm=SpeechExpression::forVoice(v.id,Emotion::Calm),worried=SpeechExpression::forVoice(v.id,Emotion::Worried);
        check(worried.rate<calm.rate && worried.pitch<=calm.pitch,"Concern failed to soften lively voice");
        check(calm.cacheKey()!=worried.cacheKey(),"Emotion reused the same audio cache key");
        check(!calm.instruction.isEmpty() && calm.rate>=.72 && calm.rate<=1.22,"Invalid expression profile");expressions.insert(calm.cacheKey());
    }
    check(expressions.size()==8,"Different voices share identical speaking style");
    VoicePreferences p;p.apiFamily="qwen-audio-tts";p.audioTransport="websocket-public";p.expression=slow;
    const auto old=p.cacheVoiceId(),slowCache=p.expressiveCacheId();p.expression=fast;
    check(old==p.cacheVoiceId() && slowCache!=p.expressiveCacheId(),"Expression changed speaker identity or failed cache separation");
    p.save(directory+"/voice.ini");const auto saved=VoicePreferences::load(directory+"/voice.ini");
    check(saved.audioVoice==p.audioVoice && saved.expression.instruction.isEmpty(),"Transient emotion overwrote saved voice settings");
}
void motion(const QString &directory,const QString &exportDir){
    for(int i=0;i<7;++i)for(double t:{0.0,.35,1.2,10.0}){
        const auto pose=PetAction(i);
        for(const auto &point:{QPointF(.5,.18),QPointF(.5,.6),QPointF(.5,.95)})check(SpriteMotion::offset(point,pose,t).manhattanLength()<1e-12,"Motion deformed head, torso or legs");
        for(int y=0;y<=20;++y)for(int x=0;x<=16;++x){const auto d=SpriteMotion::offset({x/16.0,y/20.0},pose,t);check(std::isfinite(d.x()) && std::abs(d.x())<.06 && std::abs(d.y())<.04,"Motion exceeded local bounds");}
    }
    check(SpriteMotion::offset({.95,.78},PetAction::Idle,.5).manhattanLength()>.001,"Tail never moves");
    check(SpriteMotion::offset({.15,.70},PetAction::Work,.5).manhattanLength()>.001,"Hair never moves");
    // Dense face coverage catches the old ellipse crossing the left cheek.
    for(int pose=0;pose<7;++pose)for(int y=18;y<=50;++y)for(int x=28;x<=68;++x)
        check(SpriteMotion::offset({x/100.0,y/100.0},PetAction(pose),.5).isNull(),"Facial region deformed");
    QImage uniform(320,400,QImage::Format_ARGB32_Premultiplied);uniform.fill(QColor(123,93,210,255));
    for(int pose=0;pose<7;++pose){
        QImage result(uniform.size(),uniform.format());result.fill(Qt::transparent);QPainter painter(&result);
        SpriteMotion::paint(painter,uniform,PetAction(pose),.6,0,0,0,true);painter.end();
        for(int y=10;y<390;++y)for(int x=10;x<310;++x)
            check(result.pixel(x,y)==uniform.pixel(x,y),"Rasterization introduced tile seams");
    }
    Companion companion(directory+"/motion.sqlite");PetWidget pet(companion);CompanionWindow window(companion,pet);
    auto *gauge=window.findChild<GrowthGauge*>("growthGauge");check(gauge && gauge->percent()==0,"Growth gauge missing");
    check(!window.findChild<QLabel*>("growthStatus"),"Old growth paragraph remains visible");
    companion.respond("你好");check(gauge->percent()==companion.memory().life().growthPercent(),"Gauge did not refresh after interaction");
    check(!window.findChild<QLabel*>("speechExpressionStatus")->text().isEmpty(),"Expression capability status missing");
    QElapsedTimer timer;timer.start();
    for(int i=0;i<7;++i){
        const auto a=pet.renderMotionFrame(PetAction(i),0),b=pet.renderMotionFrame(PetAction(i),.6);
        check(!a.isNull() && a!=b,"Pose has no local motion");
        check(a==pet.renderMotionFrame(PetAction(i),0),"Renderer is nondeterministic");
        // The posed atlas is bottom aligned; this interior ROI covers both eyes,
        // cheek and nose without reaching moving fringe or expression overlays.
        const QRect faceRoi=i==0?QRect(112,82,125,110):QRect(106,156,126,86);
        check(a.copy(faceRoi)==b.copy(faceRoi),"Authored face pixels changed during motion");
        check(i==int(PetAction::Sleep) || a!=pet.renderMotionFrame(PetAction(i),0,1),"Pose blink does not cover authored eyes");
        if(!exportDir.isEmpty()){
            QDir().mkpath(exportDir+QString("/pose-%1").arg(i));
            for(int f=0;f<32;++f){const double blink=f>=20 && f<=23?std::sin((f-20)*3.141592653589793/3):0;
                check(pet.renderMotionFrame(PetAction(i),f/12.0,blink).save(exportDir+QString("/pose-%1/%2.png").arg(i).arg(f,2,10,QChar('0'))),"Cannot export motion frame");}
        }
    }
    QTextStream(stdout)<<"Renderer duration (including optional exports): "<<timer.elapsed()<<" ms\n";
    pet.setMotionEnabled(false);check(pet.grab().toImage()==pet.grab().toImage(),"Disabling animation did not freeze the character");
    pet.setMotionEnabled(true);QElapsedTimer compositor;compositor.start();for(int i=0;i<10;++i)pet.grab();
    QTextStream(stdout)<<"Widget compositor average: "<<compositor.elapsed()/10.0<<" ms/frame\n";
}
}
int main(int argc,char **argv){
    QApplication app(argc,argv);QTemporaryDir dir;qunsetenv("DASHSCOPE_API_KEY");qunsetenv("DEEPSEEK_API_KEY");
    try{check(dir.isValid(),"Temporary directory failed");growth();speech(dir.path());motion(dir.path(),app.arguments().size()==3 && app.arguments()[1]=="--export-motion"?app.arguments()[2]:QString{});
        QTextStream(stdout)<<"PASS: "<<assertions<<" assertions (percentage gauge, per-voice emotional prosody, localized motion and rendering). No live API.\n";return 0;
    }catch(const std::exception &e){QTextStream(stderr)<<"FAIL: "<<e.what()<<"\n";return 1;}
}
