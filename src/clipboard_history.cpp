#include "clipboard_history.h"
#include <QRegularExpression>
#include <QSqlQuery>
#include <QSqlError>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <stdexcept>
namespace dragon {
namespace{void checked(QSqlQuery &q){if(!q.exec())throw std::runtime_error(q.lastError().text().toStdString());}}
ClipboardHistory::ClipboardHistory(const QString &database,QObject *parent):QObject(parent),connection_(QUuid::createUuid().toString()){
    db_=QSqlDatabase::addDatabase("QSQLITE",connection_);db_.setDatabaseName(database);if(!db_.open())throw std::runtime_error("Cannot open clipboard history");
    QSqlQuery q(db_);q.prepare("PRAGMA busy_timeout=1500");checked(q);
    q.prepare("CREATE TABLE IF NOT EXISTS clipboard_history(id INTEGER PRIMARY KEY,text TEXT NOT NULL UNIQUE,kind TEXT NOT NULL,created_at TEXT NOT NULL,pinned INTEGER NOT NULL DEFAULT 0)");checked(q);
    q.prepare("CREATE TABLE IF NOT EXISTS clipboard_settings(key TEXT PRIMARY KEY,value TEXT NOT NULL)");checked(q);
    q.prepare("SELECT key,value FROM clipboard_settings");checked(q);while(q.next()){if(q.value(0)=="enabled")enabled_=q.value(1).toString()=="1";if(q.value(0)=="retention")retentionHours_=std::clamp(q.value(1).toInt(),1,720);}q.finish();prune();
}
ClipboardHistory::~ClipboardHistory(){db_.close();db_={};QSqlDatabase::removeDatabase(connection_);}
void ClipboardHistory::setting(const QString &key,const QString &value){QSqlQuery q(db_);q.prepare("INSERT INTO clipboard_settings VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value");q.addBindValue(key);q.addBindValue(value);checked(q);}
void ClipboardHistory::setEnabled(bool enabled){setting("enabled",enabled?"1":"0");enabled_=enabled;emit changed();}
void ClipboardHistory::setRetentionHours(int hours){if(hours<1 || hours>720)throw std::runtime_error("Clipboard retention must be 1–720 hours");setting("retention",QString::number(hours));retentionHours_=hours;prune();emit changed();}
bool ClipboardHistory::sensitive(const QString &text){
    if(text.trimmed().isEmpty() || text.size()>8000)return true;
    static const QRegularExpression secrets("密码|口令|验证码|密钥|私钥|身份证|银行卡|护照|password|passwd|secret|token|api[ _-]?key|authorization|bearer|sk-|AKIA|BEGIN .*PRIVATE KEY|[?&](?:key|auth|code|session)=|://[^/\\s]+@",QRegularExpression::CaseInsensitiveOption);
    if(secrets.match(text).hasMatch())return true;
    // Unknown token-shaped strings, one-time codes and identity numbers are not retained.
    if(QRegularExpression("(?:^|[^0-9])[0-9]{6,}(?:$|[^0-9])|[A-Za-z0-9_+\\-/=]{24,}|(?:^|[\\\\/])(?:\\.env|id_rsa|credentials|secrets)(?:$|[.\\\\/])",QRegularExpression::CaseInsensitiveOption).match(text).hasMatch())return true;
    if(!text.contains(QRegularExpression("[\\s\\x{4e00}-\\x{9fff}]")) && text.size()>=12 && QRegularExpression("[A-Za-z]").match(text).hasMatch() && QRegularExpression("[0-9]").match(text).hasMatch() && !text.contains(":/") && !text.contains(":\\"))return true;
    const QUrl url(text.trimmed());if(url.isValid() && (url.scheme()=="http" || url.scheme()=="https") && (!url.query().isEmpty() || !url.fragment().isEmpty()))return true;
    return false;
}
QString ClipboardHistory::classify(const QString &text){
    const QUrl url(text.trimmed());if(url.isValid() && (url.scheme()=="http" || url.scheme()=="https") && !url.host().isEmpty())return "URL";
    if(QRegularExpression("^(?:[A-Za-z]:[\\\\/]|\\\\\\\\|/[^/])").match(text).hasMatch())return "文件路径";
    if(QRegularExpression("error|exception|traceback|fatal|错误|失败|找不到|未定义",QRegularExpression::CaseInsensitiveOption).match(text).hasMatch())return "报错";
    if(QRegularExpression("#include|\\b(?:def|class|function|const|import|return)\\b|[{};]",QRegularExpression::CaseInsensitiveOption).match(text).hasMatch())return "代码";
    return "文字";
}
bool ClipboardHistory::capture(const QString &input){
    if(!enabled_ || sensitive(input))return false;prune();const auto &text=input;
    QSqlQuery q(db_);q.prepare("INSERT INTO clipboard_history(text,kind,created_at) VALUES(?,?,?) ON CONFLICT(text) DO UPDATE SET created_at=excluded.created_at");q.addBindValue(text);q.addBindValue(classify(text));q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));checked(q);
    q.prepare("DELETE FROM clipboard_history WHERE pinned=0 AND id NOT IN (SELECT id FROM clipboard_history WHERE pinned=0 ORDER BY created_at DESC LIMIT 50)");checked(q);emit changed();return true;
}
QVector<ClipEntry> ClipboardHistory::entries() const{
    QSqlQuery q(db_);q.prepare("SELECT id,text,kind,created_at,pinned FROM clipboard_history WHERE pinned=1 OR created_at>? ORDER BY pinned DESC,created_at DESC");q.addBindValue(QDateTime::currentDateTimeUtc().addSecs(-retentionHours_*3600).toString(Qt::ISODateWithMs));checked(q);QVector<ClipEntry> result;
    while(q.next())result.append({q.value(0).toLongLong(),q.value(1).toString(),q.value(2).toString(),QDateTime::fromString(q.value(3).toString(),Qt::ISODateWithMs),q.value(4).toBool()});return result;
}
void ClipboardHistory::remove(qint64 id){QSqlQuery q(db_);q.prepare("DELETE FROM clipboard_history WHERE id=?");q.addBindValue(id);checked(q);emit changed();}
void ClipboardHistory::pin(qint64 id,bool pinned){
    QSqlQuery q(db_);if(pinned){q.prepare("SELECT COUNT(*) FROM clipboard_history WHERE pinned=1");checked(q);q.next();if(q.value(0).toInt()>=20)throw std::runtime_error("At most 20 clipboard entries can be pinned");q.finish();}
    q.prepare("UPDATE clipboard_history SET pinned=? WHERE id=?");q.addBindValue(pinned);q.addBindValue(id);checked(q);emit changed();
}
void ClipboardHistory::clear(){QSqlQuery q(db_);q.prepare("DELETE FROM clipboard_history");checked(q);emit changed();}
void ClipboardHistory::prune(QDateTime now){QSqlQuery q(db_);q.prepare("DELETE FROM clipboard_history WHERE pinned=0 AND created_at<=?");q.addBindValue(now.toUTC().addSecs(-retentionHours_*3600).toString(Qt::ISODateWithMs));checked(q);if(q.numRowsAffected()>0)emit changed();}
}
