#pragma once
#include <QDateTime>
#include <QString>
#include <QVector>

namespace dragon {
enum class Emotion { Calm, Smile, Happy, Thinking, Curious, Worried, Shy, Sleepy, Sad, Angry, Tsundere, Relaxed, Focused, Excited };
inline constexpr int EmotionCount=14;
inline QString emotionName(Emotion emotion) {
    switch (emotion) {
    case Emotion::Calm: return QStringLiteral("平静");
    case Emotion::Smile: return QStringLiteral("微笑");
    case Emotion::Happy: return QStringLiteral("开心");
    case Emotion::Thinking: return QStringLiteral("思考");
    case Emotion::Curious: return QStringLiteral("好奇");
    case Emotion::Worried: return QStringLiteral("关心");
    case Emotion::Shy: return QStringLiteral("害羞");
    case Emotion::Sad: return QStringLiteral("难过");
    case Emotion::Angry: return QStringLiteral("生气");
    case Emotion::Sleepy: return QStringLiteral("困倦");
    case Emotion::Tsundere: return QStringLiteral("傲娇");
    case Emotion::Relaxed: return QStringLiteral("放松");
    case Emotion::Focused: return QStringLiteral("认真");
    case Emotion::Excited: return QStringLiteral("兴奋");
    }
    return {};
}
struct Personality {
    int warmth = 82, playfulness = 38, tsundere = 22, patience = 90;
    int curiosity = 70, protectiveness = 78, initiative = 55, formality = 42;
};
struct EmotionState {
    Emotion emotion = Emotion::Calm;
    double intensity = 0.0;
    int priority = 0;
    QDateTime expiresAt;
    QString reason;
};
// Renderer-independent controls. A future Live2D adapter consumes these values.
struct AvatarParameters {
    double eyeOpen = 1.0, mouthOpen = 0.0, headAngle = 0.0, blush = 0.0;
};
struct Memory {
    qint64 id = 0;
    QString content;
    QDateTime createdAt;
    QString tags;
    double importance = 0.5;
    QDateTime expiresAt;
    QDateTime lastUsedAt;
    QString layer="semantic",source="manual";
    QDateTime updatedAt;
    bool proactiveAllowed=true;
};
struct Message {
    QString role, content;
    QString source = "offline";
};
struct Reply {
    QString text;
    Emotion emotion = Emotion::Calm;
};
}
