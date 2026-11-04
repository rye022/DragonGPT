#include "agent_core.h"
#include "speech_recognition.h"
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
using namespace dragon;

class Fixture:public QTcpServer {
public:
    int count=0;QVector<QJsonObject> requests;QString tool="file_read";QJsonObject arguments{{"path","sample.txt"}};
    Fixture(){listen(QHostAddress::LocalHost);connect(this,&QTcpServer::newConnection,this,[this]{auto *s=nextPendingConnection();auto buffer=std::make_shared<QByteArray>();connect(s,&QTcpSocket::readyRead,this,[this,s,buffer]{
        *buffer+=s->readAll();const int split=buffer->indexOf("\r\n\r\n");if(split<0)return;const auto header=buffer->left(split);QRegularExpression re("Content-Length: (\\d+)",QRegularExpression::CaseInsensitiveOption);const int size=re.match(QString::fromUtf8(header)).captured(1).toInt();if(buffer->size()<split+4+size)return;
        const auto request=QJsonDocument::fromJson(buffer->mid(split+4,size)).object();requests.append(request);++count;
        QJsonObject msg;
        if(count==1)msg={{"role","assistant"},{"content","Inspecting actual fixture"},{"tool_calls",QJsonArray{QJsonObject{{"id","real-call-1"},{"type","function"},{"function",QJsonObject{{"name",tool},{"arguments",QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact))}}}}}}};
        else msg={{"role","assistant"},{"content","Fixture task complete"}};
        const auto body=QJsonDocument(QJsonObject{{"choices",QJsonArray{QJsonObject{{"finish_reason",count==1?"tool_calls":"stop"},{"message",msg}}}}}).toJson(QJsonDocument::Compact);
        s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);s->disconnectFromHost();s->disconnect(this);
    });connect(s,&QTcpSocket::disconnected,s,&QObject::deleteLater);});}
    QUrl endpoint(){return QUrl("http://127.0.0.1:"+QString::number(serverPort())+"/chat/completions");}
};
class Tests:public QObject {
    Q_OBJECT
private slots:
    void validation(){
        QVERIFY(AgentCore::validate("file_read",{{"path","x.txt"}}).isEmpty());
        QVERIFY(!AgentCore::validate("file_read",{{"path","x.txt"},{"approved",true}}).isEmpty());
        QVERIFY(!AgentCore::validate("file_read",{{"path","x.txt"},{"offset",0.5}}).isEmpty());
        QVERIFY(!AgentCore::validate("file_list",{{"path","."},{"recursive","yes"}}).isEmpty());
        QVERIFY(!AgentCore::validate("shell",{{"command","whoami"}}).isEmpty());
        QVERIFY(AgentCore::needsApproval("file_trash"));QVERIFY(AgentCore::needsApproval("browser_click"));
        QVERIFY(AgentCore::needsApproval("dev_run"));QVERIFY(!AgentCore::needsApproval("file_read"));
    }
    void speechSafety(){
        auto r=ASRContextCorrector::correct("帮我清理桌面文件",{},false);QVERIFY(r["requires_review"].toBool());QVERIFY(!r["confidence_available"].toBool());
        r=ASRContextCorrector::correct("今天聊 python 和 github",{"Python","GitHub"},false);QCOMPARE(r["raw"].toString(),QString("今天聊 python 和 github"));QCOMPARE(r["corrected"].toString(),QString("今天聊 Python 和 GitHub"));QVERIFY(r["requires_review"].toBool());
        r=ASRContextCorrector::correct("今天很开心",{},false);QVERIFY(!r["requires_review"].toBool());
        r=ASRContextCorrector::correct("今天很开心",{},true);QVERIFY(r["requires_review"].toBool());
        r=ASRContextCorrector::correct("西加加",{"C++"},false);QCOMPARE(r["corrected"].toString(),QString("西加加"));
    }
    void nativeLoopAndRealFile(){
        QTemporaryDir directory(QDir::currentPath()+"/agent-fixture-XXXXXX");QVERIFY(directory.isValid());QFile file(directory.filePath("sample.txt"));QVERIFY(file.open(QIODevice::WriteOnly));file.write("actual disk bytes - untrusted fixture");file.close();
        Companion c(directory.filePath("memory.sqlite"));auto settings=c.chatSettings();settings.onlineEnabled=true;c.saveChatSettings(settings);c.saveApiKey("fixture-only-not-a-real-key");ToolHub hub;Fixture server;AgentCore core(c,hub,nullptr,server.endpoint());
        core.configure(1,directory.path(),qEnvironmentVariable("DRAGON_TEST_PYTHON"));QSignalSpy finished(&core,&AgentCore::finished);QSignalSpy approvals(&core,&AgentCore::approvalRequired);
        QVERIFY(core.start("Read sample.txt"));QVERIFY(c.busy());QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,20000);QVERIFY(!c.busy());QCOMPARE(c.memory().exchanges(),1);QCOMPARE(approvals.count(),0);QCOMPARE(server.count,2);
        const auto messages=server.requests.last()["messages"].toArray();QVERIFY(messages.last().toObject()["content"].toString().contains("actual disk bytes"));QVERIFY(messages.last().toObject()["content"].toString().contains("untrusted"));QCOMPARE(core.state(),QString("completed"));
    }
    void approvalCannotBeForgedAndCancel(){
        QTemporaryDir directory(QDir::currentPath()+"/agent-approve-XXXXXX");Companion c(directory.filePath("memory.sqlite"));auto settings=c.chatSettings();settings.onlineEnabled=true;c.saveChatSettings(settings);c.saveApiKey("fixture-only-not-a-real-key");ToolHub hub;Fixture server;server.tool="file_write";server.arguments={{"path","output.txt"},{"text","must await user"}};
        AgentCore core(c,hub,nullptr,server.endpoint());QVERIFY(!core.start("no permission"));core.configure(2,directory.path(),qEnvironmentVariable("DRAGON_TEST_PYTHON"));QSignalSpy approvals(&core,&AgentCore::approvalRequired);QSignalSpy finished(&core,&AgentCore::finished);
        QVERIFY(core.start("Write output.txt"));QTRY_COMPARE_WITH_TIMEOUT(approvals.count(),1,10000);QVERIFY(!QFileInfo::exists(directory.filePath("output.txt")));
        core.pause(true);core.approve(true);QTest::qWait(80);QVERIFY(!QFileInfo::exists(directory.filePath("output.txt")));core.pause(false);QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,15000);QFile file(directory.filePath("output.txt"));QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("must await user"));
        server.count=0;server.arguments={{"path","cancelled.txt"},{"text","never"}};QVERIFY(core.start("Write cancelled.txt"));QTRY_COMPARE_WITH_TIMEOUT(approvals.count(),2,10000);QVERIFY(core.start("Queued independent goal"));QCOMPARE(core.queuedTasks(),1);QVERIFY(!core.start("Queued independent goal"));core.cancel();core.approve(true);QTest::qWait(80);QVERIFY(!QFileInfo::exists(directory.filePath("cancelled.txt")));QVERIFY(!core.busy());QCOMPARE(core.queuedTasks(),0);
    }
    void readOnlyExplorationRejectsWritesAndRevokes(){
        QTemporaryDir directory(QDir::currentPath()+"/exploration-XXXXXX");
        Companion c(directory.filePath("memory.sqlite"));auto settings=c.chatSettings();settings.onlineEnabled=true;c.saveChatSettings(settings);c.saveApiKey("fixture-only-not-a-real-key");
        auto life=c.lifeSettings();life.explorationEnabled=true;c.saveLifeSettings(life);
        ToolHub hub;Fixture server;server.tool="file_write";server.arguments={{"path","forbidden.txt"},{"text","untrusted attempted write"}};
        AgentCore core(c,hub,nullptr,server.endpoint());core.configure(2,directory.path(),qEnvironmentVariable("DRAGON_TEST_PYTHON"));
        QSignalSpy approvals(&core,&AgentCore::approvalRequired),finished(&core,&AgentCore::finished);
        QVERIFY(core.start("Read-only exploration",true));QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,10000);
        QCOMPARE(approvals.count(),0);QVERIFY(!QFileInfo::exists(directory.filePath("forbidden.txt")));QCOMPARE(c.memory().life().growth.dailyTasks,0);
        const auto toolResult=server.requests.last()["messages"].toArray().last().toObject()["content"].toString();QVERIFY(toolResult.contains("Read-only exploration denies"));
        for(const auto &v:server.requests.first()["tools"].toArray())QVERIFY(AgentCore::explorationToolAllowed(v.toObject()["function"].toObject()["name"].toString()));
        server.count=0;server.tool="web_read";server.arguments={{"url","https://example.com"}};
        QVERIFY(core.start("Review one webpage",true));QTRY_COMPARE_WITH_TIMEOUT(approvals.count(),1,10000);
        life.explorationEnabled=false;c.saveLifeSettings(life);QVERIFY(!core.busy());core.approve(true);QTest::qWait(30);QCOMPARE(server.count,1);QCOMPARE(finished.count(),2);
    }
    void echoGuardAndConfiguration(){
        QTemporaryDir directory;LocalSpeech legacy;SpeechRecognitionService speech(legacy,directory.path());speech.setOutputActive(true);QVERIFY(!speech.start().isEmpty());QVERIFY(!speech.listening());speech.setOutputActive(false);QVERIFY(speech.start().contains("回声"));
        AsrSettings s;s.engine="local";s.hotwords={"DragonGPT","C++"};s.silenceMs=1400;s.save(directory.filePath("asr.ini"));const auto loaded=AsrSettings::load(directory.filePath("asr.ini"));QCOMPARE(loaded.hotwords,s.hotwords);QCOMPARE(loaded.silenceMs,1400);QVERIFY(!loaded.autoSend);
    }
};
QTEST_GUILESS_MAIN(Tests)
#include "test_agent_core.moc"
