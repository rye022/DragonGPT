#pragma once
#include "pet_actions.h"
#include <QDateTime>

namespace dragon {
enum class LocalIntent { None, Time, Weekday, Cpu, Memory, Health, Action, PomodoroStart,
    PomodoroPause, PomodoroRemaining, TodoList, TodoAdd, Water, HealthPage, DailySummary };
struct LocalCommand {
    LocalIntent intent=LocalIntent::None;
    PetAction action=PetAction::Idle;
    int minutes=0;
    QString argument;
    explicit operator bool() const {return intent!=LocalIntent::None;}
};
class LocalBrain {
public:
    static LocalCommand recognize(const QString &text);
};
}
