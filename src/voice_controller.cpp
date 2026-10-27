#include "voice_controller.h"
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace dragon {
VoiceController::VoiceController(Companion &companion,QObject *parent,QUrl speechEndpoint,int audioWaitMs):QObject(parent),companion_(companion),local_(this),
    recognition_(local_,companion.dataDirectory(),this),qwen_(this,std::move(speechEndpoint)),player_(this),offline_(this),cache_(companion.dataDirectory()+"/voice-cache"),preferences_(VoicePreferences::load(companion.dataDirectory()+"/voice.ini")),
    preferenceFile_(companion.dataDirectory()+"/voice.ini"),voiceCredentials_(companion.dataDirectory()+"/dashscope-key.dpapi"){
    audioWaitMs_=qBound(50,audioWaitMs,30000);audioDeadline_.setSingleShot(true);
    auto timing=[this](const QString &event){latency_.mark(event,{{"segment",latencySegment_}});};
    connect(&qwen_,&QwenSpeech::timing,this,timing);connect(&player_,&SpeechPlayer::timing,this,timing);
    textFlush_.setSingleShot(true);textFlush_.setInterval(250);pipelinePump_.setSingleShot(true);
    connect(&textFlush_,&QTimer::timeout,this,[this]{
        if(!pipeline_ || textComplete_)return;
        enqueueText(textBuffer_.takeReady(false,true));
        if(textBuffer_.pending())textFlush_.start();
    });
    connect(&pipelinePump_,&QTimer::timeout,this,&VoiceController::pumpPipeline);
    connect(&companion_,&Companion::turnStarted,this,[this]{stopOutput();userSuppressedTurn_=false;streamSuppressed_=streamRewrite_=false;});
    connect(&companion_,&Companion::replyAborted,this,[this]{if(!interactionOutput_)stopOutput();streamSuppressed_=true;});
    connect(&companion_,&Companion::partialReply,this,&VoiceController::receivePartial);
    connect(&audioDeadline_,&QTimer::timeout,this,[this]{speechFallback("语音等待超时");});
    textInteractionTimer_.setSingleShot(true);connect(&textInteractionTimer_,&QTimer::timeout,this,&VoiceController::stopOutput);
    connect(&player_,&SpeechPlayer::started,this,[this]{
        if(status_.startsWith("正在初始化扬声器"))status("正在朗读 · "+playbackLabel_);
        if(!currentDialogueId_.isEmpty()){if(noticeOutput_ && !noticePresented_){companion_.dialoguePresented(currentDialogueId_);noticePresented_=true;}companion_.dialogueHeard(currentDialogueId_);}
        if(proactiveOutput_)companion_.traceProactive("tts_playing",playbackLabel_);
        emit outputStarted(playbackLabel_);
    });
    connect(&local_,&LocalSpeech::pcmReady,this,[this](const QByteArray &pcm){latency_.mark("local_pcm_ready",{{"segment",latencySegment_}});playPcm(pcm,playbackLabel_);});
    connect(&local_,&LocalSpeech::synthesisFailed,this,[this](const QString &reason){stopOutput();status(reason+" · 请检查系统中文语音包");});
    connect(&local_,&LocalSpeech::mouthChanged,this,&VoiceController::mouthChanged);
    connect(&recognition_,&SpeechRecognitionService::partial,this,&VoiceController::transcriptPartial);
    connect(&companion_,&Companion::projectContextChanged,&recognition_,&SpeechRecognitionService::setProjectName);
    connect(&recognition_,&SpeechRecognitionService::status,this,[this](const QString &s){status(s);});
    connect(&recognition_,&SpeechRecognitionService::failed,this,[this](const QString &s){recognitionPending_=false;status(s+" · 未执行任何语音任务");});
    connect(&recognition_,&SpeechRecognitionService::finalResult,this,[this](const QJsonObject &result){
        if(!active_ || muted_)return;
        recognitionPending_=true;emit transcriptReady(result);
        status("最终识别已完成 · 请核对并发送，原文与术语规范化结果可查看");
        if(recognition_.settings().autoSend && !result["requires_review"].toBool())submitRecognition(result["corrected"].toString());
    });
    connect(&companion_,&Companion::replyReady,this,&VoiceController::receiveFinal);
    connect(&local_,&LocalSpeech::speechFinished,this,[this]{
        outputFinished();
    });
    connect(&qwen_,&QwenSpeech::chunkReady,this,[this](const QByteArray &pcm){
        if(currentSegment_.isEmpty())return;
        if(!cloudStreaming_){
            if(!pipeline_ || !player_.active()){
                const auto error=player_.beginStream();
                if(!error.isEmpty()){systemSpeechFallback(error);return;}
            }
            cloudStreaming_=true;playbackLabel_="百炼 · "+selectedVoiceName();
        }
        const auto error=player_.appendPcm(pcm);
        if(!error.isEmpty()){systemSpeechFallback(error);return;}
        if(preferences_.fastResponse)audioDeadline_.start(audioWaitMs_+player_.bufferedMilliseconds());
        status(player_.preparingOutput()?"正在初始化扬声器 · 音频已排队":"正在流式朗读 · "+playbackLabel_+" · 可点击桌宠切换短句");
    });
    connect(&qwen_,&QwenSpeech::ready,this,[this](const QByteArray &pcm){
        latency_.mark("tts_complete",{{"segment",latencySegment_}});
        audioDeadline_.stop();
        if(preferences_.cacheAudio)cache_.write(utterancePreferences_.expressiveCacheId(),currentSegment_,pcm);
        if(pipeline_ && cloudStreaming_){cloudStreaming_=false;segmentProduced();}
        else if(cloudStreaming_){cloudStreaming_=false;player_.finishStream();}
        else playPcm(pcm,"百炼 · "+selectedVoiceName());
    });
    connect(&qwen_,&QwenSpeech::failed,this,&VoiceController::speechFallback);
    connect(&offline_,&OfflineSpeech::ready,this,[this](const QByteArray &pcm){
        audioDeadline_.stop();
        if(preferences_.cacheAudio)cache_.write("offline:Serena",currentSegment_,pcm);
        playPcm(pcm,offlineLabel_);
    });
    connect(&offline_,&OfflineSpeech::progress,this,[this](const QString &message){status(message);});
    connect(&offline_,&OfflineSpeech::failed,this,&VoiceController::systemSpeechFallback);
    connect(&player_,&SpeechPlayer::failed,this,&VoiceController::systemSpeechFallback);
    connect(&player_,&SpeechPlayer::mouthChanged,this,&VoiceController::mouthChanged);
    connect(&player_,&SpeechPlayer::finished,this,[this]{if(pipeline_)outputFinished();else{currentSegment_.clear();nextSegment();}});
    connect(&companion_,&Companion::busyChanged,this,[this](bool busy){if(active_ && !busy && !outputBusy())localListen();});
}
VoiceController::~VoiceController(){endCall();}
void VoiceController::beginCall(){
    endCall();companion_.cancelReply();active_=true;muted_=false;companion_.voice().begin();
    localListen();
}
void VoiceController::localListen(){
    if(!active_ || muted_ || recognitionPending_ || companion_.busy() || outputBusy() || recognition_.listening())return;
    const auto error=recognition_.start();
    if(error.startsWith("扬声器回声保护") || error.startsWith("上一段 ASR")){QTimer::singleShot(700,this,&VoiceController::localListen);status(error);return;}
    if(error.isEmpty())status("语音聆听中 · 最终稿按设置确认发送");
    else{active_=false;companion_.voice().stop();status(error);}
}
void VoiceController::submitRecognition(const QString &text){
    if(!recognitionPending_ || text.trimmed().isEmpty())return;
    recognitionPending_=false;recognition_.stop();emit transcriptCleared();emit caption("你："+text);companion_.voice().finishInput(companion_.voice().epoch());status("已确认最终语音指令 · 正在处理");
    try{if(!companion_.sendMessage(text))localListen();}catch(const std::exception &){status("语音指令处理失败，未自动重发");}
}
void VoiceController::discardRecognition(){recognitionPending_=false;recognition_.stop();status("已丢弃语音识别稿");localListen();}
void VoiceController::endCall(){if(companion_.busy() || pipeline_)userSuppressedTurn_=streamSuppressed_=true;active_=false;muted_=false;recognitionPending_=false;recognition_.stop();stopOutput();companion_.voice().stop();status("通话已结束 · 麦克风已关闭");}
void VoiceController::interrupt(){
    stopOutput();companion_.cancelReply();companion_.voice().interrupt();companion_.voice().resume();localListen();
}
void VoiceController::setMuted(bool muted){
    if(!active_){muted_=false;status("语音未开启 · 麦克风已关闭");return;}
    muted_=muted;if(muted)recognition_.stop();else localListen();
    status(muted?"麦克风已静音":"本地中文聆听中");
}
void VoiceController::previewVoice(){endCall();speak("你好，我是霜翎。我会安静地陪着你。今天也一起慢慢来吧。");}
void VoiceController::setReadAloud(bool value){
    if(preferences_.readAloud==value)return;
    auto preferences=preferences_;preferences.readAloud=value;preferences.save(preferenceFile_);preferences_=preferences;
    if(!value && readingTextReply_){userSuppressedTurn_=streamSuppressed_=true;stopOutput();status("已关闭文字回复朗读 · 麦克风未开启");}
    else emit changed();
}
QStringList VoiceController::interactionLines(){
    QFile file(":/assets/interaction/lines.json");QStringList lines;
    if(file.open(QIODevice::ReadOnly))for(const auto &line:QJsonDocument::fromJson(file.readAll()).array())lines<<line.toString();
    return lines;
}
QByteArray VoiceController::interactionAudio(const QString &text){
    const auto index=interactionLines().indexOf(text);
    QString path;
    if(index>=0)path=QString(":/assets/interaction/%1.pcm").arg(index,2,10,QChar('0'));
    else{
        QFile catalog(":/assets/interaction/actions.json");
        if(!catalog.open(QIODevice::ReadOnly))return {};
        const auto groups=QJsonDocument::fromJson(catalog.readAll()).object();
        for(const auto &group:groups)for(const auto &clip:group.toArray()){
            const auto record=clip.toObject();if(record.value("text").toString()==text)path=":/assets/interaction/"+record.value("file").toString();
        }
    }
    if(path.isEmpty())return {};
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))return {};
    return file.readAll();
}
QStringList VoiceController::actionLines(PetAction action){
    if(action==PetAction::Idle){const auto lines=interactionLines();return lines.size()>=6?QStringList{lines[0],lines[2],lines[5]}:QStringList{};}
    QFile file(":/assets/interaction/actions.json");if(!file.open(QIODevice::ReadOnly))return {};
    QStringList lines;for(const auto &clip:QJsonDocument::fromJson(file.readAll()).object().value(QString::number(int(action))).toArray())lines.append(clip.toObject().value("text").toString());
    return lines;
}
void VoiceController::speakActionLine(PetAction action){
    if(int(action)<0 || int(action)>=PetActions::Count)action=PetAction::Idle;
    DialogueSelection selected;
    try{selected=companion_.clickLine(action);}catch(const std::exception &){status("点击台词历史暂时无法保存");return;}
    if(!selected)return;
    if(companion_.busy() || pipeline_)userSuppressedTurn_=streamSuppressed_=true;
    emit caption("霜翎："+selected.text);speak(selected.text,true,action,selected.id);
    if(active_)companion_.voice().beginOutput(companion_.voice().epoch());
}
void VoiceController::speakRandomLine(){speakActionLine(PetAction::Idle);}
void VoiceController::deliverProactive(const QString &text){
    if(active_ || outputBusy()){companion_.traceProactive("deferred","voice became busy before presentation");companion_.finishProactive("voice busy");return;}
    emit caption("霜翎："+text);
    const auto selected=companion_.lastProactiveLine();const QString id=selected.text==text?selected.id:QString();
    const auto line=DialogueEngine::find(id);const auto category=DialogueEngine::category(line);
    const auto categoryAction=category=="害羞"?PetAction::Shy:category=="开心"?PetAction::Wave:category=="休息提醒"?PetAction::Sleep:
        category=="工作陪伴"?PetAction::Work:category=="傲娇"?PetAction::Jealous:PetAction::Comfort;
    const auto mood=companion_.effectiveMood();
    const auto action=mood==Emotion::Calm?categoryAction:EmotionEngine::preferredAction(mood);
    if(companion_.lifeSettings().proactiveSpeech){
        speak(text,true,action,id,true);
    }else{
        stopOutput();interactionOutput_=proactiveOutput_=true;currentDialogueId_=id;
        emit interactionStarted(action);emit textOnlyBubble(text);
        if(!id.isEmpty())companion_.dialoguePresented(id);
        companion_.traceProactive("bubble_shown",id);companion_.traceProactive("tts_skipped","proactive speech disabled");textInteractionTimer_.start(9000);
    }
}
bool VoiceController::previewProactiveLine(){
    if(engaged() || companion_.busy())return false;
    const auto text=companion_.previewProactive();
    if(text.isEmpty())return false;
    // Explicit listening preview: no dialogue ID, reward or scheduler event.
    speak(text);return true;
}
bool VoiceController::previewCollectedLine(const QString &id){
    if(engaged() || companion_.busy())return false;
    for(const auto &v:companion_.memory().collection()){
        const auto l=v.toObject();if(l["id"]==id && l["unlocked"].toBool() && l["discovered"].toBool()){
            // No collection/growth/action mutations: this is a listening preview.
            speak(l["text"].toString());return true;
        }
    }return false;
}
QString VoiceController::selectedVoiceName() const{
    if(preferences_.audioTts()){
        for(const auto &v:audioVoiceChoices(preferences_.audioModel,preferences_.region))if(v.id==preferences_.audioVoice)return "Audio TTS · "+v.name;
        return "Audio TTS · "+preferences_.audioVoice;
    }
    for(const auto &v:voiceChoices())if(v.id==preferences_.voice)return v.name;
    return "系统中文";
}
bool VoiceController::outputBusy() const{return (pipeline_ && !textComplete_) || local_.speaking() || qwen_.busy() || offline_.busy() || player_.active() || !currentSegment_.isEmpty() || !segments_.isEmpty();}
SpeechExpression VoiceController::speechExpression() const{
    auto mood=companion_.effectiveMood();const auto life=companion_.memory().life();
    // Keep supportive delivery for real distress, even when a focus task owns the pose.
    if(EmotionEngine::distressed(life.mood) && life.moodUntil>companion_.currentTime())mood=life.mood;
    const auto user=companion_.pendingText();
    if(!user.isEmpty()){
        const auto classified=EmotionEngine::classify(user,life.growthPercent());
        if(classified!=Emotion::Calm && (!EmotionEngine::distressed(mood) || EmotionEngine::distressed(classified)))mood=classified;
    }
    return SpeechExpression::forVoice(preferences_.audioTts()?preferences_.audioVoice:preferences_.voice,mood,life.growthPercent());
}
void VoiceController::prepareExpression(bool interaction,PetAction action){
    Q_UNUSED(interaction);Q_UNUSED(action);
    // The visible pose cannot override an actual sad/concerned persistent mood.
    utterancePreferences_=preferences_;utterancePreferences_.expression=speechExpression();
}
void VoiceController::stopOutput(){
    recognition_.setOutputActive(false);noticeOutput_=noticePresented_=false;const bool proactive=proactiveOutput_;proactiveOutput_=false;currentDialogueId_.clear();textInteractionTimer_.stop();const bool interaction=interactionOutput_;interactionOutput_=false;pipeline_=false;textComplete_=false;textFlush_.stop();pipelinePump_.stop();textBuffer_.reset();readingTextReply_=false;cloudStreaming_=false;systemOutput_=false;audioDeadline_.stop();qwen_.cancel();offline_.cancel();player_.stop();segments_.clear();currentSegment_.clear();local_.stopSpeaking();if(interaction)emit interactionFinished();if(proactive)companion_.finishProactive();}
