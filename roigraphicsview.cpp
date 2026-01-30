#include "roigraphicsview.h"
#include <QMouseEvent>
#include <QGraphicsScene>
#include <QTimer>
#include <QDebug>

#include "AbstractRoiItem.h"
#include "rotrectroiitem.h"

RoiGraphicsView::RoiGraphicsView(QWidget* parent)
    : QGraphicsView(parent)
{
    // 设置背景色为深色，方便看清图片边缘
    setBackgroundBrush(QBrush(QColor(0,0,0)));
    //取消居中对齐，改为左上角对齐（或者根据需求保持居中）
    setAlignment(Qt::AlignCenter);

    // 启用拖拽模式（按住右键或 Alt 键时可以移动整个视图，可选）
    //setDragMode(QGraphicsView::ScrollHandDrag);

    setMouseTracking(true);
    setRenderHint(QPainter::Antialiasing);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
}

void RoiGraphicsView::mousePressEvent(QMouseEvent* event) {
    QPointF scenePos = mapToScene(event->pos());
    // 调试打印：看看你点的这个像素坐标是多少
    // qDebug() << "鼠标点击位置 (场景坐标):" << scenePos;

    static int a = 0;
    // --- 逻辑 1: 正在编辑中的 ROI 处理 ---
    if (m_editingItem) {
        if (event->button() == Qt::RightButton) {
            // 右键锁定
            m_editingItem->lock();
            m_editingItem->setSelected(false);
            m_editingItem = nullptr;

            m_mode = RoiMode::None;
            setCursor(Qt::ArrowCursor);

            // 发射信号通知 UI（如果有的话）
            emit itemFinished();

            event->accept();
            return;
        }

        // 如果是左键，交给场景。场景会自动判断点中了哪个手柄。
        QGraphicsView::mousePressEvent(event);
        return;
    }

    // --- 逻辑 2: 创建新的 ROI ---
    if (m_mode == RoiMode::DrawRotRect && event->button() == Qt::LeftButton) {
        auto* roi = new RotRectRoiItem(QRectF(scenePos, QSize(0, 0)));

        qDebug() << "press:" << a++;

        roi->setOperation(m_isPositive ? AbstractRoiItem::RoiOperation::Add
                                       : AbstractRoiItem::RoiOperation::Subtract);
        if(roi->operation() == AbstractRoiItem::RoiOperation::Add){
            qDebug() << "选择添加RoatRectROI->RoiOperation::Add";
        }
        else{
            qDebug() << "选择删减RoatRectROI->RoiOperation::Subtract";
        }

        scene()->addItem(roi);
        m_editingItem = roi;
        m_editingItem->setSelected(true);

        // 初始化交互状态
        m_editingItem->setActiveHandle(AbstractRoiItem::HandleType::Scale);
        m_editingItem->setLastScenePos(scenePos);

        setCursor(Qt::CrossCursor);
        event->accept();
        emit roiAdded(roi);
    }
    else {
        QGraphicsView::mousePressEvent(event);
    }
}

void RoiGraphicsView::mouseMoveEvent(QMouseEvent* event) {
    //static int a = 0;
    // 1. QPointer 自动判断：如果对象已销毁，m_editingItem 会自动变成 null
    // 2. 增加 scene() 检查，确保对象不仅存在，且还在场景中
    if (!m_editingItem.isNull() && m_editingItem->scene() != nullptr) {
        if ((event->buttons() & Qt::LeftButton) &&  m_editingItem->activeHandle() == AbstractRoiItem::HandleType::Scale) {
            // 在这一行（Line 88）之前，做一个万全的本地变量保护
            // 锁定指针，防止在函数执行期间发生意外
            AbstractRoiItem* safeItem = m_editingItem.data();

            QPointF currentScenePos = mapToScene(event->pos());

            // 限制坐标在图片范围内
            QRectF limitRect = this->scene()->sceneRect();
            currentScenePos.setX(qBound(limitRect.left(), currentScenePos.x(), limitRect.right()));
            currentScenePos.setY(qBound(limitRect.top(), currentScenePos.y(), limitRect.bottom()));


            QPointF lastPos = safeItem->lastScenePos();

            // 检查坐标是否有效（防止内存坏掉后的异常值）
            if (qIsFinite(lastPos.x()) && qIsFinite(lastPos.y())) {

                //() << "move:" << a++;

                QPointF lastPosLocal = safeItem->mapFromScene(lastPos);
                QPointF currentPosLocal = safeItem->mapFromScene(currentScenePos);

                safeItem->applyScale(currentPosLocal - lastPosLocal);
                safeItem->setLastScenePos(currentScenePos);

                event->accept();
                return;// 仅在此处拦截创建过程
            }
        }
    }
    // 如果不处于创建缩放模式，正常分发事件
    QGraphicsView::mouseMoveEvent(event);
}

void RoiGraphicsView::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_editingItem.isNull()) {
        // 初始拉伸完成，退出强制缩放模式，转入自由编辑/待确认状态
        m_editingItem->setActiveHandle(AbstractRoiItem::HandleType::None);
    }
    QGraphicsView::mouseReleaseEvent(event);
}


/**
 * @brief 安全删除选中的 ROI
 */
void RoiGraphicsView::deleteSelectedRois(){

    // 1. 停止当前 View 对鼠标的抓取状态
    if (m_editingItem) {
        m_mode = RoiMode::None;
        setCursor(Qt::ArrowCursor);
    }

    // 2. 获取副本进行删除
    QList<QGraphicsItem*> items = scene()->selectedItems();
    for (QGraphicsItem* item : items) {
        if (auto roi = dynamic_cast<AbstractRoiItem*>(item)) {
            // 3. 先从场景移除（这一步非常关键，它会让 Item 停止接收一切事件）
            scene()->removeItem(roi);

            // 4. 安全延迟销毁
            roi->deleteLater();
        }
    }
}
