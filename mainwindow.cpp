#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <HalconCpp.h>

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>

#include <QMessageBox>
#include <QFileDialog>
#include <QDebug>
#include <QPixmap>
#include <QThread>
#include <QTimer>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QSlider>

#include "AbstractRoiItem.h"
#include "roitoolbar.h"
#include "roigraphicsview.h"
#include "matchresultitem.h"
#include "MatchResult.h"
#include "halconworker.h"


using namespace HalconCpp;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    loadStyleSheet();

    ui->splitter->setStretchFactor(0,7);
    ui->splitter->setStretchFactor(1,3);
    //ui->tableWidget->setMinimumWidth(350);

    // --- 1. 严格的初始化顺序 ---
    m_view = ui->graphicsView;
    m_scene = new QGraphicsScene(this); // 全局唯一的场景
    m_view->setScene(m_scene);          // 绑定
    m_view->setBackgroundBrush(QBrush(QColor(45,45,45)));

    // 设置默认场景大小
    m_scene->setSceneRect(0,0,1000,1000);

    // --- 2. 创建背景图层 ---
    m_imgItem = new QGraphicsPixmapItem();
    m_imgItem->setZValue(-1);
    m_scene->addItem(m_imgItem);

    m_resultItem = new MatchResultItem();
    m_resultItem->setZValue(100);
    m_scene->addItem(m_resultItem);

    // --- 3. 配置视图外观（合并 initParameter 和 initGraphicsView） ---
    m_view->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);

    // --- 4. 初始化子系统 ---
    initAlgorithmThread();// 初始化线程
    initParamConnections();
    initUICtrl();//初始化UI控件
    initRoiSystem();   // 工具栏
    initConnections(); // 信号槽
}

MainWindow::~MainWindow()
{
    if(m_workerThread->isRunning()){
        m_workerThread->quit();
        m_workerThread->wait();
    }
    delete ui;
}

//====================================================
// ====================== 初始化 ======================
//初始化ROI
void MainWindow::initRoiSystem(){
    // 1. 创建工具栏
    m_roiToolbar = new RoiToolbar(ui->toolButton_frame);//设置父对象

    // 3. 核心信号槽连接
    // 切换绘制形状
    connect(m_roiToolbar, &RoiToolbar::modeChanged, m_view, &RoiGraphicsView::setWorkMode);
    // 切换正负区域 (包含/剔除)
    connect(m_roiToolbar, &RoiToolbar::operationChanged, m_view, &RoiGraphicsView::setCurrentRoiPositive);

    // 删除选中的 ROI
    connect(m_roiToolbar, &RoiToolbar::deleteRequested, this, [this](){
        for (auto item : m_scene->selectedItems()) {
            if (auto roi = dynamic_cast<AbstractRoiItem*>(item)) {
                m_scene->removeItem(roi);
                roi->deleteLater(); // 安全删除
            }
        }
        m_resultItem->setVisible(false);
        triggerUpdate();
    });

    // 清空所有 ROI

    connect(m_roiToolbar, &RoiToolbar::clearAllRequested, this, [this](){
        for (auto item : m_scene->items()) {
            auto roi = dynamic_cast<AbstractRoiItem*>(item);
            if (roi) {
                m_scene->removeItem(roi);
                roi->deleteLater();
            }
        }
        m_resultItem->setVisible(false);
    });
}

