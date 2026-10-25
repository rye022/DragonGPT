#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QJsonArray>
namespace dragon {
class SemanticMemory final:public QObject{
    Q_OBJECT
public:
    explicit SemanticMemory(const QString &database,QObject *parent=nullptr);
    ~SemanticMemory() override;
    bool available() const;
    QString status() const{return status_;}
    qint64 workerProcessId() const{return process_.processId();}
    void search(quint64 id,const QString &query,const QString &layer={});
    void cancel();
signals:
    void searched(quint64 id,const QJsonArray &matches,const QString &status);
private:
    QString database_,home_,status_="文字检索 · 未安装本地语义模型";
    QProcess process_;
    QTimer deadline_,idle_;
    QByteArray buffer_,request_;
    quint64 active_=0;
    void fail();
    void send();
};
}
