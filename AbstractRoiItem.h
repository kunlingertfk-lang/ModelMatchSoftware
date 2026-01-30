#ifndef ABSTRACTROIITEM_H
#define ABSTRACTROIITEM_H

#include <QGraphicsObject>
#include <QGraphicsSceneMouseEvent>
#include <QPainterPath>

namespace HalconCpp {
class HObject;
}

class AbstractRoiItem : public QGraphicsObject
{
    Q_OBJECT
public:
    enum class RoiType {
        None,               //无
        Rect,//平行矩形
        RotRect, //旋转矩形
        Circle,//圆形
        Ellipse, //椭圆
        Custom//自定义
    };

    enum class HandleType { //当前手柄状态
        None,
        Move,
        Rotate,
        Scale
    };

    enum class RoiOperation{
        Add,        // 包含区域 (对应 Halcon 的正 ROI)
        Subtract    // 剔除区域 (对应 Halcon 的负 ROI)
    };


    // Qt 内置类型识别
    enum { Type = UserType + 1001 };
    int type() const override { return Type; }

    explicit AbstractRoiItem(QGraphicsItem* parent = nullptr);
    virtual ~AbstractRoiItem() = default;

    // ===== 状态 =====
    void lock();
    bool isLocked() const { return m_locked; }
    RoiType roiType() const { return m_roiType; }

    void setPositive(bool pos){
        m_isPositive = pos;
        update(); // 属性改变后触发重绘，颜色会变
    }

    bool isPositive() const { return m_isPositive; }

    //==== 绘图与几何接口 ===== =====
    virtual QPainterPath shapePath() const = 0;     //用于碰撞检测
    virtual QRectF localRect() const = 0;          // 纯几何矩形
    virtual HalconCpp::HObject toHalconRegion() const = 0;

    // 手动设置当前激活的手柄（用于创建过程）// ===== 外部强制交互接口 =====
    void setActiveHandle(HandleType handle) { m_activeHandle = handle; }

    // 还需要一个方法来初始化起始位置，否则第一次拖动会跳变
    void setLastScenePos(const QPointF& pos) { m_lastScenePos = pos; }
    QPointF lastScenePos() const { return m_lastScenePos; }

    void setOperation(RoiOperation op){
        m_operation = op;
        m_isPositive = (op == RoiOperation::Add);
        update();
    }
    RoiOperation operation() const { return m_operation; }

    /**
     * @brief 缩放实现
     * @param localDelta 已经在局部坐标系下转换好的位移增量
     */
    virtual void applyScale(const QPointF& delta) = 0;

    HandleType activeHandle() const;

protected:
    // ===== Qt统一的事件分发 =====
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;

    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

    // ===== 子类必须实现的交互逻辑 =====
    virtual HandleType hitTest(const QPointF& localPos) const = 0;


    QColor getPreferredColor() const;
    Qt::PenStyle getPreferredPenStyle() const;



protected:
    RoiType      m_roiType  = RoiType::None;
    RoiOperation m_operation = RoiOperation::Add;
    HandleType   m_activeHandle = HandleType::None;
    bool         m_locked = false;
    bool         m_isPositive = true; // 默认为正（包含）
    QPointF      m_lastScenePos;

    // 共享的 UI 常量（保护成员，子类 paint 时使用）
    const qreal m_handleRadius = 6.0;
    const qreal m_rotateOffset = 30.0;

signals:
    void roiMove();//roi位置改变信号


};

#endif // ABSTRACTROIITEM_H
