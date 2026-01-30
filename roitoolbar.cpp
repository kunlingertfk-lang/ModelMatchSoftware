#include "roitoolbar.h"

#include <QHBoxLayout>
#include <QStyle>
#include <QLabel>

RoiToolbar::RoiToolbar(QWidget *parent)
    : QWidget{parent}
{
    setupUi();
}


void RoiToolbar::setupUi()
{
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(5);

    QLabel* label = new QLabel("ROI 工具：", this);
    layout->addWidget(label);

    // 1. 正负模式按钮组 (图标 1, 2 (Add,Subtract))
    m_opGroup = new QButtonGroup(this);
    m_opGroup->setExclusive(true);

    QToolButton* btnAdd = new QToolButton(this);
    btnAdd->setCheckable(true);
    btnAdd->setChecked(true);
    btnAdd->setIcon(QIcon(":/icons/images/ic_roi_op_union.png"));
    btnAdd->setToolTip("包含区域(Add)");

    QToolButton* btnSub = new QToolButton(this);
    btnSub->setCheckable(true);
    btnSub->setIcon(QIcon(":/icons/images/ic_roi_op_difference.png"));
    btnSub->setToolTip("剔除区域(Subtract)");

    m_opGroup->addButton(btnAdd, 0); // ID 0 代表 Positive
    m_opGroup->addButton(btnSub, 1);// ID 1 代表 Negative

    // 2. 形状按钮组 (图标 3, 4, 5, 6, 7)
    m_shapeGroup = new QButtonGroup(this);
    m_shapeGroup->setExclusive(true);

    layout->addWidget(btnAdd);
    layout->addWidget(btnSub);
    layout->addSpacing(10); // 分隔

    // --- 2. 形状按钮组 --
    m_shapeGroup = new QButtonGroup(this);
    m_shapeGroup->setExclusive(true);

    //旋转矩形按钮
    m_btnRotRect = new QToolButton(this);
    m_btnRotRect->setCheckable(true);
    m_btnRotRect->setIcon(QIcon(":/icons/images/ic_gen_rect2.png"));
    m_btnRotRect->setToolTip("绘制旋转矩形");
    m_shapeGroup->addButton(m_btnRotRect,(int)RoiGraphicsView::RoiMode::DrawRotRect);

    //矩形按钮
    m_btnRect = new QToolButton(this);
    m_btnRect->setCheckable(true);
    m_btnRect->setIcon(QIcon(":/icons/images/ic_gen_rect1.png"));
    m_btnRect->setToolTip("绘制平行矩形");
    m_shapeGroup->addButton(m_btnRect,(int)RoiGraphicsView::RoiMode::DrawRect);

    //圆形按钮
    m_btnCircle = new QToolButton(this);
    m_btnCircle->setCheckable(true);
    m_btnCircle->setIcon(QIcon(":/icons/images/ic_gen_circle.png"));
    m_btnCircle->setToolTip("绘制圆形");
    m_shapeGroup->addButton(m_btnCircle,(int)RoiGraphicsView::RoiMode::DrawCircle);

    //椭圆形按钮
    m_btnEllipse = new QToolButton(this);
    m_btnEllipse->setCheckable(true);
    m_btnEllipse->setIcon(QIcon(":/icons/images/ic_gen_ellipse.png"));
    m_btnEllipse->setToolTip("绘制椭圆形");
    m_shapeGroup->addButton(m_btnEllipse,(int)RoiGraphicsView::RoiMode::DrawEllipse);

    layout->addWidget(m_btnRotRect);
    layout->addWidget(m_btnRect);
    layout->addWidget(m_btnCircle);
    layout->addWidget(m_btnEllipse);

    // --- 3. 管理按钮 ---
    QToolButton* btnDel = new QToolButton(this);
    btnDel->setIcon(QIcon(":/icons/images/ic_clear_roi_sel.png"));
    btnDel->setToolTip("删除选中的ROI");

    QToolButton* btnClear = new QToolButton(this);
    btnClear->setIcon(QIcon(":/icons/images/ic_clear_roi_all.png"));
    btnClear->setToolTip("清空所有ROI");

    layout->addSpacing(10);
    layout->addWidget(btnDel);
    layout->addWidget(btnClear);
    layout->addStretch();//弹簧

    // 信号转发
    //绑定形状按钮点击信号
    connect(m_shapeGroup,
            QOverload<int>::of(&QButtonGroup::idClicked),
            this,
            [this](int id){
                emit modeChanged(static_cast<RoiGraphicsView::RoiMode>(id));
            });

    //绑定正负模式按钮点击信号
    connect(m_opGroup,
            QOverload<int>::of(&QButtonGroup::idClicked),
            this,
            [this](int id){
                emit operationChanged(id == 0);
            });

    connect(btnDel, &QToolButton::clicked, this, &RoiToolbar::deleteRequested);
    connect(btnClear, &QToolButton::clicked, this, &RoiToolbar::clearAllRequested);
}


// 供外部调用：重置形状按钮状态
void RoiToolbar::resetShapeButtons() {
    // 暂时禁用排他性，以便清空所有选中状态
    m_shapeGroup->setExclusive(false);
    if(m_shapeGroup->checkedButton()){
        m_shapeGroup->checkedButton()->setChecked(false);
    }
    m_shapeGroup->setExclusive(true);
}









