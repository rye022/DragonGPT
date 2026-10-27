#include "companion_window.h"
#include "diagnostics.h"
#include "smart_page.h"
#include "agent_page.h"
#include <QScrollArea>
#include <QScrollBar>
#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QDataStream>
#include <QFile>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QSaveFile>
#include <QLineEdit>
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QSslSocket>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTimer>
#include <memory>

int main(int argc,char **argv) {
    qputenv("QT_MEDIA_BACKEND","windows");
    QApplication app(argc,argv);
    app.setApplicationName("DragonGPT"); app.setOrganizationName("DragonGPT"); app.setApplicationVersion("4.0.0");
    app.setFont(QFont("Microsoft YaHei UI",10));
    QCommandLineParser parser; parser.addHelpOption(); parser.addVersionOption();
    const auto legacyData=QCoreApplication::applicationDirPath()+"/data";
    const auto defaultData=QFile::exists(legacyData+"/memory.sqlite")?legacyData:QDir(qEnvironmentVariable("LOCALAPPDATA",QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))).filePath("DragonGPT/UserData");
    parser.addOption({"data-dir","User data directory (legacy portable data is retained)","directory",defaultData});
    parser.addOption({"smoke-test","Exercise UI and render previews, then exit"});
    parser.addOption({"preview-dir","Destination for smoke-test renders","directory"});
    parser.addOption({"desktop","Show the companion on desktop without the control window"});
    parser.addOption({"quit","Gracefully stop the instance using this data directory"});
    parser.addOption({"hide-pet","Hide the pet in the instance using this data directory"});
    parser.addOption({"restore-backup","Restore a verified local SQLite backup, preserving a rollback copy","sqlite-file"});
    parser.addOption({"verify-data","Verify local database migration without starting companion UI"});
    parser.addOption({"voice-self-test","Render a local Chinese WAV without microphone capture or playback","wav-file"});
    parser.addOption({"offline-self-test","Generate a WAV through the offline Serena process without playback","wav-file"});
    parser.addOption({"prepare-click-voices","Cache fixed public click phrases with configured Bailian key (comma-separated voice IDs)","voices"});
    parser.process(app);
    if(parser.isSet("smoke-test"))qputenv("DRAGONGPT_DISABLE_HOTKEY","1");
    const auto dataDir=QDir(parser.value("data-dir")).absolutePath();
    if(!QDir().mkpath(dataDir)) return 2;
    dragon::installDiagnostics(dataDir+"/logs");
    const auto serverName="DragonGPT-"+QString::fromLatin1(QCryptographicHash::hash(dataDir.toCaseFolded().toUtf8(),QCryptographicHash::Sha256).toHex().left(32));
    QLockFile lock(dataDir+"/DragonGPT.lock");
    if(!lock.tryLock(0)) {
        if(parser.isSet("restore-backup")){qCritical("Exit the running instance before restoring a backup");return 3;}
        if(!parser.isSet("smoke-test")){
            QLocalSocket socket;socket.connectToServer(serverName);
            if(socket.waitForConnected(500)){
                socket.write(parser.isSet("quit")?"quit\n":parser.isSet("hide-pet")?"hide-pet\n":parser.isSet("desktop")?"show-pet\n":"show\n");socket.flush();socket.waitForBytesWritten(500);
                if(socket.waitForReadyRead(1000) && socket.readAll().startsWith("ok"))return 0;
            }
            if(!parser.isSet("quit"))QMessageBox::information(nullptr,"DragonGPT","已有旧版实例在运行，请从角色右键或托盘退出后再启动新版。");
        }
        return 3;
    }
    if(parser.isSet("quit") || parser.isSet("hide-pet"))return 0;
    try {
        if(parser.isSet("restore-backup"))dragon::MemoryStore::restoreBackup(parser.value("restore-backup"),dataDir+"/memory.sqlite");
        dragon::Companion companion(dataDir+"/memory.sqlite");
        if(parser.isSet("verify-data")){
            const QJsonObject result{{"version",app.applicationVersion()},{"schema",7},{"integrity",companion.memory().integrity()},{"memories",int(companion.memory().memories().size())},{"exchanges",companion.memory().exchanges()},{"collection_rows",int(companion.memory().collection().size())},{"focus_records",int(companion.memory().focusSessions().size())}};
            QSaveFile report(dataDir+"/upgrade-verification.json");const auto bytes=QJsonDocument(result).toJson();if(!report.open(QIODevice::WriteOnly) || report.write(bytes)!=bytes.size() || !report.commit())return 4;return result["integrity"]=="ok"?0:5;
        }
        if(parser.isSet("prepare-click-voices")){
            const auto voices=parser.value("prepare-click-voices").split(',',Qt::SkipEmptyParts);
            const auto lines=dragon::VoiceController::interactionLines();
            if(voices.isEmpty() || voices.size()>5 || lines.size()!=12)return 9;
            for(const auto &id:voices)if(!QStringList{"Serena","Momo","Chelsie","Bella","Katerina"}.contains(id))return 9;
            auto key=qgetenv("DASHSCOPE_API_KEY").trimmed();
            if(key.isEmpty())key=dragon::CredentialStore(dataDir+"/dashscope-key.dpapi").load();
            if(key.isEmpty())return 9;
            auto prefs=dragon::VoicePreferences::load(dataDir+"/voice.ini");
            if(prefs.audioTts()){key.fill('\0');qCritical("Fixed legacy voice preparation cannot be used with Audio TTS IDs");return 9;}
            dragon::QwenSpeech speech;dragon::VoiceCache cache(dataDir+"/voice-cache");
            QTimer deadline;deadline.setSingleShot(true);QElapsedTimer clock;QJsonArray metrics;
            int cursor=0,firstMs=-1;QString currentVoice,currentText;
            auto report=[&](const QString &state){
                QSaveFile file(dataDir+"/click-voice-preparation.json");if(file.open(QIODevice::WriteOnly)){
                    file.write(QJsonDocument(QJsonObject{{"status",state},{"completed",cursor},{"total",voices.size()*lines.size()},{"requests",metrics}}).toJson());file.commit();
                }
            };
            std::function<void()> next=[&]{
                while(cursor<voices.size()*lines.size()){
                    currentVoice=voices[cursor/lines.size()];currentText=lines[cursor%lines.size()];
                    if(!cache.read(currentVoice,currentText).isEmpty()){++cursor;continue;}
                    prefs.voice=currentVoice;firstMs=-1;clock.restart();deadline.start(12000);
                    const auto error=speech.start(currentText,prefs,key);
                    if(!error.isEmpty()){report(error);app.exit(10);}return;
                }
                report("complete");app.exit(0);
            };
            QObject::connect(&speech,&dragon::QwenSpeech::chunkReady,&app,[&](const QByteArray &){if(firstMs<0)firstMs=int(clock.elapsed());});
            QObject::connect(&speech,&dragon::QwenSpeech::ready,&app,[&](const QByteArray &pcm){
                deadline.stop();cache.write(currentVoice,currentText,pcm);
                if(cache.read(currentVoice,currentText)!=pcm){report("cache write failed");app.exit(10);return;}
                metrics.append(QJsonObject{{"voice",currentVoice},{"firstAudioMs",firstMs},{"completeMs",int(clock.elapsed())},{"audioBytes",pcm.size()}});
                ++cursor;report("preparing");QTimer::singleShot(0,&app,next);
            });
            QObject::connect(&speech,&dragon::QwenSpeech::failed,&app,[&](const QString &reason){deadline.stop();report(reason);app.exit(10);});
            QObject::connect(&deadline,&QTimer::timeout,&app,[&]{speech.cancel();report("voice request timeout");app.exit(10);});
            QTimer::singleShot(0,&app,next);const int result=app.exec();speech.cancel();key.fill('\0');return result;
        }
        if(parser.isSet("offline-self-test")){
            dragon::OfflineSpeech speech;
            QObject::connect(&speech,&dragon::OfflineSpeech::failed,&app,[&](const QString &reason){qCritical("%s",qPrintable(reason));app.exit(7);});
            QObject::connect(&speech,&dragon::OfflineSpeech::ready,&app,[&](const QByteArray &pcm){
                QFile file(parser.value("offline-self-test"));
                if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)){app.exit(8);return;}
                QDataStream stream(&file);stream.setByteOrder(QDataStream::LittleEndian);
                stream.writeRawData("RIFF",4);stream<<quint32(36+pcm.size());stream.writeRawData("WAVEfmt ",8);
                stream<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(24000)<<quint32(48000)<<quint16(2)<<quint16(16);
                stream.writeRawData("data",4);stream<<quint32(pcm.size());stream.writeRawData(pcm.constData(),pcm.size());
                file.close();app.exit(stream.status()==QDataStream::Ok?0:8);
            });
            const auto error=speech.start("你好，我是霜翎。很高兴见到你。");
            if(!error.isEmpty()){qCritical("%s",qPrintable(error));return 7;}
            return app.exec();
        }
        if(parser.isSet("smoke-test")) {auto settings=companion.chatSettings();settings.onlineEnabled=false;companion.saveChatSettings(settings);}
        dragon::PetWidget pet(companion);
        dragon::CompanionWindow window(companion,pet);
        QSettings settings(dataDir+"/settings.ini",QSettings::IniFormat);
        pet.setPetHeight(settings.value("height",480).toInt());
        pet.setMotionEnabled(settings.value("motion",true).toBool());
        pet.setTopmost(settings.value("topmost",true).toBool());
        pet.setHalfView(settings.value("halfView",false).toBool());
        pet.setAutomaticRest(settings.value("automaticRest",true).toBool());
        pet.setRandomActionsEnabled(settings.value("randomActions",true).toBool());
        const auto available=app.primaryScreen()->availableGeometry();
        QPoint position=settings.value("position",QPoint(available.right()-pet.width()-30,available.bottom()-pet.height()-20)).toPoint();
        bool onScreen=false;
        for(const auto *screen:app.screens()) if(screen->availableGeometry().contains(QRect(position,pet.size()))) onScreen=true;
        if(!onScreen) position=QPoint(available.right()-pet.width()-20,available.bottom()-pet.height()-10);
        pet.move(position);
        window.move(available.center()-QPoint(window.width()/2,window.height()/2));
        QIcon icon(":/assets/character.png"); app.setWindowIcon(icon);
        auto showWindow=[&]{window.openConversation();};
        QLocalServer controlServer;
        controlServer.setSocketOptions(QLocalServer::UserAccessOption);
        if(!parser.isSet("smoke-test")){
            QLocalServer::removeServer(serverName);
            if(controlServer.listen(serverName)){
                QObject::connect(&controlServer,&QLocalServer::newConnection,&window,[&]{
                    while(controlServer.hasPendingConnections()){
                        auto *socket=controlServer.nextPendingConnection();
                        auto command=std::make_shared<QByteArray>();
                        auto receive=[&,socket,command]{
                            if(socket->property("handled").toBool())return;
                            *command+=socket->readAll();if(command->size()>64){socket->abort();return;}
                            if(!command->contains('\n'))return;
                            socket->setProperty("handled",true);
                            if(command->trimmed()=="show"){pet.showOnDesktop();showWindow();}
                            else if(command->trimmed()=="show-pet")pet.showOnDesktop();
                            else if(command->trimmed()=="hide-pet")pet.hide();
                            else if(command->trimmed()=="quit")QTimer::singleShot(0,&app,&QApplication::quit);
                            else{socket->abort();return;}
                            socket->write("ok\n");socket->flush();socket->disconnectFromServer();
                        };
                        QObject::connect(socket,&QLocalSocket::readyRead,&window,receive);
                        QObject::connect(socket,&QLocalSocket::disconnected,socket,&QLocalSocket::deleteLater);
                        QTimer::singleShot(0,socket,receive);
                        QTimer::singleShot(2000,socket,[socket]{socket->abort();});
                    }
                });
            }
        }
        QObject::connect(&pet,&dragon::PetWidget::openCompanion,&window,showWindow);
        QObject::connect(&pet,&dragon::PetWidget::quitRequested,&app,&QApplication::quit);
        QMenu menu;
        menu.addAction("显示桌宠",&pet,&dragon::PetWidget::showOnDesktop);
        menu.addAction("隐藏桌宠",&pet,&QWidget::hide);
        menu.addAction("打开聊天窗口",&window,showWindow);
        menu.addAction("打开设置",&window,&dragon::CompanionWindow::openSettings);
        menu.addAction("识别原活动窗口…",&window,&dragon::CompanionWindow::askScreen);
        menu.addSeparator(); menu.addAction("退出 DragonGPT",&app,&QApplication::quit);
        QSystemTrayIcon tray(icon); tray.setToolTip("霜翎 · DragonGPT"); tray.setContextMenu(&menu);
        QObject::connect(&tray,&QSystemTrayIcon::activated,&window,[&](auto reason){if(reason==QSystemTrayIcon::DoubleClick) showWindow();});
        if(QSystemTrayIcon::isSystemTrayAvailable()) { tray.show(); app.setQuitOnLastWindowClosed(false); }
        QObject::connect(&app,&QApplication::aboutToQuit,&app,[&]{
            if(parser.isSet("smoke-test")) return;
            settings.setValue("height",pet.height()-116); settings.setValue("motion",pet.motionEnabled()); settings.setValue("position",pet.pos());
            settings.setValue("topmost",pet.topmost());
            settings.setValue("halfView",pet.halfView());
            settings.setValue("automaticRest",pet.automaticRest());
            settings.setValue("randomActions",pet.randomActionsEnabled());
        });
        pet.showOnDesktop(); if(!parser.isSet("desktop"))window.show();
        if(!parser.isSet("smoke-test"))QTimer::singleShot(200,&pet,&dragon::PetWidget::showOnDesktop);
        if(parser.isSet("voice-self-test")){
            const auto error=window.voiceController().renderTestWav(parser.value("voice-self-test"));
            QFile result(dataDir+"/voice-test-result.txt");
            if(result.open(QIODevice::WriteOnly|QIODevice::Truncate))result.write(error.isEmpty()?"PASS: local Chinese TTS rendered WAV.\n":error.toUtf8());
            return error.isEmpty()?0:6;
        }
        if(parser.isSet("smoke-test")) {
            QTimer::singleShot(300,&app,[&]{
                try {
                    if(!QSslSocket::supportsSsl()) throw std::runtime_error("TLS backend missing from runtime package");
                    QFile master(":/assets/character.png"),manifest(":/assets/character.json");
                    if(!master.open(QIODevice::ReadOnly) || !manifest.open(QIODevice::ReadOnly))throw std::runtime_error("Canonical avatar is missing");
                    const auto identity=QJsonDocument::fromJson(manifest.readAll()).object();
                    if(identity.value("id").toString()!=pet.avatarIdentity() || !identity.value("identity_locked").toBool() ||
                       QString::fromLatin1(QCryptographicHash::hash(master.readAll(),QCryptographicHash::Sha256).toHex())!=identity.value("sha256").toString())
                        throw std::runtime_error("Avatar identity contract failed");
                    auto *input=window.findChild<QLineEdit*>("chatInput");
                    auto *send=window.findChild<QPushButton*>("sendMessage");
                    auto *chat=window.findChild<QTextBrowser*>("chatHistory");
                    if(!input || !send || !chat) throw std::runtime_error("UI controls missing");
                    for(const auto &text:{QString("叫我测试用户"),QString("记住：我在开发 DragonGPT"),QString("我有点累")}) {
                        input->setText(text); send->click();
                        if(companion.busy()){
                            QEventLoop pending;QTimer deadline;deadline.setSingleShot(true);
                            QObject::connect(&companion,&dragon::Companion::busyChanged,&pending,[&](bool busy){if(!busy)pending.quit();});QObject::connect(&deadline,&QTimer::timeout,&pending,&QEventLoop::quit);
                            deadline.start(17000);pending.exec();if(companion.busy())throw std::runtime_error("Async reply did not settle");
                        }
                    }
                    if(companion.memory().nickname()!="测试用户" || companion.memory().memories().isEmpty()
                       || companion.emotions().state().emotion!=dragon::Emotion::Worried || !input->text().isEmpty()
                       || !chat->toPlainText().contains("听起来你有些累了"))
                        throw std::runtime_error("UI interaction or persistence assertion failed");
                    const auto previewDir=parser.value("preview-dir");
                    bool saved=true;
                    if(!previewDir.isEmpty()) {
                        app.processEvents();
                        QDir().mkpath(previewDir);
                        pet.setMotionEnabled(false);pet.setHalfView(false);pet.setMouthOpen(0);
                        const auto bodyArea=QRect(20,102+(pet.height()-116)/2,pet.width()-40,(pet.height()-116)/2-35);
                        const auto baseline=pet.grab();
                        const auto ratio=baseline.devicePixelRatio();
                        const QRect physicalBody(qRound(bodyArea.x()*ratio),qRound(bodyArea.y()*ratio),qRound(bodyArea.width()*ratio),qRound(bodyArea.height()*ratio));
                        const auto canonicalBody=baseline.toImage().copy(physicalBody);
                        for(const auto emotion:{dragon::Emotion::Calm,dragon::Emotion::Smile,dragon::Emotion::Happy,dragon::Emotion::Thinking,dragon::Emotion::Curious,dragon::Emotion::Worried,dragon::Emotion::Shy,dragon::Emotion::Sleepy,dragon::Emotion::Sad,dragon::Emotion::Angry}){
                            companion.emotions().reset();companion.emotions().request(emotion,0.7,1000,10);pet.setMouthOpen(0.8);
                            if(pet.grab().toImage().copy(physicalBody)!=canonicalBody)throw std::runtime_error("Interaction changed the canonical body");
                        }
                        companion.emotions().reset();companion.emotions().request(dragon::Emotion::Worried,0.6,9000,10);pet.setMouthOpen(0);
                        saved=window.grab().save(previewDir+"/companion-preview.png") && pet.grab().save(previewDir+"/pet-preview.png");
                        pet.setHalfView(true);saved=pet.grab().save(previewDir+"/pet-half-preview.png") && saved;pet.setHalfView(false);
                        companion.emotions().reset();companion.emotions().request(dragon::Emotion::Sleepy,0.7,9000,10);
                        saved=pet.grab().save(previewDir+"/pet-blink-preview.png") && saved;
                        companion.emotions().reset();companion.emotions().request(dragon::Emotion::Happy,0.7,9000,10);pet.setMouthOpen(0.8);
                        saved=pet.grab().save(previewDir+"/pet-speaking-preview.png") && saved;pet.setMouthOpen(0);
                        companion.emotions().reset();companion.emotions().request(dragon::Emotion::Worried,0.6,9000,10);
                        auto *tabs=window.findChild<QTabWidget*>("companionTabs");
                        if(!tabs) throw std::runtime_error("Tab control missing");
                        tabs->setCurrentIndex(1); app.processEvents();
                        saved=window.grab().save(previewDir+"/memory-preview.png") && saved;
                        tabs->setCurrentIndex(2); app.processEvents();
                        saved=window.grab().save(previewDir+"/voice-preview.png") && saved;
                        if(auto *asr=window.findChild<QWidget*>("asrSettings")){if(auto *scroll=qobject_cast<QScrollArea*>(tabs->widget(2))){scroll->ensureWidgetVisible(asr,0,0);app.processEvents();saved=window.grab().save(previewDir+"/asr-v4.png") && saved;}}
                        tabs->setCurrentIndex(3); app.processEvents();
                        saved=window.grab().save(previewDir+"/connection-preview.png") && saved;
                        tabs->setCurrentIndex(4); app.processEvents();
                        saved=window.grab().save(previewDir+"/settings-preview.png") && saved;
                        companion.respond("我喜欢十九世纪西方文学");
                        tabs->setCurrentIndex(5);app.processEvents();
                        saved=window.grab().save(previewDir+"/life-preview.png") && saved;
                        tabs->setCurrentIndex(6);app.processEvents();
                        tabs->setCurrentIndex(8);app.processEvents();
                        if(auto *sections=window.findChild<QTabWidget*>("toolSections"))for(int i=0;i<sections->count();++i){sections->setCurrentIndex(i);app.processEvents();saved=window.grab().save(previewDir+QString("/tools-%1-preview.png").arg(i)) && saved;}
                        if(auto *shelf=pet.findChild<QWidget*>("clipboardShelf")){
                            if(auto *toggle=shelf->findChild<QPushButton*>("toggleClipboardShelf"))toggle->click();
                            QEventLoop settle;QTimer::singleShot(250,&settle,&QEventLoop::quit);settle.exec();saved=shelf->grab().save(previewDir+"/clipboard-preview.png") && saved;
                        }
                        tabs->setCurrentIndex(6);app.processEvents();
                        saved=window.grab().save(previewDir+"/bond-gallery-preview.png") && saved;
                        if(!pet.actionAtlasReady())throw std::runtime_error("Action atlas missing");
                        pet.setHalfView(false);pet.setMotionEnabled(true);
                        for(int i=0;i<dragon::PetActions::Count;++i){
                            pet.previewAction(static_cast<dragon::PetAction>(i),6000);
                            QEventLoop settle;QTimer::singleShot(600,&settle,&QEventLoop::quit);settle.exec();
                            saved=pet.grab().save(previewDir+QString("/action-%1.png").arg(i)) && saved;
                        }
                        tabs->setCurrentIndex(7);app.processEvents();
                        saved=window.grab().save(previewDir+"/focus-ready-preview.png") && saved;
                        tabs->setCurrentIndex(9);app.processEvents();saved=window.grab().save(previewDir+"/smart-preview.png") && saved;
                        if(auto *smart=window.findChild<dragon::SmartPage*>()){smart->proposeGoal("请检查电脑为什么变卡，再总结今天的使用情况");if(auto *scroll=smart->findChild<QScrollArea*>()){scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());app.processEvents();saved=window.grab().save(previewDir+"/agent-preview.png") && saved;}}
                        if(auto *agent=window.findChild<dragon::AgentPage*>()){tabs->setCurrentWidget(agent);app.processEvents();saved=window.grab().save(previewDir+"/agent-v4.png") && saved;}
                    }
                    QFile report(dataDir+"/smoke-result.txt");
                    if(!report.open(QIODevice::WriteOnly|QIODevice::Truncate)) {app.exit(4);return;}
                    report.write(saved?"PASS: UI initialized, messages persisted, state updated, previews rendered.\n":"FAIL: preview save failed.\n");
                    app.exit(saved?0:4);
                }catch(const std::exception &e){qCritical("%s",e.what());app.exit(5);}
            });
        }
        return app.exec();
    } catch(const std::exception &e) {
        if(!parser.isSet("smoke-test")) QMessageBox::critical(nullptr,"DragonGPT 启动失败",QString::fromUtf8(e.what()));
        qCritical("%s",e.what()); return 1;
    }
}