// 工作线程初始化
void MainWindow::initAlgorithmThread(){
    // 禁用区域裁剪，允许 ROI 坐标超出默认的 512x512 范围
    HalconCpp::SetSystem("clip_region", "false");

    // 注册自定义类型
    qRegisterMetaType<MatchParams>("MatchParams");
    qRegisterMetaType<MatchResult>("MatchResult");
    qRegisterMetaType<HalconCpp::HObject>("HalconCpp::HObject");
    qRegisterMetaType<HalconCpp::HRegion>("HalconCpp::HRegion");

    // 创建线程
    m_workerThread = new QThread(this);
    m_worker = new HalconWorker();
    m_worker->moveToThread(m_workerThread);

    // 连接：主线程请求 -> 子线程执行
    connect(this,&MainWindow::requestTrain,m_worker,&HalconWorker::trainModel);

    // 连接：主线程请求 -> 子线程执行
    connect(this,&MainWindow::requestBatchMatch,m_worker,&HalconWorker::matchBatch);

    // 连接：子线程结果 -> 主线程更新 UI(训练回调)
    connect(m_worker,&HalconWorker::modelTrained,this,[this](MatchResult res){
        m_isCalculating = false;
        qDebug() << "UI:" << "col:" <<  res.col << " row" << res.row << " angle:" <<  res.angle << "错误信息" << res.errorMsg;

        if(res.found && res.score>0.1){ // 增加一个基础分值过滤
            // 更新绿色十字架结果
            //qDebug() << "匹配成功" ;
            m_resultItem->setResult(res);
            m_resultItem->setLabel(QString("%1 ms").arg(res.time, 0, 'f', 1));
        }else {
            m_resultItem->setVisible(false);
            qDebug() << "匹配失败：特征不足或得分太低";
        }
    });

    // 连接：子线程结果 -> 主线程更新 单张图片匹配结果
    connect(m_worker,&HalconWorker::batchRowReady,this,[this](int index,MatchResult res){
        m_batchList[index].result = res;
        m_batchList[index].isTested = true;

        // 更新表格对应行
        ui->tableWidget->item(index, 1)->setText(res.found ? QString::number(res.score,'f',2) : "NG");
        ui->tableWidget->item(index, 1)->setBackground(res.found ? Qt::green : Qt::red);
        // handleBatchResult(ui->tableWidget->item(index,1));
    });


     // 连接：子线程结果 -> 主线程更新 所有图片匹配完成,让按钮可按
    connect(m_worker,&HalconWorker::batchFinished,this,[this](){
        m_isCalculating = false; //计算完了，重置为false
        ui->pushButton_DetectionAll->setEnabled(true);
        //ui->statusBar->showMessage("所有图片检测完成", 3000);//弹出一个状态条提示“批量检测完成
    });


    // 线程清理逻辑
    connect(m_workerThread,&QThread::finished,m_worker,&QObject::deleteLater);
    m_workerThread->start();
}

//绑定QSlider与QSPinBox ,并进行value联动
void MainWindow::initParamConnections(){
    //初始化参数
    m_intBindings = QVector<IntBinding>{
        IntBinding(ui->sliderContrastLow,       ui->spinContrastLow,      &m_currentParams.contrastLow),
        IntBinding(ui->sliderContrastHigh,      ui->spinContrastHigh,     &m_currentParams.contrastHigh),
        IntBinding(ui->sliderComponentSizeMin,  ui->spinComponentSizeMin, &m_currentParams.minComponentSize),
        IntBinding(ui->sliderPyramidLevel,      ui->spinPyramidLevel,     &m_currentParams.pyramidLevel),
        IntBinding(ui->sliderMaxMatchNum,       ui->spinBoxMaxMatchNum,   &m_currentParams.matchNum)
    };

    m_doubleBindings = QVector<DoubleBinding>{
        DoubleBinding(ui->sliderAngleStart,        ui->doubleSpinBox_AngleStart,   &m_currentParams.angleStart,   1.0),
        DoubleBinding(ui->sliderAngleExtent,       ui->doubleSpinBox_AngleExtent,  &m_currentParams.angleExtent,  1.0),
        DoubleBinding(ui->sliderMinScalingRow,     ui->doubleSpinBox_RowMinScale,  &m_currentParams.scaleRMin,    100.0),
        DoubleBinding(ui->sliderMaxScalingRow,     ui->doubleSpinBox_RowMaxScale,  &m_currentParams.scaleRMax,    100.0),
        DoubleBinding(ui->sliderMinScalingColumn,  ui->doubleSpinBox_ColMinScale,  &m_currentParams.scaleCMin,    100.0),
        DoubleBinding(ui->sliderMaxScalingColumn,  ui->doubleSpinBox_ColMaxScale,  &m_currentParams.scaleCMax,    100.0),
        DoubleBinding(ui->sliderMinMatchScore,     ui->doubleSpinBoxMinMatchScore, &m_currentParams.minScore,     100.0)
    };

    m_paramTimer = new QTimer(this);
    m_paramTimer->setSingleShot(true);

    // 每当参数改变时，启动/重启定时器
    connect(m_paramTimer,&QTimer::timeout,this,&MainWindow::triggerUpdate);

    // 绑定每对 spinbox和slider
    for(auto& b : m_intBindings){
        auto applyValue = [this,b](int val,bool fromUI){
            int sliderVal = val;
            if(b.slider->value() != sliderVal)
                b.slider->setValue(sliderVal);

            if(b.spinbox->value() != val)
                b.spinbox->setValue(val);
            if(fromUI){
                *b.target = val;
                m_paramTimer->start(100);
            }

        };

        // 主要显示
        connect(b.spinbox,QOverload<int>::of(&QSpinBox::valueChanged),this, [=](int val){
            applyValue(val,true);
        });
        connect(b.slider,&QSlider::valueChanged, this,[=](int val){
            if(*b.target == val) return;
            applyValue(val,false);
        });
    }

    // 绑定每对 doublespinbox和slider
    for(auto& b : m_doubleBindings){
        auto applyValue = [this,b](double val,bool fromUI){
            int sliderVal = qRound(val*b.scale);

            if(b.slider->value() != sliderVal)
                b.slider->setValue(sliderVal);

            // 浮点安全比较
            if(!qFuzzyCompare(b.spinbox->value()+1.0,val+1.0))
                b.spinbox->setValue(val);
            if(fromUI){
                *b.target = val;
                m_paramTimer->start(100);
            }
        };
        connect(b.spinbox,QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [=](double val){
            applyValue(val,true);
        });
        connect(b.slider,&QSlider::valueChanged, this,[=](int val){
            double dv = val / b.scale;
            // 防止重复回调
            if(qFuzzyCompare(b.spinbox->value() + 1.0, dv + 1.0)) return;
            applyValue(dv,false);
        });
    }
}


