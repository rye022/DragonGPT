#include "companion_window.h"
#include "focus_page.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <stdexcept>
using namespace dragon;
int main(int argc,char **argv){
    qputenv("QT_MEDIA_BACKEND","windows");QApplication app(argc,argv);QTemporaryDir dir;int checks=0;
    auto expect=[&](bool b,const char *m){++checks;if(!b)throw std::runtime_error(m);};
    try{
        Companion c(dir.filePath("memory.sqlite"));auto online=c.chatSettings();online.onlineEnabled=false;c.saveChatSettings(online);
        auto life=c.lifeSettings();life.enabled=false;c.saveLifeSettings(life);
        PetWidget pet(c);CompanionWindow window(c,pet);pet.showOnDesktop();window.show();
        QSignalSpy speech(&window.voiceController(),&VoiceController::speechRequested);
        struct Scene{QString text,name;Emotion mood;PetAction action;};
        const QList<Scene> scenes={
            {"我完成了","happy",Emotion::Happy,PetAction::Wave},
            {"我有点害羞","shy",Emotion::Shy,PetAction::Shy},
            {"小傲娇又嘴硬啦","tsundere",Emotion::Tsundere,PetAction::Jealous},
            {"我困了","sleepy",Emotion::Sleepy,PetAction::Sleep},
            {"休息一下，放松","relaxed",Emotion::Relaxed,PetAction::Comfort},
            {"一起认真工作","focused",Emotion::Focused,PetAction::Work},
            {"我很好奇？","curious",Emotion::Curious,PetAction::Idle},
            {"好耶，太棒了","excited",Emotion::Excited,PetAction::Wave}
        };
        QDir().mkpath("outputs/previews/emotions");
        for(const auto &scene:scenes){
            c.sendMessage(scene.text);QCoreApplication::processEvents();
            expect(c.effectiveMood()==scene.mood,"chat drives real resolved mood");expect(pet.currentAction()==scene.action,"mood drives real pet action");
            bool label=false;for(const auto *l:window.findChildren<QLabel*>())if(l->text()=="当前状态 · "+emotionName(scene.mood))label=true;
            expect(label,"status label reflects mood");
            expect(window.voiceController().speechExpression().emotion==scene.mood,"same mood used by TTS expression");
            QTest::qWait(600);expect(pet.grab().save("outputs/previews/emotions/"+scene.name+".png"),"emotion preview saved");
        }
        auto *page=window.focusPage();auto settings=page->service().settings();settings.focusSeconds=2;settings.focusTts=false;settings.waterEnabled=false;page->service().setSettings(settings);
        page->service().start("情绪联动验证");expect(c.effectiveMood()==Emotion::Focused && pet.currentAction()==PetAction::Work,"actual focus page sets serious mood and work");
        page->service().pause();expect(c.effectiveMood()==Emotion::Relaxed && pet.currentAction()==PetAction::Comfort,"paused focus relaxes safely");
        page->service().resume();expect(c.effectiveMood()==Emotion::Focused,"resume serious");
        QElapsedTimer wait;wait.start();while(page->service().phase()==FocusService::Phase::Focus && wait.elapsed()<4500)QTest::qWait(20);
        expect(page->service().phase()==FocusService::Phase::ShortBreak && c.effectiveMood()==Emotion::Relaxed,"completed focus enters relaxed break");page->service().end();
        c.respond("我困了");pet.previewAction(PetAction::Work,1000);expect(pet.currentAction()==PetAction::Work,"manual action has priority");pet.resumeAutomaticActions();expect(pet.currentAction()==PetAction::Sleep,"finishing manual action returns to mood");
        c.respond("我好难过");c.setFocusContext(true,false);expect(window.voiceController().speechExpression().emotion==Emotion::Sad,"focus pose does not erase supportive voice");c.setFocusContext(false,false);
        expect(speech.count()==0,"mood changes do not auto-start TTS");
        // Real page scheduler + real Windows SAPI, isolated data, no cloud.
        auto prefs=window.voiceController().voicePreferences();prefs.apiFamily="qwen-tts";prefs.voice="System";window.voiceController().saveVoicePreferences(prefs,{});
        auto quiet=c.lifeSettings();quiet.quietStart=QTime::currentTime().hour();quiet.quietEnd=(quiet.quietStart+1)%24;c.saveLifeSettings(quiet);
        settings.focusTts=true;settings.quietEnabled=false;page->service().setSettings(settings);
        QSignalSpy audio(&window.voiceController(),&VoiceController::outputStarted);
        page->service().start("本地语音回归");wait.restart();
        bool restingDuringAudio=false;
        while((audio.count()<2 || window.voiceController().engaged()) && wait.elapsed()<30000){QTest::qWait(20);if(audio.count()==2 && window.voiceController().engaged() && pet.currentAction()==PetAction::Comfort)restingDuringAudio=true;}
        expect(audio.count()==2,"start and completion actually start local audio despite proactive quiet hours");
        expect(speech.count()==2 && speech.last().first().toString().contains("休息"),"completion speech explicitly reminds rest");
        expect(restingDuringAudio,"rest action retained during actual completion audio");
        expect(page->service().phase()==FocusService::Phase::ShortBreak && pet.currentAction()==PetAction::Comfort,"rest action retained after reminder playback");
        page->service().end();
        // Click the real End button before the countdown has completed.
        settings.focusSeconds=60;page->service().setSettings(settings);page->service().start("手动结束测试");
        const int completed=page->service().todayStats().value("count").toInt();const int experience=c.memory().life().experience;
        auto *endButton=page->findChild<QPushButton*>("focusEnd");expect(endButton!=nullptr,"real end button exists");endButton->click();
        expect(!page->service().running() && c.effectiveMood()==Emotion::Relaxed,"manual end stops timer and relaxes");
        wait.restart();bool manualRestAudio=false;while((audio.count()<3 || window.voiceController().engaged()) && wait.elapsed()<20000){QTest::qWait(20);if(audio.count()==3 && window.voiceController().engaged() && pet.currentAction()==PetAction::Comfort)manualRestAudio=true;}
        expect(audio.count()==3 && speech.last().first().toString().contains("本轮专注已结束"),"manual end actually plays its own announcement");
        expect(manualRestAudio,"manual end holds rest pose during real audio");
        expect(page->service().todayStats().value("count").toInt()==completed && c.memory().life().experience==experience,"manual end does not fabricate completion or rewards");
        page->service().start("跳过进入休息测试");auto *skipButton=page->findChild<QPushButton*>("focusSkip");expect(skipButton!=nullptr,"real skip button exists");skipButton->click();
        expect(page->service().phase()==FocusService::Phase::ShortBreak && pet.currentAction()==PetAction::Comfort,"skip enters rest pose while preserving real user feelings");
        wait.restart();bool skipRestAudio=false;while((audio.count()<4 || window.voiceController().engaged()) && wait.elapsed()<20000){QTest::qWait(20);if(audio.count()==4 && window.voiceController().engaged() && pet.currentAction()==PetAction::Comfort)skipRestAudio=true;}
        expect(audio.count()==4 && speech.last().first().toString().contains("已切换到休息阶段"),"skip to break actually announces rest");expect(skipRestAudio,"skip rest pose during audio");
        expect(page->service().todayStats().value("count").toInt()==completed && c.memory().life().experience==experience,"skip does not fabricate completion or rewards");
        QTextStream(stdout)<<"PASS "<<checks<<" real emotion/action/focus/UI/expression checks; no provider calls\n";return 0;
    }catch(const std::exception &e){QTextStream(stderr)<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
