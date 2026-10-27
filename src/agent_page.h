#pragma once
#include "agent_core.h"
#include <QWidget>
class QComboBox;class QLineEdit;class QTextBrowser;class QPushButton;
namespace dragon {
class AgentPage final:public QWidget {
    Q_OBJECT
public:
    AgentPage(Companion &companion,ToolHub &existing,QWidget *parent=nullptr);
    AgentCore &core(){return core_;}
    bool dispatch(const QString &text);
signals:
    void reveal();
private:
    AgentCore core_;QComboBox *mode_;QLineEdit *root_,*python_,*goal_;
    QTextBrowser *events_,*preview_;QPushButton *approve_,*deny_;
};
}
