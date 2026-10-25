#include "bond_gallery.h"
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QTextBrowser>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTabWidget>
#include <QSignalBlocker>
#include <algorithm>
namespace dragon {
namespace {
QString stageName(int n){return QStringList{"初识期","熟悉期","亲近期","深度羁绊期"}.value(n);}
QString stamp(const QString &value){const auto d=QDateTime::fromString(value,Qt::ISODateWithMs);return d.isValid()?d.toLocalTime().toString("yyyy-MM-dd HH:mm"):QString("尚未记录");}
}
BondGallery::BondGallery(Companion &companion,VoiceController &voice,QWidget *parent):QWidget(parent),companion_(companion),voice_(voice){
    setObjectName("bondGallery");auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,12,0,0);layout->setSpacing(8);
    setStyleSheet("QListWidget {padding:6px;} QListWidget::item {padding:6px 4px;}");
    overview_=new QLabel;overview_->setObjectName("bondOverview");overview_->setWordWrap(true);layout->addWidget(overview_);
    progress_=new QProgressBar;progress_->setObjectName("bondProgress");progress_->setRange(0,100);progress_->setFixedHeight(20);layout->addWidget(progress_);
    progress_->setStyleSheet("QProgressBar {border:1px solid #51405f;border-radius:6px;text-align:center;background:#251f30;} QProgressBar::chunk {background:#8565a8;border-radius:5px;}");
    auto *filters=new QHBoxLayout;category_=new QComboBox;category_->setObjectName("bondCategory");category_->addItem("全部类别");category_->addItems(DialogueEngine::categories());
    state_=new QComboBox;state_->setObjectName("bondState");state_->addItems({"全部","已解锁","未解锁","已收集","已解锁 · 待收集","最近解锁"});
    filters->addWidget(category_,1);filters->addWidget(state_,1);layout->addLayout(filters);
    auto *second=new QHBoxLayout;stage_=new QComboBox;stage_->setObjectName("bondStage");stage_->addItem("全部阶段");for(int i=0;i<4;++i)stage_->addItem(stageName(i));
    search_=new QLineEdit;search_->setObjectName("bondSearch");search_->setPlaceholderText("搜索已公开台词或 ID");second->addWidget(stage_);second->addWidget(search_,1);layout->addLayout(second);
    auto *split=new QSplitter(Qt::Vertical);auto *tabs=new QTabWidget;
    lines_=new QListWidget;lines_->setObjectName("bondLines");recent_=new QListWidget;recent_->setObjectName("bondRecent");
    lines_->setMinimumHeight(100);recent_->setMinimumHeight(100);tabs->addTab(lines_,"台词收藏");tabs->addTab(recent_,"最近解锁");split->addWidget(tabs);
    detail_=new QTextBrowser;detail_->setObjectName("bondDetail");detail_->setMinimumHeight(90);split->addWidget(detail_);split->setStretchFactor(0,3);split->setStretchFactor(1,2);layout->addWidget(split,1);
    auto *bottom=new QHBoxLayout;hideUnheard_=new QCheckBox("隐藏未收集台词内容");hideUnheard_->setObjectName("bondHideUnheard");bottom->addWidget(hideUnheard_,1);
    preview_=new QPushButton("试听所选台词");preview_->setObjectName("bondPreview");bottom->addWidget(preview_);layout->addLayout(bottom);
    audioStatus_=new QLabel("已解锁代表满足亲密度条件；实际展示后才计入收集。试听不增加亲密度。");audioStatus_->setObjectName("bondAudioStatus");audioStatus_->setWordWrap(true);layout->addWidget(audioStatus_);
    connect(category_,&QComboBox::currentIndexChanged,this,&BondGallery::refresh);connect(state_,&QComboBox::currentIndexChanged,this,&BondGallery::refresh);
    connect(stage_,&QComboBox::currentIndexChanged,this,&BondGallery::refresh);connect(search_,&QLineEdit::textChanged,this,&BondGallery::refresh);
    connect(lines_,&QListWidget::currentItemChanged,this,[this](QListWidgetItem *item){if(item)select(item->data(Qt::UserRole).toString());});
    connect(recent_,&QListWidget::currentItemChanged,this,[this](QListWidgetItem *item){if(item)select(item->data(Qt::UserRole).toString());});
    connect(hideUnheard_,&QCheckBox::toggled,this,[this](bool value){auto p=companion_.lifeSettings();p.hideUnheard=value;companion_.saveLifeSettings(p);});
    connect(preview_,&QPushButton::clicked,this,[this]{if(!voice_.previewCollectedLine(selected_))audioStatus_->setText("请先选择已收集台词，并等待当前语音或聊天结束。");});
    connect(&companion_,&Companion::memoryChanged,this,&BondGallery::refresh);
    connect(&companion_,&Companion::busyChanged,this,[this]{updatePreview();});
    connect(&voice_,&VoiceController::changed,this,[this]{updatePreview();audioStatus_->setText(voice_.status());});refresh();
}
QString BondGallery::visibleText(const QJsonObject &l) const{
    if(!l["unlocked"].toBool())return "？？？ · 达到 "+QString::number(l["required_affinity"].toInt())+"% 解锁";
    if(companion_.lifeSettings().hideUnheard && !l["discovered"].toBool())return "已解锁 · 等待在陪伴中遇见";
    return l["text"].toString();
}
void BondGallery::refresh(){
    rows_=companion_.memory().collection();const auto life=companion_.memory().life();int unlocked=0,collected=0;
    for(const auto &v:rows_){unlocked+=v.toObject()["unlocked"].toBool();collected+=v.toObject()["discovered"].toBool();}
    const int percent=life.growthPercent(),next=percent>=80?100:percent>=50?80:percent>=20?50:20;
    overview_->setText(QString("亲密度 %1% · %2\n已解锁 %3 / %4 · 已收集 %5 · 特殊事件 %6\n%7")
        .arg(percent).arg(life.stage()).arg(unlocked).arg(rows_.size()).arg(collected).arg(companion_.memory().unlockedEvents())
        .arg(percent>=100?QString("深度羁绊已满，陪伴仍会继续"):percent>=80?QString("距离满羁绊还有 %1%").arg(100-percent):QString("距离下一阶段还有 %1%（门槛 %2%）").arg(next-percent).arg(next)));
    progress_->setValue(percent);progress_->setFormat(QString("%1% · ").arg(percent)+life.stage());
    {QSignalBlocker block(hideUnheard_);hideUnheard_->setChecked(companion_.lifeSettings().hideUnheard);}
    QSignalBlocker a(lines_),b(recent_);lines_->clear();recent_->clear();
    QVector<QJsonObject> sorted;for(const auto &v:rows_)sorted.append(v.toObject());
    std::stable_sort(sorted.begin(),sorted.end(),[](const auto &x,const auto &y){return x["unlocked_at"].toString()>y["unlocked_at"].toString();});
    const auto week=QDateTime::currentDateTimeUtc().addDays(-7);bool selectedVisible=false;
    for(const auto &l:sorted){
        const bool unlocked=l["unlocked"].toBool(),collected=l["discovered"].toBool();
        const auto date=l["unlock_inferred"].toBool()?QDateTime():QDateTime::fromString(l["unlocked_at"].toString(),Qt::ISODateWithMs);
        if(category_->currentIndex()>0 && l["category"]!=category_->currentText())continue;
        if(stage_->currentIndex()>0 && l["relation_stage"].toInt()!=stage_->currentIndex()-1)continue;
        const int f=state_->currentIndex();if((f==1 && !unlocked)||(f==2 && unlocked)||(f==3 && !collected)||(f==4 && (!unlocked || collected))||(f==5 && (!date.isValid() || date<week)))continue;
        const auto text=visibleText(l),id=l["id"].toString(),query=search_->text().trimmed();
        if(!query.isEmpty() && !id.contains(query,Qt::CaseInsensitive) && !text.contains(query,Qt::CaseInsensitive))continue;
        const auto status=!unlocked?QString("未解锁"):collected?QString("已收集"):QString("已解锁 · 待收集");
        auto *item=new QListWidgetItem(status+" · "+l["category"].toString()+" · "+QString::number(l["required_affinity"].toInt())+"%\n"+text,lines_);item->setData(Qt::UserRole,id);
        if(id==selected_){lines_->setCurrentItem(item);selectedVisible=true;}
        if(unlocked && date.isValid() && date>=week){auto *recent=new QListWidgetItem(stamp(l["unlocked_at"].toString())+" · "+status+"\n"+text,recent_);recent->setData(Qt::UserRole,id);}
    }
    if(!selectedVisible)selected_=lines_->count()?lines_->item(0)->data(Qt::UserRole).toString():QString();
    select(selected_);
}
void BondGallery::select(const QString &id){
    selected_=id;detail_->clear();
    for(const auto &v:rows_){
        const auto l=v.toObject();if(l["id"]!=id)continue;
        const auto emotion=l.contains("emotion")?emotionName(Emotion(l["emotion"].toInt())):QString("随当前心情");
        QString text=visibleText(l)+"\n\nID："+id+"　类别："+l["category"].toString()+"\n";
        text+=QString("关系阶段：%1　解锁门槛：%2%　情绪：%3\n").arg(stageName(l["relation_stage"].toInt())).arg(l["required_affinity"].toInt()).arg(emotion);
        text+=QString("已解锁：%1　实际触发过：%2　记录次数：%3%4\n").arg(l["unlocked"].toBool()?"是":"否",l["discovered"].toBool()?"是":"否",l["legacy"].toBool()?"至少 ":"").arg(l["play_count"].toInt());
        text+="首次解锁："+(l["unlock_inferred"].toBool()?QString("旧存档已解锁；原时间未记录"):stamp(l["unlocked_at"].toString()))+"\n首次展示："+(l["legacy"].toBool()?QString("旧版曾触发，首次时间未记录"):stamp(l["discovered_at"].toString()));
        text+="\n首次听到："+stamp(l["first_heard_at"].toString());
        if(l["minClicks"].toInt()>0)text+=QString("\n实际触发还需累计单击 %1 次，以及匹配的情绪/时间。").arg(l["minClicks"].toInt());
        detail_->setPlainText(text);break;
    }updatePreview();
}
void BondGallery::updatePreview(){
    bool allowed=false;for(const auto &v:rows_){const auto l=v.toObject();if(l["id"]==selected_)allowed=l["unlocked"].toBool() && l["discovered"].toBool();}
    preview_->setEnabled(allowed && !voice_.engaged() && !companion_.busy());
}
}
