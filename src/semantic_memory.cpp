#include "semantic_memory.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
namespace dragon {
SemanticMemory::SemanticMemory(const QString &database,QObject *parent):QObject(parent),database_(database){
    home_=qEnvironmentVariable("DRAGONGPT_EMBEDDING_HOME",QCoreApplication::applicationDirPath()+"/embedding");
    if(available())status_="本地语义模型 · CPU · 按需加载";
    deadline_.setSingleShot(true);idle_.setSingleShot(true);
    connect(&deadline_,&QTimer::timeout,this,&SemanticMemory::fail);
    connect(&idle_,&QTimer::timeout,this,[this]{process_.kill();});
    connect(&process_,&QProcess::started,this,[this]{if(active_)send();else process_.kill();});
    connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){if(active_)fail();});
    connect(&process_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this]{if(active_)fail();});
    connect(&process_,&QProcess::readyReadStandardError,this,[this]{process_.readAllStandardError();});
    connect(&process_,&QProcess::readyReadStandardOutput,this,[this]{
        buffer_+=process_.readAllStandardOutput();if(buffer_.size()>65536){fail();return;}
        while(buffer_.contains('\n')){
            const auto line=buffer_.left(buffer_.indexOf('\n'));buffer_.remove(0,line.size()+1);
            const auto object=QJsonDocument::fromJson(line).object();if(object["id"].toInteger()!=qint64(active_) || !active_)continue;
            if(object.contains("error")){fail();return;}
            deadline_.stop();const auto id=active_;active_=0;request_.clear();status_="本地语义检索 · ONNX CPU";idle_.start(60000);
            emit searched(id,object["matches"].toArray(),status_);
        }
    });
}
SemanticMemory::~SemanticMemory(){if(process_.state()==QProcess::Starting)process_.waitForStarted(1000);process_.kill();process_.waitForFinished(2000);}
bool SemanticMemory::available() const{return QFileInfo::exists(home_+"/ready.json") && QFileInfo::exists(home_+"/worker.py");}
void SemanticMemory::send(){if(active_ && !request_.isEmpty())process_.write(request_);}
void SemanticMemory::cancel(){active_=0;request_.clear();deadline_.stop();if(process_.state()!=QProcess::NotRunning)process_.kill();buffer_.clear();}
void SemanticMemory::fail(){
    const auto id=active_;active_=0;request_.clear();deadline_.stop();buffer_.clear();process_.kill();
    status_="文字检索降级 · 本地语义模型不可用或超时";if(id)emit searched(id,{},status_);
}
void SemanticMemory::search(quint64 id,const QString &query,const QString &layer){
    if(active_){fail();}active_=id;idle_.stop();
    request_=QJsonDocument(QJsonObject{{"id",qint64(id)},{"query",query.left(2000)},{"limit",4},{"layer",layer}}).toJson(QJsonDocument::Compact)+"\n";
    deadline_.start(15000);
    if(!available()){fail();return;}
    if(process_.state()==QProcess::Running){send();return;}
    QFile file(home_+"/ready.json");if(!file.open(QIODevice::ReadOnly)){fail();return;}
    const auto ready=QJsonDocument::fromJson(file.readAll()).object();
    const auto model=QDir(home_).absoluteFilePath(ready["model"].toString());
    if(ready["format"].toInt()!=2 || !QFileInfo::exists(ready["python"].toString()) || !QFileInfo::exists(ready["site_packages"].toString()) || !QFileInfo::exists(model+"/model.onnx")){fail();return;}
    auto env=QProcessEnvironment::systemEnvironment();env.insert("HF_HUB_OFFLINE","1");env.insert("TRANSFORMERS_OFFLINE","1");env.insert("PYTHONIOENCODING","utf-8");env.insert("PYTHONNOUSERSITE","1");env.insert("PYTHONPATH",ready["site_packages"].toString());process_.setProcessEnvironment(env);
    process_.start(ready["python"].toString(),{"-u",home_+"/worker.py","--model",model,"--database",database_});
}
}
