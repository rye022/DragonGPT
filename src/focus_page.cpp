#include "focus_page.h"
#include "companion.h"
#include "pet_widget.h"
#include "voice_controller.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimeEdit>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QtConcurrent>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wtsapi32.h>
#endif

namespace dragon {
FocusPage::FocusPage(Companion &c,PetWidget &p,VoiceController &v,QWidget *parent):QWidget(parent),companion_(c),pet_(p),voice_(v),service_(c.memory(),{QDateTime::currentDateTimeUtc(),0},this){
    clock_.start();setObjectName("focusPage");
    setStyleSheet("QWidget#focusCard{background:#f8f4ff;color:#584469;border-radius:14px;} QWidget#focusCard QLabel{color:#584469;} QWidget#focusCard QLineEdit{background:#fff;color:#584469;} QLabel#focusCountdown{font-size:56px;font-weight:600;color:#806099;} QLabel#focusPhase{font-size:16px;color:#806099;}");
    auto *outer=new QVBoxLayout(this);outer->setContentsMargins(0,12,0,0);
    auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);outer->addWidget(scroll);
    auto *card=new QWidget;card->setObjectName("focusCard");scroll->setWidget(card);auto *layout=new QVBoxLayout(card);layout->setContentsMargins(18,16,18,16);layout->setSpacing(10);
    auto *title=new QLabel("专注陪伴");title->setStyleSheet("font-size:22px;font-weight:600;");layout->addWidget(title);
    phase_=new QLabel;phase_->setObjectName("focusPhase");phase_->setAlignment(Qt::AlignCenter);layout->addWidget(phase_);
    countdown_=new QLabel;countdown_->setObjectName("focusCountdown");countdown_->setAlignment(Qt::AlignCenter);layout->addWidget(countdown_);
    task_=new QLineEdit(service_.task());task_->setObjectName("focusTask");task_->setPlaceholderText("这一轮想完成什么？");task_->setMaxLength(120);layout->addWidget(task_);
    auto *row=new QHBoxLayout;layout->addLayout(row);
    auto action=[&](const QString &text,const char *name,const std::function<void()> &fn){auto *b=new QPushButton(text);b->setObjectName(name);row->addWidget(b);connect(b,&QPushButton::clicked,this,[this,fn]{guarded(fn);});return b;};
    start_=action("开始","focusStart",[this]{service_.start(task_->text());});
    pause_=action("暂停","focusPause",[this]{service_.pause();});resume_=action("继续","focusResume",[this]{service_.resume();});
    end_=action("结束","focusEnd",[this]{service_.end();});
    row=new QHBoxLayout;layout->addLayout(row);
    skip_=action("跳过阶段","focusSkip",[this]{service_.skip();});reset_=action("重置","focusReset",[this]{service_.reset();});
    action("设置","focusSettings",[this]{settingsDialog();});
    action("专注记录","focusHistory",[this]{
        QDialog dialog(this);dialog.setWindowTitle("本地专注记录");dialog.resize(500,500);auto *layout=new QVBoxLayout(&dialog);auto *history=new QTextBrowser;layout->addWidget(history);
        QStringList lines;for(const auto &value:companion_.memory().focusSessions()){
            const auto r=value.toObject();const auto status=r.value("status").toString();
            const QString label=status=="completed"?"已完成":status=="skipped"?"已跳过":status=="reset"?"已重置":"已结束";
            lines<<QString("%1  %2\n%3 → %4\n计划 %5 分钟 / 实际 %6 分钟 · %7 · 奖励 %8（已结算）\n")
                .arg(r.value("task").toString().isEmpty()?"未命名任务":r.value("task").toString(),label,
                     QDateTime::fromString(r.value("started").toString(),Qt::ISODateWithMs).toLocalTime().toString("yyyy-MM-dd HH:mm"),
                     QDateTime::fromString(r.value("ended").toString(),Qt::ISODateWithMs).toLocalTime().toString("yyyy-MM-dd HH:mm"))
                .arg(r.value("planned_ms").toInteger()/60000).arg(r.value("actual_ms").toInteger()/60000.0,0,'f',1).arg(label).arg(r.value("reward").toInt());
        }
        history->setPlainText(lines.isEmpty()?"完成、跳过或结束一轮后，这里会保存记录。":lines.join('\n'));dialog.exec();
    });
    auto label=[&](const char *name){auto *l=new QLabel;l->setObjectName(name);l->setTextFormat(Qt::PlainText);l->setWordWrap(true);layout->addWidget(l);return l;};
    recovery_=label("focusRecovery");stats_=label("focusStats");trend_=label("focusWeekTrend");water_=label("focusWaterStatus");
    waterActions_=new QWidget;auto *waterRow=new QHBoxLayout(waterActions_);waterRow->setContentsMargins(0,0,0,0);
    for(int i=0;i<3;++i){auto *b=new QPushButton(QStringList{"我喝过啦","15 分钟后提醒","今天不再提醒"}[i]);b->setObjectName(QString("focusWaterAction%1").arg(i));waterRow->addWidget(b);
        connect(b,&QPushButton::clicked,this,[this,i]{guarded([this,i]{if(i==0)service_.confirmWater();else if(i==1)service_.snoozeWater();else service_.muteWaterToday();});});}
    layout->addWidget(waterActions_);monitor_=label("focusActivityStatus");voiceStatus_=label("focusVoiceStatus");
    auto *privacy=label("focusPrivacy");privacy->setText("仅在本机保存专注与主动确认记录；不记录按键内容、访问网站或饮水量。提醒间隔是陪伴设置，不是医学饮水标准。");privacy->setStyleSheet("font-size:11px;color:#8c799c;");layout->addStretch();
    connect(&service_,&FocusService::sessionSaved,&companion_,&Companion::focusSettled);
    connect(&service_,&FocusService::sessionSaved,this,[this]{lastStats_=-10000;});
    connect(&service_,&FocusService::changed,this,&FocusPage::refresh);
    connect(&service_,&FocusService::sessionEnded,this,[this]{companion_.emotions().request(Emotion::Relaxed,.6,30000,10);});
    connect(&pet_,&PetWidget::waterConfirmed,this,[this]{guarded([this]{service_.confirmWater();});});
    connect(&pet_,&PetWidget::waterSnoozed,this,[this]{guarded([this]{service_.snoozeWater();});});
    connect(&pet_,&PetWidget::waterMutedToday,this,[this]{guarded([this]{service_.muteWaterToday();});});
    connect(&voice_,&VoiceController::changed,this,[this]{voiceStatus_->setText("当前音色："+voice_.voiceLabel()+" · "+voice_.status());});
    connect(&probe_,&QFutureWatcher<ActivitySample>::finished,this,[this]{activity_=probe_.result();if(activity_.idleAvailable)emit activityObserved(activity_.idleMs/1000);});
    connect(qApp,&QCoreApplication::aboutToQuit,this,[this]{guarded([this]{service_.checkpoint();});});
    qApp->installNativeEventFilter(this);
#ifdef Q_OS_WIN
    QTimer::singleShot(0,this,[this]{registeredWindow_=window()->winId();WTSRegisterSessionNotification(reinterpret_cast<HWND>(registeredWindow_),NOTIFY_FOR_THIS_SESSION);});
#endif
    timer_.setInterval(1000);connect(&timer_,&QTimer::timeout,this,[this]{guarded([this]{step();});});timer_.start();refresh();
}
FocusPage::~FocusPage(){
    qApp->removeNativeEventFilter(this);
#ifdef Q_OS_WIN
    if(registeredWindow_)WTSUnRegisterSessionNotification(reinterpret_cast<HWND>(registeredWindow_));
#endif
    timer_.stop();probe_.waitForFinished();try{service_.checkpoint();}catch(const std::exception &){}pet_.setCompanionMode(-1);pet_.setWaterActionsVisible(false);}
