#pragma once
#include "companion.h"
#include <QWidget>
class QLabel;class QProgressBar;class QCheckBox;class QSpinBox;class QListWidget;class QLineEdit;class QTextBrowser;
namespace dragon {
class GrowthCenter final:public QWidget {
    Q_OBJECT
public:
    explicit GrowthCenter(Companion &companion,QWidget *parent=nullptr);
    void refresh();
signals:
    void navigate(const QString &tab);
    void explore(const QString &goal);
private:
    Companion &companion_;
    QLabel *overview_,*decision_,*status_,*unlocks_;
    QProgressBar *progress_;
    QListWidget *events_,*shared_;
    QLineEdit *sharedInput_;
    QCheckBox *enabled_,*personality_,*habits_,*sharedEnabled_,*idle_,*autonomy_,*tasks_,*followups_,*exploration_;
    QSpinBox *thresholds_[3],*base_[6],*hourly_;
    QCheckBox *locks_[6];
    QLabel *trends_[6];
    QByteArray preferencesSnapshot_;
    void save(bool reset=false);
    void exportHistory();
};
}
