#include "local_tools.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QTimeZone>
#include <QUuid>
#include <algorithm>
#include <stdexcept>
namespace dragon {
namespace {void checked(QSqlQuery &q){if(!q.exec())throw std::runtime_error(q.lastError().text().toStdString());}}
LocalTools::LocalTools(const QString &database,QObject *parent):QObject(parent),connection_(QUuid::createUuid().toString()){
    db_=QSqlDatabase::addDatabase("QSQLITE",connection_);db_.setDatabaseName(database);
    if(!db_.open())throw std::runtime_error("Cannot open local tool storage");exec("PRAGMA busy_timeout=1500");
    if(!db_.transaction())throw std::runtime_error("Cannot start tools migration");
    try{
        exec("CREATE TABLE IF NOT EXISTS todos(id INTEGER PRIMARY KEY,title TEXT NOT NULL,due_at TEXT,priority INTEGER NOT NULL DEFAULT 1,done INTEGER NOT NULL DEFAULT 0,notified INTEGER NOT NULL DEFAULT 0,created_at TEXT NOT NULL,completed_at TEXT)");
        exec("CREATE INDEX IF NOT EXISTS todos_due ON todos(done,notified,due_at)");
        exec("CREATE TABLE IF NOT EXISTS daily_stats(date TEXT NOT NULL,name TEXT NOT NULL,value INTEGER NOT NULL DEFAULT 0,PRIMARY KEY(date,name))");
        if(!db_.commit())throw std::runtime_error("Tools migration commit failed");
    }catch(...){db_.rollback();throw;}
    timer_.setInterval(1000);connect(&timer_,&QTimer::timeout,this,[this]{try{tick();}catch(const std::exception &){emit error("本地提醒暂时无法保存，下次会再次检查");}});timer_.start();
}
LocalTools::~LocalTools(){try{if(unsavedActive_>0)addStatistic("active_seconds",unsavedActive_,activeDate_);}catch(const std::exception &){}db_.close();db_={};QSqlDatabase::removeDatabase(connection_);}
void LocalTools::observeActivity(int seconds,QDate date){
    if(seconds<=0 || seconds>10)return;
    if(activeDate_.isValid() && activeDate_!=date && unsavedActive_>0){addStatistic("active_seconds",unsavedActive_,activeDate_);unsavedActive_=0;}
    activeDate_=date;unsavedActive_+=seconds;
    if(unsavedActive_>=60){addStatistic("active_seconds",unsavedActive_,activeDate_);unsavedActive_=0;}
}
void LocalTools::exec(const QString &sql) const{QSqlQuery q(db_);q.prepare(sql);checked(q);}
qint64 LocalTools::addTask(const TaskDraft &draft){
    if(draft.needsConfirmation || draft.title.trimmed().isEmpty() || draft.title.size()>200 || draft.priority<0 || draft.priority>2)throw std::runtime_error("Confirm a valid task before saving");
    QSqlQuery q(db_);q.prepare("INSERT INTO todos(title,due_at,priority,created_at) VALUES(?,?,?,?)");q.addBindValue(draft.title.trimmed());q.addBindValue(draft.due.isValid()?draft.due.toUTC().toString(Qt::ISODateWithMs):QString(""));q.addBindValue(draft.priority);q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));checked(q);const auto id=q.lastInsertId().toLongLong();emit changed();return id;
}
QVector<Todo> LocalTools::tasks() const{
    QSqlQuery q(db_);q.prepare("SELECT id,title,due_at,priority,done,notified FROM todos ORDER BY done,priority DESC,CASE WHEN due_at='' THEN 1 ELSE 0 END,due_at,id DESC LIMIT 1000");checked(q);
    QVector<Todo> rows;while(q.next())rows.append({q.value(0).toLongLong(),q.value(1).toString(),QDateTime::fromString(q.value(2).toString(),Qt::ISODateWithMs),q.value(3).toInt(),q.value(4).toBool(),q.value(5).toBool()});return rows;
}
void LocalTools::completeTask(qint64 id,bool done){
    QSqlQuery prior(db_);prior.prepare("SELECT done FROM todos WHERE id=?");prior.addBindValue(id);checked(prior);if(!prior.next())return;const bool wasDone=prior.value(0).toBool();prior.finish();
    QSqlQuery q(db_);q.prepare("UPDATE todos SET done=?,completed_at=?,notified=CASE WHEN ?=0 THEN 0 ELSE notified END WHERE id=?");q.addBindValue(done);q.addBindValue(done?QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs):QString(""));q.addBindValue(done);q.addBindValue(id);checked(q);emit changed();if(done && !wasDone)emit taskCompleted(id);
}
void LocalTools::removeTask(qint64 id){QSqlQuery q(db_);q.prepare("DELETE FROM todos WHERE id=?");q.addBindValue(id);checked(q);emit changed();}
QString LocalTools::tasksOverview() const{
    QString text="今天的待办：";int count=0;const auto today=QDate::currentDate();
    for(const auto &t:tasks())if(!t.done && (!t.due.isValid() || t.due.toLocalTime().date()<=today)){
        if(++count>10){text+="\n更多事项可在「工具」查看。";break;}
        text+="\n· "+t.title+(t.due.isValid()?"（"+t.due.toLocalTime().toString("MM-dd HH:mm")+(t.due<QDateTime::currentDateTimeUtc()?"，已逾期":"")+"）":QString{});
    }
    return count?text:QString("今天没有待处理事项哦。");
}
void LocalTools::addStatistic(const QString &name,int value,QDate date){
    if(!QStringList{"active_seconds","pomodoros","load_events","interactions"}.contains(name) || value<0)return;
    QSqlQuery q(db_);q.prepare("INSERT INTO daily_stats VALUES(?,?,?) ON CONFLICT(date,name) DO UPDATE SET value=value+excluded.value");q.addBindValue(date.toString(Qt::ISODate));q.addBindValue(name);q.addBindValue(value);checked(q);
}
void LocalTools::tick(QDateTime now){
    QSqlQuery q(db_);q.prepare("SELECT id,title,due_at FROM todos WHERE done=0 AND notified=0 AND due_at<>'' AND due_at<=? ORDER BY due_at LIMIT 10");q.addBindValue(now.toUTC().toString(Qt::ISODateWithMs));checked(q);
    QVector<Todo> due;while(q.next())due.append({q.value(0).toLongLong(),q.value(1).toString(),QDateTime::fromString(q.value(2).toString(),Qt::ISODateWithMs)});q.finish();
    if(!due.isEmpty()){
        QString text="有待办需要你留意：";
        if(!db_.transaction())throw std::runtime_error("Cannot save reminder delivery");
        try{for(const auto &t:due){QSqlQuery update(db_);update.prepare("UPDATE todos SET notified=1 WHERE id=? AND done=0 AND notified=0");update.addBindValue(t.id);checked(update);text+="\n· "+t.title+(t.due.secsTo(now)>60?"（已逾期）":"");}if(!db_.commit())throw std::runtime_error("Reminder commit failed");}catch(...){db_.rollback();throw;}
        emit notification(text,PetAction::Comfort);emit changed();
    }
}
QJsonObject LocalTools::dailyStats(QDate date) const{
    QJsonObject result;QSqlQuery q(db_);q.prepare("SELECT name,value FROM daily_stats WHERE date=?");q.addBindValue(date.toString(Qt::ISODate));checked(q);while(q.next())result.insert(q.value(0).toString(),q.value(1).toInt());q.finish();
    if(date==activeDate_)result["active_seconds"]=result["active_seconds"].toInt()+unsavedActive_;
    if(focusStatistics_){const auto focus=focusStatistics_(date);result["focus_completed"]=focus["count"].toInt();result["focus_ms"]=focus["ms"].toInteger();}
    q.prepare("SELECT COUNT(*) FROM todos WHERE done=1 AND completed_at>=? AND completed_at<?");q.addBindValue(QDateTime(date,QTime(0,0),QTimeZone::systemTimeZone()).toUTC().toString(Qt::ISODateWithMs));q.addBindValue(QDateTime(date.addDays(1),QTime(0,0),QTimeZone::systemTimeZone()).toUTC().toString(Qt::ISODateWithMs));checked(q);q.next();result["todos_completed"]=q.value(0).toInt();return result;
}
QString LocalTools::dailySummary(QDate date) const{
    const auto s=dailyStats(date);return QString("%1 的本地使用记录\n近期键鼠活跃：%2 分钟\n完成番茄钟：%3 个\n完成待办：%4 项\n持续高负载事件：%5 次\n与霜翎对话：%6 轮\n%7\n仅统计桌宠运行期间实际观察到的数据，未记录其他应用内容。").arg(date.toString("yyyy-MM-dd")).arg(s["active_seconds"].toInt()/60).arg(s["focus_completed"].toInt()).arg(s["todos_completed"].toInt()).arg(s["load_events"].toInt()).arg(s["interactions"].toInt()).arg(tasksOverview());
}
Reply LocalTools::execute(const LocalCommand &command){
    switch(command.intent){
    case LocalIntent::TodoList:return {tasksOverview()};
    case LocalIntent::TodoAdd:{const auto draft=TaskParser::parse(command.argument);if(draft.needsConfirmation){emit confirmationNeeded(draft);return {draft.question+" 请在待办确认窗口核对后保存。"};}addTask(draft);return {"已保存待办："+draft.title+(draft.due.isValid()?"\n提醒时间："+draft.due.toLocalTime().toString("yyyy-MM-dd HH:mm"):QString{})};}
    case LocalIntent::DailySummary:return {dailySummary()};
    case LocalIntent::HealthPage:emit openHealthRequested();return {"已打开电脑健康页面。"};
    default:return {};
    }
}
}
