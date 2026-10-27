#include "file_organizer.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QHash>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <stdexcept>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace dragon {
namespace{
QString canonical(const QString &path){return QDir::fromNativeSeparators(QFileInfo(path).canonicalFilePath());}
bool within(const QString &path,const QString &root){return path.compare(root,Qt::CaseInsensitive)==0 || path.startsWith(root+"/",Qt::CaseInsensitive);}
bool link(const QString &path){
    const QFileInfo info(path);if(info.isSymLink())return true;
#ifdef Q_OS_WIN
    const DWORD attributes=GetFileAttributesW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(info.absoluteFilePath()).utf16()));if(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT))return true;
#endif
    return false;
}
bool safeRoot(const QString &path){
    if(path.isEmpty() || !QFileInfo(path).isDir() || QDir(path).isRoot())return false;
    QString walk=QFileInfo(path).absoluteFilePath();while(!QDir(walk).isRoot()){if(link(walk))return false;const auto parent=QFileInfo(walk).absolutePath();if(parent==walk)break;walk=parent;}
    for(const auto &key:{"SystemRoot","ProgramFiles","ProgramFiles(x86)","ProgramData","LOCALAPPDATA","APPDATA"}){const auto value=canonical(qEnvironmentVariable(key));if(!value.isEmpty() && within(canonical(path),value))return false;}
    return true;
}
QString digest(const QString &path,const std::shared_ptr<std::atomic_bool> &cancelled){
    QFile file(path);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read a selected file");QCryptographicHash hash(QCryptographicHash::Sha256);
    while(!file.atEnd()){if(*cancelled)throw std::runtime_error("Cancelled");const auto data=file.read(1024*1024);if(data.isEmpty() && file.error()!=QFileDevice::NoError)throw std::runtime_error("File read failed");hash.addData(data);}return QString::fromLatin1(hash.result().toHex());
}
QString category(const QString &suffix){
    const auto ext=suffix.toLower();
    if(QStringList{"png","jpg","jpeg","gif","webp","bmp","svg"}.contains(ext))return "Images";
    if(QStringList{"pdf","doc","docx","txt","md","xlsx","csv","pptx"}.contains(ext))return "Documents";
    if(QStringList{"zip","7z","rar","tar","gz"}.contains(ext))return "Archives";
    if(QStringList{"mp3","wav","flac","ogg","m4a"}.contains(ext))return "Audio";
    if(QStringList{"mp4","mkv","mov","avi","webm"}.contains(ext))return "Video";
    return {}; // Unknown, configuration and executable files remain untouched.
}
bool renameNoReplace(const QString &from,const QString &to){
#ifdef Q_OS_WIN
    return MoveFileExW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(from).utf16()),reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(to).utf16()),MOVEFILE_WRITE_THROUGH)!=0;
#else
    if(QFileInfo::exists(to))return false;return QFile::rename(from,to);
