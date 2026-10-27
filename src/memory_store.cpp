#include "memory_store.h"
#include "privacy.h"
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dragon {
namespace {
void checked(QSqlQuery &q) {
    if (!q.exec()) throw std::runtime_error(q.lastError().text().toStdString());
}
QDateTime utcNow() { return QDateTime::currentDateTimeUtc(); }
QString clean(const QString &text, int max) { return text.trimmed().left(max); }
QString canonicalMemory(QString text){return text.normalized(QString::NormalizationForm_KC).toCaseFolded().remove(QRegularExpression("[\\s。！!]+"));}
Memory memoryRow(const QSqlQuery &q){
    return {q.value(0).toLongLong(),q.value(1).toString(),QDateTime::fromString(q.value(2).toString(),Qt::ISODateWithMs),
        q.value(3).toString(),q.value(4).toDouble(),QDateTime::fromString(q.value(5).toString(),Qt::ISODateWithMs),QDateTime::fromString(q.value(6).toString(),Qt::ISODateWithMs),
        q.value(7).toString(),q.value(8).toString(),QDateTime::fromString(q.value(9).toString(),Qt::ISODateWithMs),q.value(10).toBool()};
}
}
MemoryStore::MemoryStore(const QString &filePath)
    : connectionName_(QUuid::createUuid().toString()) {
    const bool existing=QFileInfo(filePath).size()>0;
    if (!QDir().mkpath(QFileInfo(filePath).absolutePath()))
        throw std::runtime_error("Cannot create data directory");
    db_ = QSqlDatabase::addDatabase("QSQLITE", connectionName_);
    try{
    db_.setDatabaseName(filePath);
    if (!db_.open()) throw std::runtime_error(db_.lastError().text().toStdString());
    exec("PRAGMA foreign_keys=ON");
    exec("PRAGMA busy_timeout=3000");
    QSqlQuery version(db_);version.prepare("PRAGMA user_version");checked(version);version.next();const int previous=version.value(0).toInt();version.finish();
    if(previous>7)throw std::runtime_error("Database is newer than this application; refusing downgrade");
    QSqlQuery shape(db_);shape.prepare("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN ('wellness_state','focus_sessions','memory_vectors')");checked(shape);shape.next();const bool missingFeatureTables=shape.value(0).toInt()!=3;shape.finish();
    if((previous<7 || missingFeatureTables) && QFileInfo(filePath).size()>0){
        QSqlQuery tables(db_);tables.prepare("SELECT COUNT(*) FROM sqlite_master WHERE type='table'");checked(tables);tables.next();const int count=tables.value(0).toInt();tables.finish();
        if(count>0)backupTo(filePath+".pre-v7-"+utcNow().toString("yyyyMMdd-HHmmsszzz")+".sqlite");
    }
    if(!db_.transaction())throw std::runtime_error("Cannot start schema migration");
    try{
    exec("CREATE TABLE IF NOT EXISTS profile(key TEXT PRIMARY KEY, value TEXT NOT NULL)");
    exec("CREATE TABLE IF NOT EXISTS memories(id INTEGER PRIMARY KEY, content TEXT NOT NULL UNIQUE, created_at TEXT NOT NULL)");
    exec("CREATE TABLE IF NOT EXISTS messages(id INTEGER PRIMARY KEY, role TEXT NOT NULL CHECK(role IN ('user','assistant')), content TEXT NOT NULL, created_at TEXT NOT NULL)");
    exec("CREATE TABLE IF NOT EXISTS relation(id INTEGER PRIMARY KEY CHECK(id=1), exchanges INTEGER NOT NULL DEFAULT 0)");
    exec("INSERT OR IGNORE INTO relation(id,exchanges) VALUES(1,0)");
    QSqlQuery columns(db_); columns.prepare("PRAGMA table_info(messages)"); checked(columns);
    bool hasSource=false;
    while(columns.next()) if(columns.value(1).toString()=="source") hasSource=true;
    columns.finish();
    if(!hasSource) exec("ALTER TABLE messages ADD COLUMN source TEXT NOT NULL DEFAULT 'offline'");
    QSqlQuery metadata(db_);metadata.prepare("PRAGMA table_info(memories)");checked(metadata);
    QSet<QString> names;while(metadata.next()) names.insert(metadata.value(1).toString());metadata.finish();
    for(const auto &definition:QStringList{"tags TEXT NOT NULL DEFAULT ''","importance REAL NOT NULL DEFAULT 0.5","expires_at TEXT","last_used_at TEXT"})
        if(!names.contains(definition.section(' ',0,0))) exec("ALTER TABLE memories ADD COLUMN "+definition);
    exec("CREATE TABLE IF NOT EXISTS companion_life(id INTEGER PRIMARY KEY CHECK(id=1),state TEXT NOT NULL)");
    exec("CREATE TABLE IF NOT EXISTS memory_suggestions(id INTEGER PRIMARY KEY,content TEXT NOT NULL UNIQUE,status TEXT NOT NULL DEFAULT 'pending',created_at TEXT NOT NULL)");
    exec("CREATE TABLE IF NOT EXISTS interaction_state(id INTEGER PRIMARY KEY CHECK(id=1),state TEXT NOT NULL)");
    for(const auto &definition:QStringList{"layer TEXT NOT NULL DEFAULT 'semantic'","source TEXT NOT NULL DEFAULT 'legacy'","updated_at TEXT NOT NULL DEFAULT ''","proactive_allowed INTEGER NOT NULL DEFAULT 1"})
        if(!names.contains(definition.section(' ',0,0)))exec("ALTER TABLE memories ADD COLUMN "+definition);
    exec("UPDATE memories SET updated_at=created_at WHERE updated_at=''");
    exec("CREATE TABLE IF NOT EXISTS memory_vectors(memory_id INTEGER PRIMARY KEY REFERENCES memories(id) ON DELETE CASCADE,model TEXT NOT NULL,fingerprint TEXT NOT NULL,vector BLOB NOT NULL,dimensions INTEGER NOT NULL,updated_at TEXT NOT NULL)");
    exec("CREATE TABLE IF NOT EXISTS proactive_memory_use(memory_id INTEGER PRIMARY KEY REFERENCES memories(id) ON DELETE CASCADE,last_prompt TEXT NOT NULL)");
    exec("CREATE TRIGGER IF NOT EXISTS memory_vector_invalidate AFTER UPDATE OF content,tags,layer ON memories BEGIN DELETE FROM memory_vectors WHERE memory_id=NEW.id; UPDATE memories SET updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=NEW.id; END");
    exec("CREATE TRIGGER IF NOT EXISTS memory_vector_delete AFTER DELETE ON memories BEGIN DELETE FROM memory_vectors WHERE memory_id=OLD.id; END");
    exec("CREATE TABLE IF NOT EXISTS dialogue_collection(id TEXT PRIMARY KEY,unlocked_at TEXT,discovered_at TEXT,first_heard_at TEXT,last_played_at TEXT,play_count INTEGER NOT NULL DEFAULT 0,legacy INTEGER NOT NULL DEFAULT 0,unlock_inferred INTEGER NOT NULL DEFAULT 0)");
    QSqlQuery collectionColumns(db_);collectionColumns.exec("PRAGMA table_info(dialogue_collection)");bool hasInferred=false;
    while(collectionColumns.next())if(collectionColumns.value(1).toString()=="unlock_inferred")hasInferred=true;collectionColumns.finish();
    if(!hasInferred)exec("ALTER TABLE dialogue_collection ADD COLUMN unlock_inferred INTEGER NOT NULL DEFAULT 0");
    // Old candidate lookup inserted null-valued `used` keys. They are not
    // evidence of presentation; normalize any early v6 imports idempotently.
    exec("UPDATE dialogue_collection SET discovered_at=NULL,last_played_at=NULL,play_count=0,legacy=0 WHERE legacy=1 AND COALESCE(discovered_at,'')='' AND play_count<=1 AND COALESCE(first_heard_at,'')=''");
    exec("CREATE TABLE IF NOT EXISTS bond_events(id TEXT PRIMARY KEY,created_at TEXT NOT NULL)");
    if(previous<6){
            const auto used=dialogueHistory()["used"].toObject();
            for(auto it=used.begin();it!=used.end();++it)if(!DialogueEngine::find(it.key()).isEmpty() && QDateTime::fromString(it.value().toString(),Qt::ISODateWithMs).isValid()){
                QSqlQuery q(db_);q.prepare("INSERT OR IGNORE INTO dialogue_collection(id,unlocked_at,discovered_at,last_played_at,play_count,legacy,unlock_inferred) VALUES(?,?,?,?,1,1,1)");
                q.addBindValue(it.key());q.addBindValue(utcNow().toString(Qt::ISODateWithMs));q.addBindValue(it.value().toString());q.addBindValue(it.value().toString());checked(q);
            }
            syncCollection(utcNow(),existing);
    }
    exec("CREATE TABLE IF NOT EXISTS api_usage(id INTEGER PRIMARY KEY,source TEXT NOT NULL,input_tokens INTEGER,output_tokens INTEGER,created_at TEXT NOT NULL)");
    QSqlQuery wellnessTable(db_);wellnessTable.prepare("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='wellness_state'");checked(wellnessTable);wellnessTable.next();const bool hasWellness=wellnessTable.value(0).toInt()>0;wellnessTable.finish();
    exec("CREATE TABLE IF NOT EXISTS wellness_state(id INTEGER PRIMARY KEY CHECK(id=1),json TEXT NOT NULL)");
    exec("CREATE TABLE IF NOT EXISTS focus_sessions(id TEXT PRIMARY KEY,json TEXT NOT NULL,reward_day TEXT,reward INTEGER NOT NULL DEFAULT 0)");
    if(!hasWellness)syncCollection(utcNow());
    pruneExpired();exec("PRAGMA user_version=7");
    if(integrity()!="ok")throw std::runtime_error("Migration integrity check failed");
    if(!db_.commit())throw std::runtime_error("Schema migration commit failed");
    }catch(...){db_.rollback();throw;}
    }catch(...){db_.close();db_={};QSqlDatabase::removeDatabase(connectionName_);throw;}
}
MemoryStore::~MemoryStore() {
    db_.close();
    db_ = {};
    QSqlDatabase::removeDatabase(connectionName_);
}
void MemoryStore::exec(const QString &sql) const {
    QSqlQuery q(db_); q.prepare(sql); checked(q);
}
QString MemoryStore::integrity() const{
    QSqlQuery q(db_);q.prepare("PRAGMA integrity_check");checked(q);return q.next()?q.value(0).toString():QString{};
}
void MemoryStore::backupTo(const QString &path) const{
    if(QFileInfo::exists(path))throw std::runtime_error("Backup destination already exists");
    QSqlQuery q(db_);q.prepare("VACUUM INTO ?");q.addBindValue(QFileInfo(path).absoluteFilePath());checked(q);
}
void MemoryStore::restoreBackup(const QString &backup,const QString &destination){
    // Caller must hold the application's data-directory lock, before opening stores.
    if(QFileInfo(backup).canonicalFilePath()==QFileInfo(destination).canonicalFilePath())throw std::runtime_error("Backup and destination must differ");
    const auto name=QUuid::createUuid().toString();const auto temporary=destination+".restore-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    try{
        {
            auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setConnectOptions("QSQLITE_OPEN_READONLY");db.setDatabaseName(backup);
            if(!db.open())throw std::runtime_error("Cannot read backup");
            QSqlQuery q(db);q.prepare("PRAGMA integrity_check");checked(q);if(!q.next() || q.value(0).toString()!="ok")throw std::runtime_error("Backup integrity check failed");q.finish();
            q.prepare("PRAGMA user_version");checked(q);q.next();if(q.value(0).toInt()>7)throw std::runtime_error("Backup requires a newer application");q.finish();
            q.prepare("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN ('memories','messages','profile','relation')");checked(q);q.next();if(q.value(0).toInt()!=4)throw std::runtime_error("Not a DragonGPT backup");q.finish();
            q.prepare("VACUUM INTO ?");q.addBindValue(temporary);checked(q);q.finish();db.close();
        }
        QSqlDatabase::removeDatabase(name);
        if(QFileInfo::exists(destination)){
            MemoryStore current(destination);current.backupTo(destination+".before-restore-"+utcNow().toString("yyyyMMdd-HHmmsszzz")+".sqlite");
        }
        QFile source(temporary);QSaveFile output(destination);if(!source.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot stage database restore");
        while(!source.atEnd()){const auto chunk=source.read(1024*1024);if(chunk.isEmpty() || output.write(chunk)!=chunk.size())throw std::runtime_error("Database restore copy failed");}
        if(!output.commit())throw std::runtime_error("Database restore commit failed");source.close();QFile::remove(temporary);
    }catch(...){QSqlDatabase::removeDatabase(name);QFile::remove(temporary);throw;}
}
void MemoryStore::clearMemories(){
    if(!db_.transaction())throw std::runtime_error("Cannot begin memory clear");
    try{exec("DELETE FROM memories");exec("UPDATE memory_suggestions SET status='dismissed' WHERE status='pending'");if(!db_.commit())throw std::runtime_error("Memory clear commit failed");}catch(...){db_.rollback();throw;}
}
void MemoryStore::setMemoryMetadata(qint64 id,const QString &layer,const QString &source,bool allowed){
    if(!QStringList{"semantic","episodic","relationship"}.contains(layer) || source.size()>120)throw std::runtime_error("Invalid memory metadata");
    QSqlQuery q(db_);q.prepare("UPDATE memories SET layer=?,source=?,proactive_allowed=?,updated_at=? WHERE id=?");
    q.addBindValue(layer);q.addBindValue(source.isNull()?QString(""):source);q.addBindValue(allowed);q.addBindValue(utcNow().toString(Qt::ISODateWithMs));q.addBindValue(id);checked(q);
}
QVector<Memory> MemoryStore::byIds(const QVector<qint64> &ids,const QString &layer) const{
    QVector<Memory> result;QSqlQuery q(db_);
    q.prepare("SELECT id,content,created_at,tags,importance,expires_at,last_used_at,layer,source,updated_at,proactive_allowed FROM memories WHERE id=? AND (expires_at IS NULL OR expires_at='' OR expires_at>?) AND (?='' OR layer=?)");
    const auto now=utcNow().toString(Qt::ISODateWithMs);
    for(const auto id:ids){q.bindValue(0,id);q.bindValue(1,now);q.bindValue(2,layer.isNull()?QString(""):layer);q.bindValue(3,layer.isNull()?QString(""):layer);checked(q);if(q.next()){const auto m=memoryRow(q);if(!looksLikeCredential(m.content))result.append(m);}q.finish();}
    return result;
}
QString MemoryStore::nickname() const {
    QSqlQuery q(db_); q.prepare("SELECT value FROM profile WHERE key='nickname'"); checked(q);
    return q.next() ? q.value(0).toString() : QString{};
}
void MemoryStore::setNickname(const QString &name) {
    QSqlQuery q(db_);
    q.prepare("INSERT INTO profile(key,value) VALUES('nickname',?) ON CONFLICT(key) DO UPDATE SET value=excluded.value");
    q.addBindValue(clean(name, 30)); checked(q);
}
qint64 MemoryStore::remember(const QString &content) {
    pruneExpired();
    const auto value = clean(content, 500);
    if(looksLikeCredential(value))throw std::runtime_error("Credentials must be saved in connection settings, not memories");
    if (value.isEmpty()) return 0;
    for(const auto &m:memories())if(canonicalMemory(m.content)==canonicalMemory(value))return m.id;
    QSqlQuery q(db_);
    q.prepare("INSERT OR IGNORE INTO memories(content,created_at,updated_at,source) VALUES(?,?,?,'manual')");
    q.addBindValue(value);const auto now=utcNow().toString(Qt::ISODateWithMs);q.addBindValue(now);q.addBindValue(now);checked(q);
    q.prepare("SELECT id FROM memories WHERE content=?"); q.addBindValue(value); checked(q);
    const auto id=q.next()?q.value(0).toLongLong():0;q.finish();
    if(id>0){q.prepare("UPDATE memory_suggestions SET status='accepted' WHERE content=? AND status='pending'");q.addBindValue(value);checked(q);}
    return id;
}
QVector<Memory> MemoryStore::memories() const {
    QSqlQuery q(db_); q.prepare("SELECT id,content,created_at,tags,importance,expires_at,last_used_at,layer,source,updated_at,proactive_allowed FROM memories WHERE expires_at IS NULL OR expires_at='' OR expires_at>? ORDER BY id DESC");
    q.addBindValue(utcNow().toString(Qt::ISODateWithMs));checked(q);
    QVector<Memory> result;
    while (q.next()) result.push_back(memoryRow(q));
    return result;
}
QVector<Memory> MemoryStore::relevant(const QString &query, int limit) const {
    if (limit <= 0 || query.trimmed().isEmpty()) return {};
    // Character bigrams work for Chinese without an external tokenizer.
    const auto lower = query.toLower();
    QVector<QPair<int,Memory>> ranked;
    for (const auto &m : memories()) {
        if(looksLikeCredential(m.content))continue;
        int score = 0;
        const auto content = (m.content+" "+m.tags).toLower();
        for (qsizetype i = 0; i + 1 < lower.size(); ++i) {
            const auto gram = lower.mid(i, 2);
            if (!gram.at(0).isSpace() && !gram.at(1).isSpace() && content.contains(gram)) ++score;
        }
        if (score > 0) ranked.push_back({score*10+qRound(m.importance*5.0)-qMin(5,int(qMax<qint64>(0,m.updatedAt.isValid()?m.updatedAt.daysTo(utcNow()):m.createdAt.daysTo(utcNow()))/90)),m});
    }
    std::stable_sort(ranked.begin(),ranked.end(),[](const auto &a,const auto &b){return a.first>b.first;});
    QVector<Memory> result;
    for (const auto &entry : ranked) {
        if (result.size() >= limit) break;
        result.push_back(entry.second);
        QSqlQuery touch(db_);touch.prepare("UPDATE memories SET last_used_at=? WHERE id=?");
        touch.addBindValue(utcNow().toString(Qt::ISODateWithMs));touch.addBindValue(entry.second.id);checked(touch);
    }
    return result;
}
void MemoryStore::recordApiUsage(const QString &source,const QJsonObject &usage) {
    if(!usage.value("input_tokens").isDouble() || !usage.value("output_tokens").isDouble())return;
    QSqlQuery q(db_);q.prepare("INSERT INTO api_usage(source,input_tokens,output_tokens,created_at) VALUES(?,?,?,?)");
    q.addBindValue(source);q.addBindValue(usage.value("input_tokens").toInteger());q.addBindValue(usage.value("output_tokens").toInteger());q.addBindValue(utcNow().toString(Qt::ISODateWithMs));checked(q);
}
QJsonObject MemoryStore::usageSummary() const {
    QSqlQuery q(db_);q.prepare("SELECT COUNT(*),COALESCE(SUM(input_tokens),0),COALESCE(SUM(output_tokens),0) FROM api_usage");checked(q);q.next();
    return {{"requests",q.value(0).toInt()},{"input_tokens",q.value(1).toLongLong()},{"output_tokens",q.value(2).toLongLong()}};
}
void MemoryStore::forget(qint64 id) {
    QSqlQuery q(db_); q.prepare("DELETE FROM memories WHERE id=?"); q.addBindValue(id); checked(q);
}
void MemoryStore::updateMemory(qint64 id,const QString &content,const QString &tags,double importance,QDateTime expiresAt) {
    if(looksLikeCredential(content))throw std::runtime_error("Credentials cannot be stored as memories");
    if(content.trimmed().isEmpty() || !std::isfinite(importance) || importance<0 || importance>1)
        throw std::runtime_error("Invalid memory content or importance");
    QSqlQuery q(db_);q.prepare("UPDATE memories SET content=?,tags=?,importance=?,expires_at=? WHERE id=?");
    q.addBindValue(content.trimmed().left(500));q.addBindValue(tags.left(120).isNull()?QString(""):tags.left(120));q.addBindValue(importance);
    q.addBindValue(expiresAt.isValid()?expiresAt.toUTC().toString(Qt::ISODateWithMs):QString(""));q.addBindValue(id);checked(q);
}
int MemoryStore::pruneExpired(QDateTime now) {
    QSqlQuery q(db_);q.prepare("DELETE FROM memories WHERE expires_at IS NOT NULL AND expires_at<>'' AND expires_at<=?");
    q.addBindValue(now.toUTC().toString(Qt::ISODateWithMs));checked(q);return q.numRowsAffected();
}
QByteArray MemoryStore::exportMemories() const {
    QJsonArray rows;
    for(const auto &m:memories()) rows.append(QJsonObject{{"content",m.content},{"tags",m.tags},{"importance",m.importance},
        {"created_at",m.createdAt.toUTC().toString(Qt::ISODateWithMs)},{"expires_at",m.expiresAt.toUTC().toString(Qt::ISODateWithMs)},
        {"layer",m.layer},{"source",m.source},{"updated_at",m.updatedAt.toUTC().toString(Qt::ISODateWithMs)},{"proactive_allowed",m.proactiveAllowed}});
    return QJsonDocument(QJsonObject{{"format","DragonGPT memories"},{"version",2},{"nickname",nickname()},{"memories",rows}}).toJson(QJsonDocument::Indented);
}
int MemoryStore::importMemories(const QByteArray &json) {
    if(json.size()>2*1024*1024) throw std::runtime_error("Memory file exceeds 2 MB");
    QJsonParseError error;const auto doc=QJsonDocument::fromJson(json,&error);const auto root=doc.object();
    if(error.error!=QJsonParseError::NoError || root.value("format")!="DragonGPT memories" || !QList<int>{1,2}.contains(root.value("version").toInt()) || !root.value("memories").isArray())
        throw std::runtime_error("Unsupported memory file format");
    const auto rows=root.value("memories").toArray();if(rows.size()>1000) throw std::runtime_error("Memory import exceeds 1000 rows");
    if(!db_.transaction()) throw std::runtime_error("Cannot begin memory import");
    int added=0;
    try {
        QSet<QString> known;for(const auto &m:memories())known.insert(canonicalMemory(m.content));
        for(const auto &row:rows) {
            const auto m=row.toObject();const auto text=m.value("content").toString().trimmed();const auto tags=m.value("tags").toString();
            const auto importance=m.value("importance").toDouble(0.5);const auto date=m.value("expires_at").toString();
            const auto expiry=QDateTime::fromString(date,Qt::ISODateWithMs);
            if(text.isEmpty() || text.size()>500 || tags.size()>120 || !std::isfinite(importance) || importance<0 || importance>1 || (!date.isEmpty() && !expiry.isValid()))
                throw std::runtime_error("Invalid memory row; import cancelled");
            if(expiry.isValid() && expiry<=utcNow()) continue;
            if(known.contains(canonicalMemory(text)))continue;
            const auto id=remember(text);updateMemory(id,text,tags,importance,expiry);
            setMemoryMetadata(id,m.value("layer").toString("semantic"),m.value("source").toString("imported"),m.value("proactive_allowed").toBool(true));
            const auto created=QDateTime::fromString(m.value("created_at").toString(),Qt::ISODateWithMs);
            if(created.isValid()){QSqlQuery stamp(db_);stamp.prepare("UPDATE memories SET created_at=? WHERE id=?");stamp.addBindValue(created.toUTC().toString(Qt::ISODateWithMs));stamp.addBindValue(id);checked(stamp);}known.insert(canonicalMemory(text));++added;
        }
        if(nickname().isEmpty() && root.value("nickname").isString()) setNickname(root.value("nickname").toString().left(30));
        if(!db_.commit()) throw std::runtime_error("Memory import commit failed");
    }catch(...){db_.rollback();throw;}
    return added;
}
void MemoryStore::appendExchange(const QString &user, const QString &assistant, const QString &source,QDateTime now) {
    if (!db_.transaction()) throw std::runtime_error(db_.lastError().text().toStdString());
    try {
        for (const auto &m : QVector<Message>{{"user",user},{"assistant",assistant}}) {
            QSqlQuery q(db_); q.prepare("INSERT INTO messages(role,content,created_at,source) VALUES(?,?,?,?)");
            q.addBindValue(m.role); q.addBindValue(m.content.left(16000));
            q.addBindValue(now.toUTC().toString(Qt::ISODateWithMs)); q.addBindValue(source); checked(q);
        }
        exec("UPDATE relation SET exchanges=exchanges+1 WHERE id=1");
        auto state=life();state.interact(user,now);saveLife(state);syncCollection(now);
        // Bound local chat retention; explicit memories are independently retained.
        exec("DELETE FROM messages WHERE id NOT IN (SELECT id FROM messages ORDER BY id DESC LIMIT 200)");
        if (!db_.commit()) throw std::runtime_error(db_.lastError().text().toStdString());
    } catch (...) { db_.rollback(); throw; }
}
QVector<Message> MemoryStore::recentMessages(int limit) const {
    QSqlQuery q(db_); q.prepare("SELECT role,content,source FROM (SELECT id,role,content,source FROM messages ORDER BY id DESC LIMIT ?) ORDER BY id");
    q.addBindValue(std::clamp(limit,0,200)); checked(q);
    QVector<Message> out;
    while(q.next()) out.push_back({q.value(0).toString(),q.value(1).toString(),q.value(2).toString()});
    return out;
}
int MemoryStore::exchanges() const {
    QSqlQuery q(db_); q.prepare("SELECT exchanges FROM relation WHERE id=1"); checked(q);
    return q.next() ? q.value(0).toInt() : 0;
}
QString MemoryStore::relationship() const {
    return life().stage();
}
LifeState MemoryStore::life() const {
    QSqlQuery q(db_);q.prepare("SELECT state FROM companion_life WHERE id=1");checked(q);
    if(q.next())return LifeState::fromJson(QJsonDocument::fromJson(q.value(0).toByteArray()).object());
    // Preserve the old relationship stage without inventing dated interactions.
    LifeState s;const int n=exchanges();
    if(n>=30){s.experience=200;s.legacyStage=2;}
    else if(n>=10){s.experience=60;s.legacyStage=1;}
    return s;
}
void MemoryStore::saveLife(const LifeState &s) {
    QSqlQuery q(db_);q.prepare("INSERT INTO companion_life(id,state) VALUES(1,?) ON CONFLICT(id) DO UPDATE SET state=excluded.state");
    q.addBindValue(QString::fromUtf8(QJsonDocument(s.json()).toJson(QJsonDocument::Compact)));checked(q);
}
void MemoryStore::appendProactive(const QString &text,QDateTime now) {
    if(text.trimmed().isEmpty())return;
    if(!db_.transaction())throw std::runtime_error("Cannot begin proactive write");
    try{
        QSqlQuery q(db_);q.prepare("INSERT INTO messages(role,content,created_at,source) VALUES('assistant',?,?,'proactive-local')");
        q.addBindValue(text.left(2000));q.addBindValue(now.toUTC().toString(Qt::ISODateWithMs));checked(q);
        auto state=life();const auto date=now.toLocalTime().date();
        if(!state.proactiveDate.isValid() || date>state.proactiveDate){state.proactiveDate=date;state.proactiveCount=0;}
        if(state.awaitingReply)state.ignoredOpenings=qMin(10,state.ignoredOpenings+1);++state.proactiveCount;++state.topic;state.lastProactive=now.toUTC();state.awaitingReply=true;saveLife(state);
        exec("DELETE FROM messages WHERE id NOT IN (SELECT id FROM messages ORDER BY id DESC LIMIT 200)");
        if(!db_.commit())throw std::runtime_error("Proactive commit failed");
    }catch(...){db_.rollback();throw;}
}
void MemoryStore::appendNotice(const QString &text){
    QSqlQuery q(db_);q.prepare("INSERT INTO messages(role,content,created_at,source) VALUES('assistant',?,?,'local-reminder')");q.addBindValue(text.left(2000));q.addBindValue(utcNow().toString(Qt::ISODateWithMs));checked(q);
    exec("DELETE FROM messages WHERE id NOT IN (SELECT id FROM messages ORDER BY id DESC LIMIT 200)");
}
QVector<Memory> MemoryStore::suggestions() const {
    QSqlQuery q(db_);q.prepare("SELECT id,content,created_at FROM memory_suggestions WHERE status='pending' ORDER BY id DESC LIMIT 20");checked(q);
    QVector<Memory> rows;while(q.next())rows.append({q.value(0).toLongLong(),q.value(1).toString(),QDateTime::fromString(q.value(2).toString(),Qt::ISODateWithMs),CompanionLife::tags(q.value(1).toString()),0.7,{},{}});return rows;
}
void MemoryStore::suggest(const QString &text) {
    const auto value=CompanionLife::candidate(text);if(value.isEmpty())return;
    for(const auto &m:memories())if(canonicalMemory(m.content)==canonicalMemory(value))return;
    for(const auto &m:suggestions())if(canonicalMemory(m.content)==canonicalMemory(value))return;
    QSqlQuery q(db_);q.prepare("INSERT OR IGNORE INTO memory_suggestions(content,created_at) SELECT ?,? WHERE NOT EXISTS(SELECT 1 FROM memories WHERE content=?) AND (SELECT COUNT(*) FROM memory_suggestions WHERE status='pending')<20");
    q.addBindValue(value);q.addBindValue(utcNow().toString(Qt::ISODateWithMs));q.addBindValue(value);checked(q);
}
QVector<Memory> MemoryStore::conflicts(const QString &content) const{
    QVector<Memory> result;const auto value=canonicalMemory(content);
    QString opposite;if(value.startsWith("我不喜欢"))opposite="我喜欢"+value.mid(4);else if(value.startsWith("我喜欢"))opposite="我不喜欢"+value.mid(3);
    if(!opposite.isEmpty())for(const auto &m:memories())if(canonicalMemory(m.content)==opposite)result.append(m);return result;
}
void MemoryStore::resolveSuggestion(qint64 id,bool accept,bool replaceConflict) {
    if(!db_.transaction())throw std::runtime_error("Cannot begin memory confirmation");
    try{
        QSqlQuery q(db_);q.prepare("SELECT content FROM memory_suggestions WHERE id=? AND status='pending'");q.addBindValue(id);checked(q);
        if(q.next()){
            const auto text=q.value(0).toString();q.finish();
            if(accept){const auto old=conflicts(text);if(!old.isEmpty() && !replaceConflict)throw std::runtime_error("Conflicting preference requires explicit replacement confirmation");
                qint64 memoryId=0;if(!old.isEmpty()){memoryId=old.first().id;updateMemory(memoryId,text,CompanionLife::tags(text),.7);}else memoryId=remember(text);updateMemory(memoryId,text,CompanionLife::tags(text),0.7);const bool episode=QRegularExpression("^我(?:最近)?(?:完成了|遇到了)").match(text).hasMatch();setMemoryMetadata(memoryId,episode?"episodic":"semantic","confirmed-conversation");}
            q.prepare("UPDATE memory_suggestions SET status=? WHERE id=?");q.addBindValue(accept?"accepted":"dismissed");q.addBindValue(id);checked(q);
        }
        if(!db_.commit())throw std::runtime_error("Memory confirmation failed");
    }catch(...){db_.rollback();throw;}
}
QVector<Memory> MemoryStore::interactionMemories() const{
    QVector<Memory> rows;
    const QRegularExpression privateContent("密码|密钥|银行卡|身份证|住址|电话|sk-|不要提|别再提|不再使用|不要引用",QRegularExpression::CaseInsensitiveOption);
    for(const auto &m:memories()){
        const bool topic=m.tags.contains("偏好") || m.tags.contains("话题") || m.tags.contains("兴趣");
        if(m.proactiveAllowed && topic && !privateContent.match(m.content).hasMatch())rows.append(m);
    }
    return rows;
}
QVector<Memory> MemoryStore::proactiveMemories(QDateTime now) const{
    QVector<Memory> result;const auto cutoff=now.addDays(-3);QSqlQuery q(db_);q.prepare("SELECT memory_id,last_prompt FROM proactive_memory_use");checked(q);QSet<qint64> blocked;
    while(q.next())if(QDateTime::fromString(q.value(1).toString(),Qt::ISODateWithMs)>cutoff)blocked.insert(q.value(0).toLongLong());
    for(const auto &m:interactionMemories())if(!blocked.contains(m.id))result.append(m);return result;
}
DialogueSelection MemoryStore::memoryFollowup(QDateTime now){
    if(life().awaitingReply)return {};
    QSqlQuery used(db_);used.prepare("SELECT MAX(last_prompt) FROM proactive_memory_use");checked(used);used.next();const auto last=QDateTime::fromString(used.value(0).toString(),Qt::ISODateWithMs);used.finish();
    if(last.isValid() && last.toLocalTime().date()>=now.toLocalTime().date())return {};
    for(const auto &m:proactiveMemories(now)){
        if(m.layer!="semantic" || m.createdAt.secsTo(now)<86400 || m.createdAt.daysTo(now)>90 || looksLikeCredential(m.content))continue;
        if(!QRegularExpression("^我(?:最近)?(?:开始|正在|在)?(?:学习|学|阅读)").match(m.content).hasMatch())continue;
        QString topic;
        if(m.content.contains("多线程") || m.content.contains("线程池"))topic="多线程学习";
        else if(m.content.contains("C++",Qt::CaseInsensitive))topic="C++ 学习";
        else if(m.content.contains("高数") || m.content.contains("高等数学"))topic="高等数学";
        else if(m.content.contains("英语"))topic="英语学习";
        else if(m.content.contains("文学"))topic="文学阅读";
        if(topic.isEmpty())continue;
        // Deliberately use a short, known topic label instead of quoting private text.
        const auto text=QString("上次提到的%1，最近进展怎么样？愿意的话可以和我聊聊哦。").arg(topic);
        used.prepare("INSERT INTO proactive_memory_use VALUES(?,?) ON CONFLICT(memory_id) DO UPDATE SET last_prompt=excluded.last_prompt");used.addBindValue(m.id);used.addBindValue(now.toUTC().toString(Qt::ISODateWithMs));checked(used);
        appendProactive(text,now);return {"memory.followup",text,"memory-followup",false,0,m.id};
    }
    return {};
}
QJsonObject MemoryStore::dialogueHistory() const {
    QSqlQuery q(db_);q.prepare("SELECT state FROM interaction_state WHERE id=1");checked(q);
    return q.next()?QJsonDocument::fromJson(q.value(0).toByteArray()).object():QJsonObject{};
}
int MemoryStore::syncCollection(QDateTime now,bool inferred){
    const auto state=life();int added=0;
    for(const auto &v:DialogueEngine::catalog()){
        const auto l=v.toObject();if(state.growthPercent()<DialogueEngine::requiredAffinity(l))continue;
        QSqlQuery q(db_);q.prepare("INSERT OR IGNORE INTO dialogue_collection(id,unlocked_at,unlock_inferred) VALUES(?,?,?)");
        q.addBindValue(l["id"].toString());q.addBindValue(now.toUTC().toString(Qt::ISODateWithMs));q.addBindValue(inferred);checked(q);added+=std::max(0,q.numRowsAffected());
    }
    for(int stage=1;stage<=state.stageIndex();++stage){
        QSqlQuery q(db_);q.prepare("INSERT OR IGNORE INTO bond_events(id,created_at) VALUES(?,?)");q.addBindValue(QString("stage-%1").arg(stage));q.addBindValue(now.toUTC().toString(Qt::ISODateWithMs));checked(q);
    }
    return added;
}
int MemoryStore::unlockedEvents() const{QSqlQuery q(db_);q.prepare("SELECT COUNT(*) FROM bond_events");checked(q);q.next();return q.value(0).toInt();}
QJsonArray MemoryStore::collection() const{
    QHash<QString,QJsonObject> records;QSqlQuery q(db_);
    q.prepare("SELECT id,unlocked_at,discovered_at,first_heard_at,last_played_at,play_count,legacy,unlock_inferred FROM dialogue_collection");checked(q);
    while(q.next())records.insert(q.value(0).toString(),QJsonObject{{"unlocked_at",q.value(1).toString()},{"discovered_at",q.value(2).toString()},
        {"first_heard_at",q.value(3).toString()},{"last_played_at",q.value(4).toString()},{"play_count",q.value(5).toInt()},{"legacy",q.value(6).toBool()},{"unlock_inferred",q.value(7).toBool()}});
    QJsonArray result;const int percent=life().growthPercent();
    for(const auto &v:DialogueEngine::catalog()){
        auto l=v.toObject();const auto saved=records.value(l["id"].toString());for(auto it=saved.begin();it!=saved.end();++it)l[it.key()]=it.value();
        const int required=DialogueEngine::requiredAffinity(l);l["required_affinity"]=required;l["category"]=DialogueEngine::category(l);
        l["relation_stage"]=required>=80?3:required>=50?2:required>=20?1:0;
        l["unlocked"]=percent>=required || !l["unlocked_at"].toString().isEmpty();l["discovered"]=!l["discovered_at"].toString().isEmpty();
        result.append(l);
    }return result;
}
void MemoryStore::recordPresented(const QString &id,QDateTime now){
    const auto l=DialogueEngine::find(id);if(l.isEmpty())return;
    QSqlQuery q(db_);q.prepare("UPDATE dialogue_collection SET discovered_at=COALESCE(NULLIF(discovered_at,''),?),last_played_at=?,play_count=play_count+1 WHERE id=?");
    q.addBindValue(now.toUTC().toString(Qt::ISODateWithMs));q.addBindValue(now.toUTC().toString(Qt::ISODateWithMs));q.addBindValue(id);checked(q);
}
void MemoryStore::recordHeard(const QString &id,QDateTime now){
    QSqlQuery q(db_);q.prepare("UPDATE dialogue_collection SET first_heard_at=COALESCE(first_heard_at,?) WHERE id=? AND discovered_at IS NOT NULL");q.addBindValue(now.toUTC().toString(Qt::ISODateWithMs));q.addBindValue(id);checked(q);
}
bool MemoryStore::awardInteraction(const QString &kind,const QString &text,QDateTime now,const QString &eventId){
    if(!db_.transaction())throw std::runtime_error("Cannot begin growth award");
    try{auto state=life();const bool changed=state.award(kind,text,now,eventId);saveLife(state);syncCollection(now);
        if(!db_.commit())throw std::runtime_error("Growth commit failed");return changed;
    }catch(...){db_.rollback();throw;}
}
DialogueSelection MemoryStore::selectDialogue(DialogueContext context,quint32 seed){
    if(!db_.transaction())throw std::runtime_error("Cannot begin interaction selection");
    try{
        auto history=dialogueHistory();
        const auto last=QDateTime::fromString(history["last_click"].toString(),Qt::ISODateWithMs);
        if(!context.proactive && last.isValid() && last.msecsTo(context.now)<1100){db_.rollback();return {};}
        const int burst=last.isValid() && last.secsTo(context.now)<8?history["burst"].toInt()+1:1;
        context.rapid=!context.proactive && burst>=3;context.memories.clear();
        auto selected=DialogueEngine::select(context,history,seed);
        if(selected){
            if(!context.proactive){history["last_click"]=context.now.toUTC().toString(Qt::ISODateWithMs);history["burst"]=std::min(burst,100);}
            QSqlQuery q(db_);q.prepare("INSERT INTO interaction_state(id,state) VALUES(1,?) ON CONFLICT(id) DO UPDATE SET state=excluded.state");
            q.addBindValue(QString::fromUtf8(QJsonDocument(history).toJson(QJsonDocument::Compact)));checked(q);
            if(context.proactive){
                if(selected.memoryId>0){QSqlQuery used(db_);used.prepare("INSERT INTO proactive_memory_use VALUES(?,?) ON CONFLICT(memory_id) DO UPDATE SET last_prompt=excluded.last_prompt");used.addBindValue(selected.memoryId);used.addBindValue(context.now.toUTC().toString(Qt::ISODateWithMs));checked(used);}
                QSqlQuery msg(db_);msg.prepare("INSERT INTO messages(role,content,created_at,source) VALUES('assistant',?,?,'proactive-local')");
                msg.addBindValue(selected.text);msg.addBindValue(context.now.toUTC().toString(Qt::ISODateWithMs));checked(msg);
                auto lifeState=life();const auto date=context.now.toLocalTime().date();
                if(!lifeState.proactiveDate.isValid() || date>lifeState.proactiveDate){lifeState.proactiveDate=date;lifeState.proactiveCount=0;}
                if(lifeState.awaitingReply)lifeState.ignoredOpenings=qMin(10,lifeState.ignoredOpenings+1);++lifeState.proactiveCount;++lifeState.topic;lifeState.lastProactive=context.now.toUTC();lifeState.awaitingReply=true;saveLife(lifeState);
                exec("DELETE FROM messages WHERE id NOT IN (SELECT id FROM messages ORDER BY id DESC LIMIT 200)");
            }
        }
        if(!db_.commit())throw std::runtime_error("Interaction commit failed");
        return selected;
    }catch(...){db_.rollback();throw;}
}
void MemoryStore::clearAll() {
    if (!db_.transaction()) throw std::runtime_error(db_.lastError().text().toStdString());
    try {
        exec("DELETE FROM profile"); exec("DELETE FROM memories"); exec("DELETE FROM messages");
        exec("UPDATE relation SET exchanges=0 WHERE id=1");
        exec("DELETE FROM api_usage");
        exec("DELETE FROM interaction_state");exec("DELETE FROM companion_life");exec("DELETE FROM memory_suggestions");
        exec("DELETE FROM dialogue_collection");exec("DELETE FROM bond_events");syncCollection();
        if (!db_.commit()) throw std::runtime_error(db_.lastError().text().toStdString());
        exec("VACUUM");
    } catch (...) { db_.rollback(); throw; }
}
}

