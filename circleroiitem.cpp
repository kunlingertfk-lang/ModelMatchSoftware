#include "circleroiitem.h"

#include <HalconCpp.h>
#include <QPainter>
#include <QtMath>
#include <QCursor>
#include <QGraphicsSceneMouseEvent>
#include <QStyleOptionGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QDebug>


CircleRoiItem::CircleRoiItem(const QRectF& rect,QGraphicsItem* parent) : AbstractRoiItem(parent)
{
    m_roiType = RoiType::Circle;

    m_radius = qMax(rect.width(),rect.height()) / 2.0;

    if(m_radius < 5) m_radius = 5;
    setPos(rect.center());
}


QRectF CircleRoiItem::boundingRect() const {
    qreal s = 1.0 / getLod();
    return QRectF(-m_radius,-m_radius,m_radius*2,m_radius*2).adjusted(-10*s,-10*s,10*s,10*s);
}

AbstractRoiItem::HandleType CircleRoiItem::hitTest(const QPointF& localPos) const{
    qreal s = 1.0 / getLod();
    qreal dist = QLineF(QPointF(0,0), localPos).length();

    // 1. 边缘缩放优先级最高（误差范围约 8 像素）
    if (qAbs(dist - m_radius) < 8 * s) {
        return HandleType::Scale;
    }

    // 2. 只要在圆内部，都可以移动（增加点击面积）
    if (dist < m_radius) {
        return HandleType::Move;
    }

    return HandleType::None;
}


void CircleRoiItem::applyScale(const QPointF& localDelta) {
    prepareGeometryChange();

    // 计算鼠标当前相对于圆心的局部坐标
    // 这种做法比依赖 delta 更直观：半径就是中心到鼠标点的距离
    QPointF currentLocalPos = mapFromScene(m_lastScenePos);

    m_radius = QLineF(QPointF(0,0), currentLocalPos).length();

    // 限制最小半径，防止消失
    if (m_radius < 2.0) m_radius = 2.0;
}

void CircleRoiItem::paint(QPainter* p,
                          const QStyleOptionGraphicsItem* option,
                          QWidget*) {
    // 1. 获取缩放比例
    qreal lod = option->levelOfDetailFromTransform(p->worldTransform());
    qreal s = 1.0 / lod;

    p->setRenderHint(QPainter::Antialiasing);

    // 2. 配置画笔 (核心修复点)
    QColor color = getPreferredColor();
    Qt::PenStyle style = getPreferredPenStyle();

    // 如果是 Subtract 模式，建议使用自定义虚线，防止在圆很小时线段消失
    QPen pen;
    pen.setColor(color);
    pen.setWidthF(2.0); // 使用宽度为 2 的细线
    pen.setCosmetic(true); // 关键：保证缩放时粗细不变

    if (style == Qt::DashLine) {
        // 自定义虚线模式：[线段长度, 间隔长度]
        // 这样可以确保即使圆很小，也能看到明显的红虚线
        QVector<qreal> dashes;
        dashes << 5 << 5;
        pen.setDashPattern(dashes);
    } else {
        pen.setStyle(Qt::SolidLine);
    }

    p->setPen(pen);
    p->setBrush(Qt::NoBrush); // 确保内部不填充

    // 3. 绘制圆形本体 (改用 QRectF 绘图更稳健)
    QRectF circleRect(-m_radius, -m_radius, m_radius * 2, m_radius * 2);
    p->drawEllipse(circleRect);

    // 4. 绘制交互手柄
    if (!isLocked()) {
        // 绘制中心十字
        p->setPen(QPen(Qt::green, 1, Qt::SolidLine));
        pen.setCosmetic(true);
        qreal cs = 8 * s;
        p->drawLine(QPointF(-cs, 0), QPointF(cs, 0));
        p->drawLine(QPointF(0, -cs), QPointF(0, cs));

        // 绘制右侧缩放手柄点
        p->setPen(Qt::NoPen);
        p->setBrush(color);
        qreal handleR = 5 * s;
        p->drawEllipse(QPointF(m_radius, 0), handleR, handleR);
    }
}

HalconCpp::HObject CircleRoiItem::toHalconRegion() const {
    HalconCpp::HObject ho;
    HalconCpp::GenCircle(&ho, scenePos().y(), scenePos().x(), m_radius);
    return ho;
}


QPainterPath CircleRoiItem::shapePath() const {
    QPainterPath path;
    qreal r = m_radius;
    path.addEllipse(-r,-r,2*r,2*r);
    return path;
}


void CircleRoiItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    AbstractRoiItem::mousePressEvent(event); // 必须调用基类逻辑
    if (m_activeHandle != HandleType::None) {
        setCursor(Qt::ClosedHandCursor); // 抓取感
    }
}
void CircleRoiItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if(m_activeHandle == HandleType::Move)
        emit roiMove();

    AbstractRoiItem::mouseReleaseEvent(event);
    unsetCursor(); // 恢复悬停状态的光标
}


void CircleRoiItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event) {
    if (isLocked()) {
        AbstractRoiItem::hoverMoveEvent(event);
        return;
    }

    // 利用子类已实现的 hitTest 逻辑
    HandleType handle = hitTest(event->pos());

    if (handle == HandleType::Scale) {
        // 移到圆周边缘，提示可以缩放
        setCursor(Qt::SizeAllCursor);
    } else if (handle == HandleType::Move) {
        // 移到圆心，提示可以平移
        setCursor(Qt::OpenHandCursor);
    } else {
        unsetCursor();
    }

    AbstractRoiItem::hoverMoveEvent(event);
}


void CircleRoiItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    unsetCursor();
    AbstractRoiItem::hoverLeaveEvent(event);
}
























