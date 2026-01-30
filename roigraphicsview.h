#ifndef ROIGRAPHICSVIEW_H
#define ROIGRAPHICSVIEW_H

#include <QGraphicsView>
#include <QPointer>

class AbstractRoiItem;

class RoiGraphicsView : public QGraphicsView
{
    Q_OBJECT
public:
    enum class RoiMode { None, DrawRotRect, DrawCircle, DrawRect, DrawEllipse };

    explicit RoiGraphicsView(QWidget* parent = nullptr);

    void setWorkMode(RoiMode mode) { m_mode = mode; }
    void setCurrentRoiPositive(bool pos) { m_isPositive = pos; }

    void deleteSelectedRois();

signals:
    void itemFinished();// 当右键确认一个ROI 后发送
    void roiAdded(AbstractRoiItem* item);  //每当 new 了一个 ROI 就发信号


protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    RoiMode m_mode = RoiMode::None;
    bool m_isPositive = true;

    // 当前正在创建或正在编辑且未确认的那个 ROI
    QPointer<AbstractRoiItem> m_editingItem;


};

#endif // ROIGRAPHICSVIEW_H
