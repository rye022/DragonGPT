#pragma once
#include "companion.h"
#include "pet_widget.h"
#include "voice_controller.h"
#include "growth_gauge.h"
#include <QWidget>
class QLabel;
class QLineEdit;
class QTextBrowser;
class QListWidget;
class QTabWidget;
class QPushButton;

namespace dragon {
class FocusPage;
class ClipboardPanel;
class SmartPage;
class AgentPage;
class CompanionWindow final : public QWidget {
    Q_OBJECT
public:
    CompanionWindow(Companion &companion, PetWidget &pet,QUrl speechEndpoint=QUrl("https://dashscope.aliyuncs.com/api/v1/services/aigc/multimodal-generation/generation"));
    void refresh();
    void openConversation();
    void sendText(const QString &text);
    void openSettings();
    void openClipboard();
    void askScreen();
    qint64 confirmTask(TaskDraft draft,std::function<bool()> stillCurrent={});
    FocusPage *focusPage() const{return focusPage_;}
    VoiceController &voiceController(){return voice_;}
private:
    FocusPage *focusPage_=nullptr;
    SmartPage *smartPage_=nullptr;
    AgentPage *agentPage_=nullptr;
    Companion &companion_;
    PetWidget &pet_;
    VoiceController voice_;
    QTimer desktopMonitor_;
    QTimer bondNoticeTimer_;
    QTabWidget *tabs_ = nullptr;
    QTextBrowser *chat_ = nullptr;
    QLineEdit *input_ = nullptr, *nickname_ = nullptr, *newMemory_ = nullptr;
    QLineEdit *memorySearch_=nullptr;
    QLabel *semanticStatus_=nullptr;
    void refreshMemories();
    QWidget *toolsPage();
    void refreshTools();
    QListWidget *todos_=nullptr;
    QLabel *pomodoroStatus_=nullptr;
    ClipboardPanel *clipboardPanel_=nullptr;
    struct ToolNotice{QString text;PetAction action;bool passive=false;QDateTime queued;};
    QVector<ToolNotice> pendingNotices_;
    QListWidget *memories_ = nullptr;
    QLabel *emotion_ = nullptr, *relationship_ = nullptr, *memoryCount_ = nullptr, *notice_ = nullptr;
    QLabel *routeBadge_ = nullptr, *usage_ = nullptr;
    QPushButton *send_ = nullptr, *cancel_ = nullptr;
    QString streamingText_;
    QWidget *conversationPage();
    QWidget *memoryPage();
    QWidget *settingsPage();
    QWidget *connectionPage();
    QWidget *voicePage();
    QWidget *lifePage();
    QLabel *lifeStatus_=nullptr;
    GrowthGauge *growthGauge_=nullptr;
    QListWidget *suggestions_=nullptr;
    void editMemory();
    void renderHistory();
    void refreshLifeView();
    void handleError(const std::exception &error);
};
}
