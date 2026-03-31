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


enum class  UserRole{
    Operator = 0,   //产线工人：仅限 启动/停止/查看结果
    Engineer = 1,   //维护工程师：可调对比度、得分、阈值
    Developer = 2   //开发者：
};


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

using IntBinding = ParamBinding<QSlider, QSpinBox, int>;
using DoubleBinding = ParamBinding<QSlider, QDoubleSpinBox, double>;

struct BatchData{
    QString filePath;
    QVector<MatchResult> result;
    bool isTested  = false;
};


enum ControlEdge { MinEdge, MaxEdge };


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

    void setRoisVisible(bool visdible);//是否隐藏模板ROI

private://私有接口

    void initConnections();
    void initRoiSystem(); // 初始化工具栏和连接


    void loadImage(const QString& path);
    void onSelectFiles();

    void initParamConnections(); //Halcon算子参数 与 控件 链接

    void handleContrastChange(QWidget* source, int val, bool isLow);
    void handleScalingChange(ControlEdge edge, double val, bool isRow);

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

    double m_cutImgWidth;//裁剪图片的宽高
    double m_cutImgHeight;
    int m_pairCount;

private://自带库私有变量
    QWidget window;

private://自定义私有变量
    BatchData m_templateResult;//初始结果
    QString m_ImgPath;
    QStringList m_ImgFolderPath;
    QString m_lastOpenPath;
    QString m_BatchFloderPath;//批量图片文件夹路径

    QVector<IntBinding> m_intBindings;
    QVector<DoubleBinding> m_doubleBindings;

    QList<TestImage> m_testImages;
    QVector<BatchData> m_batchList;
    UserRole m_currentRole;

    QString m_configIni;
    QString m_curExePath;

private slots:
    void handleImageLoad();

    void handleImgPathSelect();

    void handleExportRegion();

    void triggerUpdate();

    void updateUIparams();

    void batchMatchResult(int index,QVector<MatchResult> res);

    void contrastLHAutoSelect();

    void onButtonClicked_DetectAllImages();

    void printThreadLogInfo(QString Text);

    void updateUserPermissions(UserRole role);

    void on_comboxUserRole_activated(int index);

    void on_pushbutton_changePassword();

    void on_pushbutton_OutModelFile();
    void on_pushButton_saveAll_clicked();

    void onTableCellClicked(int row,int col);
    void on_pushButton_2_clicked();

public:
    void initPasswordCofigure();
    QString hashPassword(const QString& plainText);
    void changePassword(UserRole role,const QString& newPassword);

    void loadParamersFromIni();
    void saveParamersToIni();

    QString GetFileDirectory(QString& filePath);  //获取文件目录
    void AffineCutting(HalconCpp::HImage& srcImg, const QString& filePath,const MatchResult& result,const double& width,const double& height);//仿射裁剪

private:
    Ui::MainWindow *ui;

signals:
    void requestTrain(const QString& path, const HalconCpp::HObject& region, const MatchParams& params);

    void requestBatchMatch(const QStringList& imgPaths,const MatchParams& p);

    void requestSaveModel(const QString& path);
};
#endif // MAINWINDOW_H
