#include "pet_widget.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <algorithm>
using namespace dragon;
int main(int argc,char **argv){
    QApplication app(argc,argv);QTemporaryDir dir;
    Companion c(dir.filePath("memory.sqlite"));PetWidget pet(c);
    pet.setRandomActionsEnabled(false);pet.show();app.processEvents();
    QJsonArray cases;
    for(int height:{260,480,680})for(bool half:{false,true})for(auto pose:{PetAction::Idle,PetAction::Work,PetAction::Comfort}){
        pet.setPetHeight(height);pet.setHalfView(half);pet.previewAction(pose,300000);
        QElapsedTimer settle;settle.start();while(settle.elapsed()<650)app.processEvents(QEventLoop::AllEvents,10);
        for(int warmup=0;warmup<3;++warmup)pet.grab();
        QVector<double> samples;double sum=0;
        for(int i=0;i<30;++i){QElapsedTimer frame;frame.start();pet.grab();const auto ms=frame.nsecsElapsed()/1000000.0;samples.append(ms);sum+=ms;}
        std::sort(samples.begin(),samples.end());
        cases.append(QJsonObject{{"height",height},{"half_view",half},{"pose",int(pose)},{"dpr",pet.devicePixelRatioF()},
            {"frames",samples.size()},{"mean_ms",sum/samples.size()},{"median_ms",samples[samples.size()/2]},{"p95_ms",samples[int(samples.size()*.95)]}});
    }
    QJsonObject result{{"cases",cases},{"network","none"},{"method","30 synchronous QWidget::grab frames after warmup; same Qt/DPR and machine"}};
    if(app.arguments().size()>1){QFile file(app.arguments()[1]);if(!file.open(QIODevice::WriteOnly))return 2;file.write(QJsonDocument(result).toJson());}
    QTextStream(stdout)<<QJsonDocument(result).toJson(QJsonDocument::Compact)<<'\n';return 0;
}
