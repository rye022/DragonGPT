#pragma once
#include <QString>
#include <array>
#include <QList>
#include <QRandomGenerator>
#include <QMetaType>

namespace dragon {
enum class PetAction { Idle, Wave, Shy, Jealous, Sleep, Work, Comfort };
QString actionName(PetAction action);

// Blend from the currently visible mixture, including interrupted transitions.
// Durations use the widget's monotonic clock, so wall-clock changes are harmless.
class PetActions {
public:
    static constexpr int Count=7;
    using Weights=std::array<double,Count>;
    void set(PetAction action,qint64 now);
    void tick(qint64 now);
    void reset();
    PetAction target() const {return target_;}
    const Weights &weights() const {return weights_;}
    bool transitioning() const {return transitioning_;}
    PetAction visible() const;
    static constexpr int Duration=520;
private:
    PetAction target_=PetAction::Idle;
    Weights weights_{{1,0,0,0,0,0,0}},from_=weights_;
    qint64 started_=0;
    bool transitioning_=false;
};
class ActionCycle {
public:
    explicit ActionCycle(quint32 seed=QRandomGenerator::global()->generate()):random_(seed){}
    void tick(qint64 now,bool paused=false,const QList<PetAction> &allowed={});
    PetAction current() const{return current_;}
    qint64 nextAt() const{return nextAt_;}
private:
    QRandomGenerator random_;
    QList<PetAction> bag_,allowed_;
    PetAction current_=PetAction::Idle;
    qint64 nextAt_=0,lastAt_=-1;
};
}
Q_DECLARE_METATYPE(dragon::PetAction)
