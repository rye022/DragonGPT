#include "agent_page.h"
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QSettings>
namespace dragon {
AgentPage::AgentPage(Companion &c,ToolHub &hub,QWidget *parent):QWidget(parent),core_(c,hub,this){
    setObjectName("agentPage");auto *outer=new QVBoxLayout(this);auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);auto *body=new QWidget;auto *layout=new QVBoxLayout(body);scroll->setWidget(body);outer->addWidget(scroll);
    auto note=[layout](const QString &s){auto *l=new QLabel(s);l->setWordWrap(true);l->setTextFormat(Qt::PlainText);layout->addWidget(l);return l;};
    note("Agent 4.0 · 任务执行\n模式和工作区授权仅对本次会话有效；撤销会停止任务。网页、文件和工具输出不具有授权权力。操作外部数据、运行构建脚本前请核对每个预览。");
    mode_=new QComboBox;mode_->setObjectName("agentMode");mode_->addItems({"A · 普通聊天（默认）","B · Agent 自动模式","C · 高级开发模式"});layout->addWidget(mode_);
    auto *folderRow=new QHBoxLayout;root_=new QLineEdit;root_->setObjectName("agentWorkspace");root_->setReadOnly(true);root_->setPlaceholderText("尚未授权本地工作区");auto *choose=new QPushButton("选择授权目录");folderRow->addWidget(root_,1);folderRow->addWidget(choose);layout->addLayout(folderRow);
    python_=new QLineEdit;python_->setObjectName("agentPython");python_->setPlaceholderText("工具运行时 python.exe 的绝对路径");
    QSettings settings(c.dataDirectory()+"/agent-runtime.ini",QSettings::IniFormat);python_->setText(settings.value("python",core_.python()).toString());
    auto *runtimeRow=new QHBoxLayout;runtimeRow->addWidget(python_,1);auto *runtime=new QPushButton("选择 Python");runtimeRow->addWidget(runtime);layout->addLayout(runtimeRow);
    auto apply=[this]{core_.configure(mode_->currentIndex(),root_->text(),python_->text());};
    connect(mode_,qOverload<int>(&QComboBox::currentIndexChanged),this,[apply](int){apply();});
    connect(choose,&QPushButton::clicked,this,[this,apply]{const auto p=QFileDialog::getExistingDirectory(this,"仅授权所选目录；排除系统目录、链接及凭据文件");if(!p.isEmpty()){root_->setText(p);apply();}});
    connect(runtime,&QPushButton::clicked,this,[this,apply,&c]{const auto p=QFileDialog::getOpenFileName(this,"选择工具运行时 Python","","Python (python.exe)");if(!p.isEmpty()){python_->setText(p);QSettings s(c.dataDirectory()+"/agent-runtime.ini",QSettings::IniFormat);s.setValue("python",p);apply();}});
    connect(python_,&QLineEdit::editingFinished,this,apply);apply();
    note("文件/网页工具使用 Python 标准库；浏览器需 playwright 和 Edge；PDF、Word、Excel 需 pypdf、python-docx、openpyxl。缺失依赖会显示实际错误。不会自动安装软件。高级模式允许构建脚本运行代码，每次均需批准。");
    goal_=new QLineEdit;goal_->setObjectName("agentTaskInput");goal_->setMaxLength(4000);goal_->setPlaceholderText("例如：读取项目源码，分析编译配置并提出修改");layout->addWidget(goal_);
    auto *row=new QHBoxLayout;auto *start=new QPushButton("执行任务");start->setObjectName("startAgent");auto *pause=new QPushButton("暂停 / 继续");auto *cancel=new QPushButton("停止任务");auto *revoke=new QPushButton("撤销授权");row->addWidget(start);row->addWidget(pause);row->addWidget(cancel);row->addWidget(revoke);layout->addLayout(row);
    connect(start,&QPushButton::clicked,this,[this]{dispatch(goal_->text());});connect(goal_,&QLineEdit::returnPressed,start,&QPushButton::click);
    connect(pause,&QPushButton::clicked,this,[this]{core_.pause(core_.state()!="paused");});connect(cancel,&QPushButton::clicked,&core_,&AgentCore::cancel);connect(revoke,&QPushButton::clicked,this,[this]{root_->clear();mode_->setCurrentIndex(0);core_.configure(0,{},python_->text());});
    auto *state=note("状态：idle");state->setObjectName("agentTaskStatus");connect(&core_,&AgentCore::changed,this,[this,state]{state->setText("状态："+core_.state());if(core_.state()!="approval"){approve_->setEnabled(false);deny_->setEnabled(false);}});
    preview_=new QTextBrowser;preview_->setObjectName("agentOperationPreview");preview_->setMinimumHeight(160);layout->addWidget(preview_);
    auto *approvals=new QHBoxLayout;approve_=new QPushButton("批准此项具体操作");approve_->setObjectName("approveAgentOperation");deny_=new QPushButton("拒绝此项");approve_->setEnabled(false);deny_->setEnabled(false);approvals->addWidget(approve_);approvals->addWidget(deny_);layout->addLayout(approvals);
    connect(&core_,&AgentCore::approvalRequired,this,[this](const QString &tool,const QJsonObject &args){preview_->setPlainText("待执行工具："+tool+"\n参数 / 写入内容：\n"+QString::fromUtf8(QJsonDocument(args).toJson())+"\n批准仅适用于本次参数。写入备份在源文件旁，移动结果包含反向路径，回收删除通过 Windows 回收站恢复。");approve_->setEnabled(true);deny_->setEnabled(true);emit reveal();});
    connect(approve_,&QPushButton::clicked,this,[this]{approve_->setEnabled(false);deny_->setEnabled(false);core_.approve(true);});connect(deny_,&QPushButton::clicked,this,[this]{approve_->setEnabled(false);deny_->setEnabled(false);core_.approve(false);});
    events_=new QTextBrowser;events_->setObjectName("agentExecutionLog");events_->setMinimumHeight(230);events_->document()->setMaximumBlockCount(1200);layout->addWidget(events_);
    connect(&core_,&AgentCore::progress,this,[this](const QString &s){events_->append(s.toHtmlEscaped().replace('\n',"<br>"));});
    connect(&core_,&AgentCore::finished,this,[this](const QString &s,bool ok){events_->append((ok?"任务回复：\n":"任务失败：\n")+s.toHtmlEscaped().replace('\n',"<br>"));emit reveal();});
    layout->addStretch();
}
bool AgentPage::dispatch(const QString &text){if(core_.mode()==0)return false;goal_->setText(text);const bool started=core_.start(text);if(started)emit reveal();return started;}
}
