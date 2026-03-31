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
#include <QList>
#include <QVariant>
#include <QCryptographicHash>
#include <QSettings>
#include <QInputDialog>
#include <QDateTime>

#include <vector>

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

    m_configIni = "config.ini";
    m_curExePath = QDir::currentPath();

    loadStyleSheet();
    qRegisterMetaType<QVector<MatchResult>>("QVector<MatchResult>");

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

    initPasswordCofigure();//初始化ini配置
    loadParamersFromIni();

    //ui->tabWidget->removeTab(3);
    //ui->tabWidget->removeTab(4);

}

MainWindow::~MainWindow()
{
    GetCurrentParams();
    saveParamersToIni();
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

    // 切换页隐藏ROI
    connect(ui->tabWidget,&QTabWidget::currentChanged,this,[this](int index){
        qDebug() << "点击第几页：" << index;
        if(index == 2)
            setRoisVisible(false);
        else if(index == 0){
            //if()
            m_resultItem->clearContour();
            loadImage(m_ImgPath);
            setRoisVisible(true);// 回到参数页，重新显示 ROI 供用户调节
            m_resultItem->setResult(m_currentResult,false);
        }
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
            m_resultItem->setResult(res,false);
            m_resultItem->setLabel(QString("%1 ms").arg(res.time, 0, 'f', 1));
            m_currentResult = res;
        }else {
            m_resultItem->setVisible(false);
            qDebug() << "匹配失败：特征不足或得分太低";
        }
    });



    // 连接：子线程结果 -> 主线程更新 单张图片匹配结果
    connect(m_worker,&HalconWorker::batchRowReady,this,&MainWindow::batchMatchResult);


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

void MainWindow::handleContrastChange(QWidget* source, int val, bool isLow){
    const QList<QWidget*> widgets = {
        ui->spinContrastLow,    ui->sliderContrastLow,
        ui->spinContrastHigh,   ui->sliderContrastHigh
    };

    for(auto w : widgets) w->blockSignals(true);

    if(isLow) m_currentParams.contrastLow = val;
    else m_currentParams.contrastHigh = val;

    if(m_currentParams.contrastLow > m_currentParams.contrastHigh){
        if(isLow) {
            m_currentParams.contrastHigh = m_currentParams.contrastLow;
        }
        else{
            m_currentParams.contrastLow = m_currentParams.contrastHigh;
        }
    }

    ui->spinContrastLow->setValue(m_currentParams.contrastLow);
    ui->sliderContrastLow->setValue(m_currentParams.contrastLow);
    ui->spinContrastHigh->setValue(m_currentParams.contrastHigh);
    ui->sliderContrastHigh->setValue(m_currentParams.contrastHigh);

    for(auto w : widgets) w->blockSignals(false);
}

void MainWindow::handleScalingChange(ControlEdge edge, double val, bool isRow) {
    // 1. 屏蔽信号
    const QList<QWidget*> widgets = {
        ui->doubleSpinBox_RowMinScale, ui->doubleSpinBox_RowMaxScale,
        ui->doubleSpinBox_ColMinScale, ui->doubleSpinBox_ColMaxScale,
        ui->sliderMinScalingRow, ui->sliderMaxScalingRow,
        ui->sliderMinScalingColumn, ui->sliderMaxScalingColumn
    };
    for(auto w : widgets) w->blockSignals(true);

    bool isMin = (edge == MinEdge);

    // 2. 第一阶段：赋值当前操作维度的变量
    if (isRow) {
        if (isMin) m_currentParams.scaleRMin = val;
        else m_currentParams.scaleRMax = val;
    } else {
        if (isMin) m_currentParams.scaleCMin = val;
        else m_currentParams.scaleCMax = val;
    }

    // 3. 第二阶段：处理【Min <= Max】逻辑 (推力逻辑)
    // 必须在行列同步之前处理，保证数据的合法性基准
    if (isRow) {
        if (m_currentParams.scaleRMin > m_currentParams.scaleRMax) {
            if (isMin) m_currentParams.scaleRMax = m_currentParams.scaleRMin;
            else m_currentParams.scaleRMin = m_currentParams.scaleRMax;
        }
    } else {
        if (m_currentParams.scaleCMin > m_currentParams.scaleCMax) {
            if (isMin) m_currentParams.scaleCMax = m_currentParams.scaleCMin;
            else m_currentParams.scaleCMin = m_currentParams.scaleCMax;
        }
    }

    // 4. 第三阶段：处理【行列同步】
    if (ui->checkLockScale->isChecked()) {
        if (isRow) {
            m_currentParams.scaleCMin = m_currentParams.scaleRMin;
            m_currentParams.scaleCMax = m_currentParams.scaleRMax;
        } else {
            m_currentParams.scaleRMin = m_currentParams.scaleCMin;
            m_currentParams.scaleRMax = m_currentParams.scaleCMax;
        }
    }

    // 5. 第四阶段：刷新 UI
    auto refresh = [](QDoubleSpinBox* sb, QSlider* sd, double v) {
        if (sb->value() != v) sb->setValue(v);
        int sVal = qRound(v * 100.0);
        if (sd->value() != sVal) sd->setValue(sVal);
    };

    refresh(ui->doubleSpinBox_RowMinScale, ui->sliderMinScalingRow, m_currentParams.scaleRMin);
    refresh(ui->doubleSpinBox_RowMaxScale, ui->sliderMaxScalingRow, m_currentParams.scaleRMax);
    refresh(ui->doubleSpinBox_ColMinScale, ui->sliderMinScalingColumn, m_currentParams.scaleCMin);
    refresh(ui->doubleSpinBox_ColMaxScale, ui->sliderMaxScalingColumn, m_currentParams.scaleCMax);

    for(auto w : widgets) w->blockSignals(false);
}

