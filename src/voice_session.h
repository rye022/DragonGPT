#pragma once
#include <QObject>

namespace dragon {
// Protocol state only in v0.1. No microphone or audio transport is connected.
// Each turn has an epoch; adapters must discard callbacks with old epochs.
class VoiceSession final : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Listening, Thinking, Speaking, Interrupted };
    Q_ENUM(State)
    explicit VoiceSession(QObject *parent = nullptr) : QObject(parent) {}
    State state() const { return state_; }
    quint64 epoch() const { return epoch_; }
    bool begin();
    bool finishInput(quint64 epoch);
    bool beginOutput(quint64 epoch);
    bool finishOutput(quint64 epoch);
    void interrupt();
    bool resume();
    void stop();
signals:
    void changed();
    void cancelOutput(quint64 cancelledEpoch);
private:
    State state_ = State::Idle;
    quint64 epoch_ = 0;
    void setState(State state);
};
}