//连接信号与槽
void MainWindow::initConnections(){
    // 添加ROi,触发更新信号
    connect(m_view,&RoiGraphicsView::roiAdded,this,[this](AbstractRoiItem* item){
        connect(item,&AbstractRoiItem::roiMove,this,&MainWindow::triggerUpdate);
    });

    // 只有右键确认 ROI 后才触发算法（防止频繁创建模型导致卡顿）
    connect(m_view,&RoiGraphicsView::itemFinished,this,&MainWindow::triggerUpdate);

    // 处理 ROI 结束后的工具栏状态
    connect(m_view, &RoiGraphicsView::itemFinished, m_roiToolbar, &RoiToolbar::resetShapeButtons);

    // 按钮点击：只负责获取路径并调用加载函数
    connect(ui->pushButton, &QPushButton::clicked, this,&MainWindow::handleImageLoad);

    // 获取文件夹图片逻辑
    connect(ui->pushButton_ImgsPath, &QPushButton::clicked, this, &MainWindow::onSelectFiles);

    //点击 自动计算对比度
    connect(ui->ContrastLHAuto_Button,&QPushButton::clicked,this,&MainWindow::contrastLHAutoSelect);

    // 点击表格单元格显示图片和匹配结果
    setupConnect();

    // 检测所有图片
    connect(ui->pushButton_DetectionAll,&QPushButton::clicked,this,&MainWindow::onButtonClicked_DetectAllImages);

    // 打印workerThread的信息
    connect(m_worker,&HalconWorker::threadLogInfo,this,&MainWindow::printThreadLogInfo);
}

// connect table和clicked
void MainWindow::setupConnect(){
    static bool connected = false;
    if (connected) return;
    connected = true;

    connect(ui->tableWidget, &QTableWidget::cellClicked,
            this,
            [this](int row, int col) {

                if (row < 0 || row >= m_batchList.size())
                    return;

                BatchData &data = m_batchList[row];

                // 1. 加载图片
                loadImage(data.filePath);

                // 2. 恢复显示该图的匹配结果
                if (data.isTested) {
                    m_resultItem->setResult(data.result);   // 用该行自己的结果
                    m_resultItem->setVisible(true);
                } else {
                    m_resultItem->setVisible(false);
                }
            });
}

void MainWindow::initUICtrl(){
    ui->tableWidget->setColumnCount(3);
    ui->tableWidget->setHorizontalHeaderLabels({"名称","看的见的","检测到的"});
    ui->tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);


}


//======================================================
// ====================== 业务逻辑 ======================

