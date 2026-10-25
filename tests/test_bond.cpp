#include "companion_window.h"
#include "bond_gallery.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTabWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QTextStream>
#include <stdexcept>
using namespace dragon;
namespace {
int checks=0;void check(bool ok,const char *why){++checks;if(!ok)throw std::runtime_error(why);}
bool until(std::function<bool()> f,int ms=3000){QElapsedTimer t;t.start();while(!f() && t.elapsed()<ms){QCoreApplication::processEvents();QTest::qWait(1);}return f();}
QDateTime noon(){return QDateTime(QDate(2026,11,2),QTime(12,0));}
QJsonObject row(MemoryStore &s,const QString &id){for(const auto &v:s.collection())if(v.toObject()["id"]==id)return v.toObject();return {};}
void growth(){
    LifeState s;const auto t=noon();check(s.award("click",{},t),"First real click did not grow");
    for(int i=0;i<80;++i)s.award("click",{},t.addMSecs(i*100));check(s.experience==1,"Rapid clicks farmed affinity");
    s=LifeState::fromJson(s.json());check(!s.award("click",{},t.addSecs(89)),"Click cooldown lost on restart");
    check(s.award("chat","今天在写自己的游戏",t.addSecs(5)) && s.experience==3,"Meaningful chat did not grow");
    check(!s.award("chat","今天在写自己的游戏",t.addSecs(40)),"Repeated chat farmed affinity");
    for(int i=0;i<30;++i)s.award("chat",QString("新的话题 %1").arg(i),t.addSecs(100+i*31));
    check(s.dailyExperience==20 && s.experience==20,"Shared daily cap failed");
    check(!s.award("click",{},t.addDays(-1)),"Clock rollback reopened growth");
    check(s.award("click",{},t.addDays(1)) && s.experience==21 && s.activeDays==2,"New real active day did not persist");
    s.experience=600;s.activeDays=30;check(!s.award("click",{},t.addDays(2)) && s.growthPercent()==100,"Maximum affinity grew or reset");
    const int xp=s.experience;s.award("proactive",{},t.addDays(3));s.award("preview",{},t.addDays(3));check(s.experience==xp,"Passive interaction or preview gained affinity");
}
void stageUnlocks(const QString &path){
    MemoryStore store(path);check(!row(store,"s1-00")["unlocked"].toBool(),"Future stage started unlocked");
    const auto start=noon();
    for(int day=0;day<30;++day){
        for(int i=0;i<10;++i)store.awardInteraction("chat",QString("真实交流 %1/%2").arg(day).arg(i),start.addDays(day).addSecs(i*31));
        if(day==2){check(store.life().stageIndex()==1 && row(store,"s1-00")["unlocked"].toBool(),"Stage growth failed to unlock dialogue");
            check(!row(store,"s1-00")["discovered"].toBool() && !row(store,"s1-00")["unlock_inferred"].toBool(),"Unlock incorrectly collected a line or lost the real unlock origin");}
        if(day==9)check(store.life().stageIndex()==2 && row(store,"s2-00")["unlocked"].toBool(),"Close stage unlock missing");
    }
    check(store.life().growthPercent()==100 && store.unlockedEvents()==3,"Maximum growth or stage achievements missing");
    int unlocked=0,collected=0;for(const auto &v:store.collection()){unlocked+=v.toObject()["unlocked"].toBool();collected+=v.toObject()["discovered"].toBool();}
    check(unlocked==405 && collected==0,"Maximum growth lost content or fabricated collection");
    check(!store.awardInteraction("click",{},start.addDays(31)) && store.life().growthPercent()==100,"Full affinity exceeded its cap");
}
void migration(const QString &path){
    LifeState state;state.experience=310;state.activeDays=18;state.dailyExperience=8;state.legacyStage=2;state.mood=Emotion::Worried;state.moodUntil=noon().addDays(20);
    {
        auto db=QSqlDatabase::addDatabase("QSQLITE","bond-old");db.setDatabaseName(path);check(db.open(),"Cannot open legacy fixture");QSqlQuery q(db);
        check(q.exec("CREATE TABLE relation(id INTEGER PRIMARY KEY,exchanges INTEGER NOT NULL)"),"Legacy relation failed");check(q.exec("INSERT INTO relation VALUES(1,35)"),"Legacy relation insert failed");
        check(q.exec("CREATE TABLE companion_life(id INTEGER PRIMARY KEY,state TEXT NOT NULL)"),"Legacy life failed");q.prepare("INSERT INTO companion_life VALUES(1,?)");q.addBindValue(QString::fromUtf8(QJsonDocument(state.json()).toJson()));check(q.exec(),"Legacy life insert failed");
        check(q.exec("CREATE TABLE interaction_state(id INTEGER PRIMARY KEY,state TEXT NOT NULL)"),"Legacy history failed");q.prepare("INSERT INTO interaction_state VALUES(1,?)");q.addBindValue(QString::fromUtf8(QJsonDocument(QJsonObject{{"used",QJsonObject{{"s2-00",noon().toString(Qt::ISODateWithMs)},{"s2-02",QJsonValue::Null}}}}).toJson()));check(q.exec(),"Legacy history insert failed");
        check(q.exec("PRAGMA user_version=5"),"Legacy schema failed");q.finish();db.close();
    }QSqlDatabase::removeDatabase("bond-old");
    {
        MemoryStore s(path);check(s.life().experience==310 && s.life().growthPercent()==state.growthPercent() && s.exchanges()==35 && s.life().mood==Emotion::Worried,"Migration changed existing growth, relationship or emotion");
        check(row(s,"s2-00")["discovered"].toBool() && row(s,"s2-00")["legacy"].toBool(),"Legacy collection was lost");
        check(row(s,"s2-00")["first_heard_at"].toString().isEmpty(),"Migration invented a first heard time");
        check(!row(s,"s2-02")["discovered"].toBool() && !row(s,"s2-02")["legacy"].toBool(),"Null candidate cache key was fabricated into collection");
        s.recordPresented("s2-02",noon());check(row(s,"s2-02")["discovered"].toBool(),"Old null candidate could never be collected after a real presentation");
        check(s.unlockedEvents()==2,"Existing stage achievements missing");
        s.recordPresented("s2-01",noon());s.recordHeard("s2-01",noon().addSecs(1));
    }
    {
        auto db=QSqlDatabase::addDatabase("QSQLITE","bond-early-v6");db.setDatabaseName(path);check(db.open(),"Cannot open early v6 fixture");QSqlQuery q(db);
        check(q.exec("UPDATE dialogue_collection SET legacy=1,discovered_at='',play_count=1,last_played_at='' WHERE id='s2-03'"),"Cannot seed null-candidate v6 fixture");q.finish();db.close();
    }QSqlDatabase::removeDatabase("bond-early-v6");
    MemoryStore reopened(path);check(row(reopened,"s2-01")["play_count"].toInt()==1 && !row(reopened,"s2-01")["first_heard_at"].toString().isEmpty(),"Collection timestamps did not survive restart");
    check(!row(reopened,"s2-03")["discovered"].toBool() && row(reopened,"s2-03")["play_count"].toInt()==0,"Empty legacy key retained a fabricated presentation count");
    check(QDir(QFileInfo(path).absolutePath()).entryList({QFileInfo(path).fileName()+".pre-v7-*.sqlite"},QDir::Files).size()==1,"Migration backup missing or repeated");
}
struct SpeechFixture {
    QTcpServer server;int count=0;bool fail=true;QByteArray request;
    SpeechFixture(){check(server.listen(QHostAddress::LocalHost,0),"Speech fixture failed");
        QObject::connect(&server,&QTcpServer::newConnection,&server,[this]{while(server.hasPendingConnections()){
            auto *s=server.nextPendingConnection();auto buffer=std::make_shared<QByteArray>();QObject::connect(s,&QTcpSocket::disconnected,s,&QObject::deleteLater);
            QObject::connect(s,&QTcpSocket::readyRead,s,[this,s,buffer]{
                *buffer+=s->readAll();const int head=buffer->indexOf("\r\n\r\n");if(head<0 || s->property("done").toBool())return;
                int size=0;for(const auto &h:buffer->left(head).split('\n'))if(h.toLower().startsWith("content-length:"))size=h.mid(15).trimmed().toInt();if(buffer->size()<head+4+size)return;
                s->setProperty("done",true);request=*buffer;++count;
                QByteArray body=fail?QByteArray("{\"code\":\"AccessDenied\",\"message\":\"fixture denial\"}"):
                    "data: "+QJsonDocument(QJsonObject{{"output",QJsonObject{{"audio",QJsonObject{{"data",QString::fromLatin1(QByteArray(24000,0).toBase64())}}}}}}).toJson(QJsonDocument::Compact)+"\r\n\r\ndata: {\"output\":{\"finish_reason\":\"stop\"}}\r\n\r\n";
                s->write(QByteArray("HTTP/1.1 ")+(fail?"403 Denied":"200 OK")+"\r\nContent-Type: text/event-stream\r\nConnection: close\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body);s->disconnectFromHost();
            });
        }});
    }
    QUrl url() const{return QUrl(QString("http://127.0.0.1:%1/tts").arg(server.serverPort()));}
};
void schedulerAndAudio(const QString &dir,const QString &review){
    // Exercise the output device before networking, matching the existing
    // playback regression fixture (no microphone and no cloud request).
    {SpeechPlayer output;QSignalSpy finished(&output,&SpeechPlayer::finished);
        check(output.play(QByteArray(4800,0)).isEmpty(),"Output fixture could not start");
        // Device discovery is asynchronous now; this setup includes cold driver
        // enumeration as well as playback. Strict speech latency is tested separately.
        check(until([&]{return finished.size()==1;},15000),"Output fixture did not drain");}
    QDir().mkpath(dir);VoicePreferences p;p.voice="Momo";p.readAloud=false;p.preferOffline=false;p.cacheAudio=false;p.fastResponse=false;p.systemProxy=false;p.save(dir+"/voice.ini");CredentialStore(dir+"/dashscope-key.dpapi").save("fixture-secret");
    auto now=noon();Companion c(dir+"/memory.sqlite",nullptr,QUrl("http://127.0.0.1:9/responses"),100,[&]{return now;});
    auto settings=c.lifeSettings();settings.quietStart=settings.quietEnd=0;settings.focusProtection=false;settings.proactiveSpeech=true;c.saveLifeSettings(settings);
    SpeechFixture server;VoiceController voice(c,nullptr,server.url());QObject owner;bool blocked=false;
    c.attachProactiveScheduler(&owner,[&]{return blocked;});QObject::connect(&c,&Companion::proactiveReady,&voice,&VoiceController::deliverProactive);
    PetWidget pet(c);pet.show();QObject::connect(&voice,&VoiceController::speechRequested,&pet,&PetWidget::say);QObject::connect(&voice,&VoiceController::interactionStarted,&pet,&PetWidget::beginActionInteraction);QObject::connect(&voice,&VoiceController::interactionFinished,&pet,&PetWidget::finishActionInteraction);
    QSignalSpy traces(&c,&Companion::proactiveTrace),speech(&voice,&VoiceController::speechRequested);
    auto *timer=c.findChild<QTimer*>("proactiveScheduler");check(timer && timer->isSingleShot(),"One-shot scheduler missing");
    for(const QString mode:{"quiet","normal","active"}){
        settings.frequency=mode;c.saveLifeSettings(settings);const int delay=int(now.secsTo(c.nextProactiveAt()));const auto r=settings.intervalSeconds();check(delay>=r.first && delay<=r.second,"Scheduled interval outside selected frequency");
    }
    settings.frequency="off";c.saveLifeSettings(settings);check(!c.proactiveSchedulerRunning(),"Off mode left timer running");settings.frequency="active";c.saveLifeSettings(settings);
    now=now.addSecs(180);c.noteActivity(now);check(c.nextProactiveAt()>now.addSecs(179),"Manual interaction did not delay schedule");
    blocked=true;now=c.nextProactiveAt();timer->start(10);check(until([&]{return c.nextProactiveAt()>now;}) && server.count==0 && now.secsTo(c.nextProactiveAt())>=30,"Busy guard failed or queued an opening");
    blocked=false;now=c.nextProactiveAt();timer->start(10);check(until([&]{return server.count==1 && !voice.engaged();}),"Real timer or TTS failure recovery failed");
    const auto id=c.lastProactiveLine().id;check(speech.size()==1 && pet.bubbleActive() && !pet.actionInteractionActive(),"TTS failure hid bubble or left pose busy");
    check(row(c.memory(),id)["discovered"].toBool() && row(c.memory(),id)["first_heard_at"].toString().isEmpty(),"Failed TTS collected audio or lost actual text display");
    check(c.memory().life().experience==0 && c.proactiveSchedulerRunning(),"Passive opening grew affinity or stopped scheduler");
    const auto request=QJsonDocument::fromJson(server.request.mid(server.request.indexOf("\r\n\r\n")+4)).object();check(request["input"].toObject()["voice"]=="Momo" && !voice.readAloud(),"Proactive TTS did not use selected voice independently of typed read-aloud");
    check(VoicePreferences::load(dir+"/voice.ini").voice=="Momo","TTS failure changed selected voice");
    server.fail=false;const auto prior=row(c.memory(),id);const auto mood=c.emotions().state().emotion;
    pet.hide();
    auto *cloud=voice.findChild<QwenSpeech*>();auto *player=voice.findChild<SpeechPlayer*>();QSignalSpy ready(cloud,&QwenSpeech::ready),played(player,&SpeechPlayer::finished);
    check(voice.previewCollectedLine(id),"Collected preview did not start");check(!voice.previewCollectedLine(id),"Preview interrupted existing voice");
    const bool previewDone=until([&]{return server.count==2 && !voice.engaged();},6000);
    if(!previewDone)QTextStream(stdout)<<"Preview state: requests="<<server.count<<" engaged="<<voice.engaged()<<" cloud="<<cloud->busy()<<" ready="<<ready.size()<<" played="<<played.size()<<" status="<<voice.status()<<" "<<player->bufferedMilliseconds()<<"\n";
    check(previewDone,"Preview fixture did not finish");
    check(row(c.memory(),id)["play_count"]==prior["play_count"] && c.memory().life().experience==0 && c.emotions().state().emotion==mood,"Preview changed collection, affinity or emotion");
    now=c.nextProactiveAt();timer->start(10);check(until([&]{return server.count==3 && !voice.engaged();},6000),"Successful proactive did not complete and restore");
    check(c.lastProactiveLine().id!=id && !row(c.memory(),c.lastProactiveLine().id)["first_heard_at"].toString().isEmpty(),"Repeat suppression or actual first-heard recording failed");
    const auto previewLife=c.memory().life().json();const auto previewCollection=c.memory().collection();
    const auto previewDue=c.nextProactiveAt();
    check(voice.previewProactiveLine(),"Opening preview did not start speech");
    check(!voice.previewProactiveLine(),"Opening preview interrupted active audio");
    check(until([&]{return server.count==4 && !voice.engaged();},6000),"Opening preview TTS did not finish");
    check(c.memory().life().json()==previewLife && c.memory().collection()==previewCollection && c.nextProactiveAt()==previewDue,"Opening preview changed growth, collection or schedule");
    // Resume coalesces a missed deadline instead of playing every missed opening.
    const int count=server.count;now=c.nextProactiveAt().addSecs(7200);timer->start(10);check(until([&]{return c.nextProactiveAt()>now;}) && server.count==count,"Resume caused a catch-up burst");
    QJsonArray log;QSet<QString> events;for(const auto &v:traces){const auto e=v[0].toJsonObject();log.append(e);events.insert(e["event"].toString());}
    for(const auto &e:{"timer_fired","conditions_passed","line_selected","bubble_shown","tts_requested","tts_playing","tts_failed","restored","scheduled","resume_reschedule"})check(events.contains(e),"Incomplete proactive trace chain");
    if(!review.isEmpty()){QFile file(review+"/proactive-trace.json");check(file.open(QIODevice::WriteOnly),"Cannot export trace");file.write(QJsonDocument(log).toJson());pet.grab().save(review+"/proactive-bubble.png");}
}
void gallery(const QString &path,const QString &review){
    Companion c(path);PetWidget pet(c);CompanionWindow window(c,pet);auto *tabs=window.findChild<QTabWidget*>("companionTabs");check(tabs->tabText(6)=="羁绊图鉴","Gallery entry missing");
    auto *page=window.findChild<BondGallery*>();check(page,"Gallery missing");window.show();tabs->setCurrentIndex(6);QTest::qWait(50);
    auto *lines=page->findChild<QListWidget*>("bondLines"),*recent=page->findChild<QListWidget*>("bondRecent");auto *detail=page->findChild<QTextBrowser*>("bondDetail");auto *state=page->findChild<QComboBox*>("bondState");auto *category=page->findChild<QComboBox*>("bondCategory");
    check(lines->count()==405 && recent->count()>0,"Collection count or recent unlocks incorrect");
    int categories[9]={},stageCounts[4]={},unlocked=0;for(const auto &v:c.memory().collection()){const auto l=v.toObject();++categories[DialogueEngine::categories().indexOf(l["category"].toString())];++stageCounts[l["relation_stage"].toInt()];unlocked+=l["unlocked"].toBool();}
    for(int i=0;i<9;++i){category->setCurrentIndex(i+1);check(lines->count()==categories[i] && categories[i]>0,"Category count incorrect or empty");}category->setCurrentIndex(0);
    state->setCurrentIndex(1);check(lines->count()==unlocked,"Unlocked count incorrect");state->setCurrentIndex(2);check(lines->count()==405-unlocked,"Locked count incorrect");
    const QString lockedId=lines->item(0)->data(Qt::UserRole).toString();lines->setCurrentRow(0);check(!detail->toPlainText().contains(DialogueEngine::find(lockedId)["text"].toString()),"Locked text leaked in detail");
    if(!review.isEmpty())window.grab().save(review+"/bond-locked.png");
    state->setCurrentIndex(0);auto *hide=page->findChild<QCheckBox*>("bondHideUnheard");hide->setChecked(true);check(c.lifeSettings().hideUnheard,"Exploration preference not saved");hide->setChecked(false);
    const auto before=row(c.memory(),"s0-00");c.dialoguePresented("s0-00");state->setCurrentIndex(3);check(lines->count()>=1,"Collected filter did not refresh");
    c.dialogueHeard("s0-00");check(!row(c.memory(),"s0-00")["first_heard_at"].toString().isEmpty(),"Heard timestamp missing");
    check(page->findChild<QProgressBar*>("bondProgress")->value()==c.memory().life().growthPercent(),"Affinity bar stale");
    const int oldPercent=c.memory().life().growthPercent();for(int i=0;i<4;++i)c.clickLine(PetAction::Idle,noon().addDays(i));
    check(page->findChild<QProgressBar*>("bondProgress")->value()>oldPercent,"Gallery did not refresh after genuine click growth");
    if(!review.isEmpty()){state->setCurrentIndex(0);window.grab().save(review+"/bond-gallery.png");
        QJsonObject counts;for(int i=0;i<4;++i)counts[QString::number(i)]=stageCounts[i];QFile f(review+"/catalog-counts.json");f.open(QIODevice::WriteOnly);f.write(QJsonDocument(QJsonObject{{"total",405},{"stages",counts}}).toJson());}
    Q_UNUSED(before);
}
void hiddenWindowProactive(const QString &dir,const QString &review){
    QDir().mkpath(dir);VoicePreferences p;p.voice="Momo";p.readAloud=false;p.preferOffline=false;p.cacheAudio=false;p.fastResponse=false;p.systemProxy=false;p.save(dir+"/voice.ini");CredentialStore(dir+"/dashscope-key.dpapi").save("fixture-secret");
    auto now=noon();Companion c(dir+"/memory.sqlite",nullptr,QUrl("http://127.0.0.1:9/responses"),100,[&]{return now;});
    auto settings=c.lifeSettings();settings.quietStart=settings.quietEnd=0;settings.focusProtection=false;c.saveLifeSettings(settings);
    SpeechFixture fixture;PetWidget pet(c);CompanionWindow window(c,pet,fixture.url());pet.setRandomActionsEnabled(false);pet.show();window.hide();
    QTest::qWait(9100);auto *timer=c.findChild<QTimer*>("proactiveScheduler");auto *input=window.findChild<QLineEdit*>("chatInput");
    input->setText("正在聊天输入");now=c.nextProactiveAt();timer->start(10);QTest::qWait(50);check(fixture.count==0,"Typing in hidden chat failed to defer proactive");
    input->clear();now=c.nextProactiveAt();timer->start(10);check(until([&]{return fixture.count==1 && !window.voiceController().engaged();}),"Hidden control window stopped proactive presentation");
    check(!window.isVisible() && pet.bubbleActive() && !pet.actionInteractionActive(),"Proactive opened full chat, lost bubble or failed to restore pose");
    check(c.memory().life().experience==0,"Actual UI proactive farmed affinity");
    if(!review.isEmpty())pet.grab().save(review+"/hidden-window-proactive.png");pet.hide();
}
}
int main(int argc,char **argv){
    qputenv("QT_MEDIA_BACKEND","windows");QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);qunsetenv("DASHSCOPE_API_KEY");qunsetenv("DEEPSEEK_API_KEY");QTemporaryDir tmp;
    try{
        if(app.arguments().size()==3 && app.arguments()[1]=="--migrate"){MemoryStore store(app.arguments()[2]);check(!store.collection().isEmpty(),"Migration has no catalog");return 0;}
        QString review;if(app.arguments().size()==3 && app.arguments()[1]=="--review"){review=app.arguments()[2];QDir().mkpath(review);}
        growth();stageUnlocks(tmp.path()+"/stages.sqlite");migration(tmp.path()+"/old.sqlite");schedulerAndAudio(tmp.path()+"/voice",review);gallery(tmp.path()+"/ui.sqlite",review);hiddenWindowProactive(tmp.path()+"/hidden",review);
        QTextStream(stdout)<<"PASS: "<<checks<<" bond/growth/scheduler/gallery checks. Real Qt timers with an injected clock; localhost speech fixtures only.\n";return 0;
    }catch(const std::exception &e){QTextStream(stderr)<<"FAIL: "<<e.what()<<"\n";return 1;}
}