//绑定QSlider与QSPinBox ,并进行value联动
void MainWindow::initParamConnections(){
    //初始化参数
    m_intBindings = QVector<IntBinding>{
        IntBinding(ui->sliderComponentSizeMin,  ui->spinComponentSizeMin, &m_currentParams.minComponentSize),
        IntBinding(ui->sliderPyramidLevel,      ui->spinPyramidLevel,     &m_currentParams.pyramidLevel),
        IntBinding(ui->sliderMaxMatchNum,       ui->spinBoxMaxMatchNum,   &m_currentParams.matchNum)
    };

    m_doubleBindings = QVector<DoubleBinding>{
        DoubleBinding(ui->sliderAngleStart,        ui->doubleSpinBox_AngleStart,   &m_currentParams.angleStart,   1.0),
        DoubleBinding(ui->sliderAngleExtent,       ui->doubleSpinBox_AngleExtent,  &m_currentParams.angleExtent,  1.0),
        DoubleBinding(ui->sliderMinMatchScore,     ui->doubleSpinBoxMinMatchScore, &m_currentParams.minScore,     100.0)
    };

    m_paramTimer = new QTimer(this);
    m_paramTimer->setSingleShot(true);

    // 每当参数改变时，启动/重启定时器
    connect(m_paramTimer,&QTimer::timeout,this,&MainWindow::triggerUpdate);

    // 绑定每对 spinbox和slider
    for(auto& b : m_intBindings){
        auto applyValue = [this, b](int val){
            if(*b.target == val) return;
            *b.target = val;

            QSignalBlocker blocker1(b.slider);
            QSignalBlocker blocker2(b.spinbox);

            b.slider->setValue(val);
            b.spinbox->setValue(val);
            m_paramTimer->start(100);
        };

        connect(b.spinbox, QOverload<int>::of(&QSpinBox::valueChanged), this, applyValue);
        connect(b.slider, &QSlider::valueChanged, this, applyValue);
    }

    // 绑定每对 doublespinbox和slider

    for(auto& b : m_doubleBindings){
        auto applyValue = [this,b](double val){
            if(qAbs(*b.target-val) < (0.1/b.scale)) return;

            *b.target = val;

            QSignalBlocker blocker1(b.slider);
            QSignalBlocker blocker2(b.spinbox);

            b.slider->setValue(qRound(val*b.scale));
            b.spinbox->setValue(val);

            m_paramTimer->start(100);
        };
        connect(b.spinbox,QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, applyValue);
        connect(b.slider,&QSlider::valueChanged, this,[=](int val){
            double dv = val / b.scale;
            applyValue(dv);
        });
    }

    auto bindIntScaling = [this](QSpinBox* sb,QSlider* sd, bool isContrastLow){
        auto func = [this,isContrastLow](int val){
            this->handleContrastChange(nullptr,val,isContrastLow);// handle内部应处理UI同步
            m_paramTimer->start(100);
        };

        connect(sb,QOverload<int>::of(&QSpinBox::valueChanged),func);
        connect(sd,&QSlider::valueChanged,func);
    };

    bindIntScaling( ui->spinContrastLow,ui->sliderContrastLow,true);
    bindIntScaling( ui->spinContrastHigh,ui->sliderContrastHigh,false);

    auto bindScaling = [this](QDoubleSpinBox* sb, QSlider* sd, ControlEdge type, bool isRow){
        // 注意：这里去掉了硬编码 100.0，建议在 handleScalingChange 内部统一比例
        auto func = [this, type, isRow, sb, sd](double v){
            this->handleScalingChange(type, v, isRow);
            m_paramTimer->start(100);
        };

        connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), func);
        connect(sd, &QSlider::valueChanged, [=](int v){
            func((double)v / 100.0); // 假设缩放比例统一为 100
        });
    };

    bindScaling(ui->doubleSpinBox_RowMinScale,ui->sliderMinScalingRow,ControlEdge::MinEdge,true);
    bindScaling(ui->doubleSpinBox_RowMaxScale,ui->sliderMaxScalingRow,ControlEdge::MaxEdge,true);
    bindScaling(ui->doubleSpinBox_ColMinScale,ui->sliderMinScalingColumn,ControlEdge::MinEdge,false);
    bindScaling(ui->doubleSpinBox_ColMaxScale,ui->sliderMaxScalingColumn,ControlEdge::MaxEdge,false);
}