void VoiceController::speak(const QString &text,bool interaction,PetAction action,const QString &dialogueId,bool proactive,bool bubble,bool notice){
    stopOutput();recognition_.setOutputActive(true);
    latency_.begin(companion_.latencyId());latencySegment_=0;latency_.mark("speech_requested",{{"characters",text.toUcs4().size()}});
    prepareExpression(interaction,action);
    outputNotice_.clear();
    interactionOutput_=interaction;
    proactiveOutput_=proactive;currentDialogueId_=dialogueId;noticeOutput_=notice;noticePresented_=false;
    if(interaction)emit interactionStarted(action);
    if(bubble)emit speechRequested(text);
    if(!dialogueId.isEmpty() && (bubble || !notice)){companion_.dialoguePresented(dialogueId);noticePresented_=true;}
    if(proactive){companion_.traceProactive("bubble_shown",dialogueId);companion_.traceProactive("tts_requested",selectedVoiceName());}
    systemOutput_=preferences_.legacyVoice("System");
    const auto points=text.toStdU32String();
    const auto count=qsizetype(points.size());
    const qsizetype segmentLength=systemOutput_?100:preferences_.legacyVoice("Serena") && preferences_.preferOffline && offline_.available()?120:QwenSpeech::maxCharacters;
    for(qsizetype i=0;i<count;i+=segmentLength)
        segments_<<QString::fromUcs4(points.data()+i,qMin(segmentLength,count-i));
    nextSegment();
}
void VoiceController::nextSegment(){
    if(segments_.isEmpty()){currentSegment_.clear();outputFinished();return;}
    currentSegment_=segments_.takeFirst();
    ++latencySegment_;latency_.mark("tts_dispatch",{{"segment",latencySegment_},{"characters",currentSegment_.toUcs4().size()},{"transport",preferences_.audioTts()?preferences_.audioTransport:"legacy"}});
    if(systemOutput_){
        playbackLabel_=preferences_.legacyVoice("System")?"系统中文":"系统中文（非"+selectedVoiceName()+"原声）";
        local_.synthesizePcm(currentSegment_);status("正在准备 · "+playbackLabel_);return;
    }
    if(playCached(utterancePreferences_.expressiveCacheId(),"离线缓存 · "+selectedVoiceName()))return;
    if(interactionOutput_ && (preferences_.legacyVoice("Serena") || preferences_.fastResponse) && playBundledInteraction())return;
    if(preferences_.preferOffline){
        if(preferences_.legacyVoice("Serena") && offline_.available()){
            if(playCached("offline:Serena","离线苏瑶"))return;
            offlineLabel_="离线苏瑶";
            const auto error=offline_.start(currentSegment_);
            if(!error.isEmpty())systemSpeechFallback(error);else{if(preferences_.fastResponse)audioDeadline_.start(audioWaitMs_);status("正在生成离线苏瑶 · 等待超时将改用系统中文");}
            return;
        }
    }
    QByteArray key=qgetenv("DASHSCOPE_API_KEY").trimmed();
    try{if(key.isEmpty())key=voiceCredentials_.load();}
    catch(const std::exception &){speechFallback("百炼密钥无法解密");return;}
    const auto error=qwen_.start(currentSegment_,utterancePreferences_,key);key.fill('\0');
    if(!error.isEmpty())speechFallback(error);else{if(preferences_.fastResponse)audioDeadline_.start(audioWaitMs_);status("正在连接"+selectedVoiceName()+"流式语音");}
}
void VoiceController::speechFallback(const QString &reason){
    latency_.mark("tts_failed",{{"segment",latencySegment_}});
    if(proactiveOutput_)companion_.traceProactive("tts_failed",reason);
    if(pipeline_ && preferences_.fastResponse){systemSpeechFallback(reason);return;}
    audioDeadline_.stop();cloudStreaming_=false;qwen_.cancel();offline_.cancel();player_.stop();
    if(playCached(utterancePreferences_.expressiveCacheId(),"离线缓存 · "+selectedVoiceName()))return;
    if(playBundledInteraction())return;
    if(preferences_.legacyVoice("Serena") && offline_.available() && !preferences_.fastResponse){
        offlineLabel_="离线苏瑶";
        if(playCached("offline:Serena",offlineLabel_))return;
        const auto error=offline_.start(currentSegment_);
        if(error.isEmpty()){status(reason+" · 改用"+offlineLabel_);return;}
    }
    systemSpeechFallback(reason);
}
void VoiceController::systemSpeechFallback(const QString &reason){
    // With fast fallback disabled, retain speaker identity for both typed and
    // click speech. A cloud rejection must not silently replace the voice.
    if(!preferences_.legacyVoice("System") && !preferences_.fastResponse){
        streamSuppressed_=userSuppressedTurn_=true;stopOutput();
        status(reason+" · "+selectedVoiceName()+"原声不可用，已停止朗读；未切换其他音色");
        if(active_){companion_.voice().finishOutput(companion_.voice().epoch());localListen();}
        return;
    }
    if(pipeline_){
        audioDeadline_.stop();cloudStreaming_=false;qwen_.cancel();offline_.cancel();
        if(systemOutput_ || currentSegment_.isEmpty()){
            streamSuppressed_=true;stopOutput();status(reason+" · 语音输出已停止，请检查扬声器");return;
        }
        systemOutput_=true;outputNotice_=reason+" · 改用系统中文（非"+selectedVoiceName()+"原声）";
        playbackLabel_="系统中文（非"+selectedVoiceName()+"原声）";
        local_.synthesizePcm(currentSegment_);status(outputNotice_);return;
    }
    audioDeadline_.stop();cloudStreaming_=false;qwen_.cancel();offline_.cancel();player_.stop();
    const auto remaining=currentSegment_+segments_.join("");currentSegment_.clear();segments_.clear();
    if(remaining.isEmpty()){outputFinished();return;}
    if(systemOutput_){stopOutput();status(reason+" · 请检查扬声器");return;}
    outputNotice_=reason+" · 改用系统中文（非"+selectedVoiceName()+"原声）";
    systemOutput_=true;
    const auto points=remaining.toStdU32String();
    for(size_t i=0;i<points.size();i+=100)segments_<<QString::fromUcs4(points.data()+i,qMin(size_t(100),points.size()-i));
    nextSegment();status(reason+" · 本次改用系统中文（不是"+selectedVoiceName()+"）");
}
void VoiceController::playPcm(const QByteArray &pcm,const QString &label){
    audioDeadline_.stop();
    if(pipeline_){if(appendPipelinePcm(pcm,label))segmentProduced();return;}
    playbackLabel_=label;const auto error=player_.play(pcm);
    if(!error.isEmpty())systemSpeechFallback(error);else status(player_.preparingOutput()?"正在初始化扬声器 · 音频已排队":"正在朗读 · "+label+" · 可点打断");
}
bool VoiceController::playCached(const QString &id,const QString &label){
    if(!preferences_.cacheAudio)return false;
    const auto pcm=cache_.read(id,currentSegment_);if(pcm.isEmpty())return false;
    latency_.mark("cache_hit",{{"segment",latencySegment_}});
    playPcm(pcm,label);return true;
}
bool VoiceController::playBundledInteraction(){
    if(!interactionOutput_ || preferences_.audioTts())return false;
    if(!preferences_.legacyVoice("Serena") && !preferences_.fastResponse)return false;
    const auto pcm=interactionAudio(currentSegment_);if(pcm.isEmpty())return false;
    playPcm(pcm,preferences_.legacyVoice("Serena")?"内置离线苏瑶":"内置离线苏瑶（非"+selectedVoiceName()+"原声）");return true;
}
void VoiceController::outputFinished(){
    recognition_.setOutputActive(false);
    audioDeadline_.stop();
    pipeline_=false;textFlush_.stop();pipelinePump_.stop();textBuffer_.reset();
    readingTextReply_=false;
    if(interactionOutput_){interactionOutput_=false;emit interactionFinished();}
    currentDialogueId_.clear();if(proactiveOutput_){proactiveOutput_=false;companion_.finishProactive();}
    if(active_){companion_.voice().finishOutput(companion_.voice().epoch());localListen();}
    else status(outputNotice_.isEmpty()?QString("朗读完成 · 麦克风未开启"):"朗读完成 · "+outputNotice_+" · 麦克风未开启");
}
void VoiceController::receivePartial(const QString &text){
    if(streamSuppressed_ || streamRewrite_ || (!preferences_.readAloud && !active_) || text.isEmpty())return;
    if(!pipeline_){
        stopOutput();recognition_.setOutputActive(true);outputNotice_.clear();interactionOutput_=false;
        latency_.begin(companion_.latencyId());latencySegment_=0;latency_.mark("speech_first_text");
        prepareExpression();
        pipeline_=true;textComplete_=false;readingTextReply_=!active_;systemOutput_=preferences_.legacyVoice("System");
        if(active_)companion_.voice().beginOutput(companion_.voice().epoch());
    }
    if(!textBuffer_.update(text)){stopOutput();streamRewrite_=true;return;}
    enqueueText(textBuffer_.takeReady());
    if(textBuffer_.pending() && !textFlush_.isActive())textFlush_.start();
}
void VoiceController::receiveFinal(const QString &text,const QString &source){
    if(userSuppressedTurn_)return;
    if(!preferences_.readAloud && !active_)return;
    if(streamSuppressed_ && source!="offline")return;
    if(pipeline_ && source!="offline" && textBuffer_.update(text)){
        textComplete_=true;textFlush_.stop();enqueueText(textBuffer_.takeReady(true));pipelinePump_.start(0);return;
    }
    recognition_.stop();if(active_)companion_.voice().beginOutput(companion_.voice().epoch());
    speak(text);readingTextReply_=!active_ && outputBusy();
}
void VoiceController::enqueueText(const QStringList &segments){
    if(!pipeline_)return;
    for(const auto &segment:segments){segments_<<segment;latency_.mark("segment_ready",{{"characters",segment.toUcs4().size()}});emit segmentQueued(segment);emit speechRequested(segment);}
    if(!segments_.isEmpty() && !pipelinePump_.isActive())pipelinePump_.start(0);
}
void VoiceController::pumpPipeline(){
    if(!pipeline_ || !currentSegment_.isEmpty() || qwen_.busy() || offline_.busy() || local_.speaking())return;
    if(segments_.isEmpty()){
        if(textComplete_){if(player_.active())player_.finishStream();else outputFinished();}
        else if(!player_.active())status("文字与语音并行 · 等待下一句");
        return;
    }
    // Bounded audio prefetch: prepare the next sentence while the previous plays.
    if(player_.bufferedMilliseconds()>5000){pipelinePump_.start(30);return;}
    nextSegment();
}
bool VoiceController::appendPipelinePcm(const QByteArray &pcm,const QString &label){
    playbackLabel_=label;
    if(!player_.active()){
        const auto error=player_.beginStream();if(!error.isEmpty()){systemSpeechFallback(error);return false;}
    }
    const auto error=player_.appendPcm(pcm);if(!error.isEmpty()){systemSpeechFallback(error);return false;}
    status(player_.preparingOutput()?"正在初始化扬声器 · 音频已排队":"边生成边朗读 · "+label);return true;
}
void VoiceController::segmentProduced(){
    currentSegment_.clear();audioDeadline_.stop();pipelinePump_.start(0);
}
void VoiceController::saveVoicePreferences(const VoicePreferences &preferences,const QByteArray &key){
    if(preferences.audioTts()){const auto error=QwenSpeech::configurationError(preferences);if(!error.isEmpty())throw std::runtime_error(error.toUtf8().constData());}
    endCall();if(!key.trimmed().isEmpty())voiceCredentials_.save(key.trimmed());
    preferences.save(preferenceFile_);preferences_=preferences;status("已选择"+selectedVoiceName()+" · "+voiceCredentialStatus());
}
void VoiceController::removeVoiceKey(){endCall();voiceCredentials_.remove();status(voiceCredentialStatus());}
QString VoiceController::voiceCredentialStatus() const{
    if(!qgetenv("DASHSCOPE_API_KEY").trimmed().isEmpty())return "百炼密钥来自环境变量";
    if(QFileInfo::exists(companion_.dataDirectory()+"/dashscope-key.dpapi"))return "已保存加密百炼密钥";
    return offline_.available()?"未配置百炼密钥 · 可用离线苏瑶与已缓存音频":"未配置百炼密钥 · 可用已有缓存；是否使用备用音色以开关为准";
}
QString VoiceController::offlineStatus() const{
    return (offline_.available()?QString("离线苏瑶已安装，可合成新句子。"):QString("离线苏瑶尚未准备。"))+
        "云端音色的新句子需联网；关闭备用音色时，原声失败不会自动变声。";
}
}
