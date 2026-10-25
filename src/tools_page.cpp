#include "companion_window.h"
#include "clipboard_panel.h"
#include "file_organizer.h"
#include "focus_page.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextBrowser>
#include <QFileDialog>
#include <QSaveFile>
#include <QTreeWidget>
#include <QHeaderView>
#include <QFileInfo>
#include <QDir>
#include <QVBoxLayout>
namespace dragon {
namespace {
class HealthChart final:public QWidget{
public:
    QVector<HealthSample> samples;
    explicit HealthChart(QWidget *parent=nullptr):QWidget(parent){setMinimumHeight(180);setAccessibleName("最近十分钟 CPU、内存和 GPU 负载趋势");}
protected:
    void paintEvent(QPaintEvent *) override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);const QRectF plot(32,12,width()-45,height()-45);p.fillRect(plot,QColor("#1d1927"));
        p.setPen(QColor("#574765"));for(int i=0;i<=4;++i){const double y=plot.bottom()-plot.height()*i/4;p.drawLine(QPointF(plot.left(),y),QPointF(plot.right(),y));p.drawText(QPointF(1,y+4),QString::number(i*25));}
        const QColor colors[]={QColor("#c7a6e5"),QColor("#a7d8c4"),QColor("#e7bc90")};
        for(int line=0;line<3;++line){p.setPen(QPen(colors[line],2));QPointF previous;bool valid=false;
            for(int i=0;i<samples.size();++i){const double value=line==0?samples[i].cpu:line==1?samples[i].memory:samples[i].gpu;if(value<0){valid=false;continue;}const QPointF point(plot.left()+plot.width()*i/qMax(1,samples.size()-1),plot.bottom()-plot.height()*value/100);if(valid && samples[i-1].time.secsTo(samples[i].time)<=15)p.drawLine(previous,point);previous=point;valid=true;}
            p.drawText(QPointF(plot.left()+line*100,plot.bottom()+24),QStringList{"CPU","内存","GPU"}[line]);
        }
    }
};
}
void CompanionWindow::confirmTask(TaskDraft draft){
    QDialog dialog(this);dialog.setWindowTitle("确认待办与提醒时间");dialog.setMinimumWidth(430);auto *form=new QFormLayout(&dialog);
    if(!draft.question.isEmpty()){auto *question=new QLabel(draft.question);question->setWordWrap(true);form->addRow(question);}
    auto *title=new QLineEdit(draft.title);title->setMaxLength(200);form->addRow("事项",title);
    auto *hasDue=new QCheckBox("在指定时间提醒");hasDue->setChecked(draft.due.isValid() || draft.needsConfirmation);form->addRow(hasDue);
    auto *due=new QDateTimeEdit(draft.due.isValid()?draft.due.toLocalTime():QDateTime::currentDateTime().addSecs(3600));due->setCalendarPopup(true);due->setDisplayFormat("yyyy-MM-dd HH:mm");due->setEnabled(hasDue->isChecked());form->addRow("提醒时间",due);connect(hasDue,&QCheckBox::toggled,due,&QDateTimeEdit::setEnabled);
    auto *priority=new QComboBox;priority->addItems({"低","普通","高"});priority->setCurrentIndex(draft.priority);form->addRow("优先级",priority);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);form->addRow(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{if(title->text().trimmed().isEmpty() || (hasDue->isChecked() && due->dateTime()<=QDateTime::currentDateTime())){QMessageBox::information(&dialog,"请核对","请填写事项，并选择未来的提醒时间。");return;}dialog.accept();});connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;
    draft.title=title->text();draft.due=hasDue->isChecked()?due->dateTime():QDateTime{};draft.priority=priority->currentIndex();draft.needsConfirmation=false;
    try{companion_.tools().addTask(draft);}catch(const std::exception &e){handleError(e);}
}
QWidget *CompanionWindow::toolsPage(){
    auto *page=new QWidget;auto *root=new QVBoxLayout(page);root->setContentsMargins(0,10,0,0);
    auto *sections=new QTabWidget;sections->setObjectName("toolSections");root->addWidget(sections);
    auto *tasks=new QWidget;auto *layout=new QVBoxLayout(tasks);sections->addTab(tasks,"待办 / 专注");
    auto *description=new QLabel("提醒保存在本机，重启后继续检查；过期任务只提醒一次。程序退出期间无法弹出提醒，重新运行时补查。");description->setWordWrap(true);layout->addWidget(description);
    todos_=new QListWidget;todos_->setObjectName("todoList");todos_->setWordWrap(true);layout->addWidget(todos_,1);
    auto *row=new QHBoxLayout;auto *add=new QPushButton("新增待办");add->setObjectName("addTodo");auto *complete=new QPushButton("完成 / 重新打开");auto *remove=new QPushButton("删除选中");row->addWidget(add);row->addWidget(complete);row->addWidget(remove);layout->addLayout(row);
    connect(add,&QPushButton::clicked,this,[this]{confirmTask({});});
    connect(complete,&QPushButton::clicked,this,[this]{const auto *item=todos_->currentItem();if(!item)return;try{companion_.tools().completeTask(item->data(Qt::UserRole).toLongLong(),!item->data(Qt::UserRole+1).toBool());}catch(const std::exception &e){handleError(e);}});
    connect(remove,&QPushButton::clicked,this,[this]{const auto *item=todos_->currentItem();if(!item)return;try{companion_.tools().removeTask(item->data(Qt::UserRole).toLongLong());}catch(const std::exception &e){handleError(e);}});
    auto *natural=new QLineEdit;natural->setObjectName("naturalTodo");natural->setPlaceholderText("明天下午三点提醒我复习高数");layout->addWidget(natural);
    connect(natural,&QLineEdit::returnPressed,this,[this,natural]{const auto text=natural->text();if(text.trimmed().isEmpty())return;auto draft=TaskParser::parse(text);if(draft.needsConfirmation)confirmTask(draft);else try{companion_.tools().addTask(draft);}catch(const std::exception &e){handleError(e);}natural->clear();});
    auto *focusLink=new QPushButton("打开已有的专注陪伴与喝水提醒");layout->addWidget(focusLink);
    connect(focusLink,&QPushButton::clicked,this,[this]{tabs_->setCurrentWidget(focusPage_);});
    auto *health=new QWidget;auto *healthLayout=new QVBoxLayout(health);sections->addTab(health,"电脑健康");
    auto *healthText=new QLabel(companion_.health().overview());healthText->setWordWrap(true);healthText->setObjectName("healthOverview");healthLayout->addWidget(healthText);
    auto *chart=new HealthChart;chart->setObjectName("healthTrend");healthLayout->addWidget(chart,1);
    auto *healthNote=new QLabel("每 5 秒在后台采样，趋势最多保留 10 分钟。持续高负载 2 分钟才提醒，至少间隔 30 分钟；不会凭单次占用率判断故障。GPU 及温度依硬件和驱动支持。");healthNote->setWordWrap(true);healthLayout->addWidget(healthNote);
    connect(&companion_.health(),&HealthMonitor::changed,this,[this,healthText,chart]{healthText->setText(companion_.health().overview());chart->samples=companion_.health().trend();chart->update();});
    connect(&companion_.tools(),&LocalTools::openHealthRequested,this,[this,page,sections,health]{tabs_->setCurrentWidget(page);sections->setCurrentWidget(health);showNormal();raise();});
    auto *summary=new QWidget;auto *summaryLayout=new QVBoxLayout(summary);sections->addTab(summary,"每日总结");
    auto *report=new QTextBrowser;report->setObjectName("dailySummary");report->setPlainText(companion_.tools().dailySummary());summaryLayout->addWidget(report,1);
    auto *summaryButtons=new QHBoxLayout;auto *refresh=new QPushButton("刷新");auto *exportReport=new QPushButton("导出文本");auto *remember=new QPushButton("保存为情景记忆");summaryButtons->addWidget(refresh);summaryButtons->addWidget(exportReport);summaryButtons->addWidget(remember);summaryLayout->addLayout(summaryButtons);
    connect(refresh,&QPushButton::clicked,this,[this,report]{report->setPlainText(companion_.tools().dailySummary());});
    connect(&companion_.tools(),&LocalTools::changed,this,[this,report]{report->setPlainText(companion_.tools().dailySummary());});
    connect(exportReport,&QPushButton::clicked,this,[this,report]{const auto path=QFileDialog::getSaveFileName(this,"导出每日总结","DragonGPT-"+QDate::currentDate().toString(Qt::ISODate)+".txt","Text (*.txt)");if(path.isEmpty())return;QSaveFile file(path);const auto bytes=report->toPlainText().toUtf8();if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit())QMessageBox::warning(this,"导出失败","无法保存总结。");});
    connect(remember,&QPushButton::clicked,this,[this]{try{const auto content=companion_.tools().dailySummary().left(500);const auto id=companion_.memory().remember(content);companion_.memory().updateMemory(id,content,"每日总结",.6);companion_.memory().setMemoryMetadata(id,"episodic","daily-summary",false);companion_.memoryChanged();}catch(const std::exception &e){handleError(e);}});
    auto *aiSummary=new QPushButton("请 DeepSeek 帮我回顾这份总结…");summaryLayout->addWidget(aiSummary);
    connect(aiSummary,&QPushButton::clicked,this,[this]{if(companion_.busy() || !companion_.chatSettings().onlineEnabled){QMessageBox::information(this,"总结回顾","请先启用聊天服务，并等待当前回复完成。");return;}const auto text=companion_.tools().dailySummary();QMessageBox question(QMessageBox::Question,"发送本地总结？","将这份统计摘要发送给当前 DeepSeek 服务，生成文字回顾？",QMessageBox::Yes|QMessageBox::No,this);question.setDefaultButton(QMessageBox::No);question.setDetailedText(text);if(question.exec()==QMessageBox::Yes){openConversation();sendText("请根据这份真实统计，温和简洁地回顾今天，不推断未记录的活动：\n"+text);}});
    auto *files=new QWidget;auto *fileLayout=new QVBoxLayout(files);sections->addTab(files,"文件整理");
    auto *organizer=new FileOrganizer(companion_.dataDirectory()+"/file-operations",files);organizer->setObjectName("fileOrganizer");
    auto *fileStatus=new QLabel("选择自己的下载或文档目录，先扫描预览，再确认移动。仅整理图片、文档、压缩包、音视频；不递归，不删除，不覆盖同名文件。");fileStatus->setWordWrap(true);fileLayout->addWidget(fileStatus);
    auto *choose=new QPushButton("选择目录并预览…");choose->setObjectName("scanFiles");fileLayout->addWidget(choose);
    auto *preview=new QTreeWidget;preview->setObjectName("filePreview");preview->setHeaderLabels({"原文件","建议位置","重复"});preview->header()->setSectionResizeMode(QHeaderView::ResizeToContents);fileLayout->addWidget(preview,1);
    auto *execute=new QPushButton("确认预览后移动…");execute->setObjectName("executeFilePlan");execute->setEnabled(false);auto *cancel=new QPushButton("取消后台操作");cancel->setEnabled(false);auto *fileButtons=new QHBoxLayout;fileButtons->addWidget(execute);fileButtons->addWidget(cancel);fileLayout->addLayout(fileButtons);
    auto *logs=new QComboBox;logs->setObjectName("fileOperationLogs");fileLayout->addWidget(logs);auto *undo=new QPushButton("撤销所选记录…");undo->setObjectName("undoFilePlan");fileLayout->addWidget(undo);
    auto refreshLogs=[organizer,logs]{logs->clear();for(const auto &path:organizer->journals())logs->addItem(QFileInfo(path).baseName(),path);};refreshLogs();
    connect(choose,&QPushButton::clicked,this,[this,organizer]{const auto path=QFileDialog::getExistingDirectory(this,"选择要整理的个人目录");if(!path.isEmpty())organizer->scan(path);});
    connect(organizer,&FileOrganizer::previewReady,this,[organizer,preview,fileStatus,execute]{const auto plan=organizer->plan();preview->clear();for(const auto &move:plan.moves)new QTreeWidgetItem(preview,{QFileInfo(move.from).fileName(),QDir(plan.root).relativeFilePath(move.to),move.duplicate?"内容重复（保留）":""});fileStatus->setText(plan.error.isEmpty()?QString("%1\n共 %2 个文件。%3").arg(plan.root).arg(plan.moves.size()).arg(plan.warning):"扫描未完成："+plan.error);execute->setEnabled(plan.error.isEmpty() && !plan.moves.isEmpty());});
    connect(execute,&QPushButton::clicked,this,[this,organizer]{const auto plan=organizer->plan();if(plan.moves.isEmpty())return;if(QMessageBox::question(this,"确认移动文件",QString("按照上面的预览，将 %1 个文件移动到所选目录内的 Organized 分类文件夹？\n%2\n已存在的同名文件不会覆盖，重复内容会保留。").arg(plan.moves.size()).arg(plan.root),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes)organizer->execute();});
    connect(cancel,&QPushButton::clicked,organizer,&FileOrganizer::cancel);
    connect(organizer,&FileOrganizer::busyChanged,this,[choose,execute,cancel,undo,fileStatus](bool busy){choose->setEnabled(!busy);execute->setEnabled(false);cancel->setEnabled(busy);undo->setEnabled(!busy);if(busy)fileStatus->setText("正在后台处理，可取消。文件内容只在本机读取。");});
    connect(organizer,&FileOrganizer::finished,this,[fileStatus,preview,refreshLogs](const QString &message){fileStatus->setText(message);preview->clear();refreshLogs();});
    connect(undo,&QPushButton::clicked,this,[this,organizer,logs]{if(logs->currentData().toString().isEmpty())return;if(QMessageBox::question(this,"撤销文件移动","将内容未改变的文件移回原路径？原位置被占用或文件已改变时会停止，不覆盖任何内容。",QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes)organizer->undo(logs->currentData().toString());});
    auto *clipboard=new QPushButton("展开桌宠剪贴板");root->addWidget(clipboard);connect(clipboard,&QPushButton::clicked,this,[this]{if(clipboardPanel_)clipboardPanel_->toggle();});
    return page;
}
void CompanionWindow::refreshTools(){
    if(!todos_)return;const auto *selected=todos_->currentItem();const qint64 id=selected?selected->data(Qt::UserRole).toLongLong():0;todos_->clear();
    for(const auto &todo:companion_.tools().tasks()){
        auto *item=new QListWidgetItem((todo.done?"✓ ":todo.priority==2?"! ":"○ ")+todo.title+(todo.due.isValid()?"\n"+todo.due.toLocalTime().toString("yyyy-MM-dd HH:mm")+(!todo.done && todo.due<QDateTime::currentDateTimeUtc()?" · 已逾期":""):"\n无提醒时间"),todos_);
        item->setData(Qt::UserRole,todo.id);item->setData(Qt::UserRole+1,todo.done);if(id==todo.id)todos_->setCurrentItem(item);
    }
}
}
