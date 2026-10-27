#include "tool_hub.h"
#include "privacy.h"
#include <QJsonDocument>
#include <QDateTime>
#include <QSet>
#include <QRegularExpression>
#include <QPointer>
#include <cmath>
namespace dragon {
ToolHub::ToolHub(QObject *parent):QObject(parent){deadline_.setSingleShot(true);connect(&deadline_,&QTimer::timeout,this,[this]{
    if(!running_)return;
    // Invalidate the handler before cancellation: a cancel hook may complete
    // synchronously while unwinding an OCR request or a modal dialog.
    const auto epoch=++epoch_;const auto cursor=cursor_;const auto entry=entries_.value(plan_[cursor].toObject()["tool"].toString());
    if(entry.cancel)entry.cancel();finishStep(epoch,cursor,false,{{"error","tool timeout"}});
});}
ToolHub::~ToolHub(){cancel();}
void ToolHub::add(const ToolDefinition &d,Handler h,std::function<void()> cancel){if(!running_ && !d.id.isEmpty() && !entries_.contains(d.id))entries_.insert(d.id,{d,std::move(h),std::move(cancel)});}
QJsonArray ToolHub::catalog() const{QJsonArray rows;auto ids=entries_.keys();ids.sort();for(const auto &id:ids){const auto d=entries_[id].definition;rows.append(QJsonObject{{"id",id},{"description",d.description},{"input",d.schema},{"confirmation",d.confirmation}});}return rows;}
QString ToolHub::validate(const QJsonObject &step) const{
    if(step.size()!=2 || !step["tool"].isString() || !step["input"].isObject())return "步骤必须只有 tool 与 input";
    const auto id=step["tool"].toString();if(!entries_.contains(id))return "工具未注册："+id;
    const auto schema=entries_[id].definition.schema;const auto properties=schema["properties"].toObject();const auto args=step["input"].toObject();
    for(const auto &required:schema["required"].toArray())if(!args.contains(required.toString()))return "缺少参数："+required.toString();
    for(auto it=args.begin();it!=args.end();++it){
        if(!properties.contains(it.key()))return "未知参数："+it.key();const auto spec=properties[it.key()].toObject();const auto type=spec["type"].toString();
        if(type=="string" && (!it.value().isString() || it.value().toString().size()>spec["maxLength"].toInt(2000)))return "文本参数无效";
        if(type=="integer" && (!it.value().isDouble() || !std::isfinite(it.value().toDouble()) || std::floor(it.value().toDouble())!=it.value().toDouble() || it.value().toDouble()<spec["minimum"].toDouble(0) || it.value().toDouble()>spec["maximum"].toDouble(1440)))return "数值参数无效";
        if(looksLikeCredential(it.value().toString()))return "疑似敏感参数已拦截";
    }
    return {};
}
QString ToolHub::propose(const QJsonArray &steps){
    if(running_)return "请先取消当前任务";
    if(steps.isEmpty() || steps.size()>8)return "任务应包含 1–8 步";
    QSet<QByteArray> seen;
    for(const auto &value:steps){if(!value.isObject())return "步骤格式无效";const auto step=value.toObject();const auto error=validate(step);if(!error.isEmpty())return error;const auto digest=QJsonDocument(step).toJson(QJsonDocument::Compact);if(seen.contains(digest))return "重复步骤已拒绝，避免重复执行";seen.insert(digest);}
    ++epoch_;plan_=steps;cursor_=0;attempt_=0;state_="preview";emit changed();return {};
}
void ToolHub::run(bool confirmed){
    if(running_ || state_!="preview")return;
    for(const auto &value:plan_)if(entries_[value.toObject()["tool"].toString()].definition.confirmation && !confirmed){emit confirmationRequired();return;}
    running_=true;state_="running";++epoch_;emit changed();next();
}
void ToolHub::next(){
    if(!running_)return;
    if(cursor_>=plan_.size()){running_=false;state_="completed";emit changed();return;}
    const auto step=plan_[cursor_].toObject();const auto error=validate(step);if(!error.isEmpty()){finishStep(epoch_,cursor_,false,{{"error",error}});return;}
    const auto entry=entries_[step["tool"].toString()];const auto epoch=epoch_;const int index=cursor_;deadline_.start(qBound(100,entry.definition.timeoutMs,120000));
    QPointer<ToolHub> self(this);
    try{entry.handler(step["input"].toObject(),[self,epoch,index](bool ok,const QJsonObject &output){if(self)self->finishStep(epoch,index,ok,output);});}
    catch(const std::exception &){finishStep(epoch,index,false,{{"error","local tool failed"}});}
}
void ToolHub::finishStep(quint64 epoch,int index,bool ok,const QJsonObject &output){
    if(!running_ || epoch!=epoch_ || index!=cursor_)return;
    deadline_.stop();const auto id=plan_[cursor_].toObject()["tool"].toString();
    // Audit deliberately contains no arguments, paths, screenshot text or result body.
    audit_.append(QJsonObject{{"tool",id},{"step",cursor_+1},{"ok",ok},{"attempt",attempt_+1},{"at",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});while(audit_.size()>100)audit_.removeAt(0);
    emit result(id,ok,output);
    if(!running_ || epoch!=epoch_ || index!=cursor_)return;
    const auto entry=entries_[id];
    if(!ok){if(entry.definition.idempotent && attempt_==0 && !entry.definition.confirmation){++attempt_;++epoch_;QTimer::singleShot(0,this,[this]{next();});emit changed();return;}running_=false;state_="failed";emit changed();return;}
    ++cursor_;attempt_=0;++epoch_;emit changed();QTimer::singleShot(0,this,[this]{next();});
}
void ToolHub::cancel(){
    std::function<void()> cancel;
    if(running_ && cursor_<plan_.size())cancel=entries_[plan_[cursor_].toObject()["tool"].toString()].cancel;
    ++epoch_;deadline_.stop();running_=false;state_="cancelled";
    if(cancel)cancel();
    emit changed();
}
QJsonArray ToolHub::localPlan(const QString &goal){
    QJsonArray rows;auto add=[&](const QString &id,QJsonObject input=QJsonObject{}){rows.append(QJsonObject{{"tool",id},{"input",input}});};
    if(goal.size()>500 || QRegularExpression("不要|不许|别再|假如|如果|例如|比如|他说|她说|[“”\\\"]|^记住[:：]|如何实现|怎么实现|设计.*系统|开发.*系统").match(goal).hasMatch())return rows;
    if(!QRegularExpression("帮|请|检查|查看|为什么|怎么|解释|总结|整理|提醒|创建|添加|开始|启动|计划|我要").match(goal).hasMatch())return rows;
    if(goal.contains("卡") || goal.contains("性能") || goal.contains("电脑") || goal.contains("CPU",Qt::CaseInsensitive)){add("system.health");if(goal.contains("总结") || goal.contains("今天"))add("daily.summary");}
    else if(goal.contains("屏幕") || goal.contains("网页") || goal.contains("报错"))add("screen.read",{{"question",goal.left(500)}});
    else if(goal.contains("剪贴板") || goal.contains("复制"))add("clipboard.review");
    else if(goal.contains("总结") && (goal.contains("资料") || goal.contains("文档") || goal.contains("文件")))add("document.summary");
    else if(goal.contains("整理") && (goal.contains("文件") || goal.contains("资料") || goal.contains("下载")))add("files.preview");
    else if(goal.contains("待办") || goal.contains("提醒"))add("todo.create",{{"text",goal.left(200)}});
    else if(goal.contains("番茄") || goal.contains("专注")){const auto match=QRegularExpression("(\\d{1,3})\\s*分钟").match(goal);add("focus.start",{{"minutes",match.hasMatch()?match.captured(1).toInt():25}});}
    else if(goal.contains("总结") && (goal.contains("今天") || goal.contains("每日") || goal.contains("使用")))add("daily.summary");
    return rows;
}
}
