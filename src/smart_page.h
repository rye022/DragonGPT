#pragma once
#include "companion.h"
#include "screen_capture.h"
#include "screen_ocr.h"
#include "tool_hub.h"
#include "desktop_hotkey.h"
#include <QWidget>
#include <QNetworkAccessManager>
class QLineEdit;
class QLabel;
class QComboBox;
class QCheckBox;
class QTextBrowser;
namespace dragon {
class CompanionWindow;
class SmartPage final:public QWidget {
    Q_OBJECT
public:
    SmartPage(Companion &companion,CompanionWindow &window);
    ~SmartPage() override;
    void askScreen();
    void proposeGoal(const QString &goal);
    ScreenCapture &capture(){return capture_;}
    ToolHub &hub(){return hub_;}
    bool blocksProactive() const;
signals:
    void togglePet();
    void reveal();
private:
    Companion &companion_;
    CompanionWindow &window_;
    ScreenCapture capture_;
    ScreenOcr ocr_;
    ToolHub hub_;
    DesktopHotkey hotkey_;
    DesktopHotkey screenHotkey_;
    ChatService analysis_,planner_;
    QNetworkAccessManager discovery_;
    QLineEdit *question_=nullptr,*goal_=nullptr,*blockList_=nullptr,*complexModel_=nullptr,*visionModel_=nullptr;
    QComboBox *targets_=nullptr,*method_=nullptr;
    QCheckBox *enabled_=nullptr,*contextEnabled_=nullptr;
    QLabel *status_=nullptr,*contextLabel_=nullptr;
    QTextBrowser *result_=nullptr,*planView_=nullptr,*auditView_=nullptr;
    QTimer expiry_;
    QString screenQuestion_,screenMethod_,facts_,phase_;
    QByteArray cacheKey_,currentKey_;
    QString cacheText_;
    QDateTime cacheTime_;
    ToolHub::Completion screenCompletion_;
    void registerTools();
    void startAnalysis(const QString &facts);
    void cancelAnalysis();
    void updatePlan();
    void runPlan();
    void refreshTargets();
    void discoverModels();
    bool analysisBusy() const{return !phase_.isEmpty() || capture_.busy() || ocr_.busy() || analysis_.busy();}
};
}
