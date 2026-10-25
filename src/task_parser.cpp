#include "task_parser.h"
#include <QRegularExpression>
#include <QTimeZone>
namespace dragon {
namespace {
int number(const QString &s){
    bool ok=false;const int n=s.toInt(&ok);if(ok)return n;
    const QString digits="零一二三四五六七八九";if(s=="两")return 2;
    if(s=="十")return 10;const int ten=s.indexOf("十");
    if(ten>=0){const int a=ten==0?1:digits.indexOf(s.front());const int b=ten==s.size()-1?0:digits.indexOf(s.back());return a<0 || b<0?-1:a*10+b;}
    return s.size()==1?digits.indexOf(s.front()):-1;
}
}
TaskDraft TaskParser::parse(const QString &input,QDateTime now){
    TaskDraft d;QString s=input.trimmed();
    s.remove(QRegularExpression("^(?:请你|请|麻烦你|帮我)[，, ]*"));
    s.remove(QRegularExpression("^(?:请|帮我)?(?:添加|新增)待办[：:，, ]*"));
    if(s.startsWith("紧急") || s.startsWith("高优先级")){d.priority=2;s.remove(QRegularExpression("^(?:紧急|高优先级)[：:，, ]*"));}
    else if(s.startsWith("低优先级")){d.priority=0;s.remove(QRegularExpression("^低优先级[：:，, ]*"));}
    const auto relative=QRegularExpression("^(\\d{1,4})(分钟|小时)后(?:提醒我)?[：:，, ]*(.+)$").match(s);
    if(relative.hasMatch()){
        const int count=relative.captured(1).toInt();
        if(count<1 || count>1440){d.needsConfirmation=true;d.question="请确认有效的提醒日期和时间。";}
        else d.due=now.addSecs(qint64(count)*(relative.captured(2)=="小时"?3600:60));
        d.title=relative.captured(3).trimmed();return d;
    }
    const auto absolute=QRegularExpression("^([0-9]{4}-[0-9]{2}-[0-9]{2})[ T]([0-9]{1,2}):([0-9]{2})(?:提醒我)?[：:，, ]*(.+)$").match(s);
    if(absolute.hasMatch()){
        d.due=QDateTime(QDate::fromString(absolute.captured(1),"yyyy-MM-dd"),QTime(absolute.captured(2).toInt(),absolute.captured(3).toInt()),now.timeZone());d.title=absolute.captured(4).trimmed();
    }else{
        const auto chinese=QRegularExpression("^(今天|明天|后天)?(凌晨|早上|上午|中午|下午|晚上)?([0-9零一二三四五六七八九十两]{1,3})[点时](半|[0-9零一二三四五六七八九十]{1,3}分?)?(?:提醒我)?[：:，, ]*(.+)$").match(s);
        const auto clock=QRegularExpression("^(今天|明天|后天)?([0-9]{1,2}):([0-9]{2})(?:提醒我)?[：:，, ]*(.+)$").match(s);
        if(chinese.hasMatch()){
            const auto day=chinese.captured(1),period=chinese.captured(2);int hour=number(chinese.captured(3));
            auto minute=chinese.captured(4);minute.remove("分");const int minutes=minute.isEmpty()?0:minute=="半"?30:number(minute);
            if((period=="下午" || period=="晚上") && hour>=1 && hour<12)hour+=12;
            if(period=="中午" && hour>=1 && hour<=2)hour+=12;
            if(period=="凌晨" && hour==12)hour=0;
            d.due=QDateTime(now.date().addDays(day=="明天"?1:day=="后天"?2:0),QTime(hour,minutes),now.timeZone());d.title=chinese.captured(5).trimmed();
            if(day.isEmpty() || (period.isEmpty() && hour<=12)){d.needsConfirmation=true;d.question="请确认日期，以及上午还是下午。";}
        }else if(clock.hasMatch()){
            const auto day=clock.captured(1);d.due=QDateTime(now.date().addDays(day=="明天"?1:day=="后天"?2:0),QTime(clock.captured(2).toInt(),clock.captured(3).toInt()),now.timeZone());d.title=clock.captured(4).trimmed();
            if(day.isEmpty()){d.needsConfirmation=true;d.question="请确认提醒日期。";}
        }else{
            d.title=s;d.title.remove(QRegularExpression("^提醒我[：:，, ]*"));
            if(s.contains("提醒") || s.contains(QRegularExpression("今天|明天|后天|下周|周[一二三四五六日天]|月底|早上|下午|晚上"))){d.needsConfirmation=true;d.question="时间不完整或有歧义，请选择具体提醒时间。";}
            return d;
        }
    }
    if(!d.due.isValid() || d.due<=now){d.needsConfirmation=true;d.question="提醒时间无效或已过去，请确认新的时间。";}
    if(d.title.isEmpty() || d.title.size()>200){d.needsConfirmation=true;d.question="请填写 1–200 字的待办内容。";}
    return d;
}
}
