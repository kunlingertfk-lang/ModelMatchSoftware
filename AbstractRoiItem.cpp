#include "AbstractRoiItem.h"
#include <QtMath>
#include <QCursor>

/**
 * @brief 构造函数
 */
AbstractRoiItem::AbstractRoiItem(QGraphicsItem* parent)
    : QGraphicsObject(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptHoverEvents(true);

    // ROI 默认可选中
    setFlag(QGraphicsItem::ItemIsSelectable, true);
}

/**
 * @brief 锁定 ROI（右键确认）
 */
void AbstractRoiItem::lock()
{
    m_locked = true;
    update();
}

/**
 * @brief 鼠标按下
 */
void AbstractRoiItem::mousePressEvent(QGraphicsSceneMouseEvent* e)
{
    if (e->button() == Qt::RightButton) {
        lock();                 // 右键 = 确认
        e->accept();
        return;
    }

    m_activeHandle = hitTest(e->pos());   // 交给子类判断命中哪个 Handle
    if(m_locked){
        if(m_activeHandle != HandleType::Move){
            e->ignore();
            return;
        }
    }

    //如果没点中任何手柄（None），则不强行 accept
    // 这样用户点击 ROI 内部空白处时，依然能触发底层的“选中/移动”逻辑
    if (m_activeHandle != HandleType::None) {
        m_lastScenePos = e->scenePos(); // 记录起始点
        e->accept();
    } else {
        QGraphicsItem::mousePressEvent(e);
    }
}

/**
 * @brief 鼠标移动（核心交互）
 */
void AbstractRoiItem::mouseMoveEvent(QGraphicsSceneMouseEvent* e)
{
    // 1. 逻辑拦截：如果没有激活手柄，或者处于锁定状态且不是移动操作，直接放行/返回
    if(m_activeHandle == HandleType::None){
        QGraphicsItem::mouseMoveEvent(e);
        return;
    }

    // 1. 如果锁定了，或者没有激活手柄，交给父类处理默认行为（如选中）
    if (m_locked && m_activeHandle != HandleType::Move) {
        // 锁定时拦截 旋转/缩放，但不拦截移动
        return;
    }

    // 2. 计算场景增量


    switch (m_activeHandle) {
    case HandleType::Move:{ // 直接使用场景位移
            QPointF deltaScene = e->scenePos() - m_lastScenePos;
            moveBy(deltaScene.x(), deltaScene.y());
            break;
        }
    case HandleType::Rotate: {
        // 建议：使用 QLineF 的角度计算更稳健
        // 旋转中心取变换原点（通常是 0,0）
        QPointF centerInScene = mapToScene(transformOriginPoint());
        QLineF lastLine(centerInScene, e->lastScenePos());
        QLineF currentLine(centerInScene, e->scenePos());

        qreal da = lastLine.angle() - currentLine.angle(); // 顺逆时针处理
        // 处理跨越 0/360 度边界的情况
        if(da > 180) da -= 360;
        else if(da < -180) da += 360;

        this->setRotation(this->rotation() + da);
        break;
    }
    case HandleType::Scale: {
        // 【核心修复】映射到局部坐标的增量
        // 映射 e->scenePos() 和 m_lastScenePos 到局部，再求差
        // 这样 applyScale 就不需要考虑旋转角度
        QPointF localPos1  = mapFromScene(e->lastScenePos());
        QPointF localPos2 = mapFromScene(e->scenePos());
        applyScale(localPos2 -localPos1);
        break;
    }
    default:
        break;
    }

    m_lastScenePos = e->scenePos();
    //update(); // 强制刷新界面 moveBy 和 setRotation 会自动触发 update，Scale 内部调了 prepareGeometryChange 也会触发，这里可以省略
    e->accept(); // 标记事件已处理
}

/**
 * @brief 鼠标释放
 */
void AbstractRoiItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* e)
{
    // 如果刚才处于移动模式，说明拖动完成了
    if(m_activeHandle == HandleType::Move){
        emit roiMove();//触发信号
    }

    m_activeHandle = HandleType::None;
    unsetCursor(); // 恢复正常光标
    QGraphicsItem::mouseReleaseEvent(e);
}



QColor AbstractRoiItem::getPreferredColor() const{
    if (m_locked && m_operation == RoiOperation::Add && !isSelected()) return QColor(0, 0, 128); // 锁定
    else if(m_locked && m_operation == RoiOperation::Subtract && !isSelected())  return QColor(55, 55, 0); // 锁定
    else if(m_locked && isSelected()) return Qt::red;

    // 如果正在被操作（有激活手柄），给个高亮色
    if (m_activeHandle != HandleType::None) return Qt::red;

    if (isSelected()) return Qt::red;

    // 区分正负 ROI 颜色
    return (m_operation == RoiOperation::Add) ? Qt::cyan : Qt::yellow;
}

Qt::PenStyle AbstractRoiItem::getPreferredPenStyle() const {
    // Subtract 模式下使用虚线，视觉上更有“扣除”感
    if (m_operation == RoiOperation::Subtract) {
        return Qt::CustomDashLine; // 或者 Qt::DashLine
    }
    return Qt::SolidLine;
}


QVariant AbstractRoiItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemPositionChange || change == ItemRotationChange) {
        // 可以在这里实时发送信号，比如通知 Halcon 重新计算区域
    }
    return QGraphicsItem::itemChange(change, value);
}


AbstractRoiItem::HandleType AbstractRoiItem::activeHandle() const{
    return m_activeHandle;
}












