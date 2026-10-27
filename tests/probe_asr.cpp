#include "local_speech.h"
#include "audio_duplex.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <QRandomGenerator>
#include <cmath>
using namespace dragon;
static QByteArray wav(const QByteArray &pcm){QByteArray b;auto u16=[&](quint16 n){char v[2];qToLittleEndian(n,reinterpret_cast<uchar*>(v));b.append(v,2);};auto u32=[&](quint32 n){char v[4];qToLittleEndian(n,reinterpret_cast<uchar*>(v));b.append(v,4);};b+="RIFF";u32(pcm.size()+36);b+="WAVEfmt ";u32(16);u16(1);u16(1);u32(16000);u32(32000);u16(2);u16(16);b+="data";u32(pcm.size());b+=pcm;return b;}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);if(argc<2)return 2;const auto folder=QString::fromLocal8Bit(argv[1]);QDir().mkpath(folder);LocalSpeech legacy;QJsonArray manifest;
    const QStringList texts={"今天下午我想安静地看一会儿书。","请帮我查看今天的任务和剩余时间。","先看看这个项目，然后再分析错误，暂时不要修改。","我想学习 Python 和 C++ 的区别。","请解释 API 和 Agent 是什么意思。","这个项目叫 DragonGPT，代码放在 GitHub。","今天的天气听起来很不错。","请提醒我喝水和休息。","请先读取文件，再告诉我你的计划。","霜翎说完以后，我再继续讲话。"};
    const QStringList categories={"普通中文","快速中文","讲话停顿","中英混合","编程术语","项目热词","环境噪声","低音量","连续指令","TTS相邻"};
    for(int i=0;i<texts.size();++i){
        const auto source=folder+QString("/source-%1.wav").arg(i);const auto error=LocalSpeech::renderWav(texts[i],source);if(!error.isEmpty())return 3;QFile f(source);if(!f.open(QIODevice::ReadOnly))return 4;QByteArray pcm;const auto decode=decodePcm24kWav(f.readAll(),pcm);f.close();if(!decode.isEmpty())return 5;
        auto input=voicePcmFormat();if(i==1)input.setSampleRate(31200);QAudioFormat output;output.setSampleRate(16000);output.setChannelCount(1);output.setSampleFormat(QAudioFormat::Int16);PcmResampler resampler(input,output);pcm=resampler.feed(pcm);
        QRandomGenerator random(42);if(i==6 || i==7)for(qsizetype j=0;j+1<pcm.size();j+=2){auto *p=reinterpret_cast<uchar*>(pcm.data()+j);int value=qFromLittleEndian<qint16>(p);if(i==6)value+=int(random.bounded(500))-250;else value=qRound(value*0.12);qToLittleEndian<qint16>(qint16(qBound(-32768,value,32767)),p);}
        pcm.prepend(QByteArray(3200,'\0'));pcm.append(QByteArray(19200,'\0'));const auto name=QString("sample-%1.wav").arg(i);QFile out(folder+"/"+name);if(!out.open(QIODevice::WriteOnly))return 6;out.write(wav(pcm));out.close();QString baseline;const auto baselineError=legacy.recognizeWav(out.fileName(),baseline,10000);
        manifest.append(QJsonObject{{"audio",name},{"reference",texts[i]},{"category",categories[i]},{"synthetic",true},{"baseline",baseline},{"baseline_error",baselineError},{"note","Windows SAPI synthesized test audio; category does not establish a real human/microphone scenario"}});
    }
    QFile manifestFile(folder+"/manifest.json");if(!manifestFile.open(QIODevice::WriteOnly))return 7;manifestFile.write(QJsonDocument(manifest).toJson());return 0;
}
