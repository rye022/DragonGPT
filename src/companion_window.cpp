#include "companion_window.h"
#include "clipboard_panel.h"
#include "focus_page.h"
#include "bond_gallery.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QSaveFile>
#include <QSignalBlocker>
#include <algorithm>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QScreen>
#include <QGuiApplication>
#include <QTimer>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>
#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace dragon {
namespace {
QLabel *label(const QString &text, const char *name = "") {
    auto *l=new QLabel(text); l->setObjectName(name); l->setWordWrap(true);l->setTextFormat(Qt::PlainText); return l;
}
QPushButton *button(const QString &text, const char *name = "") {
    auto *b=new QPushButton(text); b->setObjectName(name); b->setCursor(Qt::PointingHandCursor); return b;
}
QString selectedVoiceKey(const VoicePreferences &p){return p.audioTts()?"audio:"+p.audioVoice:p.voice;}
void populateVoiceChoice(QComboBox *choice,const VoicePreferences &p){
    const QSignalBlocker blocker(choice);choice->clear();
    for(const auto &v:selectableLegacyVoices())choice->addItem(v.name,v.id);
    for(const auto &v:audioVoiceChoices(p.audioModel,p.region))choice->addItem("Audio TTS · "+v.name,"audio:"+v.id);
    if(choice->findData("audio:"+p.audioVoice)<0)choice->addItem("Audio TTS · "+p.audioVoice+"（自定义）","audio:"+p.audioVoice);
    choice->setCurrentIndex(choice->findData(selectedVoiceKey(p)));
}
}
CompanionWindow::CompanionWindow(Companion &companion,PetWidget &pet,QUrl speechEndpoint)
    : companion_(companion),pet_(pet),voice_(companion,this,std::move(speechEndpoint)) {
    setWindowTitle("DragonGPT · 霜翎的陪伴空间");
    resize(640,qMin(850,QGuiApplication::primaryScreen()->availableGeometry().height()-40)); setMinimumSize(560,620);
    setStyleSheet(R"(
        QWidget { background: #171520; color: #e9e3f3; font-family: 'Microsoft YaHei UI'; font-size: 13px; }
        QLabel { background: transparent; }
        QLabel#eyebrow { color: #afa0c9; font-size: 10px; letter-spacing: 3px; }
        QLabel#title { color: #f3edfc; font-size: 28px; font-weight: 600; }
        QLabel#subtitle { color: #b6a9c9; font-size: 12px; }
        QLabel#badge { color: #bce4d3; background: #263831; border-radius: 10px; padding: 5px 10px; }
        QLabel#notice { color: #b7a6cb; font-size: 11px; }
        QLabel#stat { background: #252130; border: 1px solid #393043; border-radius: 10px; padding: 12px; color: #cbbade; }
        QTabWidget::pane { border: none; }
        QTabBar::tab { color: #a697b9; padding: 12px 14px; border-bottom: 2px solid transparent; }
        QTabBar::tab:selected { color: #e4cef8; border-bottom: 2px solid #bd99dd; }
        QTabBar::tab:hover { color: #ffffff; }
        QTextBrowser, QListWidget { background: #1d1927; border: 1px solid #352b43; border-radius: 12px; padding: 12px; selection-background-color: #614677; }
        QListWidget::item { padding: 12px 4px; border-bottom: 1px solid #332a40; }
        QListWidget::item:selected { background: #42314f; border-radius: 6px; }
        QLineEdit { background: #292231; border: 1px solid #4a3b59; border-radius: 10px; padding: 12px; }
        QLineEdit:focus { border-color: #c6a3e5; }
        QPushButton { background: #302638; border: 1px solid #4b3b59; border-radius: 9px; padding: 10px 12px; }
        QPushButton:hover { background: #46314f; border-color: #a281ba; }
        QPushButton#primary { background: #c6a8df; color: #26182f; border: none; font-weight: 600; }
        QPushButton#primary:hover { background: #dec2f3; }
        QPushButton#danger { color: #ecb5c4; }
        QPushButton:disabled { color: #7c6d8e; background: #241f2c; border-color: #302837; }
        QScrollBar:vertical { background: transparent; width: 7px; margin: 0; }
        QScrollBar::handle:vertical { background: #51405f; border-radius: 3px; min-height: 25px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QSlider::groove:horizontal { background: #3b3048; height: 4px; border-radius: 2px; }
        QSlider::handle:horizontal { background: #d2b4eb; width: 16px; margin: -6px 0; border-radius: 8px; }
        QCheckBox { spacing: 10px; padding: 6px 0; }
    )");
    auto *root=new QVBoxLayout(this); root->setContentsMargins(24,24,24,18); root->setSpacing(13);
    auto *header=new QHBoxLayout;
    auto *text=new QVBoxLayout;
    auto *eyebrow=label("DRAGONGPT  /  2.0.1","eyebrow"); eyebrow->setWordWrap(false); text->addWidget(eyebrow);
    text->addWidget(label("霜翎","title"));
    text->addWidget(label("安静地，陪你把今天过好。","subtitle"));
    header->addLayout(text); header->addStretch();
    auto *portrait=new QLabel; QPixmap image(":/assets/character.png");
    portrait->setPixmap(image.copy(qRound(image.width()*0.27),qRound(image.height()*0.13),qRound(image.width()*0.47),qRound(image.height()*0.42)).scaled(84,84,Qt::KeepAspectRatioByExpanding,Qt::SmoothTransformation));
    portrait->setFixedSize(84,84); portrait->setAlignment(Qt::AlignCenter); header->addWidget(portrait);
    root->addLayout(header);
    auto *status=new QHBoxLayout;
    routeBadge_=label(companion_.chatSettings().onlineEnabled?"●  自动模式":"●  离线互动","badge"); routeBadge_->setWordWrap(false); status->addWidget(routeBadge_); status->addStretch();
    emotion_=label("当前状态 · 平静","subtitle"); emotion_->setWordWrap(false); status->addWidget(emotion_); root->addLayout(status);
    tabs_=new QTabWidget; tabs_->setObjectName("companionTabs"); tabs_->addTab(conversationPage(),"对话"); tabs_->addTab(memoryPage(),"记忆"); tabs_->addTab(voicePage(),"语音"); tabs_->addTab(connectionPage(),"连接"); tabs_->addTab(settingsPage(),"设置");
    tabs_->addTab(lifePage(),"陪伴");
    tabs_->addTab(new BondGallery(companion_,voice_),"羁绊图鉴");
    auto *openGallery=button("羁绊图鉴");openGallery->setObjectName("openBondGallery");status->insertWidget(1,openGallery);
    connect(openGallery,&QPushButton::clicked,this,[this]{tabs_->setCurrentIndex(6);});
    focusPage_=new FocusPage(companion_,pet_,voice_,tabs_);tabs_->addTab(focusPage_,"专注陪伴");
    companion_.setWellnessExecutor([page=QPointer<FocusPage>(focusPage_)](const LocalCommand &command)->Reply{
        if(!page)return {"专注陪伴页面暂不可用。"};auto &service=page->service();
        switch(command.intent){
        case LocalIntent::PomodoroStart:
            if(command.minutes<0 || command.minutes>180)return {"番茄钟时长需要在 1–180 分钟之间。"};
            if(service.paused())service.resume();else if(!service.running())service.start({},command.minutes*60);
            return {"番茄钟已接入现有专注陪伴，当前阶段："+service.phaseName()+"。"};
        case LocalIntent::PomodoroPause:service.pause();return {"已暂停现有番茄钟。"};
        case LocalIntent::PomodoroRemaining:return {service.running()?QString("%1还剩约 %2 分钟。%3").arg(service.phaseName()).arg((service.remainingMs()+59999)/60000).arg(service.paused()?"目前已暂停。":""):QString("当前没有进行中的番茄钟。")};
        case LocalIntent::Water:
            if(command.minutes<1 || command.minutes>1440)return {"喝水提醒时间需要在 1–1440 分钟之间。"};
            if(!service.settings().waterEnabled)return {"喝水提醒已关闭，请在「专注陪伴」设置中开启。"};
            service.requestWaterAfter(command.minutes);return {QString("已预约 %1 分钟后评估喝水提醒，沿用现有勿扰与专注保护设置。").arg(command.minutes)};
        default:return {};
        }
    });
    tabs_->addTab(toolsPage(),"工具");tabs_->setUsesScrollButtons(true);
    root->addWidget(tabs_,1);
    notice_=label(companion_.routeStatus(),"notice"); root->addWidget(notice_);
    connect(&companion_,&Companion::memoryChanged,this,&CompanionWindow::refresh);
    connect(&companion_.tools(),&LocalTools::changed,this,&CompanionWindow::refreshTools);
    connect(&companion_.tools(),&LocalTools::confirmationNeeded,this,[this](const TaskDraft &draft){QTimer::singleShot(0,this,[this,draft]{confirmTask(draft);});});
    connect(&companion_.emotions(),&EmotionEngine::changed,this,[this]{emotion_->setText("当前状态 · "+emotionName(companion_.emotions().state().emotion));emotion_->setToolTip(companion_.emotions().state().reason);});
    connect(&companion_,&Companion::partialReply,this,[this](const QString &text){streamingText_=text;renderHistory();companion_.traceLatency("text_visible");});
    connect(&companion_,&Companion::replyReady,this,[this](const QString &text,const QString &){streamingText_.clear();renderHistory();pet_.say(text);});
    connect(&companion_,&Companion::actionRequested,this,[this](PetAction action){pet_.previewAction(action,15000);});
    connect(&companion_,&Companion::proactiveReady,this,[this](const QString &text){if(companion_.proactiveOwner(this)){renderHistory();voice_.deliverProactive(text);}});
    connect(input_,&QLineEdit::textEdited,this,[this]{companion_.noteActivity();});
    connect(&pet_,&PetWidget::clicked,this,[this]{companion_.noteActivity();});
    connect(&pet_,&PetWidget::openCompanion,this,[this]{companion_.noteActivity();});
    bondNoticeTimer_.setSingleShot(true);
    connect(&companion_,&Companion::bondAdvanced,this,[this]{notice_->setText("羁绊提升！解锁了新的专属台词 ♡");bondNoticeTimer_.start(9000);});
    connect(&bondNoticeTimer_,&QTimer::timeout,this,[this]{
        if((focusPage_ && focusPage_->service().focusing()) || voice_.engaged() || companion_.busy() || pet_.bubbleActive() || pet_.protectedActionActive()){bondNoticeTimer_.start(2500);return;}
        pet_.say("羁绊提升！解锁了新的专属台词 ♡");
    });
    connect(focusPage_,&FocusPage::activityObserved,this,[this](qint64 idle){companion_.observeDesktopIdle(idle);refreshLifeView();});
    companion_.attachProactiveScheduler(this,[this]{
        bool blocked=(focusPage_ && (focusPage_->service().focusing() || focusPage_->service().waterPending())) || voice_.engaged() || pet_.protectedActionActive() || pet_.bubbleActive() || !pet_.isVisible() || QApplication::activeModalWidget()
            || !input_->text().isEmpty() || (isActiveWindow() && tabs_->currentIndex()==0 && input_->hasFocus());
        const bool protect=companion_.lifeSettings().focusProtection;
        if(protect && pet_.currentAction()==PetAction::Work)blocked=true;
#ifdef Q_OS_WIN
        const auto desktop=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
        if(!desktop)blocked=true;else{wchar_t name[64]{};DWORD size=0;
            if(!GetUserObjectInformationW(desktop,UOI_NAME,name,sizeof(name),&size) || QString::fromWCharArray(name).compare("Default",Qt::CaseInsensitive)!=0)blocked=true;
            CloseDesktop(desktop);
        }
        const HWND foreground=GetForegroundWindow();RECT bounds{};
        if(!foreground)blocked=true;
        else if(protect && foreground!=reinterpret_cast<HWND>(winId()) && foreground!=reinterpret_cast<HWND>(pet_.winId()) && foreground!=GetShellWindow() && GetWindowRect(foreground,&bounds)){
            MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
            if(GetMonitorInfo(MonitorFromWindow(foreground,MONITOR_DEFAULTTONEAREST),&monitor))
                blocked=blocked || (bounds.left<=monitor.rcMonitor.left && bounds.top<=monitor.rcMonitor.top && bounds.right>=monitor.rcMonitor.right && bounds.bottom>=monitor.rcMonitor.bottom);
        }
#endif
        refreshLifeView();return blocked;
    });
    connect(&companion_,&Companion::routeChanged,this,[this](const QString &status){
        notice_->setText(status);
        routeBadge_->setText(status.startsWith("DeepSeek")?"●  DeepSeek":status.startsWith("自动")?"●  自动模式":"●  本地 / 离线");
    });
    connect(&companion_,&Companion::busyChanged,this,[this](bool busy){
        send_->setEnabled(!busy); input_->setEnabled(!busy); cancel_->setEnabled(busy);
        if(!busy) streamingText_.clear();
        renderHistory();
    });
    connect(&companion_,&Companion::draftRestored,this,[this](const QString &text){input_->setText(text);});
    connect(&companion_,&Companion::operationFailed,this,[this](const QString &text){QMessageBox::warning(this,"操作失败",text);});
    connect(&voice_,&VoiceController::mouthChanged,&pet_,&PetWidget::setMouthOpen);
    connect(&voice_,&VoiceController::onlineVoiceReply,&pet_,&PetWidget::say);
    connect(&voice_,&VoiceController::speechRequested,&pet_,&PetWidget::say);
    connect(&voice_,&VoiceController::textOnlyBubble,&pet_,&PetWidget::say);
    connect(&pet_,&PetWidget::voiceRequested,&voice_,&VoiceController::beginCall);
    connect(&pet_,&PetWidget::actionClicked,this,[this](PetAction action){if(companion_.proactiveOwner(this))voice_.speakActionLine(action);});
    connect(&voice_,&VoiceController::interactionStarted,&pet_,&PetWidget::beginActionInteraction);
    connect(&voice_,&VoiceController::interactionFinished,&pet_,&PetWidget::finishActionInteraction);
    connect(&companion_,&Companion::usageChanged,this,[this]{
        const auto usage=companion_.lastUsage();
        const bool available=usage.value("input_tokens").isDouble() && usage.value("output_tokens").isDouble();
        QString text=!available?(usage.isEmpty()?"尚无成功 API 请求的用量记录":"上次响应未提供完整的 API 用量"):QString("上次成功请求 · 输入 %1 / 输出 %2 tokens")
            .arg(usage.value("input_tokens").toInteger()).arg(usage.value("output_tokens").toInteger());
        const auto totals=companion_.memory().usageSummary();text+=QString("\n本机累计 %1 次 · 输入 %2 / 输出 %3 tokens\n").arg(totals.value("requests").toInt()).arg(totals.value("input_tokens").toInteger()).arg(totals.value("output_tokens").toInteger());
        const auto limits=companion_.lastLimits();
        if(limits.isEmpty())text+="速率限制：服务尚未返回响应头\n";
        else{
            text+="上次响应的速率限制（可能已过时）：\n";
            for(auto it=limits.begin();it!=limits.end();++it)text+=it.key().mid(12)+": "+it.value().toString()+"\n";
        }
        text+="仅统计本应用收到的 usage，取消或断线请求可能未计入。\nAPI 费用与聊天产品订阅分开；请在提供商控制台查看账单。";
        usage_->setText(text);
    });
    emotion_->setText("当前状态 · "+emotionName(companion_.emotions().state().emotion));emotion_->setToolTip(companion_.emotions().state().reason);
    refresh();companion_.usageChanged();
    refreshTools();
    companion_.health().start();
    connect(&companion_.tools(),&LocalTools::notification,this,[this](const QString &text,PetAction action,bool passive){
        try{companion_.memory().appendNotice(text);renderHistory();}catch(const std::exception &){qWarning("Cannot save reminder notice");}
        if(pendingNotices_.size()<20)pendingNotices_.append({text,action,passive,QDateTime::currentDateTimeUtc()});
    });
    auto *noticeTimer=new QTimer(this);noticeTimer->setInterval(1000);connect(noticeTimer,&QTimer::timeout,this,[this]{
        if(pendingNotices_.isEmpty() || companion_.busy() || voice_.engaged() || pet_.actionInteractionActive() || QApplication::activeModalWidget() || !pet_.isVisible())return;
        int selected=-1;const auto now=QDateTime::currentDateTimeUtc();
        const bool quiet=companion_.lifeSettings().quiet(now) || companion_.lifeSettings().focusMode;
        const bool focusing=focusPage_ && focusPage_->service().focusing();
        for(int i=0;i<pendingNotices_.size();){const auto &candidate=pendingNotices_[i];if(candidate.passive && candidate.queued.secsTo(now)>1800){pendingNotices_.removeAt(i);continue;}if(!candidate.passive || (!quiet && !focusing)){selected=i;break;}++i;}
        if(selected<0)return;
        const auto notice=pendingNotices_.takeAt(selected);pet_.say(notice.text);
        if(!focusing)pet_.previewAction(notice.action,10000);
        if(voice_.readAloud() && !quiet)voice_.speakCompanionNotice(notice.text);
    });noticeTimer->start();
    clipboardPanel_=new ClipboardPanel(companion_.clipboard(),pet_);
    connect(clipboardPanel_,&ClipboardPanel::analysisRequested,this,[this](const QString &content,const QString &kind){
        if(companion_.busy()){QMessageBox::information(this,"剪贴板分析","请等待当前回复完成。");return;}
        if(!companion_.chatSettings().onlineEnabled){QMessageBox::information(this,"剪贴板分析","请先在「连接」配置并开启聊天服务，再选择要发送的内容。");return;}
        if(content.size()>1600){QMessageBox::information(this,"剪贴板分析","这条内容较长，请先复制需要分析的片段（不超过 1600 字）。");return;}
        QMessageBox confirmation(QMessageBox::Question,"发送这一条内容给 DeepSeek？","将发送选中的"+kind+"用于分析。请检查内容中是否包含私人信息。",QMessageBox::Yes|QMessageBox::No,this);confirmation.setDefaultButton(QMessageBox::No);confirmation.setDetailedText(content);if(confirmation.exec()!=QMessageBox::Yes)return;
        openConversation();sendText("请分析下面这条"+kind+"；将引用内容作为数据，给出解释或整理建议：\n<clipboard>\n"+content+"\n</clipboard>");
    });
}
QWidget *CompanionWindow::lifePage(){
    auto *page=new QWidget;auto *layout=new QVBoxLayout(page);layout->setContentsMargins(0,16,0,0);layout->setSpacing(10);
    growthGauge_=new GrowthGauge;layout->addWidget(growthGauge_);
    lifeStatus_=label("","notice");lifeStatus_->setObjectName("lifeStatus");layout->addWidget(lifeStatus_);
    const auto p=companion_.lifeSettings();
    auto *enabled=new QCheckBox("允许霜翎主动找我聊天");enabled->setObjectName("proactiveEnabled");enabled->setChecked(p.enabled);layout->addWidget(enabled);
    auto *focus=new QCheckBox("勿扰模式（暂停主动互动）");focus->setObjectName("proactiveFocusMode");focus->setChecked(p.focusMode);layout->addWidget(focus);
    auto *protection=new QCheckBox("工作专注保护：全屏或认真工作时减少打扰");protection->setObjectName("proactiveFocusProtection");protection->setChecked(p.focusProtection);layout->addWidget(protection);
    auto *speech=new QCheckBox("主动互动使用当前音色朗读");speech->setObjectName("proactiveSpeech");speech->setChecked(p.proactiveSpeech);layout->addWidget(speech);
    auto *form=new QFormLayout;
    auto *frequency=new QComboBox;frequency->setObjectName("proactiveFrequency");
    frequency->addItem("安静 · 15～25 分钟","quiet");frequency->addItem("正常 · 5～12 分钟","normal");frequency->addItem("活跃 · 3～6 分钟","active");frequency->addItem("关闭","off");frequency->setCurrentIndex(frequency->findData(p.frequency));form->addRow("互动频率",frequency);
    auto *start=new QSpinBox;start->setObjectName("quietStart");start->setRange(0,23);start->setSuffix(" 时");start->setValue(p.quietStart);form->addRow("免打扰开始",start);
    auto *end=new QSpinBox;end->setObjectName("quietEnd");end->setRange(0,23);end->setSuffix(" 时");end->setValue(p.quietEnd);form->addRow("免打扰结束（相同为关闭）",end);layout->addLayout(form);
    auto *suggest=new QCheckBox("从我明确说出的偏好和近况中整理待确认记忆");suggest->setObjectName("memorySuggestionsEnabled");suggest->setChecked(p.memorySuggestions);layout->addWidget(suggest);
    auto *row=new QHBoxLayout;auto *save=button("保存陪伴设置","primary");save->setObjectName("saveLifeSettings");row->addWidget(save);
    auto *quiet=button("今天安静陪我");quiet->setObjectName("quietToday");row->addWidget(quiet);
    auto *resume=button("恢复主动聊天");resume->setObjectName("resumeProactive");row->addWidget(resume);layout->addLayout(row);
    connect(save,&QPushButton::clicked,this,[this,enabled,focus,frequency,protection,speech,start,end,suggest]{
        try{auto next=companion_.lifeSettings();next.enabled=enabled->isChecked();next.focusMode=focus->isChecked();next.frequency=frequency->currentData().toString();next.focusProtection=protection->isChecked();next.proactiveSpeech=speech->isChecked();next.quietStart=start->value();next.quietEnd=end->value();next.memorySuggestions=suggest->isChecked();companion_.saveLifeSettings(next);notice_->setText("陪伴设置已保存");}catch(const std::exception &e){handleError(e);}
    });
    connect(quiet,&QPushButton::clicked,this,[this]{try{auto next=companion_.lifeSettings();next.pausedUntil=QDateTime(QDate::currentDate().addDays(1),QTime(0,0)).toUTC();companion_.saveLifeSettings(next);}catch(const std::exception &e){handleError(e);}});
    connect(resume,&QPushButton::clicked,this,[this,enabled,focus,frequency]{try{auto next=companion_.lifeSettings();next.pausedUntil={};next.enabled=true;next.focusMode=false;if(next.frequency=="off")next.frequency="normal";companion_.saveLifeSettings(next);enabled->setChecked(true);focus->setChecked(false);frequency->setCurrentIndex(frequency->findData(next.frequency));}catch(const std::exception &e){handleError(e);}});
    auto *preview=button("试听她会怎样开场");preview->setObjectName("previewProactive");layout->addWidget(preview);
    connect(preview,&QPushButton::clicked,this,[this]{
        if(voice_.previewProactiveLine())notice_->setText("开场试听 · 使用当前音色 · 不计次数、收藏或亲密度；云端音色会调用 TTS");
        else notice_->setText("当前正在聊天或播放语音，请结束后再试听");
    });
    layout->addWidget(label("主动互动与单击共用气泡、动作和所选音色。默认在本地选择台词；云端音色朗读会调用 TTS。语音失败仍保留气泡。\n刚单击或聊天后重新计时；被暂缓的互动不积压。未回复也可以继续安静陪伴。全屏保护可单独关闭。","notice"));
    layout->addWidget(label("待确认记忆","subtitle"));
    suggestions_=new QListWidget;suggestions_->setObjectName("memorySuggestions");suggestions_->setMinimumHeight(100);layout->addWidget(suggestions_);
    auto *review=new QHBoxLayout;auto *accept=button("确认保存");accept->setObjectName("acceptMemorySuggestion");auto *dismiss=button("忽略这条");dismiss->setObjectName("dismissMemorySuggestion");review->addWidget(accept);review->addWidget(dismiss);layout->addLayout(review);
    auto resolve=[this](bool accept){const auto *item=suggestions_->currentItem();if(!item)return;try{companion_.resolveSuggestion(item->data(Qt::UserRole).toLongLong(),accept);}catch(const std::exception &e){handleError(e);}};
    connect(accept,&QPushButton::clicked,this,[resolve]{resolve(true);});connect(dismiss,&QPushButton::clicked,this,[resolve]{resolve(false);});
    layout->addWidget(label("建议只有确认后才会进入长期记忆和聊天背景。确认后可在「记忆」页修改、导出或删除。","notice"));
    auto *scroll=new QScrollArea;scroll->setFrameShape(QFrame::NoFrame);scroll->setWidgetResizable(true);scroll->setWidget(page);return scroll;
}
void CompanionWindow::openConversation(){
    tabs_->setCurrentIndex(0);
    showNormal();
#ifdef Q_OS_WIN
    // A launcher using STARTF_USESHOWWINDOW/SW_HIDE can consume the first
    // native ShowWindow call while QWidget already considers itself visible.
    // A real hide/show cycle resynchronizes the native and Qt visibility.
    if(!IsWindowVisible(reinterpret_cast<HWND>(winId()))){hide();showNormal();}
#endif
    bool visible=false;
    for(const auto *screen:QGuiApplication::screens())
        if(screen->availableGeometry().contains(frameGeometry().center()))visible=true;
    if(!visible){const auto area=pet_.screen()->availableGeometry();move(area.center()-QPoint(width()/2,height()/2));}
    raise();activateWindow();
    QTimer::singleShot(0,this,[this]{input_->setFocus(Qt::OtherFocusReason);});
}
QWidget *CompanionWindow::conversationPage() {
    auto *page=new QWidget; auto *layout=new QVBoxLayout(page); layout->setContentsMargins(0,16,0,0); layout->setSpacing(12);
    auto *read=new QCheckBox("打字聊天时语音回复");read->setObjectName("typedReplyVoice");read->setChecked(voice_.readAloud());
    read->setToolTip("朗读与屏幕相同的完整回复，自动保存；不打开麦克风。关闭后打字聊天只显示文字。");layout->addWidget(read);
    connect(read,&QCheckBox::toggled,this,[this,read](bool enabled){try{voice_.setReadAloud(enabled);}catch(const std::exception &e){QSignalBlocker block(read);read->setChecked(voice_.readAloud());handleError(e);}});
    connect(&voice_,&VoiceController::changed,read,[this,read]{QSignalBlocker block(read);read->setChecked(voice_.readAloud());});
    auto *voiceState=label("","notice");voiceState->setObjectName("chatVoiceStatus");layout->addWidget(voiceState);
    auto updateVoiceState=[this,voiceState]{
        const QString name=voice_.voiceLabel();
        voiceState->setText("朗读音色："+name+" · "+voice_.status());
        voiceState->setVisible(voice_.readAloud());
    };
    connect(&voice_,&VoiceController::changed,voiceState,updateVoiceState);updateVoiceState();
    chat_=new QTextBrowser; chat_->setObjectName("chatHistory"); chat_->setOpenExternalLinks(false); chat_->setOpenLinks(false);
    chat_->setAccessibleName("对话记录"); layout->addWidget(chat_,1);
    auto *quick=new QHBoxLayout;
    for(const auto &text:{QString("你好，霜翎"),QString("我有点累"),QString("我完成了")}) {
        auto *b=button(text); quick->addWidget(b); connect(b,&QPushButton::clicked,this,[this,text]{sendText(text);});
    }
    layout->addLayout(quick);
    auto *row=new QHBoxLayout;
    input_=new QLineEdit; input_->setObjectName("chatInput"); input_->setMaxLength(2000);
    input_->setPlaceholderText("和她说句话…"); input_->setAccessibleName("消息");
    send_=button("发送","primary"); send_->setObjectName("sendMessage");
    cancel_=button("停止"); cancel_->setEnabled(false); cancel_->setObjectName("cancelReply");
    row->addWidget(input_,1); row->addWidget(send_); row->addWidget(cancel_); layout->addLayout(row);
    auto sendInput=[this]{sendText(input_->text());};
    connect(send_,&QPushButton::clicked,this,sendInput); connect(input_,&QLineEdit::returnPressed,this,sendInput);
    connect(cancel_,&QPushButton::clicked,&companion_,&Companion::cancelReply);
    layout->addWidget(label("在「连接」配置 DeepSeek 和代理。失败时回退离线规则。\n「叫我小林」「记住：…」始终在本地处理。","notice"));
    return page;
}
QWidget *CompanionWindow::memoryPage() {
    auto *page=new QWidget; auto *layout=new QVBoxLayout(page); layout->setContentsMargins(0,16,0,0); layout->setSpacing(12);
    auto *stats=new QHBoxLayout; relationship_=label("","stat"); memoryCount_=label("","stat");
    stats->addWidget(relationship_,1); stats->addWidget(memoryCount_,1); layout->addLayout(stats);
    layout->addWidget(label("只长期保存你明确要求记住的内容。","subtitle"));
    semanticStatus_=label(companion_.semanticStatus(),"notice");semanticStatus_->setObjectName("semanticMemoryStatus");layout->addWidget(semanticStatus_);
    memorySearch_=new QLineEdit;memorySearch_->setObjectName("memorySearch");memorySearch_->setPlaceholderText("筛选内容、标签、来源和记忆层次");layout->addWidget(memorySearch_);
    connect(memorySearch_,&QLineEdit::textChanged,this,&CompanionWindow::refreshMemories);
    auto *automatic=new QCheckBox("自动整理待确认记忆（确认后才长期保存）");automatic->setObjectName("automaticMemory");automatic->setChecked(companion_.lifeSettings().memorySuggestions);layout->addWidget(automatic);
    connect(automatic,&QCheckBox::toggled,this,[this,automatic](bool enabled){try{auto p=companion_.lifeSettings();p.memorySuggestions=enabled;companion_.saveLifeSettings(p);}catch(const std::exception &e){QSignalBlocker block(automatic);automatic->setChecked(companion_.lifeSettings().memorySuggestions);handleError(e);}});
    connect(&companion_,&Companion::memoryChanged,automatic,[this,automatic]{QSignalBlocker block(automatic);automatic->setChecked(companion_.lifeSettings().memorySuggestions);});
    auto *followups=new QCheckBox("允许主动跟进学习话题（概括话题，不朗读记忆原文）");followups->setObjectName("memoryFollowups");followups->setChecked(companion_.lifeSettings().memoryFollowups);layout->addWidget(followups);
    connect(followups,&QCheckBox::toggled,this,[this,followups](bool enabled){try{auto p=companion_.lifeSettings();p.memoryFollowups=enabled;companion_.saveLifeSettings(p);}catch(const std::exception &e){QSignalBlocker block(followups);followups->setChecked(companion_.lifeSettings().memoryFollowups);handleError(e);}});
    connect(&companion_,&Companion::memoryChanged,followups,[this,followups]{QSignalBlocker block(followups);followups->setChecked(companion_.lifeSettings().memoryFollowups);});
    layout->addWidget(label("标签含「用户资料」或「交流偏好」的记录每轮在线聊天都会提供，可编辑或删除；其他记忆按话题检索。","notice"));
    memories_=new QListWidget; memories_->setWordWrap(true); memories_->setAccessibleName("长期记忆"); layout->addWidget(memories_,1);
    newMemory_=new QLineEdit; newMemory_->setMaxLength(500); newMemory_->setPlaceholderText("希望霜翎记住的事…");
    layout->addWidget(newMemory_);
    auto *row=new QHBoxLayout; auto *add=button("保存记忆","primary"); auto *edit=button("编辑选中"); auto *remove=button("删除选中");
    row->addWidget(add); row->addWidget(edit); row->addWidget(remove); layout->addLayout(row);
    connect(add,&QPushButton::clicked,this,[this]{
        if(newMemory_->text().trimmed().isEmpty()) return;
        try {companion_.remember(newMemory_->text()); newMemory_->clear();} catch(const std::exception &e){handleError(e);}
    });
    connect(remove,&QPushButton::clicked,this,[this]{
        auto *item=memories_->currentItem(); if(!item) return;
        try {companion_.forget(item->data(Qt::UserRole).toLongLong());} catch(const std::exception &e){handleError(e);}
    });
    connect(edit,&QPushButton::clicked,this,&CompanionWindow::editMemory);
    connect(memories_,&QListWidget::itemDoubleClicked,this,[this]{editMemory();});
    auto *files=new QHBoxLayout;auto *exportButton=button("导出记忆");auto *importButton=button("导入记忆");files->addWidget(exportButton);files->addWidget(importButton);layout->addLayout(files);
    auto *maintenance=new QHBoxLayout;auto *backup=button("备份数据库");backup->setObjectName("backupDatabase");auto *clear=button("清空长期记忆","danger");clear->setObjectName("clearMemories");maintenance->addWidget(backup);maintenance->addWidget(clear);layout->addLayout(maintenance);
    connect(backup,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getSaveFileName(this,"备份本地数据库","DragonGPT-backup.sqlite","SQLite (*.sqlite)");if(path.isEmpty())return;try{companion_.memory().backupTo(path);notice_->setText("数据库已备份；恢复步骤见升级说明");}catch(const std::exception &e){handleError(e);}});
    connect(clear,&QPushButton::clicked,this,[this]{if(QMessageBox::question(this,"清空长期记忆","删除所有长期记忆和向量索引？亲密度、情绪、已解锁台词和聊天记录会保留。",QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;try{companion_.memory().clearMemories();refresh();}catch(const std::exception &e){handleError(e);}});
    connect(exportButton,&QPushButton::clicked,this,[this]{
        const auto path=QFileDialog::getSaveFileName(this,"导出档案与长期记忆","DragonGPT-memories.json","JSON (*.json)");if(path.isEmpty())return;
        try{QSaveFile file(path);const auto data=companion_.memory().exportMemories();if(!file.open(QIODevice::WriteOnly) || file.write(data)!=data.size() || !file.commit())throw std::runtime_error("Cannot export memories");notice_->setText("已导出档案与长期记忆（不包含密钥和聊天）");}catch(const std::exception &e){handleError(e);}
    });
    connect(importButton,&QPushButton::clicked,this,[this]{
        const auto path=QFileDialog::getOpenFileName(this,"导入长期记忆",{},"JSON (*.json)");if(path.isEmpty())return;
        try{QFile file(path);if(!file.open(QIODevice::ReadOnly) || file.size()>2*1024*1024)throw std::runtime_error("Invalid or oversized memory file");
            const auto count=companion_.memory().importMemories(file.readAll());refresh();notice_->setText(QString("已导入 %1 条新记忆，重复内容已跳过").arg(count));}catch(const std::exception &e){handleError(e);}
    });
    return page;
}
void CompanionWindow::editMemory(){
    const auto *item=memories_->currentItem();if(!item)return;const auto id=item->data(Qt::UserRole).toLongLong();
    const auto all=companion_.memory().memories();auto found=std::find_if(all.begin(),all.end(),[id](const Memory &m){return m.id==id;});if(found==all.end())return;
    const auto memory=*found;QDialog dialog(this);dialog.setWindowTitle("编辑长期记忆");dialog.setMinimumWidth(430);
    auto *form=new QFormLayout(&dialog);auto *content=new QLineEdit(memory.content);content->setMaxLength(500);form->addRow("内容",content);
    auto *tags=new QLineEdit(memory.tags);tags->setMaxLength(120);tags->setPlaceholderText("例如：项目，偏好");form->addRow("标签",tags);
    auto *layer=new QComboBox;layer->addItem("长期语义","semantic");layer->addItem("情景经历","episodic");layer->addItem("关系经历","relationship");layer->setCurrentIndex(layer->findData(memory.layer));form->addRow("记忆层次",layer);
    auto *proactive=new QCheckBox("允许在主动对话中提及");proactive->setChecked(memory.proactiveAllowed);form->addRow(proactive);
    auto *importance=new QDoubleSpinBox;importance->setRange(0,1);importance->setSingleStep(0.1);importance->setValue(memory.importance);form->addRow("重要性",importance);
    auto *expires=new QCheckBox("到期后自动移除");expires->setChecked(memory.expiresAt.isValid());form->addRow(expires);
    auto *date=new QDateTimeEdit(memory.expiresAt.isValid()?memory.expiresAt.toLocalTime():QDateTime::currentDateTime().addDays(7));date->setCalendarPopup(true);date->setDisplayFormat("yyyy-MM-dd HH:mm");date->setEnabled(expires->isChecked());form->addRow("过期时间",date);connect(expires,&QCheckBox::toggled,date,&QDateTimeEdit::setEnabled);
    auto *actions=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);form->addRow(actions);connect(actions,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(actions,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()==QDialog::Accepted){try{companion_.memory().updateMemory(id,content->text(),tags->text(),importance->value(),expires->isChecked()?date->dateTime():QDateTime{});companion_.memory().setMemoryMetadata(id,layer->currentData().toString(),memory.source,proactive->isChecked());refresh();}catch(const std::exception &e){handleError(e);}}
}
QWidget *CompanionWindow::voicePage(){
    auto *page=new QWidget;auto *layout=new QVBoxLayout(page);layout->setContentsMargins(0,16,0,0);layout->setSpacing(14);
    layout->addWidget(label("听你说，也说给你听。","title"));
    auto *state=label(voice_.status(),"stat");state->setObjectName("voiceStatus");layout->addWidget(state);
    layout->addWidget(label(voice_.capabilities(),"subtitle"));
    auto *selectedVoice=label("当前朗读："+voice_.voiceLabel(),"subtitle");selectedVoice->setObjectName("selectedSpeechVoice");layout->addWidget(selectedVoice);
    connect(&voice_,&VoiceController::changed,selectedVoice,[this,selectedVoice]{selectedVoice->setText("当前朗读："+voice_.voiceLabel());});
    auto *expression=label("","notice");expression->setObjectName("speechExpressionStatus");layout->addWidget(expression);
    auto refreshExpression=[this,expression]{const auto p=voice_.speechExpression();expression->setText(voice_.voicePreferences().audioTts()?QString("语调 · %1 · %2 · 语速 %3%\n随当前情绪调整，试听、点击和聊天均使用所选原声。").arg(p.style,emotionName(p.emotion)).arg(qRound(p.rate*100)):QString("情绪语调用于 Audio TTS；系统中文与离线苏瑶保留原朗读方式。"));};
    connect(&voice_,&VoiceController::changed,expression,refreshExpression);connect(&companion_.emotions(),&EmotionEngine::changed,expression,refreshExpression);refreshExpression();
    layout->addWidget(label("选择后立即生效，试听、点击和聊天共用所选音色。已移除此前返回 403 的旧音色；保留苏瑶和当前模型可用的女声。","notice"));
    auto *voiceChoice=new QComboBox;voiceChoice->setObjectName("voiceChoice");
    populateVoiceChoice(voiceChoice,voice_.voicePreferences());
    connect(&voice_,&VoiceController::changed,voiceChoice,[this,voiceChoice]{populateVoiceChoice(voiceChoice,voice_.voicePreferences());});
    auto *voiceRow=new QHBoxLayout;auto *preview=button("试听所选音色");voiceRow->addWidget(voiceChoice,1);voiceRow->addWidget(preview);layout->addLayout(voiceRow);
    auto *credential=label(voice_.voiceCredentialStatus(),"notice");credential->setObjectName("voiceCredentialStatus");layout->addWidget(credential);
    layout->addWidget(label(voice_.offlineStatus(),"notice"));
    auto *fast=new QCheckBox("允许备用音色：失败或等待 4 秒后改用系统中文");fast->setObjectName("fastVoice");fast->setToolTip("关闭时，点击和聊天都保留所选音色；原声不可用会显示原因，不会自动变成其他声音。");fast->setChecked(voice_.voicePreferences().fastResponse);layout->addWidget(fast);
    connect(fast,&QCheckBox::toggled,this,[this,fast](bool value){try{auto p=voice_.voicePreferences();p.fastResponse=value;voice_.saveVoicePreferences(p,{});}catch(const std::exception &e){QSignalBlocker block(fast);fast->setChecked(voice_.voicePreferences().fastResponse);handleError(e);}});
    connect(&voice_,&VoiceController::changed,fast,[this,fast]{QSignalBlocker block(fast);fast->setChecked(voice_.voicePreferences().fastResponse);});
    auto *configure=button("百炼密钥与网络设置 ▾");configure->setCheckable(true);layout->addWidget(configure);
    auto *details=new QWidget;auto *detailsLayout=new QVBoxLayout(details);detailsLayout->setContentsMargins(0,0,0,0);detailsLayout->setSpacing(10);layout->addWidget(details);details->hide();
    connect(configure,&QPushButton::toggled,details,&QWidget::setVisible);
    auto *key=new QLineEdit;key->setObjectName("dashscopeKey");key->setEchoMode(QLineEdit::Password);key->setMaxLength(4096);key->setPlaceholderText("百炼 API key（需与所选地域一致）");detailsLayout->addWidget(key);
    auto *regionChoice=new QComboBox;regionChoice->setObjectName("voiceRegion");regionChoice->addItem("百炼地域：北京","beijing");regionChoice->addItem("百炼地域：新加坡","singapore");regionChoice->setCurrentIndex(regionChoice->findData(voice_.voicePreferences().region));detailsLayout->addWidget(regionChoice);
    auto *apiFamily=new QComboBox;apiFamily->setObjectName("ttsApiFamily");apiFamily->addItem("苏瑶／系统中文","qwen-tts");apiFamily->addItem("Audio TTS（当前模型女声）","qwen-audio-tts");apiFamily->setCurrentIndex(apiFamily->findData(voice_.voicePreferences().apiFamily));detailsLayout->addWidget(apiFamily);
    auto *audioTransport=new QComboBox;audioTransport->setObjectName("audioTtsTransport");audioTransport->addItem("Audio TTS：HTTP 业务空间入口","http");audioTransport->addItem("Audio TTS：WebSocket 业务空间入口","websocket-workspace");audioTransport->addItem("Audio TTS：WebSocket 官方公共入口","websocket-public");audioTransport->setCurrentIndex(audioTransport->findData(voice_.voicePreferences().audioTransport));detailsLayout->addWidget(audioTransport);
    audioTransport->setToolTip("业务空间入口使用填写的 Workspace ID；公共 WebSocket 入口使用该 Key 的默认业务空间，不会自动重试或跨入口发送合成请求。");
    detailsLayout->addWidget(label("Audio TTS 使用当前业务空间、模型和地域。新增女声已用北京区 3.1 Flash 实际合成验证；音色 ID 可在此手动配置。龙安元妃是高傲角色音，是否有傲娇感可试听判断。","notice"));
    auto *workspace=new QLineEdit(voice_.voicePreferences().audioWorkspace);workspace->setObjectName("ttsWorkspace");workspace->setMaxLength(63);workspace->setPlaceholderText("Workspace ID：百炼控制台右上角业务空间中复制");detailsLayout->addWidget(workspace);
    auto *audioModel=new QLineEdit(voice_.voicePreferences().audioModel);audioModel->setObjectName("audioTtsModel");audioModel->setMaxLength(128);audioModel->setPlaceholderText("Audio TTS 模型 ID");detailsLayout->addWidget(audioModel);
    auto *audioVoice=new QLineEdit(voice_.voicePreferences().audioVoice);audioVoice->setObjectName("audioTtsVoice");audioVoice->setMaxLength(128);audioVoice->setPlaceholderText("Audio TTS voice ID（不能填 Chelsie）");detailsLayout->addWidget(audioVoice);
    auto *audioUrl=new QLineEdit;audioUrl->setObjectName("audioTtsUrl");audioUrl->setReadOnly(true);audioUrl->setPlaceholderText("填写 Workspace ID 后显示完整官方 URL");detailsLayout->addWidget(audioUrl);
    auto refreshAudioFields=[this,apiFamily,audioTransport,workspace,audioModel,audioVoice,audioUrl,regionChoice,voiceChoice]{
        const bool active=apiFamily->currentData().toString()=="qwen-audio-tts";
        audioTransport->setEnabled(active);workspace->setEnabled(active);audioModel->setEnabled(active);audioVoice->setEnabled(active);audioUrl->setEnabled(active);
        auto selected=voice_.voicePreferences();selected.apiFamily=active?"qwen-audio-tts":"qwen-tts";
        const QSignalBlocker block(voiceChoice);voiceChoice->setCurrentIndex(voiceChoice->findData(selectedVoiceKey(selected)));
        auto p=voice_.voicePreferences();p.audioWorkspace=workspace->text().trimmed();p.region=regionChoice->currentData().toString();p.audioTransport=audioTransport->currentData().toString();audioUrl->setText((p.audioTransport=="http"?p.audioEndpoint():p.audioWebSocketEndpoint()).toString());
    };
    connect(apiFamily,&QComboBox::currentIndexChanged,page,refreshAudioFields);connect(audioTransport,&QComboBox::currentIndexChanged,page,refreshAudioFields);connect(workspace,&QLineEdit::textChanged,page,refreshAudioFields);connect(regionChoice,&QComboBox::currentIndexChanged,page,refreshAudioFields);refreshAudioFields();
    auto *proxy=new QCheckBox("百炼语音使用系统代理");proxy->setChecked(voice_.voicePreferences().systemProxy);detailsLayout->addWidget(proxy);
    auto *preferOffline=new QCheckBox("优先离线模型（新句子合成可能较慢）");preferOffline->setObjectName("preferOfflineVoice");preferOffline->setChecked(voice_.voicePreferences().preferOffline);detailsLayout->addWidget(preferOffline);
    auto *cacheAudio=new QCheckBox("保存合成语音供离线重播（最多 200 MB）");cacheAudio->setChecked(voice_.voicePreferences().cacheAudio);detailsLayout->addWidget(cacheAudio);
    auto *keyButtons=new QHBoxLayout;auto *saveVoice=button("保存语音配置");saveVoice->setObjectName("saveVoice");auto *removeVoice=button("删除百炼密钥");keyButtons->addWidget(saveVoice);keyButtons->addWidget(removeVoice);detailsLayout->addLayout(keyButtons);
    connect(voiceChoice,&QComboBox::currentIndexChanged,page,[this,voiceChoice,apiFamily,audioVoice,credential,refreshAudioFields]{
        try{auto preferences=voice_.voicePreferences();const auto selected=voiceChoice->currentData().toString();if(selected.isEmpty())return;preferences.apiFamily=selected.startsWith("audio:")?"qwen-audio-tts":"qwen-tts";if(preferences.audioTts())preferences.audioVoice=selected.mid(6);else preferences.voice=selected;voice_.saveVoicePreferences(preferences,{});audioVoice->setText(preferences.audioVoice);credential->setText(voice_.voiceCredentialStatus());}catch(const std::exception &e){handleError(e);}
        const QSignalBlocker block(apiFamily);apiFamily->setCurrentIndex(apiFamily->findData(voice_.voicePreferences().apiFamily));refreshAudioFields();
    });
    connect(saveVoice,&QPushButton::clicked,page,[this,voiceChoice,regionChoice,apiFamily,audioTransport,workspace,audioModel,audioVoice,proxy,key,credential,preferOffline,cacheAudio]{
        auto secret=key->text().toUtf8();
        try{auto preferences=voice_.voicePreferences();const auto selected=voiceChoice->currentData().toString();if(!selected.isEmpty() && !selected.startsWith("audio:"))preferences.voice=selected;preferences.region=regionChoice->currentData().toString();preferences.apiFamily=apiFamily->currentData().toString();preferences.audioTransport=audioTransport->currentData().toString();preferences.audioWorkspace=workspace->text().trimmed();preferences.audioModel=audioModel->text().trimmed();preferences.audioVoice=audioVoice->text().trimmed();preferences.systemProxy=proxy->isChecked();preferences.preferOffline=preferOffline->isChecked();preferences.cacheAudio=cacheAudio->isChecked();voice_.saveVoicePreferences(preferences,secret);key->clear();credential->setText(voice_.voiceCredentialStatus());}catch(const std::exception &e){handleError(e);}secret.fill('\0');
    });
    connect(removeVoice,&QPushButton::clicked,page,[this,key,credential]{try{voice_.removeVoiceKey();key->clear();credential->setText(voice_.voiceCredentialStatus());}catch(const std::exception &e){handleError(e);}});
    auto *clearCache=button("清空缓存语音");detailsLayout->addWidget(clearCache);connect(clearCache,&QPushButton::clicked,&voice_,&VoiceController::clearVoiceCache);
    detailsLayout->addWidget(label("百炼合成会发送朗读文本到阿里云并按服务计费。点击和聊天共用所选音色；固定点击短句可能已有缓存，新聊天需要在线合成。苏瑶还可使用本机模型。关闭备用音色时，原声失败会显示原因并停止朗读。缓存音频可随时清空。","notice"));
    auto *caption=label("通话字幕会显示在这里。","stat");caption->setMinimumHeight(90);layout->addWidget(caption,1);
    connect(&voice_,&VoiceController::caption,caption,[caption](const QString &text){caption->setText(text.right(350));});
    auto *row=new QHBoxLayout;auto *start=button("开始语音","primary");start->setObjectName("startVoice");auto *stop=button("结束通话");stop->setObjectName("endVoice");row->addWidget(start);row->addWidget(stop);layout->addLayout(row);
    auto *controls=new QHBoxLayout;auto *interrupt=button("打断 / 继续聆听");controls->addWidget(interrupt);layout->addLayout(controls);
    auto *mute=new QCheckBox("麦克风静音");layout->addWidget(mute);auto *read=new QCheckBox("打字聊天时语音回复（自动保存，不打开麦克风）");read->setObjectName("voicePageReadAloud");read->setChecked(voice_.readAloud());layout->addWidget(read);
    connect(start,&QPushButton::clicked,&voice_,&VoiceController::beginCall);connect(stop,&QPushButton::clicked,&voice_,&VoiceController::endCall);connect(interrupt,&QPushButton::clicked,&voice_,&VoiceController::interrupt);connect(preview,&QPushButton::clicked,&voice_,&VoiceController::previewVoice);
    connect(mute,&QCheckBox::toggled,&voice_,&VoiceController::setMuted);
    connect(read,&QCheckBox::toggled,this,[this,read](bool enabled){try{voice_.setReadAloud(enabled);}catch(const std::exception &e){QSignalBlocker block(read);read->setChecked(voice_.readAloud());handleError(e);}});
    connect(&voice_,&VoiceController::changed,read,[this,read]{QSignalBlocker block(read);read->setChecked(voice_.readAloud());});
    connect(&voice_,&VoiceController::changed,page,[this,state,start,stop,mute]{state->setText(voice_.status());start->setEnabled(!voice_.active());stop->setEnabled(voice_.active());if(!voice_.active()){QSignalBlocker block(mute);mute->setChecked(false);}});
    stop->setEnabled(false);
    layout->addWidget(label("单击使用当前动作的专属文字和语音，每组 3 句轮换，打断上一句。18 句动作语音已内置苏瑶，无需等待模型。其他音色使用各自的缓存或云端；原声失败会显示原因，是否使用备用音色由开关决定。双击打开聊天；只有「开始语音」才开启麦克风。","notice"));
    auto *scroll=new QScrollArea;scroll->setFrameShape(QFrame::NoFrame);scroll->setWidgetResizable(true);scroll->setWidget(page);return scroll;
}
QWidget *CompanionWindow::connectionPage() {
    auto *page=new QWidget; auto *layout=new QVBoxLayout(page); layout->setContentsMargins(0,16,8,0); layout->setSpacing(12);
    const auto settings=companion_.chatSettings();
    auto *enabled=new QCheckBox("自动选择 DeepSeek / 离线"); enabled->setObjectName("onlineEnabled"); enabled->setChecked(settings.onlineEnabled); layout->addWidget(enabled);
    layout->addWidget(label("启用后，发送消息会将本轮内容、最近 20 条聊天、称呼、最多 8 条用户资料、4 条交流偏好与 3 条相关记忆发送给 DeepSeek。离线回复仅为本地预设规则。","notice"));
    layout->addWidget(label("代理 / VPN 路由","subtitle"));
    auto *mode=new QComboBox; mode->setObjectName("proxyMode"); mode->addItems({"系统代理（无代理时离线）","HTTP 代理","SOCKS5 代理","VPN TUN（我确认已开启）","直接连接（DeepSeek，无需 VPN）"});
    mode->setCurrentIndex(int(settings.proxyMode)); layout->addWidget(mode);
    auto *proxyRow=new QHBoxLayout; auto *host=new QLineEdit(settings.proxyHost); host->setObjectName("proxyHost"); host->setMaxLength(255); host->setPlaceholderText("按代理客户端填写地址");
    auto *port=new QSpinBox; port->setObjectName("proxyPort"); port->setRange(1,65535); port->setValue(qBound(1,settings.proxyPort,65535));
    proxyRow->addWidget(host,1); proxyRow->addWidget(port); layout->addLayout(proxyRow);
    auto updateProxy=[=](int index){host->setEnabled(index==1 || index==2);port->setEnabled(index==1 || index==2);};
    updateProxy(mode->currentIndex()); connect(mode,&QComboBox::currentIndexChanged,page,updateProxy);
    layout->addWidget(label("TUN 模式使用操作系统路由，程序无法识别 VPN 是否真的开启。HTTP / SOCKS5 模式连接失败时不会改为直连。","notice"));
    layout->addWidget(label("DeepSeek 模型 ID","subtitle"));
    auto *model=new QLineEdit(settings.model); model->setObjectName("modelId"); model->setMaxLength(128); model->setPlaceholderText("填写你的 API 账户可用的文本模型 ID"); layout->addWidget(model);
    layout->addWidget(label("API key（仅在本机输入）","subtitle"));
    auto *key=new QLineEdit; key->setObjectName("apiKey"); key->setMaxLength(4096); key->setEchoMode(QLineEdit::Password); key->setPlaceholderText("留空保留现有密钥"); layout->addWidget(key);
    auto *keyStatus=label(companion_.credentialStatus(),"notice"); layout->addWidget(keyStatus);
    layout->addWidget(label("密钥用 Windows 当前用户加密保存。DEEPSEEK_API_KEY 环境变量存在时优先使用；密钥不写入聊天或普通配置文件。","notice"));
    auto *actions=new QHBoxLayout; auto *save=button("保存连接配置","primary"); save->setObjectName("saveConnection"); auto *remove=button("删除已保存密钥");
    actions->addWidget(save); actions->addWidget(remove);
    connect(save,&QPushButton::clicked,this,[=,this]{
        try {
            ChatSettings value; value.onlineEnabled=enabled->isChecked(); value.proxyMode=static_cast<ProxyMode>(mode->currentIndex());
            value.proxyHost=host->text().trimmed(); value.proxyPort=port->value(); value.model=model->text().trimmed();voice_.endCall();
            if(!key->text().trimmed().isEmpty()) {companion_.saveApiKey(key->text().trimmed().toUtf8());key->clear();}
            companion_.saveChatSettings(value); keyStatus->setText(companion_.credentialStatus()+" · 配置已保存");
        }catch(const std::exception &e){handleError(e);}
    });
    connect(remove,&QPushButton::clicked,this,[=,this]{try{voice_.endCall();companion_.removeApiKey();key->clear();keyStatus->setText(companion_.credentialStatus());}catch(const std::exception &e){handleError(e);}});
    const auto total=companion_.memory().usageSummary();
    usage_=label(QString("本机累计 %1 次 API 记录 · 输入 %2 / 输出 %3 tokens\n速率限制：尚未返回\nChatGPT 订阅额度：无授权数据源").arg(total.value("requests").toInt()).arg(total.value("input_tokens").toInteger()).arg(total.value("output_tokens").toInteger()),"stat"); layout->addWidget(usage_);
    layout->addStretch(); auto *scroll=new QScrollArea; scroll->setFrameShape(QFrame::NoFrame);scroll->setWidgetResizable(true);scroll->setWidget(page);
    auto *outer=new QWidget; auto *outerLayout=new QVBoxLayout(outer);outerLayout->setContentsMargins(0,0,0,0);outerLayout->addWidget(scroll,1);outerLayout->addLayout(actions);return outer;
}
QWidget *CompanionWindow::settingsPage() {
    auto *page=new QWidget; auto *layout=new QVBoxLayout(page); layout->setContentsMargins(0,16,0,0); layout->setSpacing(12);
    layout->addWidget(label("她如何称呼你","subtitle"));
    auto *row=new QHBoxLayout; nickname_=new QLineEdit; nickname_->setMaxLength(30); nickname_->setPlaceholderText("你的称呼（可留空）");
    auto *save=button("保存"); row->addWidget(nickname_,1); row->addWidget(save); layout->addLayout(row);
    connect(save,&QPushButton::clicked,this,[this]{try{companion_.rename(nickname_->text());}catch(const std::exception &e){handleError(e);}});
    layout->addWidget(label("桌面角色大小","subtitle"));
    auto *slider=new QSlider(Qt::Horizontal); slider->setRange(260,680); slider->setValue(480); layout->addWidget(slider);
    connect(slider,&QSlider::valueChanged,&pet_,&PetWidget::setPetHeight);
    connect(&pet_,&PetWidget::sizeChanged,slider,&QSlider::setValue);
    auto *motion=new QCheckBox("启用情境动作、自然过渡和呼吸动画"); motion->setChecked(true); layout->addWidget(motion);
    connect(motion,&QCheckBox::toggled,&pet_,&PetWidget::setMotionEnabled);
    connect(&pet_,&PetWidget::motionChanged,motion,&QCheckBox::setChecked);
    auto *actionRow=new QHBoxLayout;
    auto *actionChoice=new QComboBox;actionChoice->setObjectName("actionChoice");
    for(int i=0;i<PetActions::Count;++i)actionChoice->addItem(actionName(static_cast<PetAction>(i)),i);
    auto *preview=button("预览动作"),*automatic=button("恢复自动");
    actionRow->addWidget(actionChoice,1);actionRow->addWidget(preview);actionRow->addWidget(automatic);layout->addLayout(actionRow);
    connect(preview,&QPushButton::clicked,this,[this,actionChoice]{pet_.setMotionEnabled(true);pet_.previewAction(static_cast<PetAction>(actionChoice->currentData().toInt()));});
    connect(automatic,&QPushButton::clicked,&pet_,&PetWidget::resumeAutomaticActions);
    auto *random=new QCheckBox("六个动作随时间随机轮换（每次 20–40 秒）");random->setObjectName("randomActions");random->setChecked(pet_.randomActionsEnabled());layout->addWidget(random);
    connect(random,&QCheckBox::toggled,&pet_,&PetWidget::setRandomActionsEnabled);connect(&pet_,&PetWidget::randomActionsChanged,random,&QCheckBox::setChecked);
    layout->addWidget(label("六个动作每轮随机出现一次。单击按当前看到的动作说专属台词，每组 3 句；文字与语音一致，播完再继续自动轮换。工作时说工作，抱枕时说休息。聊天和语音通话可临时接管动作。","notice"));
    auto *top=new QCheckBox("角色始终置顶"); top->setChecked(true); layout->addWidget(top);
    auto *half=new QCheckBox("半身镜头（仅裁切同一角色）");layout->addWidget(half);connect(half,&QCheckBox::toggled,&pet_,&PetWidget::setHalfView);connect(&pet_,&PetWidget::halfViewChanged,half,&QCheckBox::setChecked);
    layout->addWidget(label("外观已锁定为九宫格参考的 Q 版白龙娘；互动不会切换成年形态、服装或身体比例。","notice"));
    connect(top,&QCheckBox::toggled,&pet_,&PetWidget::setTopmost);
    connect(&pet_,&PetWidget::topmostChanged,top,&QCheckBox::setChecked);
    layout->addWidget(label("开发进度","subtitle"));
    layout->addWidget(label("文字与记忆 / 编辑与导出     已实现\n本地中文语音 / 离线模型与缓存 已实现\n原生 2D 动画与语音口型       已实现\nLive2D 模型                需外部绑定模型","stat"));
    layout->addWidget(label("API 用量与最近返回的速率限制可在「连接」查看。ChatGPT 订阅额度尚无授权数据源。","notice"));
    auto *clear=button("清空本地档案、记忆和聊天","danger"); layout->addWidget(clear);
    connect(clear,&QPushButton::clicked,this,[this]{
        if(QMessageBox::question(this,"清空本地数据","这会删除称呼、所有记忆、聊天记录、熟悉度和缓存语音，无法撤销。继续吗？",QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes) return;
        try {voice_.clearVoiceCache();companion_.clearAll(); input_->clear(); pet_.say("重新认识一下吧。我是霜翎。");}catch(const std::exception &e){handleError(e);}
    });
    layout->addStretch();
    auto *scroll=new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame); scroll->setWidgetResizable(true); scroll->setWidget(page);
    return scroll;
}
void CompanionWindow::renderHistory() {
    QString html="<html><body style='font-family:Microsoft YaHei UI;color:#e9e3f3;font-size:13px;'>";
    const auto history=companion_.memory().recentMessages(40);
    if(history.isEmpty()) html+="<p style='color:#cbb5e3;font-size:17px;'>你来了。</p><p style='color:#baaacf;line-height:1.6;'>我是霜翎。从一句问候开始吧。<br>重要的事，你可以让我记住。</p>";
    for(const auto &m:history) {
        const bool user=m.role=="user";
        const auto text=m.content.toHtmlEscaped().replace("\n","<br>");
        html+=QString("<p style='color:%1;font-size:11px;margin-top:16px;'>%2</p><p style='color:%3;line-height:1.65;'>%4</p>")
            .arg(user?"#b6dbc9":"#cdb2e7",user?"你":m.source=="local-brain"?"霜翎 · 本地小脑":m.source=="local-reminder"?"霜翎 · 本地提醒":m.source=="proactive-local"?"霜翎 · 主动开场 · 本地":m.source=="deepseek"?"霜翎 · DeepSeek":m.source=="openai"?"霜翎 · OpenAI（历史）":m.source=="openai-voice"?"霜翎 · OpenAI 语音（历史）":"霜翎 · 本地离线",user?"#d9eee4":"#eee4fa",text);
    }
    if(companion_.busy()) {
        const auto partial=streamingText_.isEmpty()?QString("正在等待回复…"):streamingText_;
        html+="<p style='color:#b6dbc9;'>你</p><p>"+companion_.pendingText().toHtmlEscaped()+"</p><p style='color:#cdb2e7;'>霜翎 · DeepSeek · 生成中</p><p>"+partial.toHtmlEscaped().replace("\n","<br>")+"</p>";
    }
    chat_->setHtml(html+"</body></html>"); chat_->verticalScrollBar()->setValue(chat_->verticalScrollBar()->maximum());
}
void CompanionWindow::refresh() {
    renderHistory();refreshMemories();
    const auto list=companion_.memory().memories();
    relationship_->setText(QString::number(companion_.memory().life().growthPercent())+"%");
    relationship_->setToolTip(companion_.memory().relationship()+"\n"+companion_.memory().life().growthHint());
    memoryCount_->setText(QString::number(list.size())+" 条长期记忆");
    if(!nickname_->hasFocus()) nickname_->setText(companion_.memory().nickname());
    refreshLifeView();
    if(suggestions_){
        const auto *selected=suggestions_->currentItem();const qint64 selectedId=selected?selected->data(Qt::UserRole).toLongLong():0;
        suggestions_->clear();for(const auto &m:companion_.memory().suggestions()){auto *item=new QListWidgetItem(m.content,suggestions_);item->setData(Qt::UserRole,m.id);if(m.id==selectedId)suggestions_->setCurrentItem(item);}
        if(suggestions_->currentRow()<0 && suggestions_->count()>0)suggestions_->setCurrentRow(0);
    }
}
void CompanionWindow::refreshMemories(){
    if(!memories_)return;const auto *selected=memories_->currentItem();const qint64 selectedId=selected?selected->data(Qt::UserRole).toLongLong():0;
    memories_->clear();
    const auto list=companion_.memory().memories();
    for(const auto &m:list) {
        const auto layer=m.layer=="episodic"?QString("情景经历"):m.layer=="relationship"?QString("关系经历"):QString("长期语义");
        const auto source=m.source=="confirmed-conversation"?QString("对话确认"):m.source=="daily-summary"?QString("每日总结"):m.source=="relationship-event"?QString("关系成长"):m.source=="imported"?QString("导入"):m.source=="legacy"?QString("旧版记录，来源未记录"):m.source=="manual"?QString("手动添加"):m.source.left(60);
        const auto search=memorySearch_?memorySearch_->text().trimmed():QString{};
        if(!search.isEmpty() && !(m.content+" "+m.tags+" "+m.layer+" "+m.source+" "+layer+" "+source).contains(search,Qt::CaseInsensitive))continue;
        auto *item=new QListWidgetItem(m.content+"\n"+layer+" · "+source+(m.tags.isEmpty()?QString{}:" · "+m.tags),memories_); item->setData(Qt::UserRole,m.id);
        if(m.id==selectedId)memories_->setCurrentItem(item);
        item->setToolTip(QString("创建：%1\n重要性：%2\n过期：%3\n层次：%4\n来源：%5\n更新：%6\n主动引用：%7").arg(m.createdAt.toLocalTime().toString("yyyy-MM-dd HH:mm")).arg(m.importance).arg(m.expiresAt.isValid()?m.expiresAt.toLocalTime().toString("yyyy-MM-dd HH:mm"):"永不过期").arg(m.layer,m.source,m.updatedAt.toLocalTime().toString("yyyy-MM-dd HH:mm"),m.proactiveAllowed?"允许":"不提及"));
    }
    if(semanticStatus_)semanticStatus_->setText(companion_.semanticStatus());
}
void CompanionWindow::refreshLifeView(){
    if(growthGauge_){
        const auto s=companion_.memory().life();
        growthGauge_->setState(s);
        const auto p=companion_.lifeSettings();
        const QString status=!p.enabled || p.frequency=="off"?"主动互动已关闭":p.focusMode?"勿扰模式":p.pausedUntil>QDateTime::currentDateTimeUtc()?"今天安静陪伴":p.quiet(QDateTime::currentDateTime())?"免打扰时段":QString("下次计划 %1（遇到忙碌会暂缓）").arg(companion_.nextProactiveAt().toLocalTime().toString("HH:mm:ss"));
        const auto mood=s.moodUntil>QDateTime::currentDateTimeUtc()?s.mood:Emotion::Calm;
        lifeStatus_->setText("陪伴心情 · "+emotionName(mood)+" · "+status+"\n情绪会随对话变化并自然回到平静，不代表对你的评价。");
    }
}
void CompanionWindow::sendText(const QString &text) {
    if(text.trimmed().isEmpty() || companion_.busy()) return;
    voice_.endCall();
    const auto original=input_->text(); input_->clear();
    try {
        if(!companion_.sendMessage(text))input_->setText(original);
    }catch(const std::exception &e){input_->setText(original);handleError(e);}
}
void CompanionWindow::handleError(const std::exception &error) {
    QMessageBox::warning(this,"本地数据操作失败",QString::fromUtf8(error.what()));
}
}