#endif
}
void saveJournal(const QString &path,const QJsonObject &journal){
    QSaveFile file(path);const auto data=QJsonDocument(journal).toJson();if(!file.open(QIODevice::WriteOnly) || file.write(data)!=data.size() || !file.commit())throw std::runtime_error("Cannot safely save operation journal");
}
QJsonObject moveJson(const FileMove &m){return {{"from",m.from},{"to",m.to},{"sha256",m.sha256},{"size",qint64(m.size)},{"modified",m.modified.toString(Qt::ISODateWithMs)},{"state","planned"}};}
void verifyPath(const QString &root,const QString &path,bool existing){
    const auto full=QDir::cleanPath(QFileInfo(path).absoluteFilePath());if(!within(full,root) || full==root)throw std::runtime_error("Operation escaped the selected directory");
    QString walk=existing?full:QFileInfo(full).absolutePath();while(within(walk,root) && walk!=root){if(link(walk))throw std::runtime_error("Links and reparse points are not allowed");walk=QFileInfo(walk).absolutePath();}
    if(existing && (!QFileInfo(full).isFile() || !within(canonical(full),root)))throw std::runtime_error("File changed or no longer belongs to selected directory");
}
}
FileOrganizer::FileOrganizer(const QString &directory,QObject *parent):QObject(parent),journalDirectory_(directory){
    connect(&scan_,&QFutureWatcher<FilePlan>::finished,this,[this]{plan_=scan_.result();busy_=false;emit busyChanged(false);emit previewReady();});
    connect(&operations_,&QFutureWatcher<FileResult>::finished,this,[this]{const auto result=operations_.result();busy_=false;plan_={};emit busyChanged(false);emit finished(QString("已处理 %1 个文件。%2").arg(result.moved).arg(result.error.isEmpty()?"操作日志已保存，可尝试撤销。":"\n"+result.error));});
}
FileOrganizer::~FileOrganizer(){cancel();scan_.waitForFinished();operations_.waitForFinished();}
void FileOrganizer::cancel(){if(cancelled_)*cancelled_=true;}
QStringList FileOrganizer::journals() const{QStringList result;const auto files=QDir(journalDirectory_).entryInfoList({"*.json"},QDir::Files,QDir::Time);for(const auto &file:files)result.append(file.absoluteFilePath());return result;}
void FileOrganizer::scan(const QString &path){if(busy_)return;busy_=true;plan_={};cancelled_=std::make_shared<std::atomic_bool>(false);emit busyChanged(true);scan_.setFuture(QtConcurrent::run(&FileOrganizer::inspect,path,cancelled_));}
void FileOrganizer::execute(){if(busy_ || plan_.moves.isEmpty() || !plan_.error.isEmpty())return;busy_=true;cancelled_=std::make_shared<std::atomic_bool>(false);emit busyChanged(true);operations_.setFuture(QtConcurrent::run(&FileOrganizer::movePlan,plan_,journalDirectory_,cancelled_));}
void FileOrganizer::undo(const QString &path){if(busy_ || QFileInfo(path).absolutePath()!=QFileInfo(journalDirectory_).absoluteFilePath())return;busy_=true;cancelled_=std::make_shared<std::atomic_bool>(false);emit busyChanged(true);operations_.setFuture(QtConcurrent::run(&FileOrganizer::undoJournal,path,cancelled_));}
FilePlan FileOrganizer::inspect(const QString &directory,const std::shared_ptr<std::atomic_bool> &cancelled){
    FilePlan plan;plan.root=canonical(directory);
    try{
        if(!safeRoot(directory))throw std::runtime_error("Choose a normal user folder; system folders and links are not supported");
        const auto files=QDir(plan.root).entryInfoList(QDir::Files|QDir::NoDotAndDotDot|QDir::NoSymLinks,QDir::Name);if(files.size()>5000)throw std::runtime_error("Select a folder with at most 5000 direct files");
        QHash<QString,int> hashes;
        QSet<QString> targets;
        for(const auto &file:files){
            if(*cancelled)throw std::runtime_error("Cancelled");const auto kind=category(file.suffix());if(kind.isEmpty() || file.isHidden() || link(file.absoluteFilePath()))continue;
            const auto targetDirectory=plan.root+"/Organized/"+kind;QString target=targetDirectory+"/"+file.fileName();int suffix=1;
            while(QFileInfo::exists(target) || targets.contains(target.toCaseFolded())){target=targetDirectory+"/"+file.completeBaseName()+" ("+QString::number(suffix++)+")."+file.suffix();}
            targets.insert(target.toCaseFolded());
            // Full hashes verify content before execute/undo; duplicate detection never deletes.
            const auto hash=digest(file.absoluteFilePath(),cancelled);FileMove move{file.absoluteFilePath(),target,hash,quint64(file.size()),file.lastModified(),false};
            if(hashes.contains(hash)){move.duplicate=true;plan.moves[hashes[hash]].duplicate=true;}else hashes.insert(hash,int(plan.moves.size()));plan.moves.append(move);
        }
        plan.warning="仅扫描所选目录的直接子文件。子目录、隐藏文件、程序和未知类型保持原位；重复文件只标记，不删除。";
    }catch(const std::exception &e){plan.error=QString::fromUtf8(e.what());plan.moves.clear();}
    return plan;
}
FileResult FileOrganizer::movePlan(const FilePlan &plan,const QString &directory,const std::shared_ptr<std::atomic_bool> &cancelled){
    FileResult result;QJsonObject journal;QJsonArray rows;
    try{
        if(!plan.error.isEmpty() || plan.moves.isEmpty() || !safeRoot(plan.root))throw std::runtime_error("Preview is no longer valid");
        const auto root=canonical(plan.root);if(root!=plan.root)throw std::runtime_error("Selected root changed");
        QSet<QString> fromPaths,toPaths;
        // Preflight the entire batch before the first move.
        for(const auto &m:plan.moves){verifyPath(root,m.from,true);verifyPath(root,m.to,false);
            if(QFileInfo(m.from).absolutePath()!=root || !m.to.startsWith(root+"/Organized/",Qt::CaseInsensitive) || QFileInfo::exists(m.to) || fromPaths.contains(m.from.toCaseFolded()) || toPaths.contains(m.to.toCaseFolded()))throw std::runtime_error("Preview contains a conflicting or invalid path");
            fromPaths.insert(m.from.toCaseFolded());toPaths.insert(m.to.toCaseFolded());
            const QFileInfo file(m.from);if(quint64(file.size())!=m.size || file.lastModified()!=m.modified || digest(m.from,cancelled)!=m.sha256)throw std::runtime_error("A file changed after preview; scan again");rows.append(moveJson(m));
        }
        if(!QDir().mkpath(directory))throw std::runtime_error("Cannot create operation log directory");
        result.journal=QDir(directory).absoluteFilePath(QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmsszzz")+"-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".json");
        journal={{"format","DragonGPT file operations"},{"version",1},{"root",root},{"entries",rows},{"state","moving"}};saveJournal(result.journal,journal);
        for(int i=0;i<plan.moves.size();++i){
            if(*cancelled)throw std::runtime_error("Cancelled; completed moves remain in the undo log");const auto &m=plan.moves[i];verifyPath(root,m.from,true);verifyPath(root,m.to,false);
            if(digest(m.from,cancelled)!=m.sha256)throw std::runtime_error("File changed during execution");
            if(!QDir().mkpath(QFileInfo(m.to).absolutePath()))throw std::runtime_error("Cannot create destination directory");verifyPath(root,m.to,false);
            auto row=rows[i].toObject();row["state"]="moving";rows[i]=row;journal["entries"]=rows;saveJournal(result.journal,journal);
            if(!renameNoReplace(m.from,m.to))throw std::runtime_error("Move failed; no existing file was overwritten");++result.moved;
            row["state"]="moved";rows[i]=row;journal["entries"]=rows;saveJournal(result.journal,journal);
        }
        journal["state"]="complete";saveJournal(result.journal,journal);
    }catch(const std::exception &e){
        result.error=QString::fromUtf8(e.what());
        if(result.moved>0 && !result.journal.isEmpty()){
            const auto rollback=undoJournal(result.journal,std::make_shared<std::atomic_bool>(false));
            if(rollback.error.isEmpty()){result.moved=0;result.error+="; completed moves were rolled back";}
            else result.error+="; rollback incomplete, review the operation journal";
        }
    }
    return result;
}
FileResult FileOrganizer::undoJournal(const QString &path,const std::shared_ptr<std::atomic_bool> &cancelled){
    FileResult result;result.journal=path;
    try{
        QFile input(path);if(!input.open(QIODevice::ReadOnly) || input.size()>4*1024*1024)throw std::runtime_error("Cannot read operation journal");auto journal=QJsonDocument::fromJson(input.readAll()).object();input.close();
        if(journal["format"]!="DragonGPT file operations" || journal["version"]!=1)throw std::runtime_error("Unsupported operation journal");
        const auto root=journal["root"].toString();if(!safeRoot(root) || canonical(root)!=root)throw std::runtime_error("Selected folder changed");auto rows=journal["entries"].toArray();
        for(int i=rows.size()-1;i>=0;--i){
            auto row=rows[i].toObject();if(row["state"]=="planned" || row["state"]=="undone")continue;
            if(*cancelled)throw std::runtime_error("Undo cancelled; remaining operations stay in the log");
            const auto from=row["from"].toString(),to=row["to"].toString();verifyPath(root,from,false);verifyPath(root,to,false);
            if(QFileInfo(from).absolutePath()!=root || !QDir::cleanPath(to).startsWith(root+"/Organized/",Qt::CaseInsensitive))throw std::runtime_error("Invalid undo path");
            if(QFileInfo::exists(from) && !QFileInfo::exists(to) && (row["state"]=="moving" || row["state"]=="undoing")){
                verifyPath(root,from,true);if(digest(from,cancelled)!=row["sha256"].toString())throw std::runtime_error("Recovery file changed");row["state"]="undone";rows[i]=row;journal["entries"]=rows;saveJournal(path,journal);continue;
            }
            if(QFileInfo::exists(from))throw std::runtime_error("Undo destination already exists; no file was overwritten");
            verifyPath(root,to,true);
            if(digest(to,cancelled)!=row["sha256"].toString())throw std::runtime_error("Moved file changed; refusing to undo over newer content");
            row["state"]="undoing";rows[i]=row;journal["entries"]=rows;saveJournal(path,journal);
            if(!renameNoReplace(to,from))throw std::runtime_error("Undo move failed");++result.moved;row["state"]="undone";rows[i]=row;journal["entries"]=rows;saveJournal(path,journal);
        }
        journal["state"]="undone";saveJournal(path,journal);
    }catch(const std::exception &e){result.error=QString::fromUtf8(e.what());}
    return result;
}
}
