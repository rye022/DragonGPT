#pragma once
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QTimer>
#include <functional>
namespace dragon {
struct ToolDefinition {
    QString id,description;
    QJsonObject schema;
    bool confirmation=false,idempotent=true;
    int timeoutMs=15000;
};
class ToolHub final:public QObject {
    Q_OBJECT
public:
    using Completion=std::function<void(bool,const QJsonObject &)>;
    using Handler=std::function<void(const QJsonObject &,Completion)>;
    explicit ToolHub(QObject *parent=nullptr);
    ~ToolHub() override;
    void add(const ToolDefinition &definition,Handler handler,std::function<void()> cancel={});
    QJsonArray catalog() const;
    QString propose(const QJsonArray &steps);
    QJsonArray plan() const{return plan_;}
    bool busy() const{return running_;}
    QString state() const{return state_;}
    quint64 generation() const{return epoch_;}
    void run(bool confirmed=false);
    void cancel();
    QJsonArray audit() const{return audit_;}
    static QJsonArray localPlan(const QString &goal);
signals:
    void changed();
    void confirmationRequired();
    void result(const QString &tool,bool success,const QJsonObject &output);
private:
    struct Entry{ToolDefinition definition;Handler handler;std::function<void()> cancel;};
    QHash<QString,Entry> entries_;
    QJsonArray plan_,audit_;
    QTimer deadline_;
    quint64 epoch_=0;
    int cursor_=0,attempt_=0;
    bool running_=false;
    QString state_="idle";
    void next();
    void finishStep(quint64 epoch,int cursor,bool ok,const QJsonObject &output);
    QString validate(const QJsonObject &step) const;
};
}
