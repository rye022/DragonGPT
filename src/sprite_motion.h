#pragma once
#include "pet_actions.h"
#include <QImage>
#include <QPainter>

namespace dragon {
// Local appendage motion on the existing authored image; no character generation.
class SpriteMotion {
public:
    static QPointF offset(QPointF normalized,PetAction pose,double seconds);
    static void paint(QPainter &p,const QImage &image,PetAction pose,double seconds,
                      double blink,double mouth,double blush,bool moving);
};
}
