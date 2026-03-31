#ifndef CIRCLEROIITEM_H
#define CIRCLEROIITEM_H

#include "AbstractRoiItem.h"


namespace{
class HObject;
};


class CircleRoiItem : public AbstractRoiItem
{
    Q_OBJECT


public:
    enum{Type = UserType + 1003};

    int type() const override { return Type; }


    explicit CircleRoiItem(const QRectF& rect,QGraphicsItem* parent = nullptr);
    // ===== 几何与绘制重写 =====
    QRectF boundingRect() const override;

    void paint(QPainter* p,const QStyleOptionGraphicsItem* option,QWidget* widget) override;

    void applyScale(const QPointF& localDelta) override;

    HandleType hitTest(const QPointF& localPos) const override;

    HalconCpp::HObject toHalconRegion() const override;

    QRectF localRect() const override { return QRectF(-m_radius, -m_radius, m_radius*2, m_radius*2); }

    QPainterPath shapePath() const override;
    // // ===== 悬停反馈（光标切换） =====
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

    // // ===== 鼠标点击（视觉状态切换） =====
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    qreal m_radius; //半径


};

#endif // CIRCLEROIITEM_H
