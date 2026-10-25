#include "speech_text_buffer.h"

namespace dragon {
bool SpeechTextBuffer::update(const QString &snapshot){
    if(!snapshot.startsWith(text_.left(consumed_)))return false;
    text_=snapshot;return true;
}
QStringList SpeechTextBuffer::takeReady(bool final,bool idle){
    QStringList result;
    while(consumed_<text_.size()){
        qsizetype end=-1,lastSoft=-1,completeEnd=consumed_;
        int points=0;
        for(qsizetype i=consumed_;i<text_.size();++i){
            const auto c=text_[i];
            if(c.isHighSurrogate()){
                if(i+1>=text_.size())break;
                if(text_[i+1].isLowSurrogate())++i;
            }
            ++points;completeEnd=i+1;
            const bool decimal=c=='.' && i>0 && text_[i-1].isDigit();
            const bool hard=QStringLiteral("。！？!?；;\n").contains(c) || (c=='.' && !decimal);
            const bool soft=c.isSpace() || QStringLiteral("，,：:").contains(c);
            const bool openingClause=consumed_==0 && points>=6 && QStringLiteral("，,：:").contains(c)
                && !(i>0 && text_[i-1].isDigit());
            if(soft)lastSoft=i+1;
            // Start at a complete natural opening clause instead of waiting for
            // 24 characters or the next sentence. Later segments keep their size.
            if((hard || openingClause || (soft && points>=24)) && !text_.mid(consumed_,i+1-consumed_).trimmed().isEmpty()){end=i+1;break;}
            if(points>=80){end=lastSoft>consumed_?lastSoft:i+1;break;}
        }
        if(end<0 && final)end=completeEnd;
        if(end<0 && idle && points>=8){
            const auto last=text_.at(completeEnd-1);
            if(lastSoft>consumed_)end=lastSoft;
            else if(last.unicode()>127)end=completeEnd;
        }
        if(end<=consumed_)break;
        const auto segment=text_.mid(consumed_,end-consumed_);
        consumed_=end;
        if(!segment.trimmed().isEmpty())result<<segment;
    }
    return result;
}
}
