#pragma once
#include "types.h"
#include "pet_actions.h"
#include <QObject>
namespace dragon {
struct EmotionContext {
    QDateTime now,lastInteraction,returnedAt;
    int affinity=0;
    qint64 idleSeconds=-1;
    bool focusing=false,resting=false,busy=false;
};
class EmotionEngine final : public QObject {
    Q_OBJECT
public:
    explicit EmotionEngine(QObject *parent=nullptr):QObject(parent){}
    const EmotionState &state() const{return state_;}
    bool request(Emotion emotion,double intensity,int durationMs,int priority,QDateTime now=QDateTime::currentDateTimeUtc());
    void tick(QDateTime now=QDateTime::currentDateTimeUtc());
    void reset(QDateTime now=QDateTime::currentDateTimeUtc());
    void setBaseline(Emotion emotion,double intensity,QDateTime expiresAt,QDateTime now=QDateTime::currentDateTimeUtc());
    void updateContext(const EmotionContext &context);
    Emotion moodAt(QDateTime now) const;
    AvatarParameters avatarParameters() const;
    static Emotion classify(const QString &text,int affinity=50);
    static int holdSeconds(Emotion emotion);
    static QList<PetAction> actionsFor(Emotion emotion);
    static PetAction preferredAction(Emotion emotion){return actionsFor(emotion).first();}
    static bool matches(Emotion required,Emotion current);
    static double topicWeight(Emotion emotion,const QString &topic);
    static bool distressed(Emotion emotion){return emotion==Emotion::Sad || emotion==Emotion::Worried || emotion==Emotion::Angry;}
signals:
    void changed();
private:
    EmotionState state_,baseline_,persistent_;
    EmotionContext context_;
    QDateTime requestedAt_,baselineChangedAt_,pendingAt_;
    Emotion pending_=Emotion::Calm;
    double requestedIntensity_=0;
    bool contextReady_=false;
    EmotionState desired(QDateTime now) const;
    void rebuildBaseline(QDateTime now,bool immediate);
};
}