//连接信号与槽
void MainWindow::initConnections(){
    // 添加ROi,触发更新信号
    connect(m_view,&RoiGraphicsView::roiAdded,this,[this](AbstractRoiItem* item){
        connect(item,&AbstractRoiItem::roiMove,this,&MainWindow::triggerUpdate);
    });

    // 只有右键确认 ROI 后才触发算法（防止频繁创建模型导致卡顿）
    connect(m_view,&RoiGraphicsView::itemFinished,this,[=](){
        triggerUpdate();
        updateUIparams();
    });

    // 处理 ROI 结束后的工具栏状态
    connect(m_view, &RoiGraphicsView::itemFinished, m_roiToolbar, &RoiToolbar::resetShapeButtons);

    // 按钮点击：只负责获取路径并调用加载函数
    connect(ui->pushButton, &QPushButton::clicked, this,&MainWindow::handleImageLoad);

    // 获取文件夹图片逻辑
    connect(ui->pushButton_ImgsPath, &QPushButton::clicked, this, &MainWindow::onSelectFiles);

    //点击 自动计算对比度
    connect(ui->ContrastLHAuto_Button,&QPushButton::clicked,this,&MainWindow::contrastLHAutoSelect);

    // 点击表格单元格显示图片和匹配结果
    connect(ui->tableWidget,&QTableWidget::cellClicked,this,&MainWindow::onTableCellClicked);

    // 检测所有图片
    connect(ui->pushButton_DetectionAll,&QPushButton::clicked,this,&MainWindow::onButtonClicked_DetectAllImages);

    // 打印workerThread的信息
    connect(m_worker,&HalconWorker::threadLogInfo,this,&MainWindow::printThreadLogInfo);

    //保存Model文件
    connect(ui->pushButton_OutModelFile,&QPushButton::clicked,this,&MainWindow::on_pushbutton_OutModelFile);

    connect(this, &MainWindow::requestSaveModel,m_worker,&HalconWorker::saveModel);
}

// connect table和clicked
void MainWindow::onTableCellClicked(int row,int col){

    if(m_batchList.empty()) return; //不能在这判断，不然一开没有链接
    //qDebug() << "点击" << row << "行单元格";
    if (row < 0 || row >= m_batchList.size())
        return;

    BatchData &data = m_batchList[row];
    // 1. 加载图片
    loadImage(data.filePath);
    // 2. 恢复显示该图的匹配结果
    if (data.isTested) {
        //m_scene->clear();
        m_resultItem->setResult(data.result,true);
        m_resultItem->setVisible(true);
    } else {
        m_resultItem->setVisible(false);
    }
}

void MainWindow::initUICtrl(){
    ui->tableWidget->setColumnCount(3);
    ui->tableWidget->setHorizontalHeaderLabels({"名称","看的见的","检测到的"});
    ui->tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
}


void MainWindow::updateUserPermissions(UserRole role){
    //权限对应数值：Operator=0，Engineer=1，Developer=2
    int currnetUserLevel = static_cast<int>(role);

    auto getLevelNum = [](QString levelStr)->int{
        if(levelStr == "engineer") return 1;
        if(levelStr == "developer") return 2;
        return 0;
    };

    //找到所有子控件
    QList<QWidget*> allWidgets = this->findChildren<QWidget*>();
    for(QWidget* w : allWidgets){
        QVariant levelProp = w->property("level");

        if(levelProp.isValid()){
            int widgetRequiredLevel = getLevelNum(levelProp.toString());

            if(currnetUserLevel < widgetRequiredLevel){
                if(widgetRequiredLevel == static_cast<int>(UserRole::Engineer))
                    w->setEnabled(false); //A:禁用控件
                else
                    w->setVisible(false);//B.彻底隐藏
            }
            else{
                w->setEnabled(true);
                w->setVisible(true);
            }
        }
    }

    // 特殊处理：比如 TabWidget 的某页只给 Developer 看
    // ui->tabWidget->setTabVisible(1,currnetUserLevel >=1);// 参数页给工程师以上
    // ui->tabWidget->setTabVisible(2,currnetUserLevel >=2);// 高级调试给开发人员
}



