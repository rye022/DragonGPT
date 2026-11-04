#include "growth_center.h"
#include "pet_actions.h"
#include <QCheckBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

namespace dragon {
namespace {
QLabel *note(const QString &s){auto *l=new QLabel(s);l->setWordWrap(true);l->setTextFormat(Qt::PlainText);return l;}
QString localTime(const QJsonValue &v){return QDateTime::fromString(v.toString(),Qt::ISODateWithMs).toLocalTime().toString("yyyy-MM-dd HH:mm");}
QByteArray settingsKey(const LifeState &s,const LifeSettings &p){
    return QJsonDocument(QJsonObject{{"enabled",s.growth.enabled},{"personality",s.growth.personalityEnabled},{"habits",s.growth.habitsEnabled},
        {"shared",s.growth.sharedEnabled},{"idle",s.growth.idleEnabled},{"thresholds",s.growth.thresholds},{"baseline",s.growth.baseline},{"locked",s.growth.locked},
        {"autonomy",p.autonomyEnabled},{"tasks",p.taskSuggestions},{"followups",p.experienceFollowups},{"exploration",p.explorationEnabled},{"hourly",p.hourlyLimit}}).toJson(QJsonDocument::Compact);
}
}
GrowthCenter::GrowthCenter(Companion &c,QWidget *parent):QWidget(parent),companion_(c){
    setObjectName("growthCenter");auto *outer=new QVBoxLayout(this);auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);auto *body=new QWidget;auto *layout=new QVBoxLayout(body);scroll->setWidget(body);outer->addWidget(scroll);
    layout->addWidget(note("霜翎 · 养成中心\n每一次交流、一起专注和完成任务，都能成为成长的一部分。离开不会扣分。"));
    overview_=note("");overview_->setObjectName("growthOverview");layout->addWidget(overview_);
    progress_=new QProgressBar;progress_->setObjectName("growthProgress");progress_->setRange(0,100);layout->addWidget(progress_);
    unlocks_=note("");unlocks_->setObjectName("growthUnlocks");layout->addWidget(unlocks_);
    auto *links=new QHBoxLayout;for(const auto &name:QStringList{"记忆","羁绊图鉴","陪伴","专注陪伴","智能","Agent"}){
        auto *b=new QPushButton(name);links->addWidget(b);connect(b,&QPushButton::clicked,this,[this,name]{emit navigate(name);});
    }layout->addLayout(links);
    auto *settings=new QGroupBox("成长与性格 · 可独立开关");auto *sl=new QVBoxLayout(settings);layout->addWidget(settings);
    auto check=[sl](const QString &text,const QString &id){auto *w=new QCheckBox(text);w->setObjectName(id);sl->addWidget(w);return w;};
    enabled_=check("启用养成（暂停新增成长，保留已有进度）","growthEnabled");
    personality_=check("性格随有效交流缓慢成长","growthPersonality");
    habits_=check("学习互动时段（只保存本地小时计数）","growthHabits");
    sharedEnabled_=check("启用已确认的共同经历","growthSharedEnabled");
    idle_=check("按关系和性格选择成长待机动作","growthIdle");
    auto *gates=new QHBoxLayout;gates->addWidget(note("阶段门槛（%）"));
    for(int i=0;i<3;++i){thresholds_[i]=new QSpinBox;thresholds_[i]->setRange(1,99);thresholds_[i]->setObjectName("growthThreshold"+QString::number(i));const auto label=QStringList{"熟悉期门槛","亲近期门槛","深度羁绊门槛"}[i];thresholds_[i]->setToolTip(label);thresholds_[i]->setAccessibleName(label);gates->addWidget(thresholds_[i]);}sl->addLayout(gates);
    sl->addWidget(note("沿用旧版 20 / 50 / 80，可改为 25 / 50 / 80。已收藏台词会保留；新的选择按所设阶段解锁。\n性格为角色表现模拟：每次最多变化 0.1，累计偏移最多 ±10。锁定后以你的明确设置为准。"));
    auto *traits=new QGridLayout;traits->addWidget(note("倾向"),0,0);traits->addWidget(note("初始值"),0,1);traits->addWidget(note("锁定"),0,2);traits->addWidget(note("当前 / 变化"),0,3);
    const auto names=GrowthProfile::keys(),labels=GrowthProfile::labels();
    for(int i=0;i<6;++i){
        traits->addWidget(note(labels[i]),i+1,0);base_[i]=new QSpinBox;base_[i]->setRange(0,100);base_[i]->setObjectName("growthBase_"+names[i]);traits->addWidget(base_[i],i+1,1);
        locks_[i]=new QCheckBox;locks_[i]->setObjectName("growthLock_"+names[i]);traits->addWidget(locks_[i],i+1,2);
        trends_[i]=note("");trends_[i]->setObjectName("growthTrend_"+names[i]);traits->addWidget(trends_[i],i+1,3);
    }sl->addLayout(traits);
    auto *buttons=new QHBoxLayout;auto *saveButton=new QPushButton("保存养成设置");saveButton->setObjectName("saveGrowthSettings");
    auto *reset=new QPushButton("重置性格成长偏移");reset->setObjectName("resetGrowthDrift");buttons->addWidget(saveButton);buttons->addWidget(reset);sl->addLayout(buttons);
    connect(saveButton,&QPushButton::clicked,this,[this]{save();});connect(reset,&QPushButton::clicked,this,[this]{save(true);});
    auto *autonomyBox=new QGroupBox("自主生活 · 共用已有主动调度器");auto *al=new QVBoxLayout(autonomyBox);layout->addWidget(autonomyBox);
    auto ac=[al](const QString &text,const QString &id){auto *w=new QCheckBox(text);w->setObjectName(id);al->addWidget(w);return w;};
    autonomy_=ac("启用本地自主决策与互动时段适配","growthAutonomy");
    tasks_=ac("允许概括待办建议（不朗读任务正文）","growthTaskSuggestions");
    followups_=ac("允许邀请回顾共同经历（不主动朗读经历原文）","growthExperienceFollowups");
    exploration_=ac("允许探索建议与手动发起只读探索","growthExploration");
    auto *hourRow=new QHBoxLayout;hourRow->addWidget(note("每小时最多主动次数"));hourly_=new QSpinBox;hourly_->setObjectName("growthHourlyLimit");hourly_->setRange(1,10);hourRow->addWidget(hourly_);al->addLayout(hourRow);
    al->addWidget(note("上述设置点击「保存养成设置」生效。每日上限、冷却、静音、免打扰和全屏保护沿用「陪伴」。喝水与休息仍由「专注陪伴」管理。\n探索只在你提交目标后启动：授权目录与每项网络操作仍需在 Agent 审阅。不会后台循环调用模型。"));
    decision_=note("");decision_->setObjectName("growthDecision");al->addWidget(decision_);
    auto *goal=new QLineEdit;goal->setObjectName("growthExploreGoal");goal->setMaxLength(1000);goal->setPlaceholderText("填写想一起探索的问题，审阅后再执行");al->addWidget(goal);
    auto *exploreButton=new QPushButton("到 Agent 审阅只读探索目标");exploreButton->setObjectName("reviewGrowthExploration");al->addWidget(exploreButton);
    connect(exploreButton,&QPushButton::clicked,this,[this,goal]{
        if(!companion_.lifeSettings().explorationEnabled){status_->setText("请先开启探索并保存设置。");return;}
        if(goal->text().trimmed().isEmpty()){status_->setText("请先填写探索目标。");return;}emit explore(goal->text().trimmed());
    });
    auto *sharedBox=new QGroupBox("共同经历 · 仅保存你明确确认的内容");auto *sh=new QVBoxLayout(sharedBox);layout->addWidget(sharedBox);
    shared_=new QListWidget;shared_->setObjectName("growthSharedList");shared_->setMinimumHeight(120);shared_->setMaximumHeight(180);shared_->setWordWrap(true);sh->addWidget(shared_);
    sharedInput_=new QLineEdit;sharedInput_->setObjectName("growthSharedInput");sharedInput_->setMaxLength(200);sharedInput_->setPlaceholderText("例如：我们一起完成了 DragonGPT 的第一次升级");sh->addWidget(sharedInput_);
    auto *sr=new QHBoxLayout;auto *add=new QPushButton("确认保存经历"),*edit=new QPushButton("更正所选经历"),*remove=new QPushButton("删除所选经历");
    add->setObjectName("addGrowthShared");edit->setObjectName("editGrowthShared");remove->setObjectName("removeGrowthShared");sr->addWidget(add);sr->addWidget(edit);sr->addWidget(remove);sh->addLayout(sr);
    connect(shared_,&QListWidget::currentItemChanged,this,[this](QListWidgetItem *item){if(item)sharedInput_->setText(item->data(Qt::UserRole+1).toString());});
    connect(add,&QPushButton::clicked,this,[this]{try{const bool ok=companion_.memory().addSharedExperience(sharedInput_->text(),companion_.currentTime());status_->setText(ok?"已保存确认的共同经历。":"未保存：功能关闭、重复、内容过短或含敏感信息。");if(ok){sharedInput_->clear();companion_.sharedExperiencesChanged();}}catch(const std::exception &e){status_->setText(QString::fromUtf8(e.what()));}});
    connect(edit,&QPushButton::clicked,this,[this]{if(auto *item=shared_->currentItem())try{const bool ok=companion_.memory().editSharedExperience(item->data(Qt::UserRole).toString(),sharedInput_->text(),companion_.currentTime());status_->setText(ok?"经历已更正。":"内容重复、无效或功能已关闭。");if(ok)companion_.sharedExperiencesChanged();}catch(const std::exception &e){status_->setText(QString::fromUtf8(e.what()));}});
    connect(remove,&QPushButton::clicked,this,[this]{if(auto *item=shared_->currentItem())try{companion_.memory().forgetSharedExperience(item->data(Qt::UserRole).toString());companion_.sharedExperiencesChanged();}catch(const std::exception &e){status_->setText(QString::fromUtf8(e.what()));}});
    events_=new QListWidget;events_->setObjectName("growthEvents");events_->setWordWrap(true);events_->setMinimumHeight(160);events_->setMaximumHeight(240);
    layout->addWidget(note("最近成长 / 自主行为记录（最多 200 条，不保存对话原文）"));layout->addWidget(events_);
    auto *er=new QHBoxLayout;auto *exportButton=new QPushButton("导出成长与经历记录"),*clear=new QPushButton("清空成长记录与时段统计");exportButton->setObjectName("exportGrowthHistory");clear->setObjectName("clearGrowthHistory");er->addWidget(exportButton);er->addWidget(clear);layout->addLayout(er);
    connect(exportButton,&QPushButton::clicked,this,&GrowthCenter::exportHistory);connect(clear,&QPushButton::clicked,this,[this]{try{companion_.memory().clearGrowthHistory();companion_.sharedExperiencesChanged();status_->setText("成长记录和时段统计已清空，亲密度与经历保留。");}catch(const std::exception &e){status_->setText(QString::fromUtf8(e.what()));}});
    status_=note("");status_->setObjectName("growthCenterStatus");layout->addWidget(status_);layout->addStretch();
    connect(&c,&Companion::memoryChanged,this,&GrowthCenter::refresh);connect(&c.emotions(),&EmotionEngine::changed,this,&GrowthCenter::refresh);
    connect(&c,&Companion::proactiveTrace,this,[this](const QJsonObject &){decision_->setText("最近决策："+companion_.autonomyReason());});
    refresh();
}
void GrowthCenter::save(bool reset){
    try{
        auto g=companion_.memory().life().growth;g.enabled=enabled_->isChecked();g.personalityEnabled=personality_->isChecked();g.habitsEnabled=habits_->isChecked();g.sharedEnabled=sharedEnabled_->isChecked();g.idleEnabled=idle_->isChecked();
        g.thresholds={thresholds_[0]->value(),thresholds_[1]->value(),thresholds_[2]->value()};
        const auto keys=GrowthProfile::keys();for(int i=0;i<6;++i){g.baseline[keys[i]]=base_[i]->value();g.locked[keys[i]]=locks_[i]->isChecked();}
        auto p=companion_.lifeSettings();p.autonomyEnabled=autonomy_->isChecked();p.taskSuggestions=tasks_->isChecked();p.experienceFollowups=followups_->isChecked();p.explorationEnabled=exploration_->isChecked();p.hourlyLimit=hourly_->value();
        if(!GrowthProfile::validThresholds(g.thresholds))throw std::runtime_error("阶段阈值须在 1–99 内严格递增");
        companion_.saveLifeSettings(p);companion_.saveGrowthPreferences(g,reset);status_->setText(reset?"性格成长偏移已重置。":"养成设置已保存。");
    }catch(const std::exception &e){status_->setText(QString::fromUtf8(e.what()));}
}
void GrowthCenter::refresh(){
    const auto s=companion_.memory().life();const auto &g=s.growth;const auto p=companion_.lifeSettings();const bool today=s.growthDate==companion_.currentTime().toLocalTime().date();
    overview_->setText(QString("%1 · 亲密度 %2% · 信任 %3% · 心情：%4\n累计活跃 %5 天 · 今日成长 %6 / 20\n今日有效交流 %7 · 单击 %8 / 2 · 完成任务 %9 · 完成专注 %10")
        .arg(s.stage()).arg(s.growthPercent()).arg(g.trust/6).arg(emotionName(companion_.effectiveMood())).arg(s.activeDays).arg(today?s.dailyExperience:0)
        .arg(today?g.dailyChats:0).arg(today?g.dailyClicks:0).arg(today?g.dailyTasks:0).arg(today?g.dailyFocus:0));
    progress_->setValue(s.growthPercent());progress_->setFormat(QString::number(s.growthPercent())+"% · "+s.stage());
    int unlocked=0;QStringList titles;for(const auto &v:companion_.memory().collection()){const auto row=v.toObject();if(row["unlocked"].toBool()){++unlocked;if(titles.size()<2)titles<<row["text"].toString();}}
    QStringList actions{"陪伴","抱尾陪伴"};if(s.stageIndex()>=1)actions<<"开心招手";if(s.stageIndex()>=2)actions<<"害羞傲娇";if(s.stageIndex()>=3)actions<<"认真工作";
    unlocks_->setText(QString("台词已解锁 %1 / 405 · 羁绊阶段事件 %2\n成长待机动作：%3\n台词示例：%4").arg(unlocked).arg(companion_.memory().unlockedEvents()).arg(actions.join("、"),titles.join(" / ")));
    decision_->setText("最近决策："+companion_.autonomyReason());
    const auto key=settingsKey(s,p);if(preferencesSnapshot_!=key){
        preferencesSnapshot_=key;enabled_->setChecked(g.enabled);personality_->setChecked(g.personalityEnabled);habits_->setChecked(g.habitsEnabled);sharedEnabled_->setChecked(g.sharedEnabled);idle_->setChecked(g.idleEnabled);
        autonomy_->setChecked(p.autonomyEnabled);tasks_->setChecked(p.taskSuggestions);followups_->setChecked(p.experienceFollowups);exploration_->setChecked(p.explorationEnabled);hourly_->setValue(p.hourlyLimit);
        for(int i=0;i<3;++i)thresholds_[i]->setValue(g.thresholds[i].toInt());
        const auto keys=GrowthProfile::keys();for(int i=0;i<6;++i){base_[i]->setValue(g.baseline[keys[i]].toInt());locks_[i]->setChecked(g.locked[keys[i]].toBool());}
    }
    const auto keys=GrowthProfile::keys();const auto values=g.traits();for(int i=0;i<6;++i)trends_[i]->setText(QString("%1 / %2%3").arg(values[keys[i]].toDouble(),0,'f',1).arg(g.drift[keys[i]].toInt()/10.0,0,'f',1).arg(g.locked[keys[i]].toBool()?"（已锁定）":""));
    const QString selected=shared_->currentItem()?shared_->currentItem()->data(Qt::UserRole).toString():QString{};
    {QSignalBlocker blocker(shared_);shared_->clear();for(int i=int(g.shared.size())-1;i>=0;--i){const auto row=g.shared[i].toObject();const QString source=row["source"]=="user-corrected"?"用户更正":"用户确认";auto *item=new QListWidgetItem(row["title"].toString()+"\n"+source+" · "+localTime(row["at"]),shared_);item->setData(Qt::UserRole,row["id"]);item->setData(Qt::UserRole+1,row["title"]);if(row["id"]==selected)shared_->setCurrentItem(item);}}
    const QJsonObject labels{{"chat","有效交流"},{"click","单击互动"},{"task","完成任务"},{"focus","完成专注"}};
    events_->clear();for(int i=int(g.events.size())-1;i>=std::max(0,int(g.events.size())-40);--i){const auto e=g.events[i].toObject();const auto kind=e["kind"].toString();new QListWidgetItem(localTime(e["at"])+" · "+labels.value(kind).toString(kind)+QString(" +%1 XP\n").arg(e["earned"].toInt())+e["reason"].toString(),events_);}
}
void GrowthCenter::exportHistory(){
    const auto path=QFileDialog::getSaveFileName(this,"导出已确认的经历与成长记录",{},"JSON (*.json)");if(path.isEmpty())return;
    const auto g=companion_.memory().life().growth;QSaveFile file(path);
    const auto data=QJsonDocument(QJsonObject{{"format","DragonGPT growth history"},{"version",1},{"shared_experiences",g.shared},{"growth_events",g.events}}).toJson();
    if(!file.open(QIODevice::WriteOnly) || file.write(data)!=data.size() || !file.commit()){status_->setText("导出失败。");return;}status_->setText("成长记录已导出，包含你确认的共同经历。");
}
}
