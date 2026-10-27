#pragma once
#include <QObject>
#include <QDateTime>
#include <QSqlDatabase>
namespace dragon {
struct ClipEntry{qint64 id=0;QString text,kind;QDateTime created;bool pinned=false;};
class ClipboardHistory final:public QObject{
    Q_OBJECT
public:
    explicit ClipboardHistory(const QString &database,QObject *parent=nullptr);
    ~ClipboardHistory() override;
    bool enabled() const{return enabled_;}
    void setEnabled(bool enabled);
    int retentionHours() const{return retentionHours_;}
    void setRetentionHours(int hours);
    bool capture(const QString &text);
    QVector<ClipEntry> entries() const;
    void remove(qint64 id);
    void pin(qint64 id,bool pinned);
    void clear();
    void prune(QDateTime now=QDateTime::currentDateTimeUtc());
    static bool sensitive(const QString &text);
    static QString classify(const QString &text);
signals:void changed();
private:
    QString connection_;
    QSqlDatabase db_;
    bool enabled_=false;
    int retentionHours_=24;
    void setting(const QString &key,const QString &value);
};
}
