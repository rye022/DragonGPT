#include "pet_widget.h"
#include "sprite_motion.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QHBoxLayout>
#include <QPushButton>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QCursor>
#include <QScreen>
#include <QRandomGenerator>
#include <cmath>
#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace dragon {
namespace {
int opaqueTop(const QImage &image){
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)if(qAlpha(image.pixel(x,y))>16)return y;
    return 0;
}
}
PetWidget::PetWidget(Companion &companion)
    : QWidget(nullptr,Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|Qt::Tool),
      companion_(companion),sprite_(":/assets/character.png") {
    const QImage atlas(":/assets/actions.png");
    if(!atlas.isNull()){
        for(int row=0;row<2;++row)for(int col=0;col<3;++col){
            // The authored sheet has a clear row gutter at y=529 / 1024.
            const int split=qRound(atlas.height()*529.0/1024.0);
            const int edges[]={0,qRound(atlas.width()*540.0/1536.0),atlas.width()*2/3,atlas.width()};
            const int x=edges[col],y=row==0?0:split;
            const auto cell=atlas.copy(x,y,edges[col+1]-x,(row==0?split:atlas.height())-y);
            // Unequal authored gutters are padded into equal canvases. Slicing
            // at the visible gutter avoids cutting a tail or importing a neighbor.
            QImage frame(QSize(edges[1],split),QImage::Format_ARGB32_Premultiplied);frame.fill(Qt::transparent);
            QPainter painter(&frame);painter.drawImage(QPoint((frame.width()-cell.width())/2,frame.height()-cell.height()),cell);painter.end();
            actionFrames_.append(frame);
        }
    }
    headTops_[0]=opaqueTop(sprite_);
    for(int i=0;i<actionFrames_.size();++i){const auto &frame=actionFrames_[i];
        const double ratio=qMin(double(sprite_.width())/frame.width(),double(sprite_.height())/frame.height());
        headTops_[i+1]=sprite_.height()-frame.height()*ratio+opaqueTop(frame)*ratio;
    }
    waterActions_=new QWidget(this);waterActions_->setObjectName("petWaterActions");
    auto *waterRow=new QHBoxLayout(waterActions_);waterRow->setContentsMargins(0,0,0,0);waterRow->setSpacing(3);
    waterActions_->setStyleSheet("QPushButton{background:#f6f0ff;color:#51416c;border:1px solid #cbb7e2;border-radius:5px;padding:3px;font-size:10px;}");
    for(int i=0;i<3;++i){auto *b=new QPushButton(QStringList{"我喝过啦","15 分钟后提醒","今天不再提醒"}[i]);waterRow->addWidget(b);
        b->setObjectName(QString("petWaterAction%1").arg(i));connect(b,&QPushButton::clicked,this,[this,i]{if(i==0)emit waterConfirmed();else if(i==1)emit waterSnoozed();else emit waterMutedToday();});}
    waterActions_->hide();
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle("霜翎 · DragonGPT");
    setCursor(Qt::OpenHandCursor);
    setPetHeight(petHeight_);
    clock_.start();
    clickTimer_.setSingleShot(true);
    clickTimer_.setTimerType(Qt::PreciseTimer);
    connect(&clickTimer_,&QTimer::timeout,this,[this]{lastClickMs_=-10000;capturePending_=false;emit actionClicked(clickedAction_);emit clicked();});
    timer_.setInterval(33);
    timer_.setObjectName("petAnimationTimer");
    timer_.setParent(this);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_,&QTimer::timeout,this,[this]{animate();update();});
    timer_.start();
    connect(&companion_,&Companion::turnStarted,this,[this]{resumeAutomaticActions();});
    connect(&companion_.voice(),&VoiceSession::changed,this,[this]{
        if(companion_.voice().state()!=VoiceSession::State::Idle){lastInteraction_=clock_.elapsed();if(!interactionActive_ && manualAction_==PetAction::Sleep)manualUntil_=0;}
        animate();update();
    });
    connect(&companion_.emotions(),&EmotionEngine::changed,this,[this]{animate();update();});
    say("你来了。我是霜翎。\n双击和我聊聊，右键打开菜单。");
}
void PetWidget::hideEvent(QHideEvent *event){timer_.stop();clickTimer_.stop();capturePending_=false;QWidget::hideEvent(event);}
void PetWidget::showEvent(QShowEvent *event){lastFrame_=clock_.elapsed();timer_.start();QWidget::showEvent(event);}
void PetWidget::showOnDesktop(){
    bool onScreen=false;
    for(const auto *display:QGuiApplication::screens())
        if(display->availableGeometry().intersects(frameGeometry())){onScreen=true;break;}
    if(!onScreen && QGuiApplication::primaryScreen()){
        const auto area=QGuiApplication::primaryScreen()->availableGeometry();
        move(qMax(area.left(),area.right()-width()-20),qMax(area.top(),area.bottom()-height()-10));
    }
    show();
#ifdef Q_OS_WIN
    // STARTF_USESHOWWINDOW/SW_HIDE can consume the first native show call.
    // Reconcile Windows visibility even if Qt already reports the widget visible.
    if(!IsWindowVisible(reinterpret_cast<HWND>(winId()))){hide();show();}
#endif
    raise();
}
void PetWidget::previewAction(PetAction action,int durationMs){
    manualAction_=action;manualUntil_=clock_.elapsed()+qBound(600,durationMs,300000);
    lastInteraction_=clock_.elapsed();animate();update();
}
void PetWidget::resumeAutomaticActions(){manualUntil_=0;lastInteraction_=clock_.elapsed();animate();update();}
void PetWidget::setRandomActionsEnabled(bool enabled){
    if(randomActions_==enabled)return;
    randomActions_=enabled;emit randomActionsChanged(enabled);animate();update();
}
void PetWidget::beginActionInteraction(PetAction action){interactionAction_=action;interactionActive_=true;lastInteraction_=clock_.elapsed();animate();update();}
void PetWidget::finishActionInteraction(){interactionActive_=false;speechUntil_=0;animate();update();}
PetAction PetWidget::automaticAction(qint64 now) const {
    if(dragging_ && moved_)return PetAction::Idle;
    if(capturePending_)return clickedAction_;
    if(interactionActive_)return interactionAction_;
    if(companion_.voice().state()==VoiceSession::State::Listening)return PetAction::Idle;
    if(manualUntil_>now)return manualAction_;
    if(companionMode_>=0)return companionMode_==0?PetAction::Work:PetAction::Comfort;
    if(companion_.voice().state()==VoiceSession::State::Speaking || now<speechUntil_)
        return companion_.emotions().state().emotion==Emotion::Relaxed?PetAction::Comfort:PetAction::Idle;
    if(companion_.busy() || companion_.voice().state()==VoiceSession::State::Thinking)return PetAction::Work;
    const auto mood=companion_.emotions().state().emotion;
    if(randomActions_ && companion_.growthIdleEnabled())return cycle_.current();
    if(mood!=Emotion::Calm)return EmotionEngine::preferredAction(mood);
    if(automaticRest_ && now-lastInteraction_>90000)return PetAction::Comfort;
    return PetAction::Idle;
}
void PetWidget::animate(){
    const auto now=clock_.elapsed();
    const double dt=qBound(0.0,double(now-lastFrame_)/1000.0,0.1);lastFrame_=now;
    const double smooth=1-std::exp(-dt*12);
    renderedMouth_+=(mouthOpen_-renderedMouth_)*smooth;
    cycle_.tick(now,companionMode_>=0 || !motion_ || !randomActions_ || !companion_.growthIdleEnabled() || capturePending_ || interactionActive_ || manualUntil_>now || companion_.busy() || companion_.voice().state()!=VoiceSession::State::Idle || now<speechUntil_,companion_.idleActions(companion_.emotions().state().emotion));
    if(!motion_){actions_.reset();blinkAmount_=0;blushAmount_=0;lean_=0;updateHeadPosition();return;}
    actions_.set(automaticAction(now),now);actions_.tick(now);
    if(now>=nextBlink_){blinkStarted_=now;nextBlink_=now+QRandomGenerator::global()->bounded(3200,6400);}
    const double blinkTime=double(now-blinkStarted_)/240;
    const double blink=blinkTime>=0 && blinkTime<1?std::sin(blinkTime*3.141592653589793):0;
    blinkAmount_=blink;
    const auto emotion=companion_.emotions().state().emotion;
    const double blush=(emotion==Emotion::Shy || emotion==Emotion::Happy || emotion==Emotion::Excited || emotion==Emotion::Tsundere)?companion_.emotions().state().intensity:0;
    blushAmount_+=(blush-blushAmount_)*smooth;
    double targetLean=actions_.target()==PetAction::Shy?-1.4:actions_.target()==PetAction::Jealous?1.1:0;
    if(dragging_ && moved_)targetLean=2.0;
    lean_+=(targetLean-lean_)*smooth;
    updateHeadPosition();
}
void PetWidget::updateHeadPosition(){const int y=qRound(characterGeometry().top());if(y!=lastHeadY_){lastHeadY_=y;emit headPositionChanged();}}
void PetWidget::setPetHeight(int height) {
    petHeight_=qBound(260,height,680);
    const double aspect=double(sprite_.width())/qMax(1,sprite_.height());
    setFixedSize(qRound(petHeight_*aspect)+26,petHeight_+116);
    if(waterActions_)waterActions_->setGeometry(8,92,width()-16,27);
    emit sizeChanged(petHeight_);
}
void PetWidget::setWaterActionsVisible(bool visible){
    waterActions_->setGeometry(8,92,width()-16,27);waterActions_->setVisible(visible);waterActions_->raise();
}
void PetWidget::say(const QString &text) {
    bubble_=text.left(100); bubbleUntil_=clock_.elapsed()+9000; update();
}
void PetWidget::setMotionEnabled(bool enabled) {
    if(motion_==enabled) return;
    motion_=enabled;animate();emit motionChanged(enabled);update();
}
void PetWidget::setTopmost(bool enabled) {
    if(topmost_==enabled) return;
    topmost_=enabled; setWindowFlag(Qt::WindowStaysOnTopHint,enabled); show(); emit topmostChanged(enabled);
}
QRectF PetWidget::characterGeometry() const {
    const QRectF source=halfView_?QRectF(sprite_.width()*.16,0,sprite_.width()*.70,sprite_.height()*.61):QRectF(sprite_.rect());
    const QRectF available(13,102,width()-26,petHeight_-16);
    const double scale=qMin(available.width()/source.width(),available.height()/source.height());
    double top=0;const auto weights=actions_.weights();for(int i=0;i<PetActions::Count;++i)top+=weights[i]*headTops_[i];
    const double height=source.height()*scale;
    const double visibleTop=available.bottom()-height+top*scale;
    return {available.center().x()-source.width()*scale/2,visibleTop,source.width()*scale,available.bottom()-visibleTop};
}
void PetWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    // One immutable master for every interaction. Cameras only crop this image;
    // body proportions, costume and silhouette are never morphed.
    const double time=clock_.elapsed()/1000.0;
    const double floatY=motion_?std::sin(time/1.5)*1.5:0.0;
    const QRectF source=halfView_
        ?QRectF(sprite_.width()*0.16,0,sprite_.width()*0.70,sprite_.height()*0.61)
        :QRectF(0,0,sprite_.width(),sprite_.height());
    const QRectF available(13,102+floatY,width()-26,petHeight_-16);
    const double scale=qMin(available.width()/source.width(),available.height()/source.height());
    const QRectF target(available.center().x()-source.width()*scale/2,
                        available.bottom()-source.height()*scale,
                        source.width()*scale,source.height()*scale);
    p.save();p.translate(target.center());p.rotate(lean_);p.translate(-target.center());
    // Additive premultiplied-alpha compositing keeps opacity stable while mixing
    // poses. Each new transition starts from this visible mixture, avoiding pops.
    const auto weights=actions_.weights();
    // Resample motion at the actual physical display resolution, rather than
    // warping the 1209 x 1300 master before reducing it for the screen.
    const double rasterScale=qMin(1.0,scale*devicePixelRatioF());
    const QSize rasterSize(qMax(1,qRound(sprite_.width()*rasterScale)),qMax(1,qRound(sprite_.height()*rasterScale)));
    const bool rasterChanged=renderSprite_.size()!=rasterSize;
    if(rasterChanged){
        renderSprite_=sprite_.scaled(rasterSize,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        renderFrames_.clear();
        for(const auto &frame:actionFrames_){
            const double ratio=qMin(1.0,qMin(double(rasterSize.width())/frame.width(),double(rasterSize.height())/frame.height()));
            renderFrames_.append(frame.scaled(qMax(1,qRound(frame.width()*ratio)),qMax(1,qRound(frame.height()*ratio)),Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32_Premultiplied));
        }
    }
    if(rasterChanged || blendedFrame_.isNull() || cachedWeights_!=weights || motion_ || std::abs(cachedMouth_-renderedMouth_)>.01 || std::abs(cachedBlink_-blinkAmount_)>.01 || std::abs(cachedBlush_-blushAmount_)>.01){
        blendedFrame_=QImage(rasterSize,QImage::Format_ARGB32_Premultiplied);blendedFrame_.fill(Qt::transparent);
        QPainter blend(&blendedFrame_);blend.setRenderHint(QPainter::SmoothPixmapTransform);blend.setCompositionMode(QPainter::CompositionMode_Plus);
        for(int i=0;i<PetActions::Count;++i){
            if(weights[i]<0.0001)continue;
            const QImage &frame=i==0 || !actionAtlasReady()?renderSprite_:renderFrames_[i-1];
            const double ratio=qMin(double(rasterSize.width())/frame.width(),double(rasterSize.height())/frame.height());
            const QSizeF size(frame.width()*ratio,frame.height()*ratio);
            QImage rendered(frame.size(),QImage::Format_ARGB32_Premultiplied);rendered.fill(Qt::transparent);
            QPainter local(&rendered);local.setRenderHint(QPainter::SmoothPixmapTransform);local.setRenderHint(QPainter::Antialiasing);
            SpriteMotion::paint(local,frame,static_cast<PetAction>(i),time,blinkAmount_,renderedMouth_,blushAmount_,motion_);local.end();
            blend.setOpacity(weights[i]);blend.drawImage(QRectF((rasterSize.width()-size.width())/2,rasterSize.height()-size.height(),size.width(),size.height()),rendered);
        }
        cachedWeights_=weights;
        cachedBlink_=blinkAmount_;cachedMouth_=renderedMouth_;cachedBlush_=blushAmount_;
    }
    p.setClipRect(target);p.translate(target.topLeft());p.scale(scale,scale);p.translate(-source.topLeft());
    p.drawImage(QRectF(sprite_.rect()),blendedFrame_);
    p.restore();
    const QRectF pill(width()/2.0-72,height()-24,144,22);
    p.setPen(QPen(QColor(176,158,211,180),1));
    p.setBrush(QColor(31,28,46,220)); p.drawRoundedRect(pill,11,11);
    p.setPen(QColor("#e4dcef")); p.setFont(QFont("Microsoft YaHei UI",9));
    const auto caption=interactionActive_?actionName(interactionAction_):companion_.voice().state()==VoiceSession::State::Listening?QString("正在听你说"):clock_.elapsed()<speechUntil_?QString("正在说话"):actionName(actions_.target());
    p.drawText(pill,Qt::AlignCenter,"霜翎 · "+caption);
    if(clock_.elapsed()<bubbleUntil_) {
        const QRectF bubble(10,6,width()-20,88);
        p.setPen(QPen(QColor(204,190,229,220),1)); p.setBrush(QColor(249,246,255,245));
        p.drawRoundedRect(bubble,16,16);
        p.setPen(QColor("#514260")); p.setFont(QFont("Microsoft YaHei UI",10));
        p.drawText(bubble.adjusted(16,10,-16,-10),Qt::AlignVCenter|Qt::TextWordWrap,bubble_);
    }
}
QImage PetWidget::renderMotionFrame(PetAction action,double seconds,double blink) const{
    const QImage &frame=action==PetAction::Idle || !actionAtlasReady()?sprite_:actionFrames_[int(action)-1];
    QImage result(QSize(360,388),QImage::Format_ARGB32_Premultiplied);result.fill(Qt::transparent);
    QPainter p(&result);p.setRenderHint(QPainter::SmoothPixmapTransform);p.setRenderHint(QPainter::Antialiasing);
    const double scale=qMin(double(result.width())/frame.width(),double(result.height())/frame.height());
    p.translate((result.width()-frame.width()*scale)/2,result.height()-frame.height()*scale);p.scale(scale,scale);
    SpriteMotion::paint(p,frame,action,seconds,blink,0,0,true);return result;
}
void PetWidget::mousePressEvent(QMouseEvent *event) {
    clickTimer_.stop();
    capturePending_=false;
    if(event->button()==Qt::LeftButton) {
        lastInteraction_=clock_.elapsed();clickedAction_=actions_.visible();capturePending_=true;
        dragging_=true; moved_=false; pressPosition_=event->globalPosition().toPoint();
        dragOffset_=pressPosition_-frameGeometry().topLeft(); setCursor(Qt::ClosedHandCursor);
    }
}
void PetWidget::mouseMoveEvent(QMouseEvent *event) {
    if(!dragging_) return;
    if((event->globalPosition().toPoint()-pressPosition_).manhattanLength()>QApplication::startDragDistance()) moved_=true;
    if(!moved_) return;
    capturePending_=false;
    lastClickMs_=-10000;
    clickTimer_.stop();
    auto position=event->globalPosition().toPoint()-dragOffset_;
    auto *screen=QGuiApplication::screenAt(event->globalPosition().toPoint());
    if(!screen) screen=QGuiApplication::primaryScreen();
    if(screen) {
        const auto bounds=screen->availableGeometry();
        position.setX(qBound(bounds.left(),position.x(),qMax(bounds.left(),bounds.right()-width()+1)));
        position.setY(qBound(bounds.top(),position.y(),qMax(bounds.top(),bounds.bottom()-height()+1)));
    }
    move(position);
}
void PetWidget::mouseReleaseEvent(QMouseEvent *event) {
    if(event->button()!=Qt::LeftButton) return;
    if(suppressRelease_){capturePending_=false;suppressRelease_=false;dragging_=false;setCursor(Qt::OpenHandCursor);event->accept();return;}
    if(dragging_ && !moved_) {
        const auto now=clock_.elapsed();
        const auto point=event->globalPosition().toPoint();
        if(now-lastClickMs_<=QApplication::doubleClickInterval() &&
           (point-lastClickPosition_).manhattanLength()<=QApplication::startDragDistance()){
            capturePending_=false;clickTimer_.stop();lastClickMs_=-10000;emit openCompanion();
        }else{
            lastClickMs_=now;lastClickPosition_=point;
            clickTimer_.start(QApplication::doubleClickInterval()+10);
        }
    }
    dragging_=false; setCursor(Qt::OpenHandCursor);
}
void PetWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    if(event->button()==Qt::LeftButton) {
        capturePending_=false;
        clickTimer_.stop();
        dragging_=false;moved_=false;suppressRelease_=true;lastClickMs_=-10000;
        setCursor(Qt::OpenHandCursor);event->accept();emit openCompanion();
    }
}
void PetWidget::contextMenuEvent(QContextMenuEvent *event) {
    capturePending_=false;
    clickTimer_.stop();lastClickMs_=-10000;
    QMenu menu;
    menu.addAction("和霜翎聊聊",this,[this]{emit openCompanion();});
    menu.addAction("识别原活动窗口…",this,[this]{QTimer::singleShot(0,this,[this]{emit screenRequested();});});
    menu.addAction("开始语音",this,[this]{emit voiceRequested();});
    auto *camera=menu.addMenu("镜头（同一 Q 版角色）");
    camera->addAction("全身",this,[this]{setHalfView(false);});
    camera->addAction("半身特写",this,[this]{setHalfView(true);});
    auto *sizeMenu=menu.addMenu("角色大小");
    for(const auto height:{320,480,600}) sizeMenu->addAction(QString::number(height)+" px",this,[this,height]{setPetHeight(height);});
    auto *top=menu.addAction("始终置顶"); top->setCheckable(true); top->setChecked(topmost_);
    connect(top,&QAction::toggled,this,&PetWidget::setTopmost);
    auto *actions=menu.addMenu("动作（原 Q 版形象）");
    for(int i=0;i<PetActions::Count;++i){
        const auto action=static_cast<PetAction>(i);
        auto *item=actions->addAction(actionName(action),this,[this,action]{setMotionEnabled(true);previewAction(action,action==PetAction::Sleep || action==PetAction::Work?60000:6000);});
        item->setCheckable(true);item->setChecked(currentAction()==action);
    }
    actions->addSeparator();actions->addAction("恢复自动动作",this,&PetWidget::resumeAutomaticActions);
    auto *random=actions->addAction("按当前心情自然切换动作（20–40 秒）");random->setCheckable(true);random->setChecked(randomActions_);
    connect(random,&QAction::toggled,this,&PetWidget::setRandomActionsEnabled);
    auto *rest=actions->addAction("关闭随机后，无互动 90 秒休息");rest->setCheckable(true);rest->setChecked(automaticRest_);
    connect(rest,&QAction::toggled,this,&PetWidget::setAutomaticRest);
    auto *motion=menu.addAction("启用动作和呼吸动画"); motion->setCheckable(true); motion->setChecked(motion_);
    connect(motion,&QAction::toggled,this,&PetWidget::setMotionEnabled);
    menu.addSeparator();
    menu.addAction("退出 DragonGPT",this,[this]{emit quitRequested();});
    menu.exec(event->globalPos());
}
}
