#include "audio_duplex.h"
#include <QMediaDevices>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace dragon {
QAudioFormat voicePcmFormat(){QAudioFormat f;f.setSampleRate(24000);f.setChannelCount(1);f.setSampleFormat(QAudioFormat::Int16);return f;}
QString decodePcm24kWav(const QByteArray &wav,QByteArray &pcm){
    pcm.clear();
    if(wav.size()<12 || wav.size()>16*1024*1024+4096 || wav.first(4)!="RIFF" || wav.mid(8,4)!="WAVE")return "音频文件不是有效 WAV";
    const quint64 end=quint64(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(wav.constData()+4)))+8;
    if(end<12 || end>quint64(wav.size()))return "WAV 文件被截断";
    bool format=false;QByteArray audio;
    for(quint64 p=12;p+8<=end;){
        const auto count=qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(wav.constData()+p+4));
        if(quint64(count)>end-p-8)return "WAV 音频块长度无效";
        const auto name=wav.mid(qsizetype(p),4);
        if(name=="fmt "){
            if(count<16)return "WAV 格式块无效";
            const auto *f=reinterpret_cast<const uchar*>(wav.constData()+p+8);
            format=qFromLittleEndian<quint16>(f)==1 && qFromLittleEndian<quint16>(f+2)==1 && qFromLittleEndian<quint32>(f+4)==24000 && qFromLittleEndian<quint32>(f+8)==48000 && qFromLittleEndian<quint16>(f+12)==2 && qFromLittleEndian<quint16>(f+14)==16;
            if(!format)return "WAV 必须为 24 kHz 单声道 PCM16";
        }else if(name=="data"){
            if(!audio.isEmpty() || !count || count%2 || count>16*1024*1024)return "WAV 音频数据无效";
            audio=wav.mid(qsizetype(p+8),count);
        }
        p+=8+quint64(count)+(count%2);
    }
    if(!format || audio.isEmpty())return "WAV 缺少有效格式或音频";
    pcm=audio;return {};
}
namespace {
double decode(const char *data,QAudioFormat::SampleFormat format) {
    switch(format){
    case QAudioFormat::UInt8:return (quint8(*data)-128)/128.0;
    case QAudioFormat::Int16:return qFromLittleEndian<qint16>(reinterpret_cast<const uchar*>(data))/32768.0;
    case QAudioFormat::Int32:return qFromLittleEndian<qint32>(reinterpret_cast<const uchar*>(data))/2147483648.0;
    case QAudioFormat::Float:{float v;std::memcpy(&v,data,4);return std::isfinite(v)?std::clamp(double(v),-1.0,1.0):0;}
    default:return 0;}
}
void encode(QByteArray &out,double value,QAudioFormat::SampleFormat format){
    value=std::clamp(value,-1.0,1.0);
    if(format==QAudioFormat::UInt8)out.append(char(qRound(value*127+128)));
    else if(format==QAudioFormat::Int16){char bytes[2];qToLittleEndian<qint16>(qint16(std::clamp(qRound(value*32767),-32768,32767)),reinterpret_cast<uchar*>(bytes));out.append(bytes,2);}
    else if(format==QAudioFormat::Int32){char bytes[4];qToLittleEndian<qint32>(qint32(value*2147483647),reinterpret_cast<uchar*>(bytes));out.append(bytes,4);}
    else if(format==QAudioFormat::Float){const auto sample=float(value);out.append(reinterpret_cast<const char*>(&sample),4);}
}
}
QByteArray PcmResampler::feed(const QByteArray &data){
    if(!input_.isValid() || !output_.isValid())return {};
    carry_+=data;const int bytes=input_.bytesPerFrame(),channelBytes=input_.bytesPerSample();
    if(bytes<=0 || channelBytes<=0)return {};
    const auto frames=carry_.size()/bytes;
    for(qsizetype i=0;i<frames;++i){double value=0;for(int c=0;c<input_.channelCount();++c)value+=decode(carry_.constData()+i*bytes+c*channelBytes,input_.sampleFormat());samples_.append(value/input_.channelCount());}
    carry_.remove(0,frames*bytes);QByteArray result;
    const double step=double(input_.sampleRate())/output_.sampleRate();
    while(phase_+1<samples_.size()){
        const auto index=qsizetype(phase_);const double fraction=phase_-index;
        const double value=samples_[index]*(1-fraction)+samples_[index+1]*fraction;
        for(int channel=0;channel<output_.channelCount();++channel)encode(result,value,output_.sampleFormat());
        phase_+=step;
    }
    const auto drop=std::min(qsizetype(phase_),std::max(qsizetype(0),samples_.size()-1));
    if(drop>0){samples_.remove(0,drop);phase_-=drop;}return result;
}
double pcmEnergy(const QByteArray &bytes){
    if(bytes.size()<2)return 0;
    double sum=0;
    for(qsizetype i=0;i+1<bytes.size();i+=2){const double value=qFromLittleEndian<qint16>(reinterpret_cast<const uchar*>(bytes.constData()+i))/32768.0;sum+=value*value;}
    return std::clamp(std::sqrt(sum/(bytes.size()/2))*4.0,0.0,1.0);
}
AudioDuplex::AudioDuplex(QObject *parent):QObject(parent){pump_.setInterval(10);connect(&pump_,&QTimer::timeout,this,&AudioDuplex::flush);}
QString AudioDuplex::start(){
    stop();const auto microphone=QMediaDevices::defaultAudioInput(),speaker=QMediaDevices::defaultAudioOutput();
    if(microphone.isNull() || speaker.isNull())return "未找到麦克风或扬声器";
    const auto wire=voicePcmFormat();auto in=microphone.isFormatSupported(wire)?wire:microphone.preferredFormat();auto out=speaker.isFormatSupported(wire)?wire:speaker.preferredFormat();
    if(!in.isValid() || !out.isValid())return "音频设备格式不可用";
    input_=std::make_unique<PcmResampler>(in,wire);output_=std::make_unique<PcmResampler>(wire,out);
    source_=std::make_unique<QAudioSource>(microphone,in);sink_=std::make_unique<QAudioSink>(speaker,out);
    source_->setBufferSize(in.bytesForDuration(100000));sink_->setBufferSize(out.bytesForDuration(200000));
    active_=true;playback_=sink_->start();capture_=source_->start();
    if(!playback_ || !capture_ || source_->error()!=QAudio::NoError || sink_->error()!=QAudio::NoError){stop();return "打开音频设备失败，请检查系统麦克风权限";}
    connect(capture_,&QIODevice::readyRead,this,[this]{if(active_ && capture_){const auto pcm=input_->feed(capture_->readAll());if(!pcm.isEmpty())emit captured(pcm);}});
    connect(source_.get(),&QAudioSource::stateChanged,this,[this](QAudio::State state){if(active_ && state==QAudio::StoppedState && source_->error()!=QAudio::NoError)emit failed("麦克风连接中断");});
    turnStartUs_=0;pump_.start();return {};
}
void AudioDuplex::stop(){active_=false;pump_.stop();if(source_)source_->stop();if(sink_)sink_->stop();source_.reset();sink_.reset();capture_=playback_=nullptr;queue_.clear();emit mouthChanged(0);}
void AudioDuplex::mute(bool muted){if(source_){if(muted)source_->suspend();else source_->resume();}}
void AudioDuplex::play(const QByteArray &pcm){
    if(!active_ || !sink_)return;
    if(queue_.isEmpty() && sink_->state()==QAudio::IdleState)turnStartUs_=sink_->processedUSecs();
    const auto converted=output_->feed(pcm);if(queue_.size()+converted.size()>sink_->format().bytesForDuration(5000000)){emit failed("音频播放队列超过限制");return;}
    queue_+=converted;emit mouthChanged(pcmEnergy(pcm));flush();
}
void AudioDuplex::flush(){
    if(!active_ || !sink_ || !playback_)return;
    if(!queue_.isEmpty()){const auto count=std::min(qsizetype(sink_->bytesFree()),queue_.size());if(count>0){const auto written=playback_->write(queue_.constData(),count);if(written>0)queue_.remove(0,written);}}
    if(queue_.isEmpty() && sink_->state()==QAudio::IdleState)emit mouthChanged(0);
}
void AudioDuplex::clearPlayback(){queue_.clear();if(sink_){sink_->reset();playback_=sink_->start();turnStartUs_=sink_->processedUSecs();output_=std::make_unique<PcmResampler>(voicePcmFormat(),sink_->format());}emit mouthChanged(0);}
int AudioDuplex::playedMilliseconds() const{return sink_?int(std::max(qint64(0),sink_->processedUSecs()-turnStartUs_)/1000):0;}
}
