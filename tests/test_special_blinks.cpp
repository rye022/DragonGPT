#include "pet_widget.h"
#include "sprite_motion.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTest>
#include <QElapsedTimer>
#include <stdexcept>
using namespace dragon;
namespace {
void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
QImage source(PetAction pose){
    if(pose==PetAction::Idle)return QImage(":/assets/character.png");
    const QImage atlas(":/assets/actions.png");
    const int edges[]={0,540,1024,1536},i=int(pose)-1,col=i%3,row=i/3;
    QImage result(540,529,QImage::Format_ARGB32_Premultiplied);result.fill(Qt::transparent);
    const auto cell=atlas.copy(edges[col],row?529:0,edges[col+1]-edges[col],row?495:529);
    QPainter p(&result);p.drawImage(QPoint((540-cell.width())/2,529-cell.height()),cell);return result;
}
QImage render(const QImage &im,PetAction pose,double t,double blink,double mouth=0,double blush=0){
    QImage result(im.size(),QImage::Format_ARGB32_Premultiplied);result.fill(Qt::transparent);
    QPainter p(&result);p.setRenderHint(QPainter::Antialiasing);p.setRenderHint(QPainter::SmoothPixmapTransform);
    SpriteMotion::paint(p,im,pose,t,blink,mouth,blush,true);return result;
}
QString hash(const QImage &im){return QString::fromLatin1(QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(im.constBits()),im.sizeInBytes()),QCryptographicHash::Sha256).toHex());}
int irisPixels(const QImage &im,QRect area){
    int count=0;for(int y=area.top();y<=area.bottom();++y)for(int x=area.left();x<=area.right();++x){
        const auto c=im.pixelColor(x,y);if(c.blue()>c.red()+15 && c.blue()>c.green()+20 && c.red()<225 && c.blue()>100)++count;
    }return count;
}
void liveWidget(const QString &directory){
    QTemporaryDir tmp;Companion companion(tmp.path()+"/memory.sqlite");PetWidget pet(companion);
    // Make the test window discoverable by desktop capture; rendering and the
    // production timer/compositor remain the actual PetWidget implementation.
    pet.setWindowFlag(Qt::Tool,false);pet.setWindowTitle("DragonGPT blink verification");
    pet.setRandomActionsEnabled(false);pet.setAutomaticRest(false);pet.setPetHeight(440);pet.show();
    // Exercise interrupted bindings before observing naturally timed blinks.
    for(int i=0;i<24;++i){pet.previewAction(PetAction(i%7));QTest::qWait(25);pet.grab();}
    const QRect iris[][2]={{{193,228,25,20},{265,215,30,21}},{{220,218,30,23},{297,193,30,25}}};
    for(int n=0;n<2;++n){
        const auto pose=n?PetAction::Comfort:PetAction::Work;
        pet.previewAction(pose,300000);QTest::qWait(650);
        const double height=440,masterScale=(height-16)/1300.0,scale=1209.0/540*masterScale;
        const QPointF origin((pet.width()-1209*masterScale)/2,102+(1300-529*1209.0/540)*masterScale);
        QRect probes[2];for(int e=0;e<2;++e)probes[e]=QRectF(origin+iris[n][e].topLeft()*scale,QSizeF(iris[n][e].size())*scale).toAlignedRect();
        const auto initial=pet.grab().toImage().scaled(pet.size());int reference=irisPixels(initial,probes[0])+irisPixels(initial,probes[1]);
        if(!directory.isEmpty())initial.save(directory+QString("/live-%1-open.png").arg(int(pose)));
        // A pose may be entered in mid-blink: measure the open reference too.
        int minimum=100000,cycles=0;bool wasClosed=false;QElapsedTimer timer;timer.start();
        while(timer.elapsed()<14000){
            QTest::qWait(20);const auto frame=pet.grab().toImage().scaled(pet.size());
            const int count=irisPixels(frame,probes[0])+irisPixels(frame,probes[1]);
            if(count>reference){reference=count;if(!directory.isEmpty())frame.save(directory+QString("/live-%1-open.png").arg(int(pose)));}
            const bool closed=count<reference*.15;
            if(closed && !wasClosed)++cycles;wasClosed=closed;
            if(count<minimum){minimum=count;if(!directory.isEmpty())frame.save(directory+QString("/live-%1-closed.png").arg(int(pose)));}
        }
        QTextStream(stdout)<<"LIVE "<<actionName(pose)<<": iris "<<reference<<" -> "<<minimum<<", natural blinks "<<cycles<<"\n";
        check(reference>80 && minimum<reference*.15 && cycles>=2,"Real widget did not naturally close and reopen both eyes repeatedly");
    }
    const PetAction pairs[][2]={{PetAction::Idle,PetAction::Comfort},{PetAction::Comfort,PetAction::Idle},
        {PetAction::Idle,PetAction::Work},{PetAction::Work,PetAction::Idle},
        {PetAction::Comfort,PetAction::Work},{PetAction::Work,PetAction::Comfort}};
    for(const auto &pair:pairs){
        pet.previewAction(pair[0]);QTest::qWait(550);pet.previewAction(pair[1]);QTest::qWait(550);
        check(pet.currentAction()==pair[1] && !pet.grab().isNull(),"Widget pose transition failed");
    }
    for(const auto pose:{PetAction::Comfort,PetAction::Work,PetAction::Idle,PetAction::Wave,PetAction::Shy}){
        pet.previewAction(pose);QTest::qWait(600);check(pet.currentAction()==pose,"Rapid pose switch retained stale target");
        if(!directory.isEmpty())pet.grab().save(directory+QString("/live-settled-%1.png").arg(int(pose)));
    }
}
}
int main(int argc,char **argv){
    QApplication app(argc,argv);QTemporaryDir tmp;
    const auto args=app.arguments();const QString exportDir=args.size()>1 && !args[1].startsWith("--")?args[1]:QString();
    const bool baseline=args.contains("--baseline");
    try{
        QJsonObject fingerprints;
        for(int i=0;i<7;++i){
            const auto pose=PetAction(i);const auto im=source(pose);
            for(int j=0;j<5;++j){
                const auto frame=render(im,pose,.6,j*.25);
                if(!exportDir.isEmpty()){QDir().mkpath(exportDir);check(frame.save(exportDir+QString("/pose-%1-blink-%2.png").arg(i).arg(j)),"Export failed");}
            }
            // Freeze existing idle/wave/expressions and every pose with no blink.
            for(double t:{0.,.6,1.2})for(double b:{0.,.5,1.})for(double expression:{0.,.7})
                if(i<5 || b==0)fingerprints[QString("%1/%2/%3/%4").arg(i).arg(t).arg(b).arg(expression)]=hash(render(im,pose,t,b,expression,expression));
        }
        if(!exportDir.isEmpty()){
            QFile file(exportDir+"/protected-hashes.json");check(file.open(QIODevice::WriteOnly),"Cannot write hashes");file.write(QJsonDocument(fingerprints).toJson());
        }
        QFile golden(QFINDTESTDATA("special_blink_baseline.json"));
        check(golden.open(QIODevice::ReadOnly),"Protected baseline is missing");
        check(fingerprints==QJsonDocument::fromJson(golden.readAll()).object(),"Idle, wave, expression or unblinking pose changed from the pre-fix baseline");
        const QRect eyes[][2]={{{190,228,36,22},{264,212,39,27}},{{217,214,37,28},{294,188,37,33}}};
        const QRect bounds[][2]={{{174,219,55,36},{250,202,64,42}},{{199,207,57,39},{281,179,61,46}}};
        const QRect eyebrows[][2]={{{191,216,14,4},{273,200,17,3}},{{224,205,14,4},{303,177,14,3}}};
        for(int n=0;n<2;++n){
            const auto pose=n?PetAction::Comfort:PetAction::Work;const auto im=source(pose);
            const auto open=render(im,pose,.6,0),closed=render(im,pose,.6,1);
            for(int e=0;e<2;++e){
                const int before=irisPixels(open,eyes[n][e]),after=irisPixels(closed,eyes[n][e]);
                QTextStream(stdout)<<actionName(pose)<<" eye "<<e<<": iris "<<before<<" -> "<<after<<"\n";
                check(before>100,"Eye probe does not cover the authored iris");
                if(!baseline)check(after==0,"Closed blink left the authored iris visible");
            }
            if(!baseline){
                for(double b:{.15,.5,.8,1.}){
                    const auto frame=render(im,pose,.6,b);
                    for(const auto &brow:eyebrows[n])check(frame.copy(brow)==open.copy(brow),"Blink moved an authored eyebrow");
                    for(int y=0;y<im.height();++y)for(int x=0;x<im.width();++x)
                        if(!bounds[n][0].contains(x,y) && !bounds[n][1].contains(x,y))
                            check(frame.pixel(x,y)==open.pixel(x,y),"Blink changed eyebrows, hair, head, tail, hands or laptop outside eye bounds");
                }
                const auto expression=render(im,pose,.6,0,.7,.7),expressionBlink=render(im,pose,.6,1,.7,.7);
                for(int e=0;e<2;++e)check(irisPixels(expressionBlink,eyes[n][e])==0,"Mouth or blush overrode the closed eyes");
                for(int y=0;y<im.height();++y)for(int x=0;x<im.width();++x)
                    if(!bounds[n][0].contains(x,y) && !bounds[n][1].contains(x,y))
                        check(expression.pixel(x,y)==expressionBlink.pixel(x,y),"Blink changed the existing mouth or blush overlays");
                check(open==render(im,pose,.6,0),"Eye failed to reopen exactly");
            }
        }
        if(args.contains("--live"))liveWidget(exportDir);
        QTextStream(stdout)<<"PASS: "<<fingerprints.size()<<" protected render hashes; authored iris probes and eye-only changes. No live API.\n";
        return 0;
    }catch(const std::exception &e){QTextStream(stderr)<<"FAIL: "<<e.what()<<"\n";return 1;}
}
