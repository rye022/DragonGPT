#pragma once
#include "companion_life.h"
#include <QWidget>

namespace dragon {
class GrowthGauge final:public QWidget {
    Q_OBJECT
public:
    explicit GrowthGauge(QWidget *parent=nullptr);
    void setState(const LifeState &state);
    int percent() const{return percent_;}
    int level() const{return level_;}
protected:
    void paintEvent(QPaintEvent *) override;
private:
    int percent_=0,level_=0;
};
}
