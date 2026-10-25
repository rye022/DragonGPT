#include "sprite_motion.h"
#include <QPainterPath>
#include <QTransform>
#include <cmath>
#include <algorithm>
#include <array>

namespace dragon {
namespace {
double ramp(double value,double start,double end){const auto t=std::clamp((value-start)/(end-start),0.0,1.0);return t*t*(3-2*t);}
struct Face {QPointF left,right,mouth;double angle;};
Face face(PetAction pose){
    switch(pose){
    case PetAction::Wave:return {{.417,.362},{.559,.323},{.505,.400},-14};
    case PetAction::Shy:return {{.361,.361},{.499,.329},{.434,.405},-12};
    case PetAction::Jealous:return {{.388,.358},{.526,.350},{.462,.414},-3};
    case PetAction::Sleep:return {{.379,.397},{.555,.340},{.490,.423},-14};
    case PetAction::Work:return {{.386,.380},{.534,.353},{.453,.423},-11};
    case PetAction::Comfort:return {{.386,.372},{.524,.329},{.451,.410},-14};
    default:return {{.437,.332},{.566,.301},{.507,.361},-15};
    }
}
// These two bottom-row poses use padded 540 x 529 canvases, not the
// unpadded atlas cells. Trace only the authored eye/lash contours: eyebrows,
// fringe, mouth and blush retain their existing bindings and pixels.
struct PoseEye {QPainterPath mask;QPointF center;double slope,halfWidth;QColor skinTop,skinBottom;};
std::array<PoseEye,2> poseEyes(PetAction pose){
    std::array<PoseEye,2> eyes;
    auto &l=eyes[0];auto &r=eyes[1];
    if(pose==PetAction::Work){
        l.mask.moveTo(176,232);l.mask.lineTo(181,230);l.mask.lineTo(179,227);
        l.mask.cubicTo(190,221,202,218,213,222);l.mask.cubicTo(223,228,228,239,225,247);
        l.mask.cubicTo(212,256,192,249,184,241);l.mask.closeSubpath();
        r.mask.moveTo(252,220);r.mask.cubicTo(257,207,279,202,299,205);
        r.mask.lineTo(310,204);r.mask.lineTo(307,208);r.mask.lineTo(313,208);r.mask.lineTo(307,213);
        r.mask.cubicTo(307,226,299,240,283,240);r.mask.cubicTo(268,240,259,230,257,222);r.mask.closeSubpath();
        l.center={201,237};l.slope=.14;l.halfWidth=20;
        r.center={282,224};r.slope=-.17;r.halfWidth=24;
        l.skinTop=r.skinTop=QColor("#fdeade");l.skinBottom=QColor("#fdd9d1");r.skinBottom=QColor("#fcd3c7");
    }else{
        l.mask.moveTo(201,228);l.mask.lineTo(207,224);l.mask.lineTo(204,224);
        l.mask.cubicTo(218,211,238,205,250,215);l.mask.cubicTo(258,227,253,239,241,242);
        l.mask.cubicTo(225,245,211,236,207,232);l.mask.lineTo(201,233);l.mask.lineTo(205,229);l.mask.closeSubpath();
        r.mask.moveTo(283,202);r.mask.cubicTo(288,190,309,180,328,183);
        r.mask.lineTo(336,181);r.mask.lineTo(334,185);r.mask.lineTo(341,185);r.mask.lineTo(334,191);
        r.mask.cubicTo(333,206,324,220,311,221);r.mask.cubicTo(297,223,288,213,287,204);r.mask.closeSubpath();
        l.center={230,227};l.slope=-.20;l.halfWidth=22;
        r.center={313,204};r.slope=-.23;r.halfWidth=24;
        l.skinTop=r.skinTop=QColor("#fce6d9");l.skinBottom=QColor("#fbd9ce");r.skinBottom=QColor("#fcc6bb");
    }
    return eyes;
}
void paintPoseBlink(QPainter &p,const QImage &image,PetAction pose,double blink){
    const double amount=std::clamp(blink,0.0,1.0),opening=1-amount;
    p.save();p.scale(image.width()/540.0,image.height()/529.0);
    for(const auto &eye:poseEyes(pose)){
        p.save();p.setClipPath(eye.mask,Qt::IntersectClip);
        const auto bounds=eye.mask.boundingRect();
        QLinearGradient skin(bounds.topLeft(),bounds.bottomLeft());
        skin.setColorAt(0,eye.skinTop);skin.setColorAt(1,eye.skinBottom);
        p.fillPath(eye.mask,skin);
        // Compress only the original eye texture toward its own tilted lid.
        // Both clips are needed: the source clip excludes brows/hair before
        // compression, while the destination clip protects surrounding skin.
        if(opening>.025){
            p.save();
            const QTransform close(1,(1-opening)*eye.slope,0,opening,0,
                                   (1-opening)*(eye.center.y()-eye.slope*eye.center.x()));
            p.setTransform(close,true);p.setClipPath(eye.mask,Qt::IntersectClip);
            p.drawImage(QRectF(0,0,540,529),image);p.restore();
        }
        if(amount>.85){
            p.setOpacity(p.opacity()*(amount-.85)/.15);
            p.translate(eye.center);p.rotate(std::atan(eye.slope)*180/3.141592653589793);
            p.setPen(QPen(QColor("#654459"),1.7,Qt::SolidLine,Qt::RoundCap));p.setBrush(Qt::NoBrush);
            QPainterPath lid;lid.moveTo(-eye.halfWidth,0);
            lid.cubicTo(-eye.halfWidth*.4,4,eye.halfWidth*.4,4,eye.halfWidth,0);p.drawPath(lid);
        }
        p.restore();
    }
    p.restore();
}
}
QPointF SpriteMotion::offset(QPointF point,PetAction pose,double t){
    const double x=point.x(),y=point.y();
    // Head, torso, costume and legs remain fixed. Only fringe silhouettes move.
    const double hairY=ramp(y,.30,.48)*(1-ramp(y,.88,.98));
    const double left=1-ramp(x,.20,.27),right=ramp(x,.72,.83);
    const double slow=pose==PetAction::Sleep?.58:1.0;
    QPointF out(.006*hairY*(left*std::sin(t*1.35*slow)+right*std::sin(t*1.35*slow+.65)),
                .0015*hairY*(left+right)*std::sin(t*1.15*slow));
    if(pose==PetAction::Idle || pose==PetAction::Wave || pose==PetAction::Shy || pose==PetAction::Jealous){
        const double tail=ramp(x,.77,.90)*ramp(y,.58,.71)*(1-ramp(y,.90,.98));
        const double amplitude=pose==PetAction::Wave?1.35:pose==PetAction::Shy?.65:1.0;
        out+=QPointF(.014*tail*amplitude*std::sin(t*2.25),.004*tail*amplitude*std::sin(t*2.25+.6));
    }
    // The flat authored Wave image has no separate arm layer. Its old hand
    // ellipse overlapped the face; retain the authored raised hand until an
    // original, correctly layered arm is available. Never warp the face.
    return out;
}
void SpriteMotion::paint(QPainter &p,const QImage &image,PetAction pose,double t,double blink,double mouth,double blush,bool moving){
    const double w=image.width(),h=image.height();if(w<=0 || h<=0)return;
    if(!moving)p.drawImage(QPointF(0,0),image);
    else{
        // One continuous inverse map, sampled once per pixel. Tiled QPainter
        // quads left antialiased seams even without a wireframe/debug pen.
        // Resample only the small silhouette bands; copy protected pixels exactly.
        const auto source=image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        auto result=source.copy();
        const double slow=pose==PetAction::Sleep?.58:1.0;
        const double l=std::sin(t*1.35*slow),r=std::sin(t*1.35*slow+.65),v=std::sin(t*1.15*slow);
        const bool tailPose=pose==PetAction::Idle || pose==PetAction::Wave || pose==PetAction::Shy || pose==PetAction::Jealous;
        const double tailX=std::sin(t*2.25),tailY=std::sin(t*2.25+.6);
        const double a=pose==PetAction::Wave?1.35:pose==PetAction::Shy?.65:1.0;
        const auto field=[&](double x,double y){
            const double hy=ramp(y,.30,.48)*(1-ramp(y,.88,.98));
            const double left=1-ramp(x,.20,.27),right=ramp(x,.72,.83);
            const double tail=tailPose?ramp(x,.77,.90)*ramp(y,.58,.71)*(1-ramp(y,.90,.98)):0;
            return QPointF(.006*hy*(left*l+right*r)+.014*tail*a*tailX,
                           .0015*hy*(left+right)*v+.004*tail*a*tailY);
        };
        for(int y=int(h*.30);y<int(h*.98);++y){
            auto *row=reinterpret_cast<QRgb*>(result.scanLine(y));
            for(int x=0;x<int(w);++x){
                if(x>=int(w*.27) && x<=int(w*.72))continue;
                const QPointF dest(x/w,y/h);auto uv=dest;
                uv=dest-field(dest.x(),dest.y());
                const double sx=uv.x()*w,sy=uv.y()*h;
                const int ix=int(std::floor(sx)),iy=int(std::floor(sy));
                if(ix<0 || iy<0 || ix+1>=w || iy+1>=h){row[x]=0;continue;}
                const double fx=sx-ix,fy=sy-iy;
                const auto *top=reinterpret_cast<const QRgb*>(source.constScanLine(iy));
                const auto *bottom=reinterpret_cast<const QRgb*>(source.constScanLine(iy+1));
                const QRgb pixels[]={top[ix],top[ix+1],bottom[ix],bottom[ix+1]};
                const double weights[]={(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};
                double red=0,green=0,blue=0,alpha=0;
                for(int n=0;n<4;++n){red+=qRed(pixels[n])*weights[n];green+=qGreen(pixels[n])*weights[n];blue+=qBlue(pixels[n])*weights[n];alpha+=qAlpha(pixels[n])*weights[n];}
                row[x]=qRgba(qRound(red),qRound(green),qRound(blue),qRound(alpha));
            }
        }
        p.drawImage(QPointF(0,0),result);
    }
    const auto f=face(pose);
    const auto pixel=[&](QPointF v){return QPointF(v.x()*w,v.y()*h);};
    if(blink>.01 && pose!=PetAction::Sleep){
        if(pose==PetAction::Work || pose==PetAction::Comfort)paintPoseBlink(p,image,pose,blink);
        else{
        for(const auto &eye:{f.left,f.right}){
            p.save();p.translate(pixel(eye));p.rotate(f.angle);
            p.save();p.setClipRect(QRectF(-w*.040,-h*.024,w*.080,h*.050*std::clamp(blink*1.6,0.0,1.0)),Qt::IntersectClip);
            p.setPen(Qt::NoPen);p.setBrush(QColor("#f8e2ec"));p.drawEllipse(QRectF(-w*.038,-h*.022,w*.076,h*.047));p.restore();
            p.setPen(QPen(QColor("#85607d"),qMax(1.0,w*.0026),Qt::SolidLine,Qt::RoundCap));
            const double lidY=-h*.022*(1-std::clamp(blink,0.0,1.0));
            QPainterPath lid;lid.moveTo(-w*.031,lidY);lid.cubicTo(-w*.012,lidY+h*.012,w*.015,lidY+h*.012,w*.031,lidY);p.drawPath(lid);p.restore();
        }
        }
    }
    if(blush>.01){
        for(const auto &eye:{f.left,f.right}){
            const QPointF at=pixel(eye)+QPointF(0,h*.041);QRadialGradient gradient(at,w*.036);
            gradient.setColorAt(0,QColor(237,123,165,qRound(50*blush)));gradient.setColorAt(1,Qt::transparent);
            p.save();p.setBrush(gradient);p.setPen(Qt::NoPen);p.drawEllipse(at,w*.035,h*.014);p.restore();
        }
    }
    if(mouth>.08){
        p.save();p.translate(pixel(f.mouth));p.rotate(f.angle);
        p.setPen(QPen(QColor("#b47f99"),qMax(1.0,w*.0013)));p.setBrush(QColor("#bb789a"));
        p.drawEllipse(QRectF(-w*.007,-h*.001,w*.014,h*(.003+.010*mouth)));p.restore();
    }
}
}
