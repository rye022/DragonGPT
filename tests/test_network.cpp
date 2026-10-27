#include "companion.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlQuery>
#include <QSettings>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <functional>
#include <memory>
#include <stdexcept>
using namespace dragon;
namespace {
int assertions=0;
void expect(bool value,const char *message) {++assertions;if(!value) throw std::runtime_error(message);}
bool until(const std::function<bool()> &predicate,int timeout=2500) {
    QElapsedTimer clock;clock.start();
    while(!predicate() && clock.elapsed()<timeout) {QCoreApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}
    return predicate();
}
QByteArray delta(const QString &text) {
    return "data: "+QJsonDocument(QJsonObject{{"type","response.output_text.delta"},{"delta",text}}).toJson(QJsonDocument::Compact)+"\r\n\r\n";
}
QByteArray completion(const QString &text) {
    const QJsonObject part{{"type","output_text"},{"text",text}};
    const QJsonObject message{{"type","message"},{"content",QJsonArray{part}}};
    const QJsonObject response{{"status","completed"},{"output",QJsonArray{message}},
                              {"usage",QJsonObject{{"input_tokens",30},{"output_tokens",12}}}};
    return "data: "+QJsonDocument(QJsonObject{{"type","response.completed"},{"response",response}}).toJson(QJsonDocument::Compact)+"\r\n\r\n";
}
struct Server {
    QTcpServer server;
    int status=200,delay=0,requests=0;
    QByteArray body=delta("你好，")+delta("我在。")+completion("你好，我在。");
    QByteArray lastRequest;
    Server() {
        expect(server.listen(QHostAddress::LocalHost,0),"loopback fixture failed to listen");
        QObject::connect(&server,&QTcpServer::newConnection,&server,[this]{
            while(server.hasPendingConnections()) {
                auto *socket=server.nextPendingConnection();
                auto buffer=std::make_shared<QByteArray>();
                QObject::connect(socket,&QTcpSocket::disconnected,socket,&QTcpSocket::deleteLater);
                QObject::connect(socket,&QTcpSocket::readyRead,socket,[this,socket,buffer]{
                    *buffer+=socket->readAll();
                    if(socket->property("served").toBool()) return;
                    const auto headerEnd=buffer->indexOf("\r\n\r\n"); if(headerEnd<0) return;
                    int size=0;
                    for(auto line:buffer->left(headerEnd).split('\n')) {
                        if(line.toLower().startsWith("content-length:")) size=line.mid(15).trimmed().toInt();
                    }
                    if(buffer->size()<headerEnd+4+size) return;
                    lastRequest=*buffer; ++requests; socket->setProperty("served",true);
                    const auto responseBody=body; const auto code=status;
                    QTimer::singleShot(delay,socket,[socket,responseBody,code]{
                        if(socket->state()!=QAbstractSocket::ConnectedState) return;
                        QByteArray headers="HTTP/1.1 "+QByteArray::number(code)+" Test\r\nContent-Type: text/event-stream\r\nConnection: close\r\n";
                        if(code==302) headers+="Location: http://127.0.0.1:9/leak\r\n";
                        headers+="Content-Length: "+QByteArray::number(responseBody.size())+"\r\n\r\n";
                        const auto split=responseBody.size()/2;
                        socket->write(headers+responseBody.left(split));
                        QTimer::singleShot(10,socket,[socket,responseBody,split]{socket->write(responseBody.mid(split));socket->disconnectFromHost();});
                    });
                });
            }
        });
    }
    QUrl url() const {return QUrl(QString("http://127.0.0.1:%1/v1/responses").arg(server.serverPort()));}
};
ChatSettings online() {ChatSettings settings;settings.onlineEnabled=true;settings.proxyMode=ProxyMode::Tun;settings.model="test-model";return settings;}
void parserTests() {
    ResponseStream stream;
    const auto bytes=QByteArray(": heartbeat\r\n\r\n")+delta("你好，")+delta("霜翎")+completion("你好，霜翎");
    for(const auto byte:bytes) stream.feed(QByteArray(1,byte));
    expect(stream.completed() && stream.error().isEmpty() && stream.text()=="你好，霜翎","split UTF-8 SSE failed");
    expect(stream.usage().value("input_tokens").toInt()==30,"response usage missing");
    ResponseStream partial;partial.feed(delta("部分回复")+"data: [DONE]\n\n");
    expect(!partial.completed(),"DONE marker incorrectly accepted as completed response");
    ResponseStream broken;broken.feed("data: nope\n\n");expect(!broken.error().isEmpty(),"malformed SSE accepted");
    ResponseStream failed;failed.feed(delta("partial")+"data: {\"type\":\"response.failed\"}\n\n");expect(!failed.error().isEmpty(),"failed stream accepted");
    ResponseStream large;large.feed(QByteArray(2*1024*1024+1,'x'));expect(!large.error().isEmpty(),"response size limit missing");
    ResponseStream hidden;hidden.feed("data: {\"type\":\"response.reasoning_text.delta\",\"delta\":\"fixture-private-note\"}\n\n"+delta("正式回复")+completion("正式回复"));
    expect(hidden.completed() && hidden.text()=="正式回复","Reasoning event leaked into final response");
}
void settingsAndCredentials(const QString &dir) {
    const auto legacyFile=dir+"/legacy-online.ini";
    {QSettings legacy(legacyFile,QSettings::IniFormat);legacy.setValue("online/enabled",true);legacy.setValue("online/model","old-provider-model");legacy.sync();}
    const auto fresh=ChatSettings::load(legacyFile);
    expect(!fresh.onlineEnabled && fresh.model=="deepseek-flash" && fresh.proxyMode==ProxyMode::Direct,"Old provider config was silently reused");
    QNetworkProxy direct;auto directSettings=fresh;directSettings.onlineEnabled=true;
    expect(directSettings.resolveProxy(direct,QUrl("https://api.deepseek.com/responses")).isEmpty() && direct.type()==QNetworkProxy::NoProxy,"DeepSeek unnecessarily requires VPN");
    auto settings=online();settings.proxyMode=ProxyMode::Http;settings.proxyPort=8877;
    settings.save(dir+"/online.ini");const auto read=ChatSettings::load(dir+"/online.ini");
    expect(read.onlineEnabled && read.proxyPort==8877 && read.model=="test-model","settings restart persistence failed");
    QNetworkProxy proxy;
    expect(read.resolveProxy(proxy,QUrl("https://api.deepseek.com")).isEmpty() && proxy.type()==QNetworkProxy::HttpProxy && proxy.port()==8877,"explicit HTTP proxy lost");
    settings.proxyMode=ProxyMode::Socks5;settings.resolveProxy(proxy,QUrl("https://api.deepseek.com"));expect(proxy.type()==QNetworkProxy::Socks5Proxy,"SOCKS route lost");
    settings.proxyMode=ProxyMode::Tun;settings.resolveProxy(proxy,QUrl("https://api.deepseek.com"));expect(proxy.type()==QNetworkProxy::NoProxy,"TUN route not explicit");
    settings.proxyMode=ProxyMode::Http;settings.proxyPort=0;expect(!settings.resolveProxy(proxy,QUrl()).isEmpty(),"invalid port accepted");
    settings.onlineEnabled=false;expect(!settings.resolveProxy(proxy,QUrl()).isEmpty(),"offline mode allowed online request");
    CredentialStore credentials(dir+"/key.dpapi");
    const QByteArray dummy("fixture-credential-not-a-real-key");credentials.save(dummy);
    QFile file(dir+"/key.dpapi");file.open(QIODevice::ReadOnly);
    expect(!file.readAll().contains(dummy),"credential saved as plaintext");file.close();
    expect(credentials.load()==dummy,"encrypted credential restart read failed");credentials.remove();expect(credentials.load().isEmpty(),"saved credential was not deleted");
    CredentialStore oldProvider(dir+"/api-key.dpapi"),newProvider(dir+"/deepseek-key.dpapi");oldProvider.save(dummy);
    expect(newProvider.load().isEmpty(),"Old provider credential crossed into DeepSeek");
}
void transportTests() {
    ChatService official;expect(official.endpoint()==QUrl("https://api.deepseek.com/responses"),"Default provider is not DeepSeek");
    ChatService old(nullptr,QUrl("https://api.openai.com/v1/responses"));
    expect(!old.start(online(),"fixture-deepseek-key","p","{}",{},"hello").isEmpty(),"DeepSeek key could be sent to old provider");
    Server fixture;ChatService service(nullptr,fixture.url());
    int success=0,failure=0,partials=0;QString text,reason;
    QObject::connect(&service,&ChatService::succeeded,[&](const QString &t,const QJsonObject &){text=t;++success;});
    QObject::connect(&service,&ChatService::failed,[&](const QString &t){reason=t;++failure;});
    QObject::connect(&service,&ChatService::partial,[&](const QString &){++partials;});
    expect(service.start(online(),"fixture-key","persona","{}",{},"hello").isEmpty(),"transport start failed");
    expect(until([&]{return success==1;}),"streamed HTTP response did not complete");
    expect(text=="你好，我在。" && partials>0 && failure==0,"streamed result incorrect");
    const auto payload=QJsonDocument::fromJson(fixture.lastRequest.mid(fixture.lastRequest.indexOf("\r\n\r\n")+4)).object();
    expect(payload.value("stream").toBool() && !payload.contains("store") && payload.value("reasoning").toObject().value("effort")=="none" && payload.value("model")=="test-model","API payload contract incorrect");
    expect(fixture.lastRequest.contains("Bearer fixture-key"),"authorization missing");
    for(const auto status:{401,429,302}) {
        fixture.status=status;const auto before=failure;
        service.start(online(),"fixture-key","p","{}",{},"hello");
        expect(until([&]{return failure==before+1;}),"HTTP failure not propagated");
    }
    expect(success==1,"HTTP errors or redirect became successful replies");
    fixture.status=200;fixture.body=delta("unfinished");const auto before=failure;
    service.start(online(),"fixture-key","p","{}",{},"hello");
    expect(until([&]{return failure==before+1;}),"truncated stream was committed");
    fixture.body=delta("later")+completion("later");fixture.delay=250;
    const auto requests=fixture.requests,failures=failure;
    service.start(online(),"fixture-key","p","{}",{},"hello");
    expect(until([&]{return fixture.requests>requests;}),"delayed request missing");service.cancel();
    until([]{return false;},300);
    expect(!service.busy() && success==1 && failure==failures,"cancelled callbacks leaked into live turn");
    fixture.delay=250;service.setTimeoutMs(60);
    service.start(online(),"fixture-key","p","{}",{},"hello");
    expect(until([&]{return failure==failures+1;}),"overall deadline did not fire");
    expect(reason.contains("超时"),"timeout was not actionable");
    fixture.delay=0;service.setTimeoutMs(2000);
    const auto count=fixture.requests;auto offline=online();offline.onlineEnabled=false;
    expect(!service.start(offline,"fixture-key","p","{}",{},"hello").isEmpty() && fixture.requests==count,"offline mode made a network request");
    auto noModel=online();noModel.model.clear();expect(!service.start(noModel,"fixture-key","p","{}",{},"hi").isEmpty(),"empty model accepted");
    expect(!service.start(online(),{},"p","{}",{},"hi").isEmpty(),"missing key accepted");
}
void routerAndMigration(const QString &dir) {
    const auto legacy=dir+"/legacy.sqlite";
    {auto db=QSqlDatabase::addDatabase("QSQLITE","legacy-fixture");db.setDatabaseName(legacy);expect(db.open(),"legacy database unavailable");
        QSqlQuery query(db);expect(query.exec("CREATE TABLE messages(id INTEGER PRIMARY KEY,role TEXT,content TEXT,created_at TEXT)"),"legacy schema creation failed");
        expect(query.exec("INSERT INTO messages VALUES(1,'assistant','old message','2026-10-21')"),"legacy fixture insert failed");}
    QSqlDatabase::removeDatabase("legacy-fixture");
    {MemoryStore memory(legacy);expect(memory.recentMessages().first().source=="offline","v0.1 migration dropped history or provenance");}
    Server fixture;Companion companion(dir+"/router.sqlite",nullptr,fixture.url(),1200);
    companion.saveChatSettings(online());companion.saveApiKey("fixture-key");
    const auto profile=companion.memory().remember("用户资料测试：职业 C++ 开发者");companion.memory().updateMemory(profile,"用户资料测试：职业 C++ 开发者","用户资料",1);
    const auto style=companion.memory().remember("交流偏好测试：温柔且不堆砌语气词");companion.memory().updateMemory(style,"交流偏好测试：温柔且不堆砌语气词","交流偏好",1);
    QString answer;int ready=0;
    QObject::connect(&companion,&Companion::replyReady,[&](const QString &text,const QString &){answer=text;++ready;});
    expect(companion.sendMessage("hello"),"router rejected online message");
    expect(!companion.sendMessage("duplicate"),"router allowed concurrent requests");
    expect(until([&]{return ready==1;}),"router failed to commit streamed reply");
    const auto sent=QJsonDocument::fromJson(fixture.lastRequest.mid(fixture.lastRequest.indexOf("\r\n\r\n")+4)).object();
    const auto contextMessage=sent.value("input").toArray().first().toObject().value("content").toString();
    const auto context=QJsonDocument::fromJson(contextMessage.mid(contextMessage.indexOf('\n')+1).toUtf8()).object();
    expect(context.value("profile_memories").toArray().size()==1 && context.value("communication_preferences").toArray().size()==1,"saved profile and preference omitted from actual chat request");
    expect(companion.memory().exchanges()==1 && companion.memory().recentMessages().last().source=="deepseek","online reply not stored exactly once with provenance");
    expect(companion.lastUsage().value("output_tokens").toInt()==12,"usage was not exposed");
    fixture.status=429;companion.sendMessage("我有点累");expect(until([&]{return ready==2;}),"failure did not fall back to offline");
    expect(answer.contains("有些累") && companion.routeStatus().contains("429") && companion.memory().exchanges()==2,"fallback duplicated the turn or lost its cause");
    expect(companion.memory().recentMessages().last().source=="offline","fallback mislabeled as OpenAI");
    const auto count=fixture.requests;companion.sendMessage("记住：我喜欢安静");
    expect(fixture.requests==count && companion.memory().memories().size()==3,"memory command was sent to cloud or existing profile was lost");
    fixture.status=200;fixture.delay=250;const auto exchanges=companion.memory().exchanges();
    companion.sendMessage("cancel me");expect(until([&]{return fixture.requests>count;}),"cancel test did not connect");
    QString restored;QObject::connect(&companion,&Companion::draftRestored,[&](const QString &text){restored=text;});
    companion.cancelReply();until([]{return false;},300);
    expect(restored=="cancel me" && companion.memory().exchanges()==exchanges,"cancel lost draft or committed stale content");
    auto proxy=online();proxy.proxyMode=ProxyMode::Http;proxy.proxyHost="127.0.0.1";proxy.proxyPort=1;companion.saveChatSettings(proxy);
    const auto prior=ready;companion.sendMessage("我完成了");expect(until([&]{return ready==prior+1;}),"broken proxy did not fall back");
    expect(companion.memory().recentMessages().last().source=="offline","broken proxy silently switched to direct");
    companion.removeApiKey();companion.sendMessage("hello");expect(companion.routeStatus().contains("API key"),"missing key fallback unexplained");
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    const auto original=qgetenv("DEEPSEEK_API_KEY");qunsetenv("DEEPSEEK_API_KEY");
    QTemporaryDir dir(QDir::currentPath()+"/network-tests-XXXXXX");
    try {
        expect(dir.isValid(),"fixture directory failed");parserTests();settingsAndCredentials(dir.path());transportTests();routerAndMigration(dir.path());
        qputenv("DEEPSEEK_API_KEY",original);
        QTextStream(stdout)<<"PASS: "<<assertions<<" assertions (SSE, proxy, DPAPI, cancellation, timeout, fallback, migration). No live API calls.\n";return 0;
    }catch(const std::exception &e){qputenv("DEEPSEEK_API_KEY",original);QTextStream(stderr)<<"FAIL: "<<e.what()<<"\n";return 1;}
}
