#include "local_brain.h"
#include <QRegularExpression>
namespace dragon {
LocalCommand LocalBrain::recognize(const QString &input){
    // Match the whole utterance. Emotional statements, quotations, negation,
    // hypothetical language and combined instructions belong to conversation.
    QString s=input.trimmed().toLower();
    if(s.size()>240 || s.contains(QRegularExpression("[\n\"“”]|如果|假如|比如|例如|不要|不想|别再|为什么|怎么实现|如何实现|然后|并且|顺便")))return {};
    s.remove(QRegularExpression("\\s+"));
    s.remove(QRegularExpression("[。！？!?，,、\\s]+$"));
    s.remove(QRegularExpression("^(?:霜翎[，, ]*)?(?:请你|请|麻烦你|帮我|能不能|可以)?[，, ]*"));
    s.remove(QRegularExpression("(?:好吗|好不好|可以吗|吧|呀|啦|哦|呢)$"));
    auto match=[&](const char *p){return QRegularExpression(QString::fromUtf8(p),QRegularExpression::CaseInsensitiveOption).match(s);};
    auto result=[](LocalIntent intent){LocalCommand c;c.intent=intent;return c;};
    // Specific tools outrank generic time/status and action phrases.
    if(match("^(?:打开|查看)(?:电脑|系统)(?:状态|健康)(?:页面|面板)?$").hasMatch())return result(LocalIntent::HealthPage);
    if(match("^(?:查看|显示|总结)?(?:今天|今日|每日)的?(?:使用)?总结$").hasMatch())return result(LocalIntent::DailySummary);
    if(match("^(?:查看|看看|显示|列出)?(?:我)?(?:今天|今日)的?待办(?:事项)?$").hasMatch())return result(LocalIntent::TodoList);
    if(match("^(?:添加待办|新增待办|提醒我|明天|后天|今天|[0-9]{1,4}(?:分钟|小时)后|[0-9]{4}-[0-9]{2}-[0-9]{2}).+").hasMatch()
       && (s.contains("待办") || s.contains("提醒我")) && !s.contains("喝水")){
        auto c=result(LocalIntent::TodoAdd);c.argument=input.trimmed();return c;
    }
    auto water=match("^(?:在)?(?:(\\d{1,3})分钟后)?提醒我喝水$");
    if(water.hasMatch()){auto c=result(LocalIntent::Water);c.minutes=water.captured(1).isEmpty()?30:water.captured(1).toInt();return c;}
    if(match("^(?:暂停|停止)番茄钟$").hasMatch())return result(LocalIntent::PomodoroPause);
    if(match("^(?:还有多久休息|番茄钟还剩多久|番茄钟剩余时间)$").hasMatch())return result(LocalIntent::PomodoroRemaining);
    auto pom=match("^(?:开始|启动|继续)(?:(\\d{1,3})分钟(?:的)?)?番茄钟$");
    if(pom.hasMatch()){auto c=result(LocalIntent::PomodoroStart);c.minutes=pom.captured(1).isEmpty()?0:pom.captured(1).toInt();if(!pom.captured(1).isEmpty() && c.minutes==0)c.minutes=-1;return c;}
    pom=match("^(?:开始|启动)(?:一个)?番茄钟(?:[（(]?(\\d{1,3})分钟[）)]?)?$");
    if(pom.hasMatch()){auto c=result(LocalIntent::PomodoroStart);c.minutes=pom.captured(1).isEmpty()?0:pom.captured(1).toInt();if(!pom.captured(1).isEmpty() && c.minutes==0)c.minutes=-1;return c;}
    if(match("^(?:现在|此刻)?几点(?:了|钟)?$|^(?:告诉我|查看)?(?:现在的?|当前)?时间$|^现在几点了$").hasMatch())return result(LocalIntent::Time);
    if(match("^(?:今天|今日)(?:是)?(?:星期几|周几|礼拜几)$").hasMatch())return result(LocalIntent::Weekday);
    if(match("^(?:我的|当前|现在的?)?(?:电脑的?)?cpu(?:占用率|使用率|负载)(?:是|有)?多少$").hasMatch())return result(LocalIntent::Cpu);
    if(match("^(?:我的|当前|现在的?)?(?:电脑的?)?内存(?:使用情况|占用率|使用率|占用|使用情况怎么样)(?:是|有)?(?:多少|怎么样)?$").hasMatch())return result(LocalIntent::Memory);
    if(match("^(?:我的)?电脑(?:现在)?(?:卡不卡|卡吗|健康状态怎么样|负载怎么样)$").hasMatch())return result(LocalIntent::Health);
    struct ActionRule{const char *pattern;PetAction action;};
    static const ActionRule actions[]={
        {"^(?:招招手|招个手|挥挥手|招手)$",PetAction::Wave},
        {"^(?:休息一下|休息一会儿|进入休息状态)$",PetAction::Sleep},
        {"^(?:进入|切换到|开启)(?:认真)?工作(?:状态|模式)$",PetAction::Work},
        {"^(?:换一个|来个|做个)开心的?动作$",PetAction::Wave},
        {"^(?:回到|恢复|切换到)(?:默认)?待机(?:动作|状态)?$",PetAction::Idle},
        {"^(?:抱尾陪伴|抱抱尾巴)$",PetAction::Comfort}};
    for(const auto &rule:actions)if(match(rule.pattern).hasMatch()){auto c=result(LocalIntent::Action);c.action=rule.action;return c;}
    return {};
}
}
