#pragma once
#include "local_brain.h"
#include "task_parser.h"
#include "types.h"
#include <QObject>
#include <QSqlDatabase>
#include <QTimer>
#include <QJsonObject>
#include <functional>
namespace dragon {
class LocalTools final:public QObject{
    Q_OBJECT
public:
    explicit LocalTools(const QString &database,QObject *parent=nullptr);
    ~LocalTools() override;
    Reply execute(const LocalCommand &command);
    qint64 addTask(const TaskDraft &draft);
    QVector<Todo> tasks() const;
    void completeTask(qint64 id,bool done);
    void removeTask(qint64 id);
    QString tasksOverview() const;
    void tick(QDateTime now=QDateTime::currentDateTimeUtc());
    QJsonObject dailyStats(QDate date=QDate::currentDate()) const;
    QString dailySummary(QDate date=QDate::currentDate()) const;
    void addStatistic(const QString &name,int value=1,QDate date=QDate::currentDate());
    void observeActivity(int seconds,QDate date);
    void setFocusStatisticsProvider(std::function<QJsonObject(QDate)> provider){focusStatistics_=std::move(provider);}
signals:
    void changed();
    void notification(const QString &text,dragon::PetAction action,bool passive=false);
    void confirmationNeeded(const dragon::TaskDraft &draft);
    void openHealthRequested();
    void error(const QString &message);
private:
    QString connection_;
    QSqlDatabase db_;
    QTimer timer_;
    int unsavedActive_=0;
    QDate activeDate_;
    std::function<QJsonObject(QDate)> focusStatistics_;
    void exec(const QString &sql) const;
};
}
