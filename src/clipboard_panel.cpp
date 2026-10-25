#include "clipboard_panel.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <ole2.h>
#endif

namespace dragon {
namespace {
bool commitClipboard(QClipboard *clipboard,const QString &expected){
#ifdef Q_OS_WIN
    // Qt uses OLE delayed rendering. Materialize our own text now so readers do
    // not depend on the pet's GUI thread servicing an IDataObject request.
    if(!clipboard->ownsClipboard() || FAILED(OleFlushClipboard()))return false;
    if(!OpenClipboard(nullptr))return false;
    QString actual;bool readable=false;
    if(auto handle=GetClipboardData(CF_UNICODETEXT))if(auto *text=static_cast<const wchar_t*>(GlobalLock(handle))){
        actual=QString::fromWCharArray(text);GlobalUnlock(handle);readable=true;
    }
    CloseClipboard();
    return readable && actual.replace("\r\n","\n")==QString(expected).replace("\r\n","\n");
#else
    return clipboard->text()==expected;
#endif
}
}
ClipboardPanel::ClipboardPanel(ClipboardHistory &history,PetWidget &pet)
    :QWidget(&pet,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint),
      history_(history),pet_(pet),animation_(this,"maximumHeight") {
    setObjectName("clipboardShelf");setWindowTitle("霜翎 · 剪贴板");
    setFixedWidth(110);setMinimumHeight(34);setMaximumHeight(34);
    setAttribute(Qt::WA_ShowWithoutActivating);setAttribute(Qt::WA_TranslucentBackground);
    setStyleSheet("QWidget{color:#e9e3f3;font-family:'Microsoft YaHei UI';font-size:12px;}"
        "QWidget#clipboardBody{background:#252130;border-radius:7px;}"
        "QPushButton{background:#3b3048;border:1px solid #665075;border-radius:5px;padding:5px;}"
        "QListWidget{background:#1d1927;}QListWidget::item{padding:6px;}"
        "QScrollArea{background:#252130;border:0;border-radius:7px;}"
        "QSpinBox{background:#30283d;}QLabel{background:transparent;}");
    expand_=new QPushButton("剪贴板 ▾",this);expand_->setObjectName("toggleClipboardShelf");
    expand_->setToolTip("展开本地剪贴板历史；双击条目或点击复制可取回内容");
    scroll_=new QScrollArea(this);scroll_->setWidgetResizable(true);scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    body_=new QWidget;body_->setObjectName("clipboardBody");body_->setMinimumHeight(340);
    scroll_->setWidget(body_);auto *layout=new QVBoxLayout(body_);layout->setContentsMargins(6,6,6,6);
    auto *monitor=new QCheckBox("记录安全的文字（默认关闭）");monitor->setObjectName("clipboardEnabled");
    monitor->setChecked(history_.enabled());layout->addWidget(monitor);
    auto *policy=new QLabel("仅本地分类；疑似凭据不留存。敏感检测有局限，复制隐私内容前请关闭监测。");
    policy->setWordWrap(true);layout->addWidget(policy);
    list_=new QListWidget;list_->setObjectName("clipboardEntries");list_->setWordWrap(true);list_->setMinimumHeight(100);layout->addWidget(list_,1);
    auto *actions=new QHBoxLayout;copy_=new QPushButton("复制");copy_->setObjectName("copyClipboardEntry");
    auto *pin=new QPushButton("固定 / 取消");auto *remove=new QPushButton("删除");auto *clear=new QPushButton("清空");
    for(auto *button:{copy_,pin,remove,clear})actions->addWidget(button);
    layout->addLayout(actions);
    status_=new QLabel;status_->setObjectName("clipboardStatus");status_->setWordWrap(true);status_->setTextFormat(Qt::PlainText);layout->addWidget(status_);
    auto *analyze=new QPushButton("用 AI 分析选中内容…");analyze->setObjectName("analyzeClipboard");layout->addWidget(analyze);
    auto *retention=new QSpinBox;retention->setRange(1,720);retention->setValue(history_.retentionHours());
    retention->setPrefix("未固定内容保留 ");retention->setSuffix(" 小时");layout->addWidget(retention);
    scroll_->hide();animation_.setDuration(180);animation_.setEasingCurve(QEasingCurve::InOutCubic);
    connect(expand_,&QPushButton::clicked,this,&ClipboardPanel::toggle);
    connect(&animation_,&QPropertyAnimation::valueChanged,this,[this]{resize(width(),maximumHeight());reposition();});
    connect(&animation_,&QPropertyAnimation::finished,this,[this]{if(!expanded_){scroll_->hide();setFixedWidth(110);}reposition();});
    connect(&history_,&ClipboardHistory::changed,this,[this,monitor]{
        QSignalBlocker block(monitor);monitor->setChecked(history_.enabled());scheduleRefresh();
    });
    connect(monitor,&QCheckBox::toggled,this,[this,monitor](bool value){
        try{history_.setEnabled(value);}catch(const std::exception &){QSignalBlocker block(monitor);monitor->setChecked(history_.enabled());status_->setText("设置保存失败，请重试。");}
    });
    connect(retention,&QSpinBox::valueChanged,this,[this](int hours){
        try{history_.setRetentionHours(hours);}catch(const std::exception &){status_->setText("清理设置保存失败，请重试。");}
    });
    // Return from the native clipboard notification before touching SQLite.
    captureTimer_.setSingleShot(true);captureTimer_.setInterval(30);
    connect(&captureTimer_,&QTimer::timeout,this,&ClipboardPanel::captureClipboard);
    connect(QGuiApplication::clipboard(),&QClipboard::dataChanged,this,[this]{
        if(!copying_ && history_.enabled())captureTimer_.start();
    });
    copyTimer_.setSingleShot(true);copyTimer_.setInterval(80);
    connect(&copyTimer_,&QTimer::timeout,this,&ClipboardPanel::tryCopy);
    connect(copy_,&QPushButton::clicked,this,&ClipboardPanel::copySelected);
    connect(list_,&QListWidget::itemDoubleClicked,this,[this]{copySelected();});
    connect(list_,&QListWidget::currentItemChanged,this,[this]{copy_->setEnabled(list_->currentItem() && !copying_);});
    connect(pin,&QPushButton::clicked,this,[this]{const auto entry=selected();if(entry.id)try{history_.pin(entry.id,!entry.pinned);}catch(const std::exception &){status_->setText("固定失败：最多固定 20 条，或记录暂时不可写。");}});
    connect(remove,&QPushButton::clicked,this,[this]{const auto entry=selected();if(entry.id)try{history_.remove(entry.id);}catch(const std::exception &){status_->setText("删除失败，请重试。");}});
    connect(clear,&QPushButton::clicked,this,[this]{
        if(QMessageBox::question(this,"清空剪贴板历史","删除全部历史（包括固定内容）？系统当前剪贴板不会更改。",QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes)
            try{history_.clear();}catch(const std::exception &){status_->setText("清空失败，请重试。");}
    });
    connect(analyze,&QPushButton::clicked,this,[this]{const auto entry=selected();if(entry.id && !ClipboardHistory::sensitive(entry.text))emit analysisRequested(entry.text,entry.kind);});
    auto *cleanup=new QTimer(this);cleanup->setInterval(60000);
    connect(cleanup,&QTimer::timeout,this,[this]{try{history_.prune();}catch(const std::exception &){status_->setText("过期记录清理失败，请稍后重试。");}});cleanup->start();
    pet_.installEventFilter(this);refresh();reposition();hide();
    connect(&pet_,&PetWidget::halfViewChanged,this,[this]{reposition();});
    connect(&pet_,&PetWidget::headPositionChanged,this,[this]{reposition();});
    connect(&pet_,&PetWidget::topmostChanged,this,[this](bool enabled){const bool visible=isVisible();setWindowFlag(Qt::WindowStaysOnTopHint,enabled);if(visible)show();});
}
int ClipboardPanel::expandedHeight() const {
    const auto *screen=pet_.screen();if(!screen)return 420;
    const int headY=pet_.mapToGlobal(pet_.characterGeometry().topLeft().toPoint()).y();
    return qBound(34,headY-screen->availableGeometry().top()-6,420);
}
void ClipboardPanel::toggle(){
    expanded_=!expanded_;animation_.stop();
    if(expanded_){setFixedWidth(340);if(dirty_)refresh();scroll_->show();}
    animation_.setStartValue(maximumHeight());animation_.setEndValue(expanded_?expandedHeight():34);animation_.start();
    expand_->setText(expanded_?"剪贴板 ▴":"剪贴板 ▾");reposition();if(pet_.isVisible())show();
}
void ClipboardPanel::scheduleRefresh(){
    dirty_=true;if(!expanded_ || refreshQueued_)return;refreshQueued_=true;
    QTimer::singleShot(0,this,[this]{refreshQueued_=false;if(expanded_ && dirty_)refresh();});
}
void ClipboardPanel::refresh(){
    const auto id=selected().id;
    try{entries_=history_.entries();}catch(const std::exception &){status_->setText("历史记录读取失败，请稍后重试。");return;}
    dirty_=false;QSignalBlocker block(list_);list_->clear();
    for(const auto &entry:entries_){
        auto *item=new QListWidgetItem((entry.pinned?"📌 ":"")+entry.kind+" · "+entry.text.left(120),list_);
        item->setData(Qt::UserRole,entry.id);item->setToolTip(entry.text);if(entry.id==id)list_->setCurrentItem(item);
    }
    if(list_->currentRow()<0 && list_->count())list_->setCurrentRow(0);
    copy_->setEnabled(list_->currentItem() && !copying_);
    status_->setText(entries_.isEmpty()?(history_.enabled()?"暂无记录。复制普通文字后会显示在这里；疑似敏感内容不记录。":"记录已关闭。启用后再复制普通文字，才会加入历史。"):
        (history_.enabled()?"选中一条后点击复制，或双击条目。":"记录已关闭；已有条目仍可复制。"));
}
ClipEntry ClipboardPanel::selected() const {
    const auto *item=list_->currentItem();if(item)for(const auto &entry:entries_)if(entry.id==item->data(Qt::UserRole).toLongLong())return entry;return {};
}
void ClipboardPanel::captureClipboard(){
    if(!history_.enabled() || copying_)return;
    const auto *data=QGuiApplication::clipboard()->mimeData();if(!data || !data->hasText() || data->hasImage())return;
    const auto text=data->text();
    try{if(!history_.capture(text))status_->setText("此内容疑似敏感、为空或过长，未加入历史。");}
    catch(const std::exception &){status_->setText("记录保存失败，请稍后重试。");}
}
void ClipboardPanel::copySelected(){
    const auto entry=selected();if(!entry.id){status_->setText("请先选择一条记录。");return;}
    captureTimer_.stop();copyTimer_.stop();pendingCopy_=entry.text;copyAttempts_=0;copying_=true;copy_->setEnabled(false);tryCopy();
}
void ClipboardPanel::tryCopy(){
    ++copyAttempts_;auto *clipboard=QGuiApplication::clipboard();clipboard->setText(pendingCopy_);
    if(commitClipboard(clipboard,pendingCopy_)){
        status_->setText("已复制，可以粘贴了。");
        // Also suppress queued self-notifications from the native clipboard.
        QTimer::singleShot(0,this,[this]{copying_=false;pendingCopy_.clear();copy_->setEnabled(list_->currentItem()!=nullptr);});
    }else if(copyAttempts_<5){status_->setText("剪贴板正被占用，正在重试…");copyTimer_.start();}
    else{copying_=false;pendingCopy_.clear();copy_->setEnabled(list_->currentItem()!=nullptr);status_->setText("复制失败：系统剪贴板被占用，请再次点击复制。");}
}
void ClipboardPanel::reposition(){
    const auto *screen=pet_.screen();if(!screen)return;const auto bounds=screen->availableGeometry();
    const auto character=pet_.characterGeometry();
    const QPoint head=pet_.mapToGlobal(QPoint(qRound(character.center().x()),qRound(character.top())));
    const int h=maximumHeight();
    const int x=qBound(bounds.left(),head.x()-width()/2,qMax(bounds.left(),bounds.right()-width()+1));
    const int y=qBound(bounds.top(),head.y()-6-h,qMax(bounds.top(),bounds.bottom()-h+1));
    expand_->setGeometry((width()-104)/2,h-31,104,28);
    scroll_->setGeometry(0,0,width(),qMax(0,h-34));
    move(x,y);
    if(expanded_ && animation_.state()!=QAbstractAnimation::Running && maximumHeight()!=expandedHeight()){
        setMaximumHeight(expandedHeight());resize(width(),maximumHeight());reposition();
    }
}
bool ClipboardPanel::eventFilter(QObject *object,QEvent *event){
    if(object==&pet_){
        if(event->type()==QEvent::Move || event->type()==QEvent::Resize || event->type()==QEvent::ScreenChangeInternal)reposition();
        if(event->type()==QEvent::Show){reposition();show();}
        if(event->type()==QEvent::Hide)hide();
    }
    return QWidget::eventFilter(object,event);
}
}
