#include "growth_center.h"
#include "agent_core.h"
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
using namespace dragon;
namespace {QDateTime noon(){return QDateTime(QDate(2026,11,1),QTime(12,0));}}
class GrowthTests:public QObject {
    Q_OBJECT
private slots:
    void antiFarmAndDisable(){
        LifeState s;const auto t=noon();
        for(int i=0;i<1000;++i)s.award("click",{},t.addSecs(i*90));
        // This loop spans two days; each receives at most two click XP.
        QVERIFY(s.experience<=4);QCOMPARE(s.growth.trust,0);
        const auto before=s.json();s.growth.enabled=false;
        QVERIFY(!s.award("chat","学习很开心，谢谢你",t.addDays(4)));QCOMPARE(s.experience,before["experience"].toInt());
        s.growth.enabled=true;QVERIFY(s.award("chat","学习很开心，谢谢你",t.addDays(4)));
        const int xp=s.experience;QVERIFY(!s.award("chat"," 学习很开心，谢谢你 ",t.addDays(4).addSecs(60)));QCOMPARE(s.experience,xp);
        QVERIFY(!s.award("chat","新的交流",t.addDays(-1)));QVERIFY(!s.award("focus",{},t.addDays(4)));
    }
    void continuedLifeAtFullAffinity(){
        LifeState s;s.experience=600;s.activeDays=30;const auto t=noon();
        for(int i=0;i<20;++i)QVERIFY(!s.award("chat",QString("学习长期陪伴 %1").arg(i),t.addSecs(i*31)));
        QCOMPARE(s.experience,600);QCOMPARE(s.growthPercent(),100);QCOMPARE(s.growth.dailyChats,10);QCOMPARE(s.growth.trust,10);
        QCOMPARE(s.growth.drift["curiosity"].toInt(),10);QCOMPARE(s.growth.habits[12].toInt(),10);
        QCOMPARE(s.growth.events.size(),10);for(const auto &v:s.growth.events)QCOMPARE(v.toObject()["earned"].toInt(),0);
        const auto old=s.json();QVERIFY(!s.award("click",{},t.addSecs(1500)));QCOMPARE(s.growth.events.size(),10);
        s=LifeState::fromJson(old);QVERIFY(!s.award("task",{},t.addDays(1),"task-after-full"));QCOMPARE(s.experience,600);QCOMPARE(s.growth.dailyTasks,1);
        QVERIFY(s.growth.drift["initiative"].toInt()>0);
        s.experience=900;QVERIFY(!s.award("chat","旧存档继续学习",t.addDays(2)));QCOMPARE(s.experience,900);QCOMPARE(s.dailyExperience,0);
    }
    void smoothTraitsAndPrivacy(){
        LifeState s;const auto t=noon();
        for(int d=0;d<20;++d)for(int i=0;i<10;++i)s.award("chat",QString("学习研究真开心，谢谢，哈哈 %1 %2").arg(d).arg(i),t.addDays(d).addSecs(i*31));
        QCOMPARE(s.growth.traits()["curiosity"].toDouble(),80.0);QCOMPARE(s.growth.drift["curiosity"].toInt(),100);
        QVERIFY(s.growth.events.size()<=200);QVERIFY(!QJsonDocument(s.growth.json()).toJson().contains("学习研究真开心"));
        s.growth.locked["curiosity"]=true;s.growth.baseline["curiosity"]=33;
        QCOMPARE(s.growth.traits()["curiosity"].toDouble(),33.0);
        QCOMPARE(LifeState::fromJson(s.json()).growth.traits(),s.growth.traits());
        QTemporaryDir dir;MemoryStore store(dir.filePath("db.sqlite"));store.configureGrowth(s.growth,true,t);
        QCOMPARE(store.life().growth.drift["curiosity"].toInt(),0);QCOMPARE(store.life().growth.baseline["curiosity"].toInt(),33);
        auto p=store.life().growth;p.thresholds={80,25,50};QVERIFY_EXCEPTION_THROWN(store.configureGrowth(p),std::runtime_error);
    }
    void thresholdsAndUnlocks(){
        QTemporaryDir dir;MemoryStore store(dir.filePath("db.sqlite"));auto g=store.life().growth;g.thresholds={25,50,80};store.configureGrowth(g,false,noon());
        for(int d=0;d<3;++d)for(int i=0;i<10;++i)store.awardInteraction("chat",QString("交流 %1 %2").arg(d).arg(i),noon().addDays(d).addSecs(i*31));
        QCOMPARE(store.life().growthPercent(),20);QCOMPARE(store.life().stageIndex(),0);
        for(const auto &v:store.collection())if(v.toObject()["id"]=="s1-00"){QVERIFY(!v.toObject()["unlocked"].toBool());QCOMPARE(v.toObject()["required_affinity"].toInt(),25);}
        for(int d=3;d<6;++d)for(int i=0;i<10;++i)store.awardInteraction("chat",QString("交流 %1 %2").arg(d).arg(i),noon().addDays(d).addSecs(i*31));
        QVERIFY(store.life().stageIndex()>=1);int count=0;for(const auto &v:store.collection())if(v.toObject()["unlocked"].toBool() && v.toObject()["relation_stage"].toInt()==1)++count;
        QVERIFY(count>3);
    }
    void sharedExperiences(){
        QTemporaryDir dir;const auto path=dir.filePath("db.sqlite");QString id;
        {
            MemoryStore s(path);QVERIFY(s.addSharedExperience("我们一起完成了龙娘升级",noon()));
            QVERIFY(!s.addSharedExperience(" 我们一起完成了龙娘升级！ ",noon()));QVERIFY(!s.addSharedExperience("密码：fixture-password",noon()));
            id=s.life().growth.shared.first().toObject()["id"].toString();
            QVERIFY(s.life().growth.sharedContext("共同回忆").contains("龙娘升级"));
            QVERIFY(!s.life().growth.sharedContext("今天吃什么").contains("龙娘升级"));
            QVERIFY(s.editSharedExperience(id,"我们一起完成了养成系统",noon()));
        }
        {
            Companion c(path);QVERIFY(c.promptContext("回忆").contains("养成系统"));
            QVERIFY(c.respond("回忆我们的共同经历").text.contains("养成系统"));
            const auto g=c.memory().life().growth;QVERIFY(!g.events.isEmpty());
            auto p=g;p.sharedEnabled=false;c.saveGrowthPreferences(p);QVERIFY(!c.promptContext("回忆").contains("养成系统"));
            QVERIFY(!c.respond("回忆我们的共同经历").text.contains("一起完成了养成系统"));
            c.memory().forgetSharedExperience(id);QVERIFY(c.memory().life().growth.shared.isEmpty());
            p.sharedEnabled=true;c.saveGrowthPreferences(p);QVERIFY(!c.promptContext("回忆").contains("养成系统"));
        }
    }
    void autonomyGuardsAndHabits(){
        LifeState s;LifeSettings p;p.quietStart=p.quietEnd=0;p.cooldownMinutes=5;p.hourlyLimit=2;p.dailyLimit=10;const auto t=noon();
        s.growth.recordProactive("greeting","test",t);s.growth.recordProactive("greeting","test",t.addSecs(600));
        QVERIFY(CompanionLife::blockedReason(s,p,t.addSecs(1200),t.addSecs(-60),false).contains("每小时"));
        QVERIFY(CompanionLife::eligible(s,p,t.addSecs(4300),t,false));
        p.focusMode=true;QVERIFY(!CompanionLife::eligible(s,p,t.addSecs(4300),t,false));p.focusMode=false;
        QVERIFY(!CompanionLife::eligible(s,p,t.addSecs(4300),t,true));
        QVERIFY(!CompanionLife::eligible(s,p,t.addSecs(-1),t,false));
        s.growth.habits[12]=50;QVERIFY(!s.growth.preferredHour(QDateTime(t.date(),QTime(18,0))));QVERIFY(s.growth.preferredHour(t));
        s.growth.habitsEnabled=false;QVERIFY(s.growth.preferredHour(QDateTime(t.date(),QTime(18,0))));
        QTemporaryDir dir;p.taskSuggestions=true;p.experienceFollowups=true;p.explorationEnabled=true;p.save(dir.filePath("life.ini"));
        const auto restored=LifeSettings::load(dir.filePath("life.ini"));QCOMPARE(restored.hourlyLimit,2);QVERIFY(restored.explorationEnabled && restored.experienceFollowups && restored.taskSuggestions);
    }
    void integratedTaskAndProactive(){
        QTemporaryDir dir;auto t=noon();Companion c(dir.filePath("db.sqlite"),nullptr,QUrl("http://127.0.0.1:9"),100,[&]{return t;});
        TaskDraft draft;draft.title="private-task-title";const auto id=c.tools().addTask(draft);c.tools().completeTask(id,true);
        const int xp=c.memory().life().experience;QVERIFY(xp>0);c.tools().completeTask(id,true);QCOMPARE(c.memory().life().experience,xp);
        c.tools().completeTask(id,false);t=t.addSecs(1800);c.tools().completeTask(id,true);QCOMPARE(c.memory().life().experience,xp);
        draft.title="another-private-task";c.tools().addTask(draft);
        auto p=c.lifeSettings();p.taskSuggestions=true;p.quietStart=p.quietEnd=0;c.saveLifeSettings(p);
        t=t.addSecs(180);QSignalSpy spy(&c,&Companion::proactiveReady);QVERIFY(c.tryProactive(false,t));QCOMPARE(spy.size(),1);
        QCOMPARE(c.lastProactiveLine().id,QString("life.tasks"));QVERIFY(!spy.first().first().toString().contains("private-task"));
        QCOMPARE(c.memory().life().experience,xp);QVERIFY(!c.memory().life().growth.events.isEmpty());
        QVERIFY(!QJsonDocument(c.memory().life().growth.json()).toJson().contains("private-task-title"));
    }
    void explorationPermissions(){
        for(const auto &n:{"file_read","file_search","web_read","existing_todo_list"})QVERIFY(AgentCore::explorationToolAllowed(n));
        for(const auto &n:{"file_write","file_trash","dev_run","browser_click","github_release","desktop_fill","existing_todo_create"})QVERIFY(!AgentCore::explorationToolAllowed(n));
        QVERIFY(AgentCore::needsApproval("web_read"));QVERIFY(AgentCore::needsApproval("web_search"));
        QTemporaryDir dir;Companion c(dir.filePath("db.sqlite"));ToolHub hub;AgentCore agent(c,hub);
        QVERIFY(!agent.start("explore",true));QVERIFY(!agent.busy());QVERIFY(agent.toolCatalog().size()>10);
    }
    void traitsActuallyAffectExpression(){
        DialogueContext a;a.percent=50;a.now=noon();a.emotion=Emotion::Calm;auto b=a;
        a.traits={{"care",100},{"playfulness",0},{"tsundere",0},{"humor",0},{"curiosity",0},{"initiative",0}};
        b.traits={{"care",0},{"playfulness",100},{"tsundere",100},{"humor",100},{"curiosity",100},{"initiative",100}};
        int differences=0;for(quint32 seed=1;seed<=40;++seed){QJsonObject h1,h2;const auto x=DialogueEngine::select(a,h1,seed),y=DialogueEngine::select(b,h2,seed);if(x.id!=y.id)++differences;}
        QVERIFY(differences>0);
        QTemporaryDir dir;Companion c(dir.filePath("db.sqlite"));auto p=c.memory().life().growth;p.baseline["care"]=95;c.saveGrowthPreferences(p);
        const auto context=QJsonDocument::fromJson(c.promptContext("你好").toUtf8()).object();QCOMPARE(context["personality_tendencies"].toObject()["care"].toInt(),95);
        QVERIFY(c.respond("随便说说").text.contains("认真听"));
        p.enabled=false;c.saveGrowthPreferences(p);QVERIFY(QJsonDocument::fromJson(c.promptContext("你好").toUtf8()).object()["personality_tendencies"].toObject().isEmpty());
    }
    void boundedLongRunSimulation(){
        GrowthProfile g;const auto t=noon();
        for(int i=0;i<5000;++i){g.observe("chat","学习，谢谢",2,t.addSecs(i*1800));g.recordProactive("test","本地模拟决策",t.addSecs(i*1800));}
        QVERIFY(g.events.size()<=200);QVERIFY(g.proactiveTimes.size()<=100);QCOMPARE(g.trust,600);
        QCOMPARE(g.drift["curiosity"].toInt(),100);QVERIFY(QJsonDocument(g.json()).toJson().size()<100000);
        auto s=GrowthProfile::fromJson(g.json());QCOMPARE(s.events,g.events);QCOMPARE(s.habits,g.habits);
    }
    void legacySaveMigration(){
        QTemporaryDir dir;const auto path=dir.filePath("db.sqlite");LifeState before;
        {MemoryStore s(path);s.setNickname("保留昵称");s.remember("我喜欢安静");s.appendExchange("学习","好","fixture",noon());before=s.life();}
        {
            auto db=QSqlDatabase::addDatabase("QSQLITE","growth-legacy");db.setDatabaseName(path);QVERIFY(db.open());QSqlQuery q(db);
            auto json=before.json();json.remove("growth_v5");q.prepare("UPDATE companion_life SET state=?");q.addBindValue(QString::fromUtf8(QJsonDocument(json).toJson()));QVERIFY(q.exec());QVERIFY(q.exec("PRAGMA user_version=7"));q.finish();db.close();
        }QSqlDatabase::removeDatabase("growth-legacy");
        {
            MemoryStore s(path);QCOMPARE(s.nickname(),QString("保留昵称"));QCOMPARE(s.memories().size(),1);QCOMPARE(s.life().experience,before.experience);QCOMPARE(s.life().growthPercent(),before.growthPercent());QCOMPARE(s.integrity(),QString("ok"));
            QVERIFY(!QDir(dir.path()).entryList({"*.pre-v8-*.sqlite"},QDir::Files).isEmpty());
        }
        MemoryStore s(path);QCOMPARE(s.life().growth.baseline,GrowthProfile().baseline);
    }
    void liveCenter(){
        QTemporaryDir dir;auto t=noon();Companion c(dir.filePath("db.sqlite"),nullptr,QUrl("http://127.0.0.1:9"),100,[&]{return t;});GrowthCenter page(c);page.resize(850,760);page.show();QTest::qWait(80);
        auto *enabled=page.findChild<QCheckBox*>("growthEnabled");auto *save=page.findChild<QPushButton*>("saveGrowthSettings");QVERIFY(enabled && save);
        enabled->setChecked(false);save->click();QVERIFY(!c.memory().life().growth.enabled);
        c.respond("今天一起学习");QCOMPARE(c.memory().life().experience,0);
        enabled->setChecked(true);save->click();t=t.addSecs(60);c.respond("今天一起研究");QVERIFY(c.memory().life().experience>0);
        QCOMPARE(page.findChild<QProgressBar*>("growthProgress")->value(),c.memory().life().growthPercent());
        auto *input=page.findChild<QLineEdit*>("growthSharedInput");input->setText("我们一起完成了测试");page.findChild<QPushButton*>("addGrowthShared")->click();
        QCOMPARE(page.findChild<QListWidget*>("growthSharedList")->count(),1);
        QSignalSpy explore(&page,&GrowthCenter::explore);page.findChild<QLineEdit*>("growthExploreGoal")->setText("研究Qt的事件循环");page.findChild<QPushButton*>("reviewGrowthExploration")->click();QCOMPARE(explore.size(),0);
        page.findChild<QCheckBox*>("growthExploration")->setChecked(true);save->click();page.findChild<QPushButton*>("reviewGrowthExploration")->click();QCOMPARE(explore.size(),1);QVERIFY(!c.busy());
        const auto root=qEnvironmentVariable("DRAGON_GROWTH_REVIEW");if(!root.isEmpty()){QDir().mkpath(root);QVERIFY(page.grab().save(root+"/growth-center.png"));}
    }
};
QTEST_MAIN(GrowthTests)
#include "test_growth.moc"
