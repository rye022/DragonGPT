#pragma once
#include <QStringList>

namespace dragon {
// Cumulative model snapshots become stable, ordered speech segments.
class SpeechTextBuffer final {
public:
    bool update(const QString &snapshot);
    QStringList takeReady(bool final=false,bool idle=false);
    void reset(){text_.clear();consumed_=0;}
    bool pending() const{return consumed_<text_.size();}
private:
    QString text_;
    qsizetype consumed_=0;
};
}
