#include "pet_actions.h"
#include <algorithm>

namespace dragon {
QString actionName(PetAction action){
    switch(action){
    case PetAction::Idle:return QStringLiteral("陪伴");
    case PetAction::Wave:return QStringLiteral("开心招手");
    case PetAction::Shy:return QStringLiteral("害羞傲娇");
    case PetAction::Jealous:return QStringLiteral("鼓脸吃醋");
    case PetAction::Sleep:return QStringLiteral("抱枕休息");
    case PetAction::Work:return QStringLiteral("认真工作");
    case PetAction::Comfort:return QStringLiteral("抱尾陪伴");
    }
    return {};
}
void PetActions::reset(){target_=PetAction::Idle;weights_={1,0,0,0,0,0,0};from_=weights_;transitioning_=false;}
void PetActions::set(PetAction action,qint64 now){
    if(action==target_)return;
    tick(now);from_=weights_;target_=action;started_=now;transitioning_=true;
}
void PetActions::tick(qint64 now){
    if(!transitioning_)return;
    const double t=std::clamp(double(now-started_)/Duration,0.0,1.0);
    const double ease=t*t*t*(t*(t*6-15)+10); // zero velocity and acceleration at both ends
    for(int i=0;i<Count;++i)weights_[i]=from_[i]*(1-ease)+(i==int(target_)?ease:0);
    if(t>=1)transitioning_=false;
}
PetAction PetActions::visible() const{
    return static_cast<PetAction>(std::max_element(weights_.begin(),weights_.end())-weights_.begin());
}
void ActionCycle::tick(qint64 now,bool paused,const QList<PetAction> &allowed){
    const auto dt=lastAt_<0?0:qMax(qint64(0),now-lastAt_);lastAt_=now;
    if(allowed!=allowed_){
        allowed_=allowed;bag_.clear();
        if(!allowed_.isEmpty()){
            current_=allowed_.first();nextAt_=now+30000;
            for(auto action:allowed_)if(action!=current_)bag_.append(action);
            return;
        }
        nextAt_=now;
    }
    if(paused){nextAt_+=dt;return;}
    if(now<nextAt_)return;
    if(bag_.isEmpty()){
        if(allowed_.isEmpty())for(int i=1;i<PetActions::Count;++i)bag_.append(static_cast<PetAction>(i));
        else bag_=allowed_;
    }
    int slot=random_.bounded(int(bag_.size()));
    if(bag_[slot]==current_ && bag_.size()>1)slot=(slot+1)%int(bag_.size());
    current_=bag_.takeAt(slot);nextAt_=now+random_.bounded(20000,40001);
}
}
