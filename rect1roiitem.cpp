#include "rect1roiitem.h"

#include <HalconCpp.h>
#include <QPainter>
#include <QtMath>
#include <QCursor>
#include <QGraphicsSceneMouseEvent>
#include <QStyleOptionGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QDebug>

Rect1RoiItem::Rect1RoiItem(const QRectF& rect,QGraphicsItem* parent )
    : AbstractRoiItem(parent)
{
    m_roiType = RoiType::Rect;
    qreal w = qMax(2.0,rect.width());
    qreal h = qMax(2.0,rect.height());
    m_rect = QRectF(-w/2,-h/2,w,h);

    setPos(rect.center());
    setRotation(0);//强制不旋转
    setFlag(QGraphicsItem::ItemIsMovable,false);//禁用默认移动，因为基类手动处理了 moveBy
}


QRectF Rect1RoiItem::boundingRect() const {
    qreal s = 1.0 / getLod();
    // 增加 padding 到 20，确保手柄和加粗的边框都能包含进去
    return m_rect.adjusted(-20*s, -20*s, 20*s, 20*s);
}

AbstractRoiItem::HandleType Rect1RoiItem::hitTest(const QPointF& localPos) const{
    qreal s = 1.0 / getLod();
    qreal moveSize = 9.0 * s;

    // 1. 中心移动判定
    if(isLocked()){
        return m_rect.contains(localPos) ? HandleType::Move : HandleType::None;
    }

    //在矩形范围内可以移动
    if ((localPos.x() > m_rect.left() + moveSize) &&
        (localPos.x() < m_rect.right() - moveSize)&&
        (localPos.y() > m_rect.top() + moveSize) &&
        (localPos.y() < m_rect.bottom() - moveSize))
        return HandleType::Move;

    // 2. 四条边缩放判定
    qreal margin = 8.0 * s;
    bool onLeft = qAbs(localPos.x() - m_rect.left()) < margin;
    bool onRight = qAbs(localPos.x() - m_rect.right()) < margin;
    bool onTop = qAbs(localPos.y() - m_rect.top()) < margin;
    bool onBottom = qAbs(localPos.y() - m_rect.bottom()) < margin;

    if (onLeft || onRight || onTop || onBottom) {
        // 更新缩放方向状态（为了给 applyScale 使用）
        auto* nonConstThis = const_cast<Rect1RoiItem*>(this);
        if (onLeft || onRight) nonConstThis->m_currentScaleDir = Hor;//水平
        else nonConstThis->m_currentScaleDir = Ver;//垂直

        return HandleType::Scale;
    }

    return HandleType::None;
}


void Rect1RoiItem::applyScale(const QPointF& localDelta){
    prepareGeometryChange();

    // 获取当前鼠标相对于中心的位置
    QPointF pressPos = mapFromScene(m_lastScenePos);

    // --- 修复逻辑：处理创建阶段或未命中边缘时的默认缩放 ---
    if (m_currentScaleDir == None) {
        // 如果是刚创建或从中心拉伸，同时改变宽和高
        qreal dx = qAbs(localDelta.x());
        qreal dy = qAbs(localDelta.y());

        // 注意：如果是创建阶段，应该直接根据鼠标位置确定大小
        // 这里假设 localDelta 是相对于点击中心的偏移
        qreal newHW = qMax(2.0, m_rect.width()/2 + dx);
        qreal newHH = qMax(2.0, m_rect.height()/2 + dy);

        m_rect = QRectF(-newHW, -newHH, newHW * 2, newHH * 2);
        return;
    }

    // --- 正常的边缘拉伸逻辑 ---
    if (m_currentScaleDir == Hor) {
        // 左右拉伸
        qreal dx = (pressPos.x() < 0) ? -localDelta.x() : localDelta.x();
        qreal newHW = qMax(5.0, m_rect.width() / 2 + dx);
        m_rect.setLeft(-newHW);
        m_rect.setRight(newHW);
    }
    else if (m_currentScaleDir == Ver) {
        // 上下拉伸
        qreal dy = (pressPos.y() < 0) ? -localDelta.y() : localDelta.y();
        qreal newHH = qMax(5.0, m_rect.height() / 2 + dy);
        m_rect.setTop(-newHH);
        m_rect.setBottom(newHH);
    }
}


void Rect1RoiItem::paint(QPainter* p,
                          const QStyleOptionGraphicsItem* option,
                          QWidget*){

    qreal lod = option->levelOfDetailFromTransform(p->worldTransform());
    qreal s = 1.0 / lod;

    p->setRenderHint(QPainter::Antialiasing);

    QPen pen(getPreferredColor(), 2, getPreferredPenStyle());
    pen.setCosmetic(true);
    p->setPen(pen);
    p->drawRect(m_rect);

    if (!isLocked()) {
        // 1. 绘制中心十字
        p->setPen(QPen(Qt::green, 1));
        pen.setCosmetic(true);
        qreal cs = 8 * s;
        p->drawLine(QPointF(-cs, 0), QPointF(cs, 0));
        p->drawLine(QPointF(0, -cs), QPointF(0, cs));

        // 2. 绘制边框中点手柄 (增加视觉引导)
        p->setPen(Qt::NoPen);
        p->setBrush(getPreferredColor());
        qreal hs = 4 * s; // 手柄大小
        p->drawRect(QRectF(m_rect.right() - hs, -hs, hs * 2, hs * 2)); // 右中
        p->drawRect(QRectF(m_rect.left() - hs, -hs, hs * 2, hs * 2));  // 左中
        p->drawRect(QRectF(-hs, m_rect.top() - hs, hs * 2, hs * 2));   // 上中
        p->drawRect(QRectF(-hs, m_rect.bottom() - hs, hs * 2, hs * 2));// 下中
    }

}

HalconCpp::HObject Rect1RoiItem::toHalconRegion() const {
    HalconCpp::HObject ho;
    // 获取场景中的左上角和右下角
    QPointF tl = mapToScene(m_rect.topLeft());
    QPointF br = mapToScene(m_rect.bottomRight());
    HalconCpp::GenRectangle1(&ho, tl.y(), tl.x(), br.y(), br.x());
    return ho;
}

QPainterPath Rect1RoiItem::shapePath() const {
    QPainterPath path;
    path.addRect(m_rect);
    return path;
}

void Rect1RoiItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    AbstractRoiItem::mousePressEvent(event); // 必须调用基类逻辑
    if (m_activeHandle != HandleType::None) {
        setCursor(Qt::ClosedHandCursor); // 抓取感
    }
}

void Rect1RoiItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if(m_activeHandle == HandleType::Move)
        emit roiMove();

    // --- 增加：释放后重置方向状态 ---
    m_currentScaleDir = None;

    AbstractRoiItem::mouseReleaseEvent(event);
    unsetCursor();
}


void Rect1RoiItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event) {
    if (isLocked()) {
        AbstractRoiItem::hoverMoveEvent(event);
        return;
    }

    HandleType handle = hitTest(event->pos());
    if (handle == HandleType::Scale) {
        // 更加精细的光标反馈
        if (m_currentScaleDir == Hor) setCursor(Qt::SizeHorCursor);
        else setCursor(Qt::SizeVerCursor);
    }
    else if (handle == HandleType::Move) {
        setCursor(Qt::OpenHandCursor);
    }
    else {
        unsetCursor();
    }
    AbstractRoiItem::hoverMoveEvent(event);
}


void Rect1RoiItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    unsetCursor();
    AbstractRoiItem::hoverLeaveEvent(event);
}

























