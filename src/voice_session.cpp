#include "voice_session.h"
namespace dragon {
void VoiceSession::setState(State state) { state_=state; emit changed(); }
bool VoiceSession::begin() {
    if(state_!=State::Idle) return false;
    ++epoch_; setState(State::Listening); return true;
}
bool VoiceSession::finishInput(quint64 epoch) {
    if(epoch!=epoch_ || state_!=State::Listening) return false;
    setState(State::Thinking); return true;
}
bool VoiceSession::beginOutput(quint64 epoch) {
    if(epoch!=epoch_ || state_!=State::Thinking) return false;
    setState(State::Speaking); return true;
}
bool VoiceSession::finishOutput(quint64 epoch) {
    if(epoch!=epoch_ || state_!=State::Speaking) return false;
    ++epoch_; setState(State::Listening); return true;
}
void VoiceSession::interrupt() {
    if(state_==State::Idle || state_==State::Interrupted) return;
    const auto old=epoch_++; setState(State::Interrupted); emit cancelOutput(old);
}
bool VoiceSession::resume() {
    if(state_!=State::Interrupted) return false;
    setState(State::Listening); return true;
}
void VoiceSession::stop() {
    const auto old=epoch_++; setState(State::Idle); emit cancelOutput(old);
}
}