//点击按钮,获取制作模板的图片
void MainWindow::handleImageLoad()
{
    // 按钮点击：只负责打开对话框，获取路径
    QString path = QFileDialog::getOpenFileName(this, "选择模板图片文件", "", "所有图片(*.png *.bmp *.jpg)");
    if (!path.isEmpty()) {
        m_ImgPath = path;
        this->loadImage(m_ImgPath); // 统一调用

        ui->Img_lineEdit->setText(path);
    }
}

//加载图片
void MainWindow::loadImage(const QString& path){
    QPixmap pix(path);
    if (pix.isNull()) {
        qWarning() << "图片加载失败:" << path;
        return;
    }

    // 2. 关键：强制场景的大小等于图片的像素大小
    // 这样 1个场景单位 = 1个图片像素
    m_imgItem->setPixmap(pix);
    m_imgItem->setPos(0,0);
    m_scene->setSceneRect(0,0,pix.width(),pix.height()); // 关键：同步场景大小

    // 3. 视图对齐设置 (防止视图自动居中干扰坐标判断)
    m_view->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    // --- 核心修复：更新 Halcon 工作空间尺寸 ---
    // 这样 Halcon 的内部算子就知道当前图像的真实物理边界了
    HalconCpp::SetSystem("width", (HalconCpp::HTuple)pix.width());
    HalconCpp::SetSystem("height", (HalconCpp::HTuple)pix.height());

    FitImage();
    // qDebug() << "图片载入成功: " << pix.width() << "x" << pix.height()
    //          << " 场景矩形已重置为: " << m_scene->sceneRect();
}

//触发更新矩形位置 训练模板（在参数页调整时触发）
void MainWindow::triggerUpdate(){
    if (m_isCalculating) return; // 如果算法还在跑，直接忽略本次请求（防止队列堆积）
    if (m_ImgPath.isEmpty()) {
        m_resultItem->setVisible(false);
        return;
    }

    // qDebug() << "触发更新";
    HRegion finalRegion = generateFinalRegion();
    // qDebug() << "col=" << finalRegion.Column().D()
    //          << " row=" << finalRegion.Row().D();

    // HImage h(m_ImgPath.toLocal8Bit().data());
    // qDebug() << "w=" << h.Width().D() << " h=" << h.Height().D();

    if (finalRegion.IsInitialized() && (long)finalRegion.CountObj() > 0) {
        qDebug() << "发送训练请求信号";
        m_isCalculating = true;
        // GetCurrentParams();//获取控件参数赋值给m_currentParams
        emit requestTrain(m_ImgPath, finalRegion, m_currentParams); // 异步发送
    } else {
        m_resultItem->setVisible(false);
        qDebug() << "Warning: ROI Region is empty, skipping algorithm.";
    }
}

// 获取当前控件参数
void MainWindow::GetCurrentParams(){
    m_currentParams.scaleRMin = ui->doubleSpinBox_RowMinScale->value();
    m_currentParams.scaleRMax = ui->doubleSpinBox_RowMaxScale->value();
    m_currentParams.scaleCMin = ui->doubleSpinBox_ColMinScale->value();
    m_currentParams.scaleCMax = ui->doubleSpinBox_ColMaxScale->value();

    m_currentParams.contrastLow = ui->spinContrastLow->value();
    m_currentParams.contrastHigh = ui->spinContrastHigh->value();
    m_currentParams.minComponentSize = ui->spinComponentSizeMin->value();
    m_currentParams.pyramidLevel = ui->spinPyramidLevel->value();

    m_currentParams.angleStart = ui->doubleSpinBox_AngleStart->value();
    m_currentParams.angleExtent = ui->doubleSpinBox_AngleExtent->value();

    // m_currentParams.minScore = ui->doubleSpinBoxMinMatchScore->value();
    // m_currentParams.matchNum = ui->spinBoxMaxMatchNum->value();

    qDebug() << "contrastLow=" << m_currentParams.contrastLow << " contrastHigh=" << m_currentParams.contrastHigh
             << " minComponentSize=" << m_currentParams.minComponentSize << " pyramidLevel=" << m_currentParams.pyramidLevel;
}




// ====================== 工具函数 ======================

