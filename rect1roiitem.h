#ifndef RECT1ROIITEM_H
#define RECT1ROIITEM_H

#include "AbstractRoiItem.h"

class Rect1RoiItem : public AbstractRoiItem
{
    Q_OBJECT

public:
    enum{Type = UserType + 1002};

    int type() const override{ return Type; };

    explicit Rect1RoiItem(const QRectF& rect,QGraphicsItem* parent = nullptr);

    // ===== 几何与绘制重写 =====
    QRectF boundingRect() const override;

    void paint(QPainter* p,const QStyleOptionGraphicsItem* option,QWidget* widget) override;

    void applyScale(const QPointF& localDelta) override;

    HandleType hitTest(const QPointF& localPos) const override;

    HalconCpp::HObject toHalconRegion() const override;

    QRectF localRect() const override {return QRectF();}

    QPainterPath shapePath() const override;


    // // ===== 悬停反馈（光标切换） =====
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

    // // ===== 鼠标点击（视觉状态切换） =====
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    QRectF m_rect; //平行矩形

    enum ScaleDir { None, Hor, Ver, Both };
    ScaleDir m_currentScaleDir = None;
};

#endif // RECT1ROIITEM_H