//不同用户切换密码
//初始化配置
void MainWindow::initPasswordCofigure(){
    QSettings settings("config.ini",QSettings::IniFormat);
    if(!settings.contains("Passwords/Engineer")){
        settings.setValue("Passwords/Engineer",hashPassword("123"));
    }
    if(!settings.contains("Passwords/Developer")){
        settings.setValue("Passwords/Developer",hashPassword("admin"));
    }

    connect(ui->comboBox_User,QOverload<int>::of(&QComboBox::activated),this,&MainWindow::on_comboxUserRole_activated);
    connect(ui->pushButton_ChangedPassword,&QPushButton::clicked,this,&MainWindow::on_pushbutton_changePassword);
    updateUserPermissions(UserRole::Operator);
}

QString MainWindow::hashPassword(const QString& plainText){
    QByteArray data = plainText.toUtf8();
    QByteArray hash = QCryptographicHash::hash(data,QCryptographicHash::Sha256);
    return QString(hash.toHex());
}

void MainWindow::on_comboxUserRole_activated(int index){
    UserRole selectedRole = static_cast<UserRole>(index);

    // 1. 如果用户选择的是当前已经登录的角色，直接退出
    if (selectedRole == m_currentRole) return;

    // 2. 如果切回操作员 (Operator)，无需密码直接切换
    if (selectedRole == UserRole::Operator) {
        m_currentRole = UserRole::Operator;
        updateUserPermissions(m_currentRole);
        QMessageBox::information(this, "提示", "已切回操作员权限");
        return;
    }

    // 3. 弹出密码输入框
    bool ok;
    QString roleName = (selectedRole == UserRole::Engineer) ? "工程师" : "开发人员";

    // 界面美化：根据角色弹出不同的标题
    QString password = QInputDialog::getText(this, "权限验证",
                                             QString("请输入%1密码:").arg(roleName),
                                             QLineEdit::Password, "", &ok);

    if (ok && !password.isEmpty()) {
        // 4. 验证密码 (从 INI 文件读取哈希对比)
        QSettings settings("config.ini", QSettings::IniFormat);
        QString key = (selectedRole == UserRole::Engineer) ? "Passwords/Engineer" : "Passwords/Developer";
        QString savedHash = settings.value(key).toString();

        if (hashPassword(password) == savedHash) {
            // 验证通过
            m_currentRole = selectedRole;
            updateUserPermissions(m_currentRole);
            QMessageBox::information(this, "成功", QString("权限已切换至 %1").arg(roleName));
        } else {
            // 验证失败
            QMessageBox::critical(this, "错误", "密码错误，验证未通过！");
            // 关键：把 ComboBox 的显示索引重置回原来的角色
            ui->comboBox_User->setCurrentIndex(static_cast<int>(m_currentRole));
        }
    } else {
        // 用户点了取消或者没输密码
        ui->comboBox_User->setCurrentIndex(static_cast<int>(m_currentRole));
    }
}

void MainWindow::on_pushbutton_changePassword(){
    if (m_currentRole == UserRole::Operator) {
        QMessageBox::warning(this, "权限不足", "操作员无权修改密码！");
        return;
    }

    QStringList items;
    items << "工程师" << "开发人员";

    bool ok;
    QString item = QInputDialog::getItem(this,"更改密码",QString("请选择要修改的角色"),items,0,false,&ok);
    if(!ok || item.isEmpty()) return;



    UserRole role = (item == "工程师") ? UserRole::Engineer : UserRole::Developer;
    QString roleName = (role == UserRole::Engineer) ? "工程师" : "开发人员";
    QString newPassword1 = QInputDialog::getText(this,"更改密码",
                                                QString("请输入%1的新密码").arg(roleName),
                                                QLineEdit::Password,"",&ok);

    QString newPassword2 = QInputDialog::getText(this,"更改密码",
                                                 QString("请再次输入%1的新密码").arg(roleName),
                                                 QLineEdit::Password,"",&ok);


    if(ok && (!newPassword1.isEmpty() && !newPassword2.isEmpty()) && (newPassword1 == newPassword2)){
        changePassword(role,newPassword2);
    }
    else if(ok && (newPassword1.isEmpty() || newPassword2.isEmpty())){
        QMessageBox::warning(this,"提示","密码不能为空!");
    }
}

void MainWindow::changePassword(UserRole role,const QString& newPassword){
    if(m_currentRole != UserRole::Developer){
        QMessageBox::information(this,"权限不足","只有开发人员可以修改密码");
        return;
    }

    QSettings setting("config.ini",QSettings::IniFormat);
    QString roleKey = (role == UserRole::Engineer) ? "Passwords/Engineer" : "Passwords/Developer";

    setting.setValue(roleKey,hashPassword(newPassword));
    setting.sync();
    QMessageBox::information(this,"成功","修改密码成功");
}

