#include "local_speech.h"
#include "audio_duplex.h"
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QFutureWatcher>
#include <QTemporaryDir>
#include <QtConcurrent/QtConcurrentRun>
#include <QtEndian>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sapi.h>

namespace dragon {
namespace {
constexpr GUID waveFormatId{0xC31ADBAE,0x527F,0x4FF5,{0xA2,0x30,0xF6,0x2B,0xB6,0x1F,0xF7,0x0C}};
template<class T> void release(T *&pointer) {if(pointer) {pointer->Release();pointer=nullptr;}}
void clearEvent(SPEVENT &event) {
    if(event.elParamType==SPET_LPARAM_IS_OBJECT || event.elParamType==SPET_LPARAM_IS_TOKEN) {
        if(event.lParam) reinterpret_cast<IUnknown*>(event.lParam)->Release();
    } else if(event.elParamType==SPET_LPARAM_IS_POINTER || event.elParamType==SPET_LPARAM_IS_STRING) CoTaskMemFree(reinterpret_cast<void*>(event.lParam));
}
ISpObjectToken *firstToken(const wchar_t *category,const wchar_t *filter=nullptr) {
    ISpObjectTokenCategory *group=nullptr;IEnumSpObjectTokens *list=nullptr;ISpObjectToken *token=nullptr;
    if(SUCCEEDED(CoCreateInstance(CLSID_SpObjectTokenCategory,nullptr,CLSCTX_ALL,IID_ISpObjectTokenCategory,reinterpret_cast<void**>(&group)))) {
        if(SUCCEEDED(group->SetId(category,FALSE)) && SUCCEEDED(group->EnumTokens(filter,nullptr,&list))) list->Next(1,&token,nullptr);
    }
    release(list);release(group);return token;
}
void selectChineseVoice(ISpVoice *voice) {auto *token=firstToken(SPCAT_VOICES,L"Language=804");if(token){voice->SetVoice(token);token->Release();}voice->SetRate(-1);voice->SetVolume(80);}
}
struct LocalSpeech::Impl {
    ISpVoice *voice=nullptr;
    ISpRecognizer *recognizer=nullptr;
    ISpRecoContext *context=nullptr;
    ISpRecoGrammar *grammar=nullptr;
    bool comOwned=false,active=false,talking=false;
    ULONG stream=0;
    Impl(){const auto result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);comOwned=SUCCEEDED(result);}
    ~Impl(){release(grammar);release(context);release(recognizer);release(voice);if(comOwned)CoUninitialize();}
    QString tts() {
        if(voice) return {};
        if(FAILED(CoCreateInstance(CLSID_SpVoice,nullptr,CLSCTX_ALL,IID_ISpVoice,reinterpret_cast<void**>(&voice)))) return "无法初始化 Windows 本地朗读引擎";
        selectChineseVoice(voice);
        const ULONGLONG events=(1ULL<<SPEI_START_INPUT_STREAM)|(1ULL<<SPEI_END_INPUT_STREAM)|(1ULL<<SPEI_VISEME);
        voice->SetInterest(events,events);return {};
    }
};
LocalSpeech::LocalSpeech(QObject *parent):QObject(parent),impl_(std::make_unique<Impl>()) {
    timer_.setInterval(25);connect(&timer_,&QTimer::timeout,this,&LocalSpeech::poll);timer_.start();
}
LocalSpeech::~LocalSpeech(){stopListening();stopSpeaking();}
bool LocalSpeech::speaking() const{return impl_->talking || rendering_;}
bool LocalSpeech::listening() const{return impl_->active;}
QString LocalSpeech::capabilities() const {
    auto *voice=firstToken(SPCAT_VOICES,L"Language=804");auto *reco=firstToken(SPCAT_RECOGNIZERS,L"Language=804");
    const auto result=QString("中文朗读：%1 · 中文识别：%2").arg(voice?"可用":"使用系统默认或需安装语音包",reco?"已安装":"未检测到，请安装系统中文识别包");
    release(voice);release(reco);return result;
}
QString LocalSpeech::speak(const QString &text) {
    stopSpeaking();const auto error=impl_->tts();if(!error.isEmpty())return error;
    const auto value=text.toStdWString();
    if(FAILED(impl_->voice->Speak(value.c_str(),SPF_ASYNC|SPF_PURGEBEFORESPEAK|SPF_IS_NOT_XML,&impl_->stream))) return "本地朗读失败，请检查输出设备";
    impl_->talking=true;return {};
}
void LocalSpeech::stopSpeaking() {
    ++generation_;rendering_=false;
    if(impl_->voice) {
        impl_->voice->Speak(nullptr,SPF_ASYNC|SPF_PURGEBEFORESPEAK,nullptr);
        SPEVENT event{};ULONG count=0;while(impl_->voice->GetEvents(1,&event,&count)==S_OK && count)clearEvent(event);
    }
    impl_->talking=false;emit mouthChanged(0);
}
void LocalSpeech::synthesizePcm(const QString &text){
    stopSpeaking();rendering_=true;const auto generation=generation_;
    using Result=QPair<QByteArray,QString>;
    auto *watcher=new QFutureWatcher<Result>(this);
    connect(watcher,&QFutureWatcher<Result>::finished,this,[this,watcher,generation]{
        const auto result=watcher->result();watcher->deleteLater();
        if(generation!=generation_)return;
        rendering_=false;
        if(result.second.isEmpty())emit pcmReady(result.first);else emit synthesisFailed(result.second);
    });
    watcher->setFuture(QtConcurrent::run([text]()->Result{
        const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        struct ComScope{bool owned;~ComScope(){if(owned)CoUninitialize();}} scope{SUCCEEDED(com)};
        if(FAILED(com) && com!=RPC_E_CHANGED_MODE)return {{},"无法初始化本地语音线程"};
        QTemporaryDir temporary(QDir::tempPath()+"/DragonGPT-tts-XXXXXX");
        if(!temporary.isValid())return {{},"无法创建本地语音临时目录"};
        const auto path=temporary.filePath("speech.wav");
        const auto error=LocalSpeech::renderWav(text,path);if(!error.isEmpty())return {{},error};
        QFile file(path);if(!file.open(QIODevice::ReadOnly) || file.size()>16*1024*1024)return {{},"无法读取本地语音"};
        const auto bytes=file.readAll();
        QByteArray pcm;const auto decodeError=decodePcm24kWav(bytes,pcm);return {pcm,decodeError};
    }));
}
QString LocalSpeech::listen() {
    stopSpeaking();
    if(!impl_->recognizer) {
        auto *recognizerToken=firstToken(SPCAT_RECOGNIZERS,L"Language=804");
        if(!recognizerToken)return "未安装中文语音识别引擎；文字对话仍可使用";
        auto result=CoCreateInstance(CLSID_SpInprocRecognizer,nullptr,CLSCTX_ALL,IID_ISpRecognizer,reinterpret_cast<void**>(&impl_->recognizer));
        if(SUCCEEDED(result))result=impl_->recognizer->SetRecognizer(recognizerToken);
        release(recognizerToken);
        auto *microphone=firstToken(SPCAT_AUDIOIN);
        if(SUCCEEDED(result) && microphone)result=impl_->recognizer->SetInput(microphone,TRUE);
        else if(!microphone)result=E_FAIL;
        release(microphone);
        if(SUCCEEDED(result))result=impl_->recognizer->CreateRecoContext(&impl_->context);
        if(SUCCEEDED(result))result=impl_->context->CreateGrammar(1,&impl_->grammar);
        if(SUCCEEDED(result))result=impl_->grammar->LoadDictation(nullptr,SPLO_STATIC);
        if(FAILED(result)) {stopListening();release(impl_->grammar);release(impl_->context);release(impl_->recognizer);return "无法打开本地中文识别或麦克风，请检查设备和系统权限";}
        const ULONGLONG events=(1ULL<<SPEI_RECOGNITION)|(1ULL<<SPEI_FALSE_RECOGNITION);
        impl_->context->SetInterest(events,events);
    }
    if(FAILED(impl_->grammar->SetDictationState(SPRS_ACTIVE)) || FAILED(impl_->recognizer->SetRecoState(SPRST_ACTIVE_ALWAYS)))return "麦克风未能进入聆听状态";
    impl_->active=true;return {};
}
void LocalSpeech::stopListening() {
    impl_->active=false;
    if(impl_->grammar)impl_->grammar->SetDictationState(SPRS_INACTIVE);
    if(impl_->recognizer)impl_->recognizer->SetRecoState(SPRST_INACTIVE_WITH_PURGE);
    if(impl_->context){SPEVENT event{};ULONG count=0;while(impl_->context->GetEvents(1,&event,&count)==S_OK && count)clearEvent(event);}
}
void LocalSpeech::poll() {
    SPEVENT event{};ULONG count=0;
    if(impl_->voice)while(impl_->voice->GetEvents(1,&event,&count)==S_OK && count) {
        if(event.ulStreamNum==impl_->stream) {
            if(event.eEventId==SPEI_START_INPUT_STREAM)emit speechStarted();
            if(event.eEventId==SPEI_VISEME)emit mouthChanged(LOWORD(event.lParam)==0?0.0:0.3+double(LOWORD(event.lParam)%5)*0.14);
            if(event.eEventId==SPEI_END_INPUT_STREAM && impl_->talking){impl_->talking=false;emit mouthChanged(0);emit speechFinished();}
        }
        clearEvent(event);
    }
    if(impl_->context && impl_->active)while(impl_->context->GetEvents(1,&event,&count)==S_OK && count) {
        QString transcript;
        if(event.eEventId==SPEI_RECOGNITION) {
            auto *result=reinterpret_cast<ISpRecoResult*>(event.lParam);wchar_t *text=nullptr;
            if(result && SUCCEEDED(result->GetText(SP_GETWHOLEPHRASE,SP_GETWHOLEPHRASE,TRUE,&text,nullptr)) && text){transcript=QString::fromWCharArray(text);CoTaskMemFree(text);}
        }
        clearEvent(event);
        if(!transcript.trimmed().isEmpty()){stopListening();emit recognized(transcript.trimmed().left(2000));break;}
    }
}
QString LocalSpeech::renderWav(const QString &text,const QString &path) {
    ISpVoice *voice=nullptr;ISpStream *stream=nullptr;
    auto result=CoCreateInstance(CLSID_SpVoice,nullptr,CLSCTX_ALL,IID_ISpVoice,reinterpret_cast<void**>(&voice));
    if(SUCCEEDED(result))result=CoCreateInstance(CLSID_SpStream,nullptr,CLSCTX_ALL,IID_ISpStream,reinterpret_cast<void**>(&stream));
    WAVEFORMATEX format{WAVE_FORMAT_PCM,1,24000,48000,2,16,0};const auto filename=QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()).toStdWString();
    if(SUCCEEDED(result))result=stream->BindToFile(filename.c_str(),SPFM_CREATE_ALWAYS,&waveFormatId,&format,0);
    if(SUCCEEDED(result)){selectChineseVoice(voice);result=voice->SetOutput(stream,TRUE);}
    if(SUCCEEDED(result)){const auto value=text.toStdWString();result=voice->Speak(value.c_str(),SPF_IS_NOT_XML,nullptr);}
    if(stream)stream->Close();
    release(stream);release(voice);
    if(FAILED(result))qWarning("SAPI WAV render failed: HRESULT 0x%08lx",static_cast<unsigned long>(result));
    return SUCCEEDED(result)?QString{}:QString("无法合成本地语音文件");
}
QString LocalSpeech::recognizeWav(const QString &path,QString &transcript,int timeoutMs) {
    ISpRecognizer *recognizer=nullptr;ISpRecoContext *context=nullptr;ISpRecoGrammar *grammar=nullptr;ISpStream *stream=nullptr;
    auto *token=firstToken(SPCAT_RECOGNIZERS,L"Language=804");
    auto result=token?CoCreateInstance(CLSID_SpInprocRecognizer,nullptr,CLSCTX_ALL,IID_ISpRecognizer,reinterpret_cast<void**>(&recognizer)):E_FAIL;
    if(SUCCEEDED(result))result=recognizer->SetRecognizer(token);
    release(token);
    if(SUCCEEDED(result))result=CoCreateInstance(CLSID_SpStream,nullptr,CLSCTX_ALL,IID_ISpStream,reinterpret_cast<void**>(&stream));
    const auto filename=QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()).toStdWString();
    if(SUCCEEDED(result))result=stream->BindToFile(filename.c_str(),SPFM_OPEN_READONLY,nullptr,nullptr,0);
    if(SUCCEEDED(result))result=recognizer->SetInput(stream,TRUE);
    if(SUCCEEDED(result))result=recognizer->CreateRecoContext(&context);
    if(SUCCEEDED(result))result=context->CreateGrammar(1,&grammar);
    if(SUCCEEDED(result))result=grammar->LoadDictation(nullptr,SPLO_STATIC);
    if(SUCCEEDED(result)){const ULONGLONG events=(1ULL<<SPEI_RECOGNITION)|(1ULL<<SPEI_END_SR_STREAM);context->SetInterest(events,events);result=grammar->SetDictationState(SPRS_ACTIVE);}
    if(SUCCEEDED(result))result=recognizer->SetRecoState(SPRST_ACTIVE_ALWAYS);
    bool ended=false;QElapsedTimer clock;clock.start();transcript.clear();
    while(SUCCEEDED(result) && !ended && clock.elapsed()<qBound(100,timeoutMs,10000)){
        SPEVENT event{};ULONG count=0;
        while(context->GetEvents(1,&event,&count)==S_OK && count){
            if(event.eEventId==SPEI_RECOGNITION){
                auto *recognized=reinterpret_cast<ISpRecoResult*>(event.lParam);wchar_t *text=nullptr;
                if(recognized && SUCCEEDED(recognized->GetText(SP_GETWHOLEPHRASE,SP_GETWHOLEPHRASE,TRUE,&text,nullptr)) && text){transcript+=QString::fromWCharArray(text);CoTaskMemFree(text);}
            }
            if(event.eEventId==SPEI_END_SR_STREAM)ended=true;
            clearEvent(event);
        }
        QCoreApplication::processEvents();QThread::msleep(10);
    }
    if(grammar)grammar->SetDictationState(SPRS_INACTIVE);
    if(recognizer)recognizer->SetRecoState(SPRST_INACTIVE_WITH_PURGE);
    release(grammar);release(context);release(recognizer);if(stream)stream->Close();release(stream);
    return FAILED(result)?QString("文件语音识别引擎初始化失败"):transcript.isEmpty()?QString("语音文件未识别出文字"):QString{};
}
}
