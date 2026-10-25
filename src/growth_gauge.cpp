#include "growth_gauge.h"
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>

namespace dragon {
GrowthGauge::GrowthGauge(QWidget *parent):QWidget(parent){
    setMinimumHeight(192);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);setObjectName("growthGauge");
}
void GrowthGauge::setState(const LifeState &s){
    percent_=s.growthPercent();level_=s.stageIndex();
    setToolTip(s.stage()+"\n"+s.growthHint()+QString("\n今日成长 %1 / 20").arg(s.dailyExperience));
    setAccessibleName(QString("关系成长，%1阶段，%2百分比").arg(s.stage()).arg(percent_));update();
}
void GrowthGauge::paintEvent(QPaintEvent *){
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    const QRectF card=rect().adjusted(1,1,-1,-1);p.setPen(QPen(QColor("#40334c"),1));p.setBrush(QColor("#211b2b"));p.drawRoundedRect(card,16,16);
    const double diameter=qMin(154.0,height()-28.0),cx=qMin(width()*.36,180.0),cy=height()/2.0;
    const QRectF circle(cx-diameter/2,cy-diameter/2,diameter,diameter);
    p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor("#3b3047"),11,Qt::SolidLine,Qt::RoundCap));p.drawEllipse(circle);
    QLinearGradient gradient(circle.topLeft(),circle.bottomRight());gradient.setColorAt(0,QColor("#b9a4f0"));gradient.setColorAt(1,QColor("#f0aed5"));
    if(percent_>0){p.setPen(QPen(QBrush(gradient),11,Qt::SolidLine,Qt::RoundCap));p.drawArc(circle,90*16,-qRound(percent_*3.6*16));}
    p.setPen(QColor("#f4eafc"));QFont font("Microsoft YaHei UI",29,QFont::DemiBold);p.setFont(font);p.drawText(circle,Qt::AlignCenter,QString::number(percent_)+"%");
    const double left=qMax(cx+diameter/2+34,width()*.62),spacing=qMin(39.0,(width()-left-20)/3);
    for(int i=0;i<4;++i){
        const QPointF at(left+i*spacing,cy);p.setPen(QPen(i<=level_?QColor("#c6adeb"):QColor("#554362"),1.4));
        p.setBrush(i<=level_?QColor("#6d527f"):QColor("#2b2236"));p.drawEllipse(at,11,11);
        if(i==level_){p.setBrush(QColor("#edc5ee"));p.setPen(Qt::NoPen);p.drawEllipse(at,4,4);}
        if(i<3){p.setPen(QPen(QColor("#554362"),1));p.drawLine(QPointF(at.x()+13,at.y()),QPointF(at.x()+spacing-13,at.y()));}
    }
}
}
