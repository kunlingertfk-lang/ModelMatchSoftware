#ifndef ROITOOLBAR_H
#define ROITOOLBAR_H

#include <QWidget>
#include <QToolButton>
#include <QButtonGroup>

#include "roigraphicsview.h"


class RoiToolbar : public QWidget
{
    Q_OBJECT
public:
    explicit RoiToolbar(QWidget *parent = nullptr);

    // 当 View 绘图结束或右键确认后，调用此函数让按钮弹起
    void resetShapeButtons();

signals:
    // 当模式切换时发送信号（例如：从普通切换到画矩形）
    void modeChanged(RoiGraphicsView::RoiMode mode);
    // 切换正负区域信号（true为包含，false为剔除）
    void operationChanged(bool isPositive);
    // 删除操作
    void deleteRequested();
    void clearAllRequested();
    void exportRequested();     // 导出/生成 Halcon 区域（对应红框最右侧图标）

private:
    void setupUi();
    QButtonGroup* m_shapeGroup;// 形状按钮：矩形、旋转矩形、圆等（互斥）
    QButtonGroup* m_opGroup; // 操作模式：包含(+)、剔除(-)（互斥）

    // 内部按钮引用，方便 resetShapeButtons 操作
    QToolButton* m_btnRotRect;
    QToolButton* m_btnRect;
    QToolButton* m_btnCircle;
    QToolButton* m_btnEllipse;

};

#endif // ROITOOLBAR_H
