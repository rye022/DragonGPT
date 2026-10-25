#include "pet_actions.h"
#include <QTextStream>
#include <cmath>
#include <stdexcept>
using namespace dragon;
void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
void valid(const PetActions::Weights &w){double sum=0;for(double x:w){check(x>=0 && x<=1,"Invalid weight");sum+=x;}check(std::abs(sum-1)<1e-9,"Transition changes opacity");}
int main(){try{
    PetActions a;check(a.weights()[0]==1,"Default must be original master");
    a.set(PetAction::Wave,1000);a.tick(1260);valid(a.weights());
    check(std::abs(a.weights()[0]-.5)<1e-9 && std::abs(a.weights()[1]-.5)<1e-9,"Midpoint blend incorrect");
    const auto visible=a.weights();a.set(PetAction::Sleep,1260);
    check(a.weights()==visible,"Interrupt snapped visible pose");
    a.tick(1520);valid(a.weights());check(a.weights()[1]>0 && a.weights()[4]>0,"Interrupted transition lost old mixture");
    a.set(PetAction::Work,1520);a.tick(2040);valid(a.weights());
    check(a.weights()[5]==1 && !a.transitioning(),"Work did not settle");
    a.set(PetAction::Work,2200);check(!a.transitioning(),"Repeated target restarted animation");
    for(int i=0;i<200;++i){a.set(static_cast<PetAction>(i%7),3000+i*17);a.tick(3008+i*17);valid(a.weights());}
    a.tick(10000);valid(a.weights());check(!a.transitioning(),"Rapid input never settled");
    a.reset();check(a.weights()[0]==1 && a.target()==PetAction::Idle,"Disabling motion does not restore master");
    a.set(PetAction::Work,100);a.tick(300);check(a.visible()==PetAction::Idle,"Early blend click must use visible old pose");
    a.tick(500);check(a.visible()==PetAction::Work,"Late blend click must use visible work pose");
    ActionCycle cycle(12345);qint64 now=0;PetAction previous=PetAction::Idle;
    for(int round=0;round<50;++round){
        std::array<bool,7> seen{};
        for(int i=0;i<6;++i){
            cycle.tick(now);const auto action=cycle.current();
            check(action!=PetAction::Idle && action!=previous,"Random cycle returned idle or repeated adjacent pose");
            check(!seen[int(action)],"Random round repeated before all six appeared");seen[int(action)]=true;
            const auto deadline=cycle.nextAt();check(deadline-now>=20000 && deadline-now<=40000,"Dwell outside 20-40 seconds");
            cycle.tick(deadline-1);check(cycle.current()==action,"Random pose changed before deadline");
            now=deadline;previous=action;
        }
    }
    cycle.tick(now);const auto held=cycle.current();const auto remaining=cycle.nextAt()-now;
    cycle.tick(now+120000,true);check(cycle.current()==held,"Paused interaction switched pose");
    cycle.tick(now+120000);check(cycle.current()==held && cycle.nextAt()-(now+120000)==remaining,"Pause consumed dwell time or caused catchup");
    QTextStream(stdout)<<"PASS: transitions preserve opacity and position on interruption; repeated and rapid requests settle.\n";return 0;
}catch(const std::exception &e){QTextStream(stderr)<<e.what()<<"\n";return 1;}}
