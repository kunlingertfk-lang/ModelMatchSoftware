#ifndef MATCHRESULTITEM_H
#define MATCHRESULTITEM_H

#include <QGraphicsItem>
#include "MatchResult.h"

namespace HalconCpp{
class HObject;
}

class MatchResultItem : public QGraphicsItem
{
public:
    MatchResultItem(QGraphicsItem* parent = nullptr);

    // 更新位置和角度（由 Halcon 返回的结果驱动）
    void setResult(const MatchResult& res);

    // 定义这个项占据的范围
    QRectF boundingRect() const override;

    // 绘制结果：画一个绿色十字和中心小框
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    void updateResult(double row,double col,double angleRad,double score);

    void setLabel(const QString& text);

private:
    QPainterPath convertXldToPath(const HalconCpp::HObject& xld);

private:
    double m_score;

    QString m_label;

    QPointF m_foundPos;

    QPainterPath m_contourPath;
};

#endif // MATCHRESULTITEM_H