void MainWindow::loadParamersFromIni(){
    QSettings setting(m_configIni,QSettings::IniFormat);
    setting.beginGroup("MatchParams");
    m_currentParams.contrastLow = setting.value("ContrastLow",m_currentParams.contrastLow).toInt();
    m_currentParams.contrastHigh = setting.value("ContrastHigh",m_currentParams.contrastHigh).toInt();
    m_currentParams.minComponentSize = setting.value("MinComponentSize",m_currentParams.minComponentSize).toInt();
    m_currentParams.pyramidLevel = setting.value("PyramidLevel",m_currentParams.pyramidLevel).toInt();
    m_currentParams.matchNum = setting.value("MaxMatchNum",m_currentParams.matchNum).toInt();

    m_currentParams.angleStart = setting.value("AngleStart",m_currentParams.angleStart).toDouble();
    m_currentParams.angleExtent = setting.value("AngleExtent",m_currentParams.angleExtent).toDouble();
    m_currentParams.scaleRMin = setting.value("ScaleRMin",m_currentParams.scaleRMin).toDouble();
    m_currentParams.scaleRMax = setting.value("ScaleRMax",m_currentParams.scaleRMax).toDouble();
    m_currentParams.scaleCMin = setting.value("ScaleCMin",m_currentParams.scaleCMin).toDouble();
    m_currentParams.scaleCMax = setting.value("ScaleCMax",m_currentParams.scaleCMax).toDouble();
    m_currentParams.minScore = setting.value("MinScore",m_currentParams.minScore).toDouble();
    setting.endGroup();


    setting.beginGroup("FilePath");
    m_ImgPath = setting.value("ImgFile","").toString();
    if(!m_ImgPath.isEmpty()) {
        loadImage(m_ImgPath);
    }
    m_lastOpenPath = setting.value("LastOpenPath","").toString();
    m_BatchFloderPath = setting.value("BatchFloderPath","").toString();
    setting.endGroup();

    setting.beginGroup("CutParamers");
    m_cutImgWidth = setting.value("CutImgWidth","0.0").toDouble();
    m_cutImgHeight = setting.value("CutImgHeight","0.0").toDouble();
    m_cutRotAngle = setting.value("CutImgRotAngle","0.00").toDouble();
    m_pairCount = setting.value("PairCount","0").toInt();
    setting.endGroup();

    updateUIparams();
}

void MainWindow::saveParamersToIni(){
    //创建打开ini文件
    QSettings setting(m_configIni,QSettings::IniFormat);

    setting.beginGroup("MatchParams");
    //整形参数
    setting.setValue("ContrastLow",m_currentParams.contrastLow);
    setting.setValue("ContrastHigh",m_currentParams.contrastHigh);
    setting.setValue("MinComponentSize",m_currentParams.minComponentSize);
    setting.setValue("PyramidLevel",m_currentParams.pyramidLevel);
    setting.setValue("MaxMatchNum",m_currentParams.matchNum);
    //浮点型参数
    setting.setValue("AngleStart",m_currentParams.angleStart);
    setting.setValue("AngleExtent",m_currentParams.angleExtent);
    setting.setValue("ScaleRMin",m_currentParams.scaleRMin);
    setting.setValue("ScaleRMax",m_currentParams.scaleRMax);
    setting.setValue("ScaleCMin",m_currentParams.scaleCMin);
    setting.setValue("ScaleCMax",m_currentParams.scaleCMax);
    setting.setValue("MinScore",m_currentParams.minScore);
    setting.endGroup();

    setting.beginGroup("FilePath");
    //setting.setValue("MatchModelFile/ModelFile",ui->Img_lineEdit->text());
    setting.setValue("ImgFile",m_ImgPath);
    setting.setValue("LastOpenPath",m_lastOpenPath);
    setting.setValue("BatchFloderPath",m_BatchFloderPath);
    setting.endGroup();

    setting.beginGroup("CutParamers");
    setting.setValue("PairCount",m_pairCount);
    setting.setValue("CutImgWidth",m_cutImgWidth);
    setting.setValue("CutImgHeight",m_cutImgHeight);
    setting.setValue("CutImgRotAngle",m_cutRotAngle);
    setting.endGroup();

    qDebug() << "参数已保存到ini中";
}

//======================================================
// ====================== 业务逻辑 ======================


void MainWindow::batchMatchResult(int index,QVector<MatchResult> res){
    m_batchList[index].result = res;
    m_batchList[index].isTested = true;

    // 更新表格对应行
    bool ret = false;
    for(int i = 0;i < res.size(); ++i){
        if(res[i].found == true){
            ret = true;
        }
    }
    ui->tableWidget->item(index, 1)->setText(ret ? QString::number(res[0].score,'f',2) : "NG");
    ui->tableWidget->item(index, 1)->setBackground(ret ? Qt::green : Qt::red);
    ui->tableWidget->item(index,2)->setText(QString::number(res.size()));
    //handleBatchResult(ui->tableWidget->item(index,1));
}


