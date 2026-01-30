#include "RotRectRoiItem.h"
#include <QPainter>
#include <QtMath>
#include <QCursor>
#include <QGraphicsSceneMouseEvent>
#include <QStyleOptionGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QDebug>

#include <HalconCpp.h>

using namespace HalconCpp;

RotRectRoiItem::RotRectRoiItem(const QRectF& rect, QGraphicsItem* parent)
    : AbstractRoiItem(parent)
{
    m_roiType = RoiType::RotRect;

    // 将传入的 rect 转换为以 (0,0) 为中心的矩形
    qreal w = rect.width();
    qreal h = rect.height();
    m_rect = QRectF(-w/2,-h/2,w,h);

    // 设置 Item 在场景中的位置为原矩形的中心
    setPos(rect.center());

    // 以中心为旋转基准
    setTransformOriginPoint(0,0);
    setAcceptHoverEvents(true); //启用扫描事件
    setFlag(QGraphicsItem::ItemIsMovable, false); // 禁用默认移动，因为基类手动处理了 moveBy
}

QRectF RotRectRoiItem::boundingRect() const
{
    qreal s = 1.0 / getCurrentLod();
    // qDebug() << "缩放大小:" << s;
    // 预留足够的空间，考虑缩放后的手柄长度
    qreal pad = (m_rotateOffset + m_handleRadius + 20) * s;
    return m_rect.adjusted(-pad, -pad, pad, pad); // 包含旋转箭头
}

QPainterPath RotRectRoiItem::shapePath() const
{
    QPainterPath path;
    path.addRect(m_rect);
    return path;
}

QRectF RotRectRoiItem::localRect() const
{
    return m_rect;
}

//画ROI和中心十字，箭头
void RotRectRoiItem::paint(QPainter* p,
                           const QStyleOptionGraphicsItem* option,
                           QWidget*)
{
    p->setRenderHint(QPainter::Antialiasing);

    //获取当前缩放比例（LOD：Level of Detail）
    //1.0表示原图，2.0表示放大一倍，0.5表示缩小一倍
    qreal lod = option->levelOfDetailFromTransform(p->worldTransform());

    // 使用基类提供的统一颜色和线型处理线条
    QPen mainPen(getPreferredColor(), 2, getPreferredPenStyle());
    mainPen.setCosmetic(true); // 关键：线条宽度不随缩放改变

    p->setPen(mainPen);
    p->drawRect(m_rect);
    // 如果被锁定，变灰色
    qreal s = 1.0 / lod;
    if (!isLocked()) {
        QPen crossPen(Qt::green, 1, Qt::SolidLine, Qt::SquareCap, Qt::BevelJoin);
        crossPen.setCosmetic(true);
        p->setPen(crossPen);

        // 绘制中心十字（Move标识）
        qreal crossSize = 10 * s;
        p->drawLine(QPointF(-crossSize, 0), QPointF(crossSize, 0));
        p->drawLine(QPointF(0, -crossSize), QPointF(0, crossSize));

        // 绘制旋转手柄连线
        p->setPen(QPen(getPreferredColor(), 1, Qt::DashLine));

        int x1 = m_rect.right(),y1 = m_rect.top()+(m_rect.bottom()-m_rect.top())/2;
        int x2 = m_rect.right() + m_rotateOffset*s, y2 = y1;
        p->drawLine(x1, y1, x2, y2);

        // 绘制旋转手柄圆球
        p->setPen(Qt::NoPen);
        p->setBrush(getPreferredColor());
        p->drawEllipse(rotateHandleRect());

        // 绘制边缘缩放提示（可选，增加视觉引导）
        p->setPen(QPen(Qt::gray, 1, Qt::DotLine));
        // ... 原有的缩放短线绘制 ...
    }
    else{
        qreal crossSize = 10 * s;
        p->drawLine(QPointF(-crossSize, 0), QPointF(crossSize, 0));
        p->drawLine(QPointF(0, -crossSize), QPointF(0, crossSize));
    }
}

