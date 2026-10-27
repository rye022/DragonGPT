#pragma once
#include "audio_duplex.h"
#include "companion.h"
#include "local_speech.h"
#include "qwen_speech.h"
#include "offline_speech.h"
#include "speech_text_buffer.h"
#include "speech_recognition.h"
#include "pet_actions.h"
#include <QHash>
#include <QElapsedTimer>

namespace dragon {
class VoiceController final:public QObject {
    Q_OBJECT
public:
    explicit VoiceController(Companion &companion,QObject *parent=nullptr,
        QUrl speechEndpoint=QUrl("https://dashscope.aliyuncs.com/api/v1/services/aigc/multimodal-generation/generation"),int audioWaitMs=4000);
    ~VoiceController() override;
    void beginCall();
    void endCall();
    void interrupt();
    void setMuted(bool muted);
    SpeechRecognitionService &recognition(){return recognition_;}
    void submitRecognition(const QString &text);
    void discardRecognition();
    void setReadAloud(bool value);
    bool readAloud() const{return preferences_.readAloud;}
    void speakRandomLine();
    void deliverProactive(const QString &text);
    bool previewCollectedLine(const QString &id);
    bool previewProactiveLine();
    void speakActionLine(PetAction action);
    static QStringList interactionLines();
    static QStringList actionLines(PetAction action);
    static QByteArray interactionAudio(const QString &text);
    void previewVoice();
    bool speechBusy() const{return outputBusy();}
    bool speakCompanionNotice(const QString &text,bool bubble=true,const QString &lineId={}){if(engaged() || companion_.busy())return false;speak(text,false,PetAction::Idle,lineId,false,bubble,true);return true;}
    bool active() const{return active_;}
    bool engaged() const{return active_ || outputBusy();}
    QString status() const{return status_;}
    QString voiceLabel() const{return selectedVoiceName();}
    QString capabilities() const{return local_.capabilities();}
    QString renderTestWav(const QString &path){return local_.renderWav("你好，我是霜翎。我会安静地陪着你。",path);}
    const VoicePreferences &voicePreferences() const{return preferences_;}
    void saveVoicePreferences(const VoicePreferences &preferences,const QByteArray &key);
    void removeVoiceKey();
    QString voiceCredentialStatus() const;
    QString offlineStatus() const;
    SpeechExpression speechExpression() const;
    void clearVoiceCache(){endCall();cache_.clear();status("已清空缓存语音");}
signals:
    void changed();
    void caption(const QString &text);
    void mouthChanged(double value);
    void onlineVoiceReply(const QString &text);
    void speechRequested(const QString &text);
    void textOnlyBubble(const QString &text);
    void outputStarted(const QString &label);
    void segmentQueued(const QString &text);
    void transcriptReady(const QJsonObject &result);
    void transcriptPartial(const QString &text);
    void transcriptCleared();
    void interactionStarted(dragon::PetAction action);
    void interactionFinished();
private:
    Companion &companion_;
    LocalSpeech local_;
    SpeechRecognitionService recognition_;
    bool recognitionPending_=false;
    QwenSpeech qwen_;
    SpeechPlayer player_;
    OfflineSpeech offline_;
    VoiceCache cache_;
    VoicePreferences preferences_;
    VoicePreferences utterancePreferences_;
    void prepareExpression(bool interaction=false,PetAction action=PetAction::Idle);
    QString preferenceFile_;
    CredentialStore voiceCredentials_;
    QStringList segments_;
    QString currentSegment_;
    QString playbackLabel_,offlineLabel_;
    QString outputNotice_;
    bool active_=false,muted_=false;
    bool readingTextReply_=false;
    QTimer audioDeadline_;
    int audioWaitMs_=4000;
    bool cloudStreaming_=false,interactionOutput_=false,systemOutput_=false;
    bool proactiveOutput_=false,noticeOutput_=false,noticePresented_=false;
    QString currentDialogueId_;
    QTimer textInteractionTimer_;
    QList<int> interactionBag_;
    int lastInteraction_=-1;
    QHash<int,QList<int>> actionBags_;
    QHash<int,int> lastActionLines_;
    SpeechTextBuffer textBuffer_;
    LatencyTrace latency_;int latencySegment_=0;
    QTimer textFlush_,pipelinePump_;
    bool userSuppressedTurn_=false;
    bool pipeline_=false,textComplete_=false,streamSuppressed_=false,streamRewrite_=false;
    QString status_="语音未开启 · 点击开始后才启用麦克风";
    void status(const QString &value){status_=value;emit changed();}
    void localListen();
    void speak(const QString &text,bool interaction=false,PetAction action=PetAction::Idle,const QString &dialogueId={},bool proactive=false,bool bubble=true,bool notice=false);
    void nextSegment();
    void speechFallback(const QString &reason);
    void systemSpeechFallback(const QString &reason);
    void playPcm(const QByteArray &pcm,const QString &label);
    bool playCached(const QString &id,const QString &label);
    bool playBundledInteraction();
    void outputFinished();
    void stopOutput();
    bool outputBusy() const;
    QString selectedVoiceName() const;
    void receivePartial(const QString &text);
    void receiveFinal(const QString &text,const QString &source);
    void enqueueText(const QStringList &segments);
    void pumpPipeline();
    void segmentProduced();
    bool appendPipelinePcm(const QByteArray &pcm,const QString &label);
};
}
