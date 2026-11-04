#pragma once
#include "latency_trace.h"
#include "emotion_engine.h"
#include "memory_store.h"
#include "voice_session.h"
#include "chat_service.h"
#include "local_brain.h"
#include "semantic_memory.h"
#include "local_tools.h"
#include "clipboard_history.h"
#include "health_monitor.h"
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>
#include <functional>

namespace dragon {
class Companion final : public QObject {
    Q_OBJECT
public:
    QString latencyId() const{return latency_.id();}
    void traceLatency(const QString &event){latency_.markOnce(event);}
    explicit Companion(const QString &databasePath, QObject *parent = nullptr,
                       QUrl endpoint=QUrl("https://api.deepseek.com/responses"),int timeoutMs=45000,
                       std::function<QDateTime()> clock={});
    MemoryStore &memory() { return memory_; }
    LocalTools &tools(){return tools_;}
    ClipboardHistory &clipboard(){return clipboard_;}
    HealthMonitor &health(){return health_;}
    EmotionEngine &emotions() { return emotions_; }
    VoiceSession &voice() { return voice_; }
    Reply respond(const QString &text);
    bool sendMessage(const QString &text);
    void cancelReply();
    bool busy() const {return chat_.busy() || memoryPending_ || agentBusy_;}
    void setAgentBusy(bool value){agentBusy_=value;emit busyChanged(busy());}
    void finishAgentExchange(const QString &input,const QString &output,bool completedTask=false);
    const ChatSettings &chatSettings() const {return chatSettings_;}
    void saveChatSettings(const ChatSettings &settings);
    void saveApiKey(const QByteArray &key);
    void removeApiKey();
    QString credentialStatus() const;
    QString routeStatus() const {return routeStatus_;}
    QString pendingText() const {return pendingText_;}
    void setAgentDispatcher(std::function<bool(const QString &)> dispatcher){agentDispatcher_=std::move(dispatcher);}
    void setDesktopContextProvider(std::function<QJsonObject()> provider){desktopContext_=std::move(provider);}
    QJsonObject lastUsage() const {return lastUsage_;}
    QJsonObject lastLimits() const {return lastLimits_;}
    QByteArray apiKey() const;
    void storeVoiceExchange(const QString &input,const QString &output,const QJsonObject &usage);
    void remember(const QString &text);
    void forget(qint64 id);
    void rename(const QString &name);
    void clearAll();
    QString promptContext(const QString &query) const;
    QString dataDirectory() const;
    const LifeSettings &lifeSettings() const{return lifeSettings_;}
    void saveLifeSettings(const LifeSettings &settings);
    void noteActivity(QDateTime now={});
    void observeDesktopIdle(qint64 idleSeconds,QDateTime now={});
    int desktopActiveMinutes() const{return desktopActiveSeconds_/60;}
    bool tryProactive(bool blocked,QDateTime now=QDateTime::currentDateTimeUtc());
    QString previewProactive() const;
    QString autonomyReason() const{return autonomyReason_;}
    void saveGrowthPreferences(const GrowthProfile &preferences,bool resetDrift=false);
    void sharedExperiencesChanged(){emit memoryChanged();}
    void resolveSuggestion(qint64 id,bool accept,bool replaceConflict=false);
    void refreshLife();
    void setFocusContext(bool focusing,bool resting);
    void updateEmotionContext(QDateTime now={},bool force=false);
    QDateTime currentTime() const{return now_();}
    void tickEmotions(){emotions_.tick(now_());}
    void focusSettled(){notifyBondGrowth();emit memoryChanged();}
    Emotion effectiveMood(QDateTime now={}) const;
    QList<PetAction> idleActions(Emotion mood) const;
    bool growthIdleEnabled() const{return !cachedLife_.growth.enabled || cachedLife_.growth.idleEnabled;}
    DialogueSelection clickLine(PetAction action,QDateTime now=QDateTime::currentDateTimeUtc());
    void attachProactiveScheduler(QObject *owner,std::function<bool()> blocked);
    bool proactiveOwner(const QObject *owner) const;
    bool proactiveSchedulerRunning() const{return proactiveTimer_.isActive();}
    QString semanticStatus() const{return semantic_.status();}
    void setLocalExecutor(std::function<Reply(const LocalCommand &)> executor){localExecutor_=std::move(executor);}
    void setWellnessExecutor(std::function<Reply(const LocalCommand &)> executor){wellnessExecutor_=std::move(executor);}
    QDateTime nextProactiveAt() const{return proactiveDue_;}
    DialogueSelection lastProactiveLine() const{return proactiveLine_;}
    void finishProactive(const QString &reason="finished");
    void traceProactive(const QString &event,const QString &detail={});
    void dialoguePresented(const QString &id);
    void dialogueHeard(const QString &id);
signals:
    void projectContextChanged(const QString &name);
    void turnStarted();
    void replyAborted();
    void memoryChanged();
    void partialReply(const QString &text);
    void replyReady(const QString &text,const QString &source);
    void busyChanged(bool busy);
    void routeChanged(const QString &status);
    void draftRestored(const QString &text);
    void operationFailed(const QString &message);
    void usageChanged();
    void proactiveReady(const QString &text);
    void actionRequested(dragon::PetAction action);
    void proactiveTrace(const QJsonObject &entry);
    void bondAdvanced();
private:
    LatencyTrace latency_;bool latencyFirstText_=false;
    MemoryStore memory_;
    EmotionEngine emotions_;
    VoiceSession voice_;
    Personality personality_;
    ChatService chat_;
    SemanticMemory semantic_;
    LocalTools tools_;
    ClipboardHistory clipboard_;
    HealthMonitor health_;
    bool memoryPending_=false;
    bool agentBusy_=false;
    quint64 memoryRequest_=0;
    QString semanticQuery_;
    QVector<Memory> semanticResults_;
    ChatSettings chatSettings_;
    QString settingsFile_;
    CredentialStore credentials_;
    QString pendingText_,routeStatus_="离线互动";
    QJsonObject lastUsage_;
    QJsonObject lastLimits_;
    LifeSettings lifeSettings_;
    QTimer proactiveTimer_;
    QTimer emotionTimer_;
    std::function<QDateTime()> now_;
    QDateTime proactiveDue_;
    bool proactiveInFlight_=false;
    QString autonomyReason_="等待下一次本地决策";
    DialogueSelection proactiveLine_;
    int notifiedStage_=0;
    LifeState cachedLife_;
    void scheduleProactive(bool deferred=false);
    void proactiveTimeout();
    void notifyBondGrowth();
    struct ProactiveGuard {QPointer<QObject> owner;std::function<bool()> blocked;};
    QVector<ProactiveGuard> proactiveGuards_;
    QDateTime desktopChecked_;int desktopActiveSeconds_=0;
    bool focusContext_=false,restContext_=false;
    qint64 desktopIdle_=-1;
    QDateTime emotionChecked_,returnedAt_;
    QDateTime lastActivity_=QDateTime::currentDateTimeUtc();
    QElapsedTimer turnClock_;
    std::function<Reply(const LocalCommand &)> localExecutor_;
    std::function<Reply(const LocalCommand &)> wellnessExecutor_;
    std::function<bool(const QString &)> agentDispatcher_;
    std::function<QJsonObject()> desktopContext_;
    void finishResponse(const QString &user,const Reply &reply,const QString &source,bool publish=true);
    bool dispatchLocal(const QString &text);
    bool startChat(const QString &text);
    QVector<Memory> relatedMemories(const QString &text,int limit=3) const;
    void completedInteraction(const QString &text);
    void offline(const QString &text,const QString &reason);
    void setRoute(const QString &status);
};
}