//返回操作类型 HitTest 区域划分
AbstractRoiItem::HandleType
RotRectRoiItem::hitTest(const QPointF& pos) const
{
    //获取缩放因子
    qreal s =1.0/ getCurrentLod(); // 1.0/lod

    // 优先级 2: 中心移动区域
    qreal moveSize = 9.0 * s;

    if(isLocked()){
        return m_rect.contains(pos) ? HandleType::Move : HandleType::None;
    }


    // 优先级 1: 旋转手柄 (最重要)
    qreal r = m_handleRadius * s;
    qreal offset = m_rotateOffset * s;
    QPointF rotateCenter(m_rect.right() + offset,m_rect.center().y());
    // 增加一点点击宽容度 (比如 +5 像素)
    if (QLineF(pos,rotateCenter).length() < r+5.0*s)
        return HandleType::Rotate;


    // if (QRectF(-moveSize,-moveSize,moveSize*2,moveSize*2).contains(pos))
    //     return HandleType::Move;

    //在矩形范围内可以移动
    if ((pos.x() > m_rect.left() + moveSize) &&
        (pos.x() < m_rect.right() - moveSize)&&
        (pos.y() > m_rect.top() + moveSize) &&
        (pos.y() < m_rect.bottom() - moveSize))
            return HandleType::Move;



    // 优先级 3: 边缘缩放检测
    qreal margin = 8.0 * s;// 点击感应范围随缩放调整，保证屏幕上手感一致
    // 检查是否在矩形的四条边附近
    if (qAbs(pos.x() - m_rect.left()) < margin ||
        qAbs(pos.x() - m_rect.right()) < margin ||
        qAbs(pos.y() - m_rect.top()) < margin ||
        qAbs(pos.y() - m_rect.bottom()) < margin) {
        return HandleType::Scale;
        // // 必须确保点在矩形的长度范围内才算命中边
        // if (pos.x() >= m_rect.left()-margin && pos.x() <= m_rect.right()+margin &&
        //     pos.y() >= m_rect.top()-margin && pos.y() <= m_rect.bottom()+margin) {
        //     return HandleType::Scale;
        // }
    }

    return HandleType::None;
}

//缩放逻辑（处理旋转后的坐标映射）
void RotRectRoiItem::applyScale(const QPointF& localDelta)
{
    prepareGeometryChange();

    // 拿到鼠标按下时相对于中心的位置
    // 我们需要判断用户是在拉“左右边”还是“上下边”
    QPointF pressPos = mapFromScene(m_lastScenePos);

    // 对称缩放逻辑
    if (qAbs(pressPos.x()) > qAbs(pressPos.y())) {
        // 左右拉伸 (修改宽度)
        qreal dx = localDelta.x();
        if (pressPos.x() < 0) dx = -dx; // 如果拉左边，鼠标向左(dx<0)应该是增加宽度

        qreal newHalfW = qMax(5.0, m_rect.width()/2 + dx);
        m_rect.setLeft(-newHalfW);
        m_rect.setRight(newHalfW);
    }
    else {
        // 上下拉伸 (修改高度)
        qreal dy = localDelta.y();
        if (pressPos.y() < 0) dy = -dy; // 如果拉上边，鼠标向上(dy<0)应该是增加高度

        qreal newHalfH = qMax(5.0, m_rect.height()/2 + dy);
        m_rect.setTop(-newHalfH);
        m_rect.setBottom(newHalfH);
    }
    // 注意：因为 (0,0) 始终在中心，不需要更新 TransformOriginPoint
}

QRectF RotRectRoiItem::rotateHandleRect() const{
    qreal s = 1.0 / getCurrentLod();
    qreal offset = m_rotateOffset * s;
    qreal r = m_handleRadius * s;

    QPointF rightCenter(m_rect.right() + offset, m_rect.center().y());//圆心在右侧
    return QRectF(rightCenter - QPointF(r, r),
                  QSizeF(r * 2, r * 2));
}