//============== Halcon ==============
//生成最终区域
HalconCpp::HRegion MainWindow::generateFinalRegion() {
    HalconCpp::HObject allAdd, allSub;
    HalconCpp::GenEmptyObj(&allAdd);
    HalconCpp::GenEmptyObj(&allSub);

    bool hasAdd = false;
    bool hasSub = false;

    QList<QGraphicsItem*> allItems = m_scene->items();

    // --- 第一阶段：收集（不分先后顺序） ---
    for (auto item : allItems) {
        auto roi = dynamic_cast<AbstractRoiItem*>(item);
        if (!roi) continue;

        HalconCpp::HObject ho = roi->toHalconRegion();
        if (!ho.IsInitialized() || (long)ho.CountObj() == 0) continue;

        if (roi->operation() == AbstractRoiItem::RoiOperation::Add) {
            HalconCpp::ConcatObj(allAdd, ho, &allAdd);
            hasAdd = true;
        } else {
            HalconCpp::ConcatObj(allSub, ho, &allSub);
            hasSub = true;
        }
    }

    HalconCpp::HRegion finalRegion;

    // --- 第二阶段：逻辑运算 ---
    if (hasAdd) {
        // 1. 将所有加法 ROI 融合为一个整体区域
        finalRegion = HalconCpp::HRegion(allAdd).Union1();

        // 2. 如果存在减法 ROI，从整体中一次性扣除
        if (hasSub) {
            HalconCpp::HRegion subRegion = HalconCpp::HRegion(allSub).Union1();
            finalRegion = finalRegion.Difference(subRegion);
        }
    } else {
        // 如果没有任何加法，返回空区域
        finalRegion.GenEmptyRegion();
    }

    // --- 调试与验证 ---
    if (finalRegion.IsInitialized() && (long)finalRegion.CountObj() > 0) {
        HTuple area, r, c;
        // 注意：Halcon 12 的 AreaCenter 成员函数返回 area
        area = finalRegion.AreaCenter(&r, &c);
        qDebug() << QString(">>> 最终区域生成成功: 面积=%1, 中心=(%2, %3)")
                        .arg(area.D()).arg(c.D()).arg(r.D());
    } else {
        qDebug() << ">>> 警告: 最终合成区域为空";
    }

    return finalRegion;
}


//选择当前文件夹下的所有图片 2. 加载多张图到表格
void MainWindow::onSelectFiles(){
    // 1. 让用户选择文件夹
    QStringList files = QFileDialog::getOpenFileNames(this,"选择一个或多个文件","D:/","Images(*jpg *.png *.bmp)");// 默认起始路径
    if (files.isEmpty()) return;

    QTableWidget* table = ui->tableWidget;

    // 2. 遍历文件夹获取图片文件

    for(auto& pt : files){
        BatchData data;
        data.filePath = pt;
        data.isTested = false;
        m_batchList.append(data);/*存入队列*/

        int row = table->rowCount();
        table->insertRow(row);

        QFileInfo fileinfo(pt);
        QTableWidgetItem* nameItem = new QTableWidgetItem(fileinfo.fileName());
        nameItem->setToolTip(pt);


        table->setItem(row, 0, nameItem);
        table->setItem(row, 1, new QTableWidgetItem("待处理"));
        table->setItem(row, 2, new QTableWidgetItem("-"));

        qDebug() << "图" << row << "路径:" <<  pt;
    }

    table->setUpdatesEnabled(true);
    qDebug() << "已加载图片总数：" << files.size() <<
        "m_batchList.size() = " << m_batchList.size();
    // 4. 自动加载第一张图片（可选，提升用户体验）
    // m_ImgPath = files.first();
    // loadImage(m_ImgPath);
}


//点击按钮，获取文件下的所有图片
void MainWindow::handleImgPathSelect(){
    window.show();
}

void MainWindow::handleExportRegion(){
    HRegion finalRegion;
    finalRegion.GenEmptyObj();

    // 遍历场景中所有 ROI 项
    for (auto item : m_scene->items()) {
        auto roi = dynamic_cast<AbstractRoiItem*>(item);
        if (!roi) continue;

        HObject hObj = roi->toHalconRegion();
        HRegion hReg(hObj);

        // 假设我们在 AbstractRoiItem 里加了 isPositive() 接口
        if (roi->isPositive()) {
            finalRegion = finalRegion.Union2(hReg);
        } else {
            finalRegion = finalRegion.Difference(hReg);
        }
    }

    // 此时 finalRegion 就是红框里所有操作叠加后的结果
    // 可以用于后续的 set_shape_model_clutter 或 reduce_domain
}