//隐藏ROI
void MainWindow::setRoisVisible(bool visible){
    QList<QGraphicsItem*> allItems = m_scene->items();
    for(auto& item : allItems){
        // 只有 ROI 项受影响，图片项（m_imgItem）和结果项（m_resultItem）不受影响
        if(auto roi = dynamic_cast<AbstractRoiItem*>(item))
            roi->setVisible(visible);
    }
}


//点击按钮,获取制作模板的图片
void MainWindow::handleImageLoad()
{
    // 按钮点击：只负责打开对话框，获取路径
    QString path = QFileDialog::getOpenFileName(this, "选择模板图片文件",
                        m_lastOpenPath, "所有图片(*.png *.bmp *.jpg)");
    if (!path.isEmpty()) {
        m_ImgPath = path;
        this->loadImage(m_ImgPath); // 统一调用

        ui->Img_lineEdit->setText(path);

        m_lastOpenPath = QFileInfo(path).absolutePath();//获取文件目录部分
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


void MainWindow::updateUIparams(){
    // 暂时阻塞所有控件信号，防止加载时触发 triggerUpdate
    // 因为 initParamConnections 已经建立了连接
    const QList<QWidget*> allWidgets = this->findChildren<QWidget*>();
    for(auto *w : allWidgets) w->blockSignals(true);

    ui->spinContrastLow->setValue(m_currentParams.contrastLow);
    ui->spinContrastHigh->setValue(m_currentParams.contrastHigh);
    ui->spinComponentSizeMin->setValue(m_currentParams.minComponentSize);
    ui->spinPyramidLevel->setValue(m_currentParams.pyramidLevel);

    ui->doubleSpinBox_AngleStart->setValue(m_currentParams.angleStart);
    ui->doubleSpinBox_AngleExtent->setValue(m_currentParams.angleExtent);
    ui->doubleSpinBox_RowMinScale->setValue(m_currentParams.scaleRMin);
    ui->doubleSpinBox_RowMaxScale->setValue(m_currentParams.scaleRMax);
    ui->doubleSpinBox_ColMinScale->setValue(m_currentParams.scaleCMin);
    ui->doubleSpinBox_ColMaxScale->setValue(m_currentParams.scaleCMax);

    ui->doubleSpinBoxMinMatchScore->setValue(m_currentParams.minScore);
    ui->spinBoxMaxMatchNum->setValue(m_currentParams.matchNum);


    ui->sliderContrastLow->setValue(m_currentParams.contrastLow);
    ui->sliderContrastHigh->setValue(m_currentParams.contrastHigh);
    ui->sliderComponentSizeMin->setValue(m_currentParams.minComponentSize);
    ui->sliderPyramidLevel->setValue(m_currentParams.pyramidLevel);

    ui->sliderAngleStart->setValue(m_currentParams.angleStart);
    ui->sliderAngleExtent->setValue(m_currentParams.angleExtent);
    ui->sliderMinScalingRow->setValue(m_currentParams.scaleRMin*100);
    ui->sliderMaxScalingRow->setValue(m_currentParams.scaleRMax*100);
    ui->sliderMinScalingColumn->setValue(m_currentParams.scaleCMin*100);
    ui->sliderMaxScalingColumn->setValue(m_currentParams.scaleCMax*100);

    ui->sliderMinMatchScore->setValue(m_currentParams.minScore*100);
    ui->sliderMaxMatchNum->setValue(m_currentParams.matchNum);

    ui->lineEdit_W->setText(QString::number(m_cutImgWidth,'f',2));
    ui->lineEdit_H->setText(QString::number(m_cutImgHeight,'f',2));
    ui->lineEdit_Angle->setText(QString::number(m_cutRotAngle,'f',3));

    for(auto *w : allWidgets) w->blockSignals(false);
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

    m_currentParams.minScore = ui->doubleSpinBoxMinMatchScore->value();
    m_currentParams.matchNum = ui->spinBoxMaxMatchNum->value();

    m_cutImgWidth = ui->lineEdit_W->text().toDouble();
    m_cutImgHeight = ui->lineEdit_H->text().toDouble();
    m_cutRotAngle = ui->lineEdit_Angle->text().toDouble();

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
    for (auto& item : allItems) {
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
    QStringList files = QFileDialog::getOpenFileNames(this,"选择一个或多个文件",m_BatchFloderPath,"Images(*jpg *.png *.bmp)");// 默认起始路径
    if (files.isEmpty()) return;
    m_BatchFloderPath = GetFileDirectory(files[0]);

    QTableWidget* table = ui->tableWidget;

    // 2. 遍历文件夹获取图片文件
    table->setUpdatesEnabled(false);//先别刷新UI，遍历完之后进行统一刷新

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
        for(auto& p : m_batchList[i].result) {
            p.modelContours.Clear();
            p.found = false;
        }

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


// 接收 Worker 回传的单张图匹配结果  目前没用到，不用管
void MainWindow::handleBatchResult(QTableWidgetItem *item) {
    // 1. 更新内存数据
    int row = item->row();
    if (row < 0 || row >= m_batchList.size()) return;

    // A. 切换图片（随用随读，不占内存）
    loadImage(m_batchList[row].filePath);

    // B. 直接从缓存显示结果（零延迟）
    for(auto& p : m_batchList[row].result)
    {
        if (m_batchList[row].isTested && p.found) {
            m_resultItem->setResult(p,true);
            m_resultItem->setVisible(true);
        } else {
            m_resultItem->setVisible(false);
        }
    }
}

void MainWindow::printThreadLogInfo(QString Text){
    qDebug() << Text;
}

//输出模板匹配文件
void MainWindow::on_pushbutton_OutModelFile(){

    QString shmPath = m_curExePath + "/" + QDateTime::currentDateTime().toString("yyyyMMdd_") + "template_match.shm";

    QString modelPath = QFileDialog::getSaveFileName(this,"导出模板",shmPath,"Halcon Model(*.shm)");
    if(modelPath.isEmpty()) return;

    if(!modelPath.endsWith(".shm")) modelPath += ".shm";

    emit requestSaveModel(modelPath);

    // 2. 保存参数配置文件 (给 你的业务逻辑/另一个软件 用)
    // 建议存为同名但后缀不同的 .ini
    QString configureName = modelPath.section('.',0,0) + ".ini";
    QSettings ini(configureName,QSettings::IniFormat);

    ini.beginGroup("MatchParams");
    // --- 建模参考参数 (用于追溯或另一个软件重新学习时使用) ---
    ini.setValue("ContrastLow",m_currentParams.contrastLow);
    ini.setValue("ContrastHigh",m_currentParams.contrastHigh);
    ini.setValue("MinComponentSize",m_currentParams.minComponentSize);
    ini.setValue("PyramidLevel",m_currentParams.pyramidLevel);
    ini.setValue("Metric", "use_polarity");                // 极性 (保持一致，否则黑白反转找不着)

    // --- 查找必备参数 (FindAnisoShapeModel 算子必须填写的) ---
    ini.setValue("AngleStart",m_currentParams.angleStart);
    ini.setValue("AngleExtent",m_currentParams.angleExtent);
    ini.setValue("ScaleRMin",m_currentParams.scaleRMin);
    ini.setValue("ScaleRMax",m_currentParams.scaleRMax);
    ini.setValue("ScaleCMin",m_currentParams.scaleCMin);
    ini.setValue("ScaleCMax",m_currentParams.scaleCMax);
    ini.setValue("MinScore",m_currentParams.minScore);

    // --- 缺少的核心执行参数 (决定性能和精度) ---
    ini.setValue("MaxMatchNum",m_currentParams.matchNum);   // 找几个？(1还是多个)
    ini.setValue("MaxOverlap", 0.5);                       // 最大重叠度 (防止一个目标找出来两个重叠的框)
    ini.setValue("SubPixel", "least_squares");             // 亚像素精度模式 (工业通常用这个)
    ini.setValue("Greediness", 0.9);                       // 贪婪度 (0.9速度快, 0.7更鲁棒)
    ini.endGroup();
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
        if(m_ImgPath.isEmpty()) return;

        HImage img(m_ImgPath.toLocal8Bit().data());
        HRegion roi = generateFinalRegion();

        HTuple hv_ParamName, hv_ParamValue;

        // 执行自动确定算子
        DetermineShapeModelParams(img.ReduceDomain(roi),
                                  "auto", 0, 360.0*M_PI/180.0,
                                  0.5, 2,// 建议使用你 UI 上的缩放范围
                                  "auto",
                                  "use_polarity",
                                  "auto",// 自动对比度
                                  "auto", // 自动最小组件
                                  "all",
                                  &hv_ParamName, &hv_ParamValue);


        int low = 30;  // 默认值
        int high = 60; // 默认值

        for(int i = 0; i < hv_ParamName.Length(); ++i){
            QString paramName = QString::fromStdString(hv_ParamName[i].S().Text());
            if(paramName == "contrast"){
                HTuple val = hv_ParamValue.TupleSelect(i);
                if(val.Length() == 1){
                    low = val[0].I();
                    high = low+20;// 如果只返回一个，人为给高阈值留间距
                }
                else if(val.Length() > 2){
                    low = val[0].I();
                    high = val[1].I();
                }
            }
            else if(paramName == "min_size"){
                int minSize = hv_ParamValue.TupleSelect(i);
                ui->spinComponentSizeMin->setValue(minSize);
            }
        }

        ui->sliderContrastLow->blockSignals(true);
        ui->sliderContrastHigh->blockSignals(true);
        // 更新 UI 控件，控件会自动触发 triggerUpdate
        ui->sliderContrastLow->setValue(low);
        ui->spinContrastLow->setValue(low);
        ui->spinContrastHigh->setValue(high);
        ui->sliderContrastHigh->setValue(high);

        ui->sliderContrastLow->blockSignals(false);
        ui->sliderContrastHigh->blockSignals(false);

        m_currentParams.contrastLow = low;
        m_currentParams.contrastHigh = high;
        triggerUpdate();
        qDebug() << "自动选择完成: Low=" << low << " High=" << high;

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


//获取文件目录
QString MainWindow::GetFileDirectory(QString& filePath){
    int pox = filePath.lastIndexOf("/");
    return filePath.left(pox+1);
}

//仿射裁剪
void MainWindow::AffineCutting(HImage& srcImg, const QString& filePath,const MatchResult& result,
                               const double& width,const double& height,const double& angle){
    try{
        double tmp_height = height, tmp_width = width, tmp_angle = angle;
        qDebug() << "width:" << tmp_width
                 << "height:" << tmp_height;

        HHomMat2D homMat;
        homMat.HomMat2dInvert();

        homMat = homMat.HomMat2dRotate(-result.angle + tmp_angle,result.row,result.col);
        homMat = homMat.HomMat2dTranslate(tmp_height/2 - result.row,tmp_width/2 - result.col);

        // 3. 裁剪
        HImage rectfiedImg = srcImg.AffineTransImageSize(homMat,"bilinear",tmp_width, tmp_height);
        if(!rectfiedImg.IsInitialized()){
            qDebug() << "裁剪结果为空";
            return;
        }

        static int a = 0;
        QString now = QDateTime::currentDateTime().toString("yyyyMMdd_HH-mm-ss-zzz_");

        QFileInfo info(filePath);
        QString outFile = info.path() + "/" + now + QString::number(++a) + "_" + info.fileName();

        QByteArray path = QFile::encodeName(outFile);
        rectfiedImg.WriteImage("jpg",0,path.constData());

        if(!QFile::exists(outFile))
            qDebug() << outFile << ":写入失败";
        else
            qDebug() << outFile << ":写入成功";
    }
    catch(HalconCpp::HException &except){
        // 这是最关键的：它会告诉你哪个算子报错，错误代码是多少
        QString Identify = except.ProcName().Text(); // 报错的算子名
        QString ErrorMsg = except.ErrorMessage().Text(); // 报错的具体原因
        int ErrorCode = except.ErrorCode(); // 错误代码

        qDebug() << "Halcon Error in" << Identify
                 << ":" << ErrorMsg
                 << "(Code:" << ErrorCode << ")";
    }
}

//保存所有裁剪后的图片
void MainWindow::on_pushButton_saveAll_clicked()
{
    if(m_batchList.isEmpty() || m_BatchFloderPath.isEmpty()) return;

    GetCurrentParams();

    QString Dir = QDir(m_BatchFloderPath).filePath("Cut/");//裁剪图片存放路径
    //判断路径是否存在，不存在则创建
    QDir dir;
    if(!dir.exists(Dir)){
        if(!dir.mkpath(Dir)){
            qDebug() << "目录创建失败";
        }
    }

    for(auto& matchData : m_batchList){
        for(auto& p : matchData.result){
            if(p.found){
                HImage img(matchData.filePath.toLocal8Bit().data());
                //1.普通裁剪
                // HRegion cropRegion;
                // double l1 = 600.0, l2 = 370.0;
                // cropRegion.GenRectangle2(matchData.result.row,matchData.result.col,matchData.result.angle,l1,l2);
                // HImage cropped = img.ReduceDomain(cropRegion).CropDomain();

                QString fileName = QFileInfo(matchData.filePath).fileName();
                fileName = Dir + fileName;
                //QString format = fileName.right(fileName.size() - fileName.indexOf(".")-1);
                // cropped.WriteImage(format.toLocal8Bit(),0,fileName.toLocal8Bit());

                //2.仿射裁剪，
                AffineCutting(img,fileName,p,m_cutImgWidth,m_cutImgHeight,m_cutRotAngle);
            }
        }
    }
    QMessageBox::information(this,"裁剪图片","所有裁剪图片保存完毕");
}




void MainWindow::on_pushButton_2_clicked()
{
    saveParamersToIni();
}