//给hitTest和boundinfRect 缩放因子
qreal RotRectRoiItem:: getCurrentLod() const{
    if(scene() && !scene()->views().isEmpty()){
        return QStyleOptionGraphicsItem::levelOfDetailFromTransform(
            scene()->views().first()->viewportTransform());
    }
    return 1.0;
}


//移动手柄矩形
QRectF RotRectRoiItem::moveHandleRect() const
{
    QPointF c = m_rect.center();
    return QRectF(c - QPointF(10, 10), QSizeF(20, 20));
}


//===============================================================================
//生成旋转矩形
HObject RotRectRoiItem::toHalconRegion() const
{
    QPointF sceneCenter = scenePos(); // 获取 Item 在场景中的位置

    double row = sceneCenter.y();
    double col = sceneCenter.x();

    // Qt 的 rotation() 是顺时针为正，角度单位
    // Halcon 的 phi 是逆时针为正，弧度单位
    double phi = -qDegreesToRadians(rotation());

    double length1 = qMax(1.0, m_rect.width() / 2.0);
    double length2 = qMax(1.0, m_rect.height() / 2.0);



    HObject rect;
    // 防御逻辑：如果长度太小，Halcon 无法生成区域
    if (length1 < 0.5 || length2 < 0.5) {
        rect.GenEmptyObj();
        return rect;
    }
    try {
        // 生成 Rectangle2
        GenRectangle2(&rect, row, col, phi, length1, length2);
        // 打印调试信息（运行后在控制台查看输出）
        qDebug() << "Halcon Parameter -> Row:" << row << "Col:" << col << "L1:" << length1 << "L2:" << length2 << "phi:" << phi;
    } catch (...) {
        rect.GenEmptyObj();
    }
    if(!rect.IsInitialized())  qDebug() << "rect没有被正确赋值";
    qDebug() << "rect已初始化";
    return rect;
}

//移动事件，铺货当前坐标
void RotRectRoiItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event)
{
    QPointF pos = event->pos(); // item 坐标（非常重要）
    QPixmap rotateImg(":/icons/images/rotate.png");
    QPixmap rotateImg16 = rotateImg.scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    static QCursor rotateCursor(
        rotateImg16,  // 透明背景圆弧箭头,停放显示
        16, 16                           // hotspot
        );

    if (!isLocked() && rotateHandleRect().contains(pos)) {
        // 悬停在旋转手柄上
        setCursor(rotateCursor/*Qt::OpenHandCursor*/);   // 旋转提示
    }
    else if (/*!isLocked() && */moveHandleRect().contains(pos)) {
        setCursor(Qt::SizeAllCursor);    // ROI锁没锁都有移动光标
    }
    else {
        unsetCursor();
    }

    QGraphicsItem::hoverMoveEvent(event);
}

void RotRectRoiItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*)
{
    unsetCursor();
}

void RotRectRoiItem::mousePressEvent(QGraphicsSceneMouseEvent* event){
    // 1. 先调用基类逻辑
    // 这步会完成：右键锁定判断、记录 m_lastScenePos、执行 hitTest 确定 m_activeHandle
    AbstractRoiItem::mousePressEvent(event);

    // 2. 如果基类忽略了事件（比如 ROI 已被锁定），我们也直接返回
    if(!event->isAccepted()) return;
    else{
        if (m_activeHandle == HandleType::Rotate) setCursor(Qt::ClosedHandCursor);//设置光标
    }

    // 3. 派生类特有的视觉处理：按下时的光标反馈
    switch (m_activeHandle) {
    case HandleType::Rotate:
        setCursor(Qt::ClosedHandCursor); // 旋转时握紧
        break;
    case HandleType::Scale:
        setCursor(Qt::SizeFDiagCursor); // 缩放时的光标
        break;
    case HandleType::Move:
        setCursor(Qt::SizeAllCursor);   // 移动时的光标
        break;
    default:
        break;
    }
}

void RotRectRoiItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    AbstractRoiItem::mouseReleaseEvent(event);

    unsetCursor();
    m_activeHandle = HandleType::None;
}











