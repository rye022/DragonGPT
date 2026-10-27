#pragma once
#include "voice_controller.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMediaDevices>
#include <QProgressBar>
#include <QRegularExpression>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
namespace dragon {
// Header-only UI helper so existing UI test targets continue sharing one page.
inline QWidget *asrSettingsUi(VoiceController &voice,QWidget *parent){
    auto *group=new QGroupBox("语音识别 4.0 · 与朗读音色独立",parent);group->setObjectName("asrSettings");auto *layout=new QVBoxLayout(group);auto *form=new QFormLayout;const auto &settings=voice.recognition().settings();
    auto *engine=new QComboBox;engine->setObjectName("asrEngine");engine->addItem("Windows 中文 SAPI（兼容）","sapi");engine->addItem("本地 faster-whisper + WebRTC VAD","local");engine->addItem("云端兼容转写接口 + WebRTC VAD","cloud");engine->setCurrentIndex(qMax(0,engine->findData(settings.engine)));form->addRow("识别引擎",engine);
    auto *language=new QComboBox;language->addItem("中文","zh");language->addItem("自动检测（本地 / 云端）","");language->addItem("English（本地 / 云端）","en");language->setCurrentIndex(qMax(0,language->findData(settings.language)));form->addRow("语言",language);
    auto *microphone=new QComboBox;microphone->addItem("系统默认设备","");form->addRow("麦克风（本地 / 云端）",microphone);
    auto *devices=new QFutureWatcher<QList<QAudioDevice>>(group);const auto selectedMicrophone=settings.microphone;
    QObject::connect(devices,&QFutureWatcher<QList<QAudioDevice>>::finished,group,[devices,microphone,selectedMicrophone]{for(const auto &d:devices->result())microphone->addItem(d.description(),QString::fromUtf8(d.id().toBase64()));microphone->setCurrentIndex(qMax(0,microphone->findData(selectedMicrophone)));devices->deleteLater();});
    devices->setFuture(QtConcurrent::run([]{return QMediaDevices::audioInputs();}));
    auto *volume=new QProgressBar;volume->setRange(0,100);volume->setValue(0);volume->setTextVisible(true);form->addRow("输入音量",volume);QObject::connect(&voice.recognition(),&SpeechRecognitionService::level,volume,[volume](double n){volume->setValue(qRound(n*100));});
    auto *silence=new QSpinBox;silence->setObjectName("asrSilenceMs");silence->setRange(600,3000);silence->setSingleStep(100);silence->setSuffix(" ms");silence->setValue(settings.silenceMs);form->addRow("结束静音时长",silence);
    auto *sensitivity=new QSpinBox;sensitivity->setRange(0,3);sensitivity->setValue(settings.sensitivity);form->addRow("WebRTC VAD 严格度 0–3",sensitivity);
    auto *words=new QLineEdit(settings.hotwords.join(", "));words->setObjectName("asrHotwords");words->setMaxLength(2000);form->addRow("热词 / 项目名（逗号分隔；支持 别名=>术语）",words);
    auto *automatic=new QCheckBox("普通聊天最终稿自动发送；不确定、术语修改或操作类指令仍确认");automatic->setObjectName("asrAutoSend");automatic->setChecked(settings.autoSend);layout->addLayout(form);layout->addWidget(automatic);
    auto *python=new QLineEdit(settings.python);auto *model=new QLineEdit(settings.modelPath);auto *device=new QComboBox;device->addItem("CPU int8（默认，2线程）","cpu");device->addItem("CUDA int8_float16（失败回退CPU）","cuda");device->setCurrentIndex(qMax(0,device->findData(settings.device)));
    auto *details=new QFormLayout;details->addRow("ASR Python 路径",python);details->addRow("已准备的本地模型目录",model);details->addRow("本地推理设备",device);
    auto *endpoint=new QLineEdit(settings.endpoint);endpoint->setPlaceholderText("明确授权的 HTTPS multipart /audio/transcriptions 接口");auto *cloudModel=new QLineEdit(settings.cloudModel);auto *key=new QLineEdit;key->setEchoMode(QLineEdit::Password);key->setPlaceholderText("留空保留已保存的 ASR 密钥（DPAPI）");details->addRow("云端转写地址",endpoint);details->addRow("云端模型 ID",cloudModel);details->addRow("云端 ASR 凭据",key);layout->addLayout(details);
    auto *save=new QPushButton("保存识别配置（不更换朗读音色）");save->setObjectName("saveAsrSettings");layout->addWidget(save);
    auto *notice=new QLabel("默认先核对最终识别稿。SAPI 使用系统中文引擎与系统麦克风，静音聚合并显示临时假设；本地/云端用 16kHz PCM16、300ms 前置缓存与 WebRTC VAD。30 秒单段上限，半双工与650ms回声尾部保护。没有可信置信度时不显示分数。\n本地模型需自行准备；不自动下载。CUDA需兼容的 CTranslate2、CUDA12/cuDNN9，失败会明确标记CPU回退。云端模式将本次音频发送到你配置的服务，可能计费。");notice->setWordWrap(true);notice->setTextFormat(Qt::PlainText);layout->addWidget(notice);
    QObject::connect(save,&QPushButton::clicked,group,[&voice,engine,language,microphone,silence,sensitivity,words,automatic,python,model,device,endpoint,cloudModel,key,notice]{
        AsrSettings s;s.engine=engine->currentData().toString();s.language=language->currentData().toString();s.microphone=microphone->currentData().toString();s.silenceMs=silence->value();s.sensitivity=sensitivity->value();s.hotwords=words->text().split(QRegularExpression("[,，]"),Qt::SkipEmptyParts);for(auto &w:s.hotwords)w=w.trimmed();s.autoSend=automatic->isChecked();s.python=python->text().trimmed();s.modelPath=model->text().trimmed();s.device=device->currentData().toString();s.endpoint=endpoint->text().trimmed();s.cloudModel=cloudModel->text().trimmed();
        voice.endCall();try{auto secret=key->text().toUtf8();voice.recognition().configure(s,secret);secret.fill('\0');key->clear();notice->setText("语音识别设置已保存。只在点击「开始语音」后打开麦克风。GPU / 云端能力需实际运行验证。");}catch(const std::exception &){notice->setText("识别设置或密钥保存失败，请检查权限");}
    });return group;
}
}