namespace dragon {
QJsonObject MemoryStore::wellnessState() const {
    QSqlQuery q(db_);q.prepare("SELECT json FROM wellness_state WHERE id=1");checked(q);
    return q.next()?QJsonDocument::fromJson(q.value(0).toByteArray()).object():QJsonObject{};
}
void MemoryStore::saveWellness(const QJsonObject &state,const QJsonObject &session) {
    if(!db_.transaction())throw std::runtime_error("Cannot begin wellness transaction");
    try {
        QSqlQuery q(db_);
        if(!session.isEmpty()){
            const auto id=session.value("id").toString();
            q.prepare("SELECT reward FROM focus_sessions WHERE id=?");q.addBindValue(id);checked(q);
            const bool exists=q.next();
            if(!exists){
                const auto day=session.value("reward_day").toString();
                q.prepare("SELECT COUNT(*) FROM focus_sessions WHERE reward_day=? AND reward>0");q.addBindValue(day);checked(q);q.next();
                const bool eligible=session.value("status")=="completed" && session.value("actual_ms").toInteger()>=15*60000 && q.value(0).toInt()<4;
                auto lifeState=life();const int before=lifeState.experience;
                if(eligible)lifeState.award("focus",session.value("task").toString(),QDateTime::fromString(session.value("ended").toString(),Qt::ISODateWithMs),id);
                const int reward=lifeState.experience-before;
                auto record=session;record["reward_settled"]=true;record["reward"]=reward;
                q.prepare("INSERT INTO focus_sessions(id,json,reward_day,reward) VALUES(?,?,?,?)");
                q.addBindValue(id);q.addBindValue(QString::fromUtf8(QJsonDocument(record).toJson(QJsonDocument::Compact)));q.addBindValue(day);q.addBindValue(reward);checked(q);
                if(reward){saveLife(lifeState);syncCollection(QDateTime::fromString(session.value("ended").toString(),Qt::ISODateWithMs));}
            }
        }
        q.prepare("INSERT INTO wellness_state(id,json) VALUES(1,?) ON CONFLICT(id) DO UPDATE SET json=excluded.json");
        q.addBindValue(QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact)));checked(q);
        if(!db_.commit())throw std::runtime_error("Cannot commit wellness state");
        if(!session.isEmpty())focusStatsCache_.clear();
    }catch(...){db_.rollback();throw;}
}
QJsonArray MemoryStore::focusSessions() const {
    QSqlQuery q(db_);q.prepare("SELECT json FROM focus_sessions ORDER BY rowid DESC");checked(q);QJsonArray rows;
    while(q.next())rows.append(QJsonDocument::fromJson(q.value(0).toByteArray()).object());
    return rows;
}
QJsonObject MemoryStore::focusStatistics(QDate day) const {
    const auto cacheKey=QString::fromUtf8(QTimeZone::systemTimeZoneId())+day.toString(Qt::ISODate);
    if(focusStatsCache_.contains(cacheKey))return focusStatsCache_.value(cacheKey);
    const auto start=QDateTime(day,QTime(0,0)).toUTC().toMSecsSinceEpoch();
    const auto end=QDateTime(day.addDays(1),QTime(0,0)).toUTC().toMSecsSinceEpoch();
    qint64 duration=0;int count=0;
    for(const auto &value:focusSessions()){
        const auto row=value.toObject();
        if(row.value("status")=="completed" && QDateTime::fromString(row.value("ended").toString(),Qt::ISODateWithMs).toLocalTime().date()==day)++count;
        for(const auto &v:row.value("segments").toArray()){
            const auto segment=v.toObject();const auto a=segment.value("at").toInteger(),ms=segment.value("ms").toInteger();
            duration+=qMax<qint64>(0,qMin(a+ms,end)-qMax(a,start));
        }
    }
    QJsonObject result{{"ms",duration},{"count",count}};
    if(focusStatsCache_.size()>512)focusStatsCache_.clear();
    focusStatsCache_.insert(cacheKey,result);return result;
}
void MemoryStore::collectCompanionLine(const QString &id,const QString &text){
    Q_UNUSED(text);recordPresented(id);
}
}
