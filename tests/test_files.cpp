#include "file_organizer.h"
#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
using namespace dragon;
namespace{
void write(const QString &path,const QByteArray &text){QFile file(path);if(!file.open(QIODevice::WriteOnly) || file.write(text)!=text.size())qFatal("fixture write failed");}
QByteArray read(const QString &path){QFile file(path);file.open(QIODevice::ReadOnly);return file.readAll();}
auto token(){return std::make_shared<std::atomic_bool>(false);}
}
class FileTests:public QObject{
    Q_OBJECT
private slots:
    void previewMoveUndo(){
        QTemporaryDir dir(QDir::currentPath()+"/files-XXXXXX");QVERIFY(dir.isValid());write(dir.path()+"/one.txt","same data");write(dir.path()+"/two.txt","same data");write(dir.path()+"/keep.exe","do not move");QDir().mkpath(dir.path()+"/Organized/Documents");write(dir.path()+"/Organized/Documents/one.txt","existing");
        const auto plan=FileOrganizer::inspect(dir.path(),token());QVERIFY2(plan.error.isEmpty(),qPrintable(plan.error));QCOMPARE(plan.moves.size(),2);QVERIFY(plan.moves[0].duplicate && plan.moves[1].duplicate);QVERIFY(QFile::exists(dir.path()+"/one.txt"));QVERIFY(plan.moves[0].to.contains("(1)"));
        const auto moved=FileOrganizer::movePlan(plan,dir.path()+"/logs",token());QVERIFY2(moved.error.isEmpty(),qPrintable(moved.error));QCOMPARE(moved.moved,2);QCOMPARE(read(dir.path()+"/Organized/Documents/one.txt"),QByteArray("existing"));QVERIFY(QFile::exists(dir.path()+"/keep.exe"));
        const auto undo=FileOrganizer::undoJournal(moved.journal,token());QVERIFY2(undo.error.isEmpty(),qPrintable(undo.error));QCOMPARE(undo.moved,2);QCOMPARE(read(dir.path()+"/one.txt"),QByteArray("same data"));QCOMPARE(read(dir.path()+"/two.txt"),QByteArray("same data"));
        QCOMPARE(FileOrganizer::undoJournal(moved.journal,token()).moved,0);
    }
    void changedAndCollision(){
        QTemporaryDir dir(QDir::currentPath()+"/files-XXXXXX");write(dir.path()+"/a.txt","original");auto plan=FileOrganizer::inspect(dir.path(),token());QCOMPARE(plan.moves.size(),1);write(dir.path()+"/a.txt","modified");auto moved=FileOrganizer::movePlan(plan,dir.path()+"/logs",token());QVERIFY(!moved.error.isEmpty());QCOMPARE(moved.moved,0);QCOMPARE(read(dir.path()+"/a.txt"),QByteArray("modified"));
        plan=FileOrganizer::inspect(dir.path(),token());QDir().mkpath(QFileInfo(plan.moves.first().to).absolutePath());write(plan.moves.first().to,"new target");moved=FileOrganizer::movePlan(plan,dir.path()+"/logs",token());QVERIFY(!moved.error.isEmpty());QCOMPARE(read(plan.moves.first().to),QByteArray("new target"));QVERIFY(QFile::exists(dir.path()+"/a.txt"));
    }
    void protectUndo(){
        QTemporaryDir dir(QDir::currentPath()+"/files-XXXXXX");write(dir.path()+"/a.txt","original");const auto plan=FileOrganizer::inspect(dir.path(),token());const auto moved=FileOrganizer::movePlan(plan,dir.path()+"/logs",token());QVERIFY(moved.error.isEmpty());write(dir.path()+"/a.txt","new original position");auto undone=FileOrganizer::undoJournal(moved.journal,token());QVERIFY(!undone.error.isEmpty());QCOMPARE(read(dir.path()+"/a.txt"),QByteArray("new original position"));QCOMPARE(read(plan.moves.first().to),QByteArray("original"));
        QFile::remove(dir.path()+"/a.txt");write(plan.moves.first().to,"changed after move");undone=FileOrganizer::undoJournal(moved.journal,token());QVERIFY(!undone.error.isEmpty());QCOMPARE(read(plan.moves.first().to),QByteArray("changed after move"));
    }
    void cancellationAndBoundary(){
        QTemporaryDir dir(QDir::currentPath()+"/files-XXXXXX");write(dir.path()+"/a.txt","original");auto cancelled=token();*cancelled=true;QVERIFY(!FileOrganizer::inspect(dir.path(),cancelled).error.isEmpty());QVERIFY(QFile::exists(dir.path()+"/a.txt"));
        auto plan=FileOrganizer::inspect(dir.path(),token());plan.moves.first().to=dir.path()+"/../escape.txt";QVERIFY(!FileOrganizer::movePlan(plan,dir.path()+"/logs",token()).error.isEmpty());QVERIFY(QFile::exists(dir.path()+"/a.txt"));
        QVERIFY(!FileOrganizer::inspect(qEnvironmentVariable("SystemRoot"),token()).error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(FileTests)
#include "test_files.moc"
