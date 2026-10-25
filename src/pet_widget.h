#pragma once
#include "companion.h"
#include "pet_actions.h"
#include <QElapsedTimer>
#include <QImage>
#include <QTimer>
#include <QWidget>

namespace dragon {
class PetWidget final : public QWidget {
    Q_OBJECT
public:
    explicit PetWidget(Companion &companion);
    void say(const QString &text);
    void showOnDesktop();
    void setCompanionMode(int mode){if(companionMode_==mode)return;companionMode_=mode;animate();update();}
    void setWaterActionsVisible(bool visible);
    void setPetHeight(int height);
    bool motionEnabled() const { return motion_; }
    bool topmost() const { return topmost_; }
    void setMotionEnabled(bool enabled);
    void setTopmost(bool enabled);
    void setMouthOpen(double value){mouthOpen_=qBound(0.0,value,1.0);if(value>0.08){speechUntil_=clock_.elapsed()+300;if(!interactionActive_ && manualAction_==PetAction::Sleep)manualUntil_=0;}update();}
    bool halfView() const{return halfView_;}
    void setHalfView(bool enabled){if(halfView_==enabled)return;halfView_=enabled;emit halfViewChanged(enabled);update();}
    QString avatarIdentity() const{return "shuangling-chibi-v1";}
    void previewAction(PetAction action,int durationMs=6000);
    void resumeAutomaticActions();
    PetAction currentAction() const {return actions_.target();}
    bool actionAtlasReady() const {return actionFrames_.size()==6;}
    void setAutomaticRest(bool enabled){automaticRest_=enabled;lastInteraction_=clock_.elapsed();}
    bool automaticRest() const{return automaticRest_;}
    bool randomActionsEnabled() const{return randomActions_;}
    void setRandomActionsEnabled(bool enabled);
    void beginActionInteraction(PetAction action);
    void finishActionInteraction();
    bool actionInteractionActive() const{return interactionActive_;}
    bool bubbleActive() const{return clock_.elapsed()<bubbleUntil_;}
    bool protectedActionActive() const{return interactionActive_ || capturePending_ || dragging_ || (manualUntil_>clock_.elapsed() && manualAction_==PetAction::Sleep);}
    QImage renderMotionFrame(PetAction action,double seconds,double blink=0) const;
    QRectF characterGeometry() const;
signals:
    void headPositionChanged();
    void waterConfirmed();
    void waterSnoozed();
    void waterMutedToday();
    void openCompanion();
    void quitRequested();
    void sizeChanged(int height);
    void motionChanged(bool enabled);
    void topmostChanged(bool enabled);
    void halfViewChanged(bool enabled);
    void voiceRequested();
    void clicked();
    void actionClicked(dragon::PetAction action);
    void randomActionsChanged(bool enabled);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
private:
    int companionMode_=-1;
    QWidget *waterActions_=nullptr;
    Companion &companion_;
    QImage sprite_;
    QVector<QImage> actionFrames_;
    std::array<double,PetActions::Count> headTops_{};
    int lastHeadY_=-10000;
    void updateHeadPosition();
    PetActions actions_;
    ActionCycle cycle_;
    bool randomActions_=true,capturePending_=false,interactionActive_=false;
    PetAction clickedAction_=PetAction::Idle,interactionAction_=PetAction::Idle;
    QImage blendedFrame_;
    QImage renderSprite_;
    QVector<QImage> renderFrames_;
    PetActions::Weights cachedWeights_{{-1,0,0,0,0,0,0}};
    double cachedBlink_=-1,cachedMouth_=-1,cachedBlush_=-1;
    qint64 manualUntil_=0,lastInteraction_=0,speechUntil_=0;
    PetAction manualAction_=PetAction::Idle;
    bool automaticRest_=true;
    double renderedMouth_=0,blinkAmount_=0,blushAmount_=0,lean_=0;
    qint64 lastFrame_=0,nextBlink_=4100,blinkStarted_=-1000;
    void animate();
    PetAction automaticAction(qint64 now) const;
    QElapsedTimer clock_;
    QTimer timer_;
    QTimer clickTimer_;
    QString bubble_;
    qint64 bubbleUntil_ = 0;
    QPoint dragOffset_, pressPosition_;
    bool dragging_ = false, moved_ = false, motion_ = true, topmost_ = true;
    int petHeight_ = 480;
    double mouthOpen_=0;
    bool halfView_=false;
    qint64 lastClickMs_=-10000;
    QPoint lastClickPosition_;
    bool suppressRelease_=false;
};
}
