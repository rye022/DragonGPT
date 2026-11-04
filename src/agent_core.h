#pragma once
#include "companion.h"
#include "tool_hub.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QQueue>
#include <QJsonArray>

namespace dragon {
// One active task. UI approval is bound to the exact pending tool+arguments.
// Results are transient, untrusted context; audit contains no file contents.
class AgentCore final: public QObject {
    Q_OBJECT
public:
    AgentCore(Companion &companion,ToolHub &existing,QObject *parent=nullptr,
              QUrl endpoint=QUrl("https://api.deepseek.com/chat/completions"));
    ~AgentCore() override;
    bool busy() const{return active_;}
    int queuedTasks() const{return queuedGoals_.size();}
    int mode() const{return mode_;}
    QString root() const{return root_;}
    QString state() const{return state_;}
    QString python() const{return python_;}
    void configure(int mode,const QString &root,const QString &python);
    bool start(const QString &goal,bool readOnlyExploration=false);
    static bool explorationToolAllowed(const QString &name);
    void approve(bool yes);
    void pause(bool paused);
    void cancel();
    static QJsonArray toolCatalog();
    static QString validate(const QString &name,const QJsonObject &args);
    static bool needsApproval(const QString &name);
signals:
    void changed();
    void progress(const QString &text);
    void approvalRequired(const QString &tool,const QJsonObject &args);
    void finished(const QString &text,bool ok);
    void auditEntry(const QJsonObject &entry);
private:
    Companion &companion_;QPointer<ToolHub> existing_;
    QNetworkAccessManager network_;QPointer<QNetworkReply> reply_;
    QUrl endpoint_;QTimer deadline_,taskDeadline_,workerDeadline_;
    QProcess worker_;QByteArray workerBuffer_,body_;
    QJsonArray messages_;QQueue<QJsonObject> calls_;
    QQueue<QString> queuedGoals_;
    QJsonObject pending_;QString pendingName_,root_,python_,state_="idle",goal_;
    bool active_=false,paused_=false,approved_=false,awaiting_=false,workerWaiting_=false,workerStopping_=false;
    int mode_=0,steps_=0,rounds_=0,requestId_=0,networkRetries_=0;
    bool exploration_=false;
    bool taskToolSucceeded_=false,taskToolFailed_=false;
    quint64 epoch_=0;QMetaObject::Connection existingResult_,existingState_;
    void requestModel();void next();void execute();void receiveTool(bool ok,const QJsonObject &result);
    void fail(const QString &reason);void complete(const QString &text,bool ok);
    void consumeWorker();void log(const QString &tool,bool ok,const QString &event);
};
}
