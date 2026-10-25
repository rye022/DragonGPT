#include <QCoreApplication>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QElapsedTimer>
#include <QTextStream>
#include <QTimer>
int main(int argc,char **argv){
    qputenv("QT_MEDIA_BACKEND","windows");QCoreApplication app(argc,argv);QElapsedTimer elapsed;elapsed.start();
    QMediaDevices monitor;
    if(app.arguments().contains("--prime")){QTextStream(stdout)<<"priming backend"<<Qt::endl;QObject::connect(&monitor,&QMediaDevices::audioOutputsChanged,&app,[]{});QTextStream(stdout)<<"backend primed ms="<<elapsed.elapsed()<<Qt::endl;}
    QFutureWatcher<QAudioDevice> query;int beats=0;QTimer heartbeat;heartbeat.setInterval(100);QObject::connect(&heartbeat,&QTimer::timeout,&app,[&]{++beats;});heartbeat.start();
    QObject::connect(&query,&QFutureWatcher<QAudioDevice>::finished,&app,[&]{QTextStream(stdout)<<"query complete ms="<<elapsed.elapsed()<<" device_available="<<!query.result().isNull()<<" heartbeats="<<beats<<Qt::endl;app.exit(query.result().isNull()?2:0);});
    query.setFuture(QtConcurrent::run([]{QTextStream(stdout)<<"worker entered"<<Qt::endl;auto result=QMediaDevices::defaultAudioOutput();QTextStream(stdout)<<"worker returned"<<Qt::endl;return result;}));
    QTimer::singleShot(30000,&app,[&]{QTextStream(stdout)<<"query not completed at 30 seconds; heartbeats="<<beats<<Qt::endl;app.exit(3);});return app.exec();
}
