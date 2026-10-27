#include "context_engine.h"
#include <QRegularExpression>
namespace dragon {
QJsonObject DesktopContext::json() const{return {{"kind",kind},{"confidence",confidence},{"low_interruption",lowInterruption},{"since",since.toString(Qt::ISODate)}};}
void ContextEngine::setEnabled(bool enabled){enabled_=enabled;context_={};candidate_.clear();candidateAt_={};recentConversation_.clear();conversationAt_={};emit changed();}
void ContextEngine::noteConversation(const QString &text,QDateTime now){if(enabled_){recentConversation_=text.left(1000);conversationAt_=now;}}
DesktopContext ContextEngine::classify(const QString &title,const QString &text,bool fullscreen){
    if(fullscreen)return {"fullscreen",.98,true,{}};
    const auto data=(title+" "+text.left(12000)).toLower();
    if(QRegularExpression("会议|meeting|zoom|teams|腾讯会议").match(data).hasMatch())return {"meeting",.9,true,{}};
    if(QRegularExpression("error:|exception|traceback|fatal error|报错|编译失败|错误代码").match(data).hasMatch())return {"troubleshooting",.86,false,{}};
    if(QRegularExpression("c\\+\\+|cppreference|cmake|visual studio|vscode|编程|代码").match(data).hasMatch())return {"programming",.8,true,{}};
    if(QRegularExpression("数学|方程|高数|证明|积分|习题").match(data).hasMatch())return {"study",.8,false,{}};
    if(QRegularExpression("阅读|文章|文档|pdf|documentation").match(data).hasMatch())return {"reading",.72,false,{}};
    return {};
}
void ContextEngine::observe(const QString &title,const QString &text,bool fullscreen,QDateTime now){
    if(!enabled_)return;
    if(conversationAt_.isValid() && (conversationAt_.secsTo(now)<0 || conversationAt_.secsTo(now)>300))recentConversation_.clear();
    auto next=classify(title,text+" "+recentConversation_,fullscreen);
    if(next.kind==context_.kind){candidate_.clear();return;}
    if(next.kind!=candidate_){candidate_=next.kind;candidateAt_=now;}
    if((next.lowInterruption && next.confidence>=.9) || candidateAt_.secsTo(now)>=10){next.since=now;context_=next;candidate_.clear();emit changed();}
}
}
