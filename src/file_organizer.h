#pragma once
#include <QObject>
#include <QDateTime>
#include <QFutureWatcher>
#include <QJsonObject>
#include <atomic>
#include <memory>
namespace dragon {
struct FileMove{QString from,to,sha256;quint64 size=0;QDateTime modified;bool duplicate=false;};
struct FilePlan{QString root;QVector<FileMove> moves;QString warning,error;};
struct FileResult{int moved=0;QString journal,error;};
class FileOrganizer final:public QObject{
    Q_OBJECT
public:
    explicit FileOrganizer(const QString &journalDirectory,QObject *parent=nullptr);
    ~FileOrganizer() override;
    bool busy() const{return busy_;}
    void scan(const QString &directory);
    void execute();
    void undo(const QString &journal);
    void cancel();
    FilePlan plan() const{return plan_;}
    QStringList journals() const;
    static FilePlan inspect(const QString &directory,const std::shared_ptr<std::atomic_bool> &cancelled);
    static FileResult movePlan(const FilePlan &plan,const QString &journalDirectory,const std::shared_ptr<std::atomic_bool> &cancelled);
    static FileResult undoJournal(const QString &path,const std::shared_ptr<std::atomic_bool> &cancelled);
signals:
    void previewReady();
    void finished(const QString &message);
    void busyChanged(bool busy);
private:
    QString journalDirectory_;
    FilePlan plan_;
    bool busy_=false;
    std::shared_ptr<std::atomic_bool> cancelled_;
    QFutureWatcher<FilePlan> scan_;
    QFutureWatcher<FileResult> operations_;
};
}