// 3. 执行批量匹配 发送信号给工作线程
void MainWindow::onButtonClicked_DetectAllImages(){
    if(m_batchList.isEmpty() || m_isCalculating) return;

    m_isCalculating = true;
    ui->pushButton_DetectionAll->setEnabled(false);

    GetCurrentParams(); // 更新最新参数

    QStringList paths;
    // 禁用界面更新，防止大批量数据更新时闪烁
    ui->tableWidget->setUpdatesEnabled(false);

    for(int i = 0; i < m_batchList.size(); ++i){
        paths << m_batchList[i].filePath;

        // --- 改进 1：重置内存数据状态 ---
        m_batchList[i].isTested = false;
        // 关键：清空旧的 Halcon 轮廓对象，释放引用计数
        m_batchList[i].result.modelContours.Clear();
        m_batchList[i].result.found = false;

        // --- 改进 2：UI 性能优化 (复用 Item 而非 new) ---
        QTableWidgetItem *statusItem = ui->tableWidget->item(i, 1);
        if(!statusItem) {
            statusItem = new QTableWidgetItem();
            ui->tableWidget->setItem(i, 1, statusItem);
        }
        statusItem->setText("Testing...");
        statusItem->setBackground(Qt::yellow);
        statusItem->setForeground(Qt::black);

        // 如果有得分列，也重置一下
        QTableWidgetItem *scoreItem = ui->tableWidget->item(i, 2);
        if(scoreItem) scoreItem->setText("-");
    }
    ui->tableWidget->setUpdatesEnabled(true);

    emit requestBatchMatch(paths, m_currentParams);
}


// 接收 Worker 回传的单张图匹配结果
void MainWindow::handleBatchResult(QTableWidgetItem *item) {
    // 1. 更新内存数据
    int row = item->row();
    if (row < 0 || row >= m_batchList.size()) return;

    // A. 切换图片（随用随读，不占内存）
    loadImage(m_batchList[row].filePath);

    // B. 直接从缓存显示结果（零延迟）
    if (m_batchList[row].isTested && m_batchList[row].result.found) {
        m_resultItem->setResult(m_batchList[row].result);
        m_resultItem->setVisible(true);
    } else {
        m_resultItem->setVisible(false);
    }
}

void MainWindow::printThreadLogInfo(QString Text){
    qDebug() << Text;
}


//====================== ====== ======================

//适应QGraphicsView窗口
void MainWindow::FitImage(){
    // 安全检查：如果场景是空的或者没有图片，不执行
    if (!m_scene || !m_imgItem || m_imgItem->pixmap().isNull())
        return;

    // 强制左上角对齐（在工业视觉中通常比居中更好计算坐标）
    //m_view->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    // 1. 重置所有缩放和旋转变换（回到 100% 状态）
    m_view->resetTransform();

    // 2. 自动计算比例，让图片填满当前视图窗口
    m_view->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
}

//重置尺寸事件
void MainWindow::resizeEvent(QResizeEvent* event){
    QMainWindow::resizeEvent(event);
    if (m_imgItem && !m_imgItem->pixmap().isNull()) {
        FitImage();
    }
}

//=================== PushButton ===================
void MainWindow::contrastLHAutoSelect(){
    try{
        HImage img(m_ImgPath.toLocal8Bit().data());
        HRegion roi = generateFinalRegion();

        HTuple hv_ParamName, hv_ParamValue;

        // 执行自动确定算子
        DetermineShapeModelParams(img.ReduceDomain(roi), "auto", 0, 360.0*M_PI/180.0,
                                  0.9, 1.1, "auto", "use_polarity", "auto", "auto", "all",
                                  &hv_ParamName, &hv_ParamValue);

        // 更新 UI 控件，控件会自动触发 triggerUpdate
        ui->sliderContrastLow->setValue(hv_ParamValue[0].I());
        ui->sliderContrastHigh->setValue(hv_ParamValue[1].I());
    }catch(HException& e){
        qDebug() << "自动选择对比度失败：" << e.ErrorMessage();
    }
}


void MainWindow::loadStyleSheet(){
    QFile file(":/Qss/Qss/style.qss");
    if(file.open(QFile::ReadOnly)){
        QString qss = file.readAll();
        qApp->setStyleSheet(qss);
    }
    else{
        qDebug() << "加载qss文件失败";
    }
}