bool FocusPage::nativeEventFilter(const QByteArray &,void *message,qintptr *){
#ifdef Q_OS_WIN
    auto *msg=static_cast<MSG*>(message);
    if(msg->message==WM_POWERBROADCAST && msg->wParam==PBT_APMSUSPEND)
        guarded([this]{service_.systemPause("系统即将休眠");});
    if(msg->message==WM_WTSSESSION_CHANGE && msg->wParam==WTS_SESSION_LOCK){activity_.locked=true;activity_.lockAvailable=true;guarded([this]{service_.systemPause("系统已锁屏");});}
#else
    Q_UNUSED(message);
#endif
    return false;
}
void FocusPage::guarded(const std::function<void()> &fn){try{fn();}catch(const std::exception &e){failed_=true;timer_.stop();recovery_->setText("本地保存失败，调度已停止："+QString::fromUtf8(e.what()));start_->setEnabled(false);pause_->setEnabled(false);resume_->setEnabled(false);}}
void FocusPage::step(){
    const auto ms=clock_.elapsed();
    if(ms-lastProbe_>=5000 && !probe_.isRunning()){lastProbe_=ms;probe_.setFuture(QtConcurrent::run(systemActivity));}
    auto sample=activity_;if(ms-lastProbe_>15000){sample.idleAvailable=false;sample.source="活跃检测暂不可用，暂停累计";}
    bool typing=false;if(auto *input=qobject_cast<QLineEdit*>(QApplication::focusWidget()))typing=input->isEnabled() && !input->text().isEmpty();
    const bool busy=companion_.busy() || voice_.active() || voice_.speechBusy() || typing || pet_.actionInteractionActive();
    const auto now=QDateTime::currentDateTimeUtc();const auto &life=companion_.lifeSettings();
    service_.tick({now,ms},sample,busy,life.focusMode || life.quiet(now.toLocalTime()));
    companion_.updateEmotionContext(now);
    CompanionNotice notice;if(!busy && service_.takeNotice(notice)){
        const bool bubble=notice.priority>=3 || !notice.water || service_.settings().waterBubble;
        if(bubble)pet_.say(notice.text);
        const bool tts=!notice.silent && (notice.priority>=3?service_.settings().focusTts:notice.water?service_.settings().waterTts:service_.settings().focusTts);
        bool spoken=false;if(tts)spoken=voice_.speakCompanionNotice(notice.text,bubble,notice.lineId);
        if(!tts)voiceStatus_->setText(notice.silent?"提醒：勿扰时段，已显示文字，不播放语音。":"提醒：语音开关关闭，已显示文字。");
        if(bubble && !spoken){service_.shown(notice);if(notice.water)companion_.memoryChanged();}
    }
}
void FocusPage::refresh(){
    if(failed_)return;
    const qint64 seconds=(service_.remainingMs()+999)/1000;
    countdown_->setText(QString("%1:%2").arg(seconds/60,2,10,QChar('0')).arg(seconds%60,2,10,QChar('0')));
    phase_->setText(service_.phaseName()+(service_.paused()?" · 已暂停":service_.running()?" · 进行中":" · 准备好就开始"));
    start_->setEnabled(!service_.running());pause_->setEnabled(service_.running() && !service_.paused());resume_->setEnabled(service_.paused());end_->setEnabled(service_.running());skip_->setEnabled(service_.running());
    task_->setEnabled(!service_.running());recovery_->setText(service_.recoveryStatus());
    water_->setText(service_.waterStatus());waterActions_->setVisible(service_.waterPending());
    pet_.setWaterActionsVisible(service_.waterPending() && service_.settings().waterBubble);
    companion_.setFocusContext(service_.focusing(),service_.running() && !service_.focusing());
    pet_.setCompanionMode(!service_.running()?-1:service_.phase()==FocusService::Phase::Focus?(service_.paused()?-1:0):1);
    if(clock_.elapsed()-lastStats_>=5000){
        lastStats_=clock_.elapsed();auto stats=service_.todayStats();
        stats_->setText(QString("今日专注 %1 分钟  ·  完成 %2 个番茄钟").arg(stats.value("ms").toInteger()/60000).arg(stats.value("count").toInt()));
        trend_->setText("最近七天专注趋势\n"+service_.weekTrend());
    }
    const auto minutes=[](qint64 ms){return ms<0?QString("不可用"):QString::number(ms/60000)+" 分钟";};
    monitor_->setText(QString("系统开机：%1  ·  登录会话：%2\n本次应用会话已监测活跃：%3\n%4\n空闲检测：%5；锁屏检测：%6").arg(minutes(activity_.uptimeMs),minutes(activity_.sessionMs),minutes(service_.sessionActiveMs()),activity_.source.isEmpty()?"正在获取系统状态…":activity_.source,activity_.idleAvailable?minutes(activity_.idleMs):"不可用",activity_.lockAvailable?(activity_.locked?"已锁屏":"可用"):"不可用"));
}
void FocusPage::settingsDialog(){
    QDialog dialog(this);dialog.setWindowTitle("专注陪伴设置");dialog.resize(480,680);
    auto *root=new QVBoxLayout(&dialog);auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);root->addWidget(scroll);auto *body=new QWidget;scroll->setWidget(body);auto *form=new QFormLayout(body);
    auto s=service_.settings();
    auto spin=[&](const QString &name,int value,int min,int max){auto *w=new QSpinBox;w->setRange(min,max);w->setValue(value);form->addRow(name,w);return w;};
    auto check=[&](const QString &name,bool value){auto *w=new QCheckBox(name);w->setChecked(value);form->addRow(w);return w;};
    auto *focus=spin("专注（分钟）",s.focusSeconds/60,1,180),*shortBreak=spin("短休息（分钟）",s.shortSeconds/60,1,60),*longBreak=spin("长休息（分钟）",s.longSeconds/60,1,120),*every=spin("每几个专注后长休息",s.longEvery,1,12);
    auto *autoNext=check("自动开始下一阶段",s.autoNext),*focusTts=check("朗读专注开始与结束提醒（当前音色）",s.focusTts);
    auto *water=check("启用喝水提醒",s.waterEnabled);auto *interval=spin("喝水评估间隔（分钟）",s.waterMinutes,1,240);
    auto *activity=check("按实际活跃使用时长判断",s.useActivity),*defer=check("专注时延迟提醒",s.deferFocus),*prefer=check("优先在休息阶段提醒",s.preferBreak),*silent=check("专注剩余超过 15 分钟时可无声提示",s.silentDuringLongFocus);
    auto *tts=check("喝水提醒朗读（当前音色）",s.waterTts),*bubble=check("显示桌宠喝水气泡和操作",s.waterBubble),*quiet=check("启用每日勿扰时段",s.quietEnabled);
    auto *begin=new QTimeEdit(s.quietStart),*end=new QTimeEdit(s.quietEnd);form->addRow("勿扰开始",begin);form->addRow("勿扰结束",end);
    auto *note=new QLabel("正在运行的阶段保持原计划时长，新设置从下一阶段生效。\n完成至少 15 分钟专注可按现有规则获得最多 4 点成长，每天最多奖励 4 轮；仍受 25 分钟奖励冷却和总成长日上限约束。跳过、重置、喝水确认及试听不增加关系进展。\n勿扰时段不产生喝水提醒；已完成的阶段可显示无声通知。锁屏/休眠后需手动继续。");note->setWordWrap(true);form->addRow(note);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);root->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()==QDialog::Accepted){s.focusSeconds=focus->value()*60;s.shortSeconds=shortBreak->value()*60;s.longSeconds=longBreak->value()*60;s.longEvery=every->value();s.autoNext=autoNext->isChecked();s.focusTts=focusTts->isChecked();s.waterEnabled=water->isChecked();s.waterMinutes=interval->value();s.useActivity=activity->isChecked();s.deferFocus=defer->isChecked();s.preferBreak=prefer->isChecked();s.silentDuringLongFocus=silent->isChecked();s.waterTts=tts->isChecked();s.waterBubble=bubble->isChecked();s.quietEnabled=quiet->isChecked();s.quietStart=begin->time();s.quietEnd=end->time();service_.setSettings(s);}
}
}
