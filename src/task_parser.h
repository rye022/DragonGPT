#pragma once
#include <QDateTime>
namespace dragon {
struct TaskDraft {
    QString title,question;
    QDateTime due;
    int priority=1;
    bool needsConfirmation=false;
};
struct Todo {qint64 id=0;QString title;QDateTime due;int priority=1;bool done=false,notified=false;};
class TaskParser {
public:static TaskDraft parse(const QString &text,QDateTime now=QDateTime::currentDateTime());
};
}
Q_DECLARE_METATYPE(dragon::TaskDraft)
