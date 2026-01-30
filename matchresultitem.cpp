#include "matchresultitem.h"
#include <QGraphicsItem>  //基础图形项
#include <QPainter> //绘图工具
#include <QRectF>   //矩形区域
#include <QtMath>   //数学计算（弧度转角度）
#include <QPen>     // 画笔设置
#include <QStyleOptionGraphicsItem>

#include <HalconCpp.h>

MatchResultItem::MatchResultItem(QGraphicsItem* parent) :QGraphicsItem(parent)
{
    setZValue(100); // 确保结果显示在最顶层，不会被图片或 ROI 遮挡

    setAcceptedMouseButtons(Qt::NoButton);//静止接收鼠标事件
    setVisible(false); // 初始状态不可见，只有找到目标才显示
}

// 更新位置和角度（由 Halcon 返回的结果驱动）
void MatchResultItem::setResult(const MatchResult& res) {
    // 1. 告诉 Qt 几何形状即将改变，防止留下残影
    prepareGeometryChange();

    setPos(0,0);
    m_score = res.score;
    m_contourPath = convertXldToPath(res.modelContours);

    // 我们还是需要一个中心点坐标来画十字
    m_foundPos = QPointF(res.col, res.row);


    //this->setPos(res.col, res.row); // Qt 的 X 是 Column, Y 是 Row
    //this->setRotation(qRadiansToDegrees(res.angle)); // Halcon 弧度转 Qt 角度
    this->setVisible(true);
    update();
}

// 设置显示的标签（比如 "Template 1"）
void MatchResultItem::setLabel(const QString& text){
    m_label = text;
}



// 需要重写的接口

// 定义这个项占据的范围
QRectF MatchResultItem::boundingRect() const  {
    return QRectF(-50, -50, 150, 100);
}

// 绘制结果：画一个绿色十字和中心小框
void MatchResultItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
     Q_UNUSED(widget);

    // 1. 获取当前视图的缩放比例（LOD）
    // lod = 1.0 代表原图, > 1.0 放大, < 1.0 缩小
    qreal lod = option->levelOfDetailFromTransform(painter->worldTransform());
    if(lod <= 0) lod = 1.0;

    // 计算逆缩放因子，用于保持 UI 元素在屏幕上像素大小恒定
    qreal s = 1.0 / lod;

    painter->setRenderHint(QPainter::Antialiasing);

    QPen xldPen(QColor(255,0,0),1);//橙色
    xldPen.setCosmetic(true);
    painter->setPen(xldPen);
    painter->drawPath(m_contourPath);

    // 2. 配置画笔：使用 Cosmetic Pen (宽度永远 2px)
    QPen pen(Qt::green, 2);
    pen.setCosmetic(true);
    painter->setPen(pen);

    // --- 绘制中心准星 ---
    // 绘制 20x20 像素的恒定十字
    qreal cs = 15 * s;
    painter->drawLine(QPointF(-cs, 0), QPointF(cs, 0));
    painter->drawLine(QPointF(0, -cs), QPointF(0, cs));

    // 绘制中心小方框
    qreal bs = 5 * s;
    painter->drawRect(QRectF(-bs, -bs, bs*2, bs*2));

    // --- 绘制方向箭头（指示 0 度方向） ---
    painter->setPen(QPen(Qt::green, 1, Qt::DashLine));
    painter->drawLine(QPointF(0, 0), QPointF(30 * s, 0)); // 箭头长度视觉恒定

    // --- 绘制文本信息（得分和标签） ---
    // 文本也需要逆缩放，否则放大图片时字会变得像磨盘一样大
    painter->setPen(Qt::yellow);
    QFont font = painter->font();
    font.setPointSizeF(10 * s); // 字号随缩放调整，保持视觉一致
    painter->setFont(font);

    QString info = QString("Score: %1").arg(m_score, 0, 'f', 2);
    if (!m_label.isEmpty()) info = m_label + "\n" + info;

    // 在十字架右下方显示文字
    painter->drawText(QPointF(18 * s, 25 * s), info);
}


void MatchResultItem::updateResult(double row,double col,double angleRad,double score){
    // 1. 基础变换
    setPos(col, row);
    setRotation(qRadiansToDegrees(angleRad));

    // 2. 存储得分供 paint 使用
    m_score = score;

    // 3. 只有分值达到一定程度才显示
    setVisible(score > 0.1);
    update();
}


QPainterPath MatchResultItem::convertXldToPath(const HalconCpp::HObject& xld){
    QPainterPath path;
    if (!xld.IsInitialized()) return path;

    try {
        HalconCpp::HTuple hv_NumPaths;
        HalconCpp::CountObj(xld, &hv_NumPaths);

        // 使用 int 遍历，避免 long 导致的构造歧义
        int num = (int)hv_NumPaths.I();

        for (int i = 1; i <= num; ++i) {
            HalconCpp::HObject ho_Selected;
            // 显式将 i 转换为 HTuple
            HalconCpp::SelectObj(xld, &ho_Selected, HalconCpp::HTuple(i));

            HalconCpp::HTuple hv_Rows, hv_Cols;
            HalconCpp::GetContourXld(ho_Selected, &hv_Rows, &hv_Cols);

            if (hv_Rows.Length() > 0) {
                path.moveTo(hv_Cols[0].D(), hv_Rows[0].D());
                for (int j = 1; j < hv_Rows.Length(); ++j) {
                    path.lineTo(hv_Cols[j].D(), hv_Rows[j].D());
                }
            }
        }
    } catch (...) {
        // 捕获异常防止崩溃
    }
    return path;
}
















