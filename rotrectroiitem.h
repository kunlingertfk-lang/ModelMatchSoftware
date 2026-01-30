#ifndef ROTRECTROIITEM_H
#define ROTRECTROIITEM_H

#include "AbstractRoiItem.h"
#include <QRectF>

namespace HalconCpp{
    class HObject;
}

class RotRectRoiItem : public AbstractRoiItem
{
public:

    // 增加 Qt 类型识别 ID
    enum { Type = UserType + 1001 };
    int type() const override { return Type; }

    explicit RotRectRoiItem(const QRectF& rect, QGraphicsItem* parent = nullptr);

    // ===== 几何与绘制重写 =====
    QRectF boundingRect() const override;
    QPainterPath shapePath() const override;// 用于精准点击测试
    QRectF localRect() const override;

    void paint(QPainter* painter,
               const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

    // ===== Halcon 转换 =====
    HalconCpp::HObject toHalconRegion() const override;

public:
    // ===== 核心交互逻辑实现 =====
    HandleType hitTest(const QPointF& localPos) const override;

    /**
     * @brief 实现对称缩放
     * @param localDelta 基类已经转换好的局部坐标系下的位移向量
     */
    void applyScale(const QPointF& localDelta) override;

    // ===== 悬停反馈（光标切换） =====
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

    // ===== 鼠标点击（视觉状态切换） =====
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;



private:
    // 计算手柄位置的辅助函数
    QRectF rotateHandleRect() const;
    QRectF moveHandleRect() const;
    QRectF scaleHandleRect() const; // 建议增加专门的缩放感应区计算

    qreal getCurrentLod() const;

private:
    QRectF m_rect;// 存储以 (0,0) 为中心的矩形：QRectF(-w/2, -h/2, w, h)
    //m_handleRadius 和 m_rotateOffset 已经在基类定义为 protected，直接使用
};

#endif // ROTRECTROIITEM_H
