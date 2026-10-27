#pragma once
#include "clipboard_history.h"
#include "pet_widget.h"
#include <QWidget>
#include <QPropertyAnimation>
#include <QTimer>
class QListWidget;
class QPushButton;
class QLabel;
class QScrollArea;
namespace dragon {
class ClipboardPanel final:public QWidget{
    Q_OBJECT
public:
    ClipboardPanel(ClipboardHistory &history,PetWidget &pet);
    void toggle();
signals:void analysisRequested(const QString &content,const QString &kind);
protected:bool eventFilter(QObject *object,QEvent *event) override;
private:
    ClipboardHistory &history_;
    PetWidget &pet_;
    QListWidget *list_;
    QWidget *body_;
    QPushButton *expand_;
    QPushButton *copy_;
    QLabel *status_;
    QScrollArea *scroll_;
    QPropertyAnimation animation_;
    QVector<ClipEntry> entries_;
    QTimer captureTimer_,copyTimer_;
    QString pendingCopy_;
    int copyAttempts_=0;
    bool expanded_=false,copying_=false,dirty_=true,refreshQueued_=false;
    void scheduleRefresh();
    void captureClipboard();
    void copySelected();
    void tryCopy();
    int expandedHeight() const;
    void refresh();
    void reposition();
    ClipEntry selected() const;
};
}
