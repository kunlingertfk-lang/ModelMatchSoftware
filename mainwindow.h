#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <opencv2/opencv.hpp>
#include <QVector>

#include "RotRectRoiItem.h"
#include "MatchResult.h"

namespace HalconCpp{
    class HRegion;
    class HImage;
}

class QGraphicsView; //前向声明
class QGraphicsScene;
class QGraphicsPixmapItem;
class QGraphicsRectItem;
class QThread;
class QSlider;
class QSpinBox;
class QDoubleSpinBox;
class QTableWidgetItem;

class RoiToolbar;
class RoiGraphicsView;
class MatchResultItem;
class HalconWorker;



QT_BEGIN_NAMESPACE
namespace Ui {
    class MainWindow;
}
QT_END_NAMESPACE

template<typename SliderT, typename SpinBoxT, typename ValueT>
struct ParamBinding{
    SliderT* slider = nullptr;
    SpinBoxT* spinbox = nullptr;
    ValueT* target = nullptr;

    double scale = 1.0;

    ParamBinding(SliderT* sl,SpinBoxT* sp,ValueT* t,double sc = 1.0)
        :slider(sl),spinbox(sp),target(t), scale(sc) {}
};

using IntBinding = ParamBinding< QSlider, QSpinBox, int>;
using DoubleBinding = ParamBinding<QSlider, QDoubleSpinBox, double>;

struct BatchData{
    QString filePath;
    MatchResult result;
    bool isTested  = false;
};





class MainWindow : public QMainWindow
{
    Q_OBJECT

public://函数接口
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void FitImage();

    void resizeEvent(QResizeEvent* event);

    HalconCpp::HRegion generateFinalRegion();

    void initAlgorithmThread();

    void handleBatchResult(QTableWidgetItem *item);

    void GetCurrentParams();// 获取当前控件参数

private://私有接口

    void initConnections();
    void initRoiSystem(); // 初始化工具栏和连接
    void setupConnect();

    void loadImage(const QString& path);
    void onSelectFiles();

    void initParamConnections(); //Halcon算子参数 与 控件 链接

    void loadStyleSheet();

    void initUICtrl();

private://内部控件私有变量
    // 关键组件
    RoiToolbar* m_roiToolbar = nullptr;
    RoiGraphicsView* m_view = nullptr; // UI文件中的 view 建议“提升”为此类

    QGraphicsScene* m_scene = nullptr;
    QGraphicsPixmapItem* m_imgItem = nullptr;
    RotRectRoiItem* m_roiItem = nullptr;

    // 不再需要单个 m_roiItem，因为现在有很多个
    QList<AbstractRoiItem*> m_roiList;

    MatchResultItem* m_resultItem = nullptr;


    QThread* m_workerThread;
    HalconWorker* m_worker;
    bool m_isCalculating = false; // 防抖标志

    MatchResult m_currentResult;
    MatchParams m_currentParams;
    QTimer* m_paramTimer; //用于防抖

private://自带库私有变量
    QWidget window;

private://自定义私有变量
    QString m_ImgPath;
    QStringList m_ImgFolderPath;

    QVector<IntBinding> m_intBindings;
    QVector<DoubleBinding> m_doubleBindings;

    QList<TestImage> m_testImages;
    QVector<BatchData> m_batchList;

private slots:
    void handleImageLoad();

    void handleImgPathSelect();

    void handleExportRegion();

    void triggerUpdate();

    void contrastLHAutoSelect();

    void onButtonClicked_DetectAllImages();

    void printThreadLogInfo(QString Text);

private:
    Ui::MainWindow *ui;

signals:
    void requestTrain(const QString& path, const HalconCpp::HObject& region, const MatchParams& params);

    void requestBatchMatch(const QStringList& imgPaths,const MatchParams& p);

};
#endif // MAINWINDOW_H
