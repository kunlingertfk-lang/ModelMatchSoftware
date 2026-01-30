#ifndef MATCHRESULT_H
#define MATCHRESULT_H


#include <QString>
#include <QMetaType>
#include <HalconCpp.h>

struct MatchResult{
    double row = 0, col = 0, angle = 0, score = 0, time = 0; // 算法执行耗时
    bool found = false;// 是否找到目标
    HalconCpp::HObject modelContours;// 变换后的轮廓
    QString errorMsg;// 错误信息
};

struct MatchParams{
    //建模参数
    int contrastLow = 30;
    int contrastHigh = 60;
    int minComponentSize = 10;
    int pyramidLevel = 0;//0 为 auto

    double angleStart = 0.0;
    double angleExtent = 360.0;

    //缩放参数
    double scaleRMin = 1.0, scaleRMax = 1.0;
    double scaleCMin = 1.0, scaleCMax = 1.0;

    //查找参数
    double minScore = 0.5;
    int matchNum = 0; //暂无意义
    int itemIndex = -1;
};


struct TestImage{
    QString path;   //图片路径
    QString absolutePath;
    bool isDetected = false;  //是否匹配过
    double score = 0.0;   //匹配得分
    // 存储结果坐标，点击表格行时能直接恢复显示
    double resultRow, resultCol, resultAngle;
};


// 【非常重要】注册元类型
// 只有注册了，Qt 的信号槽才能跨线程传递这个自定义结构体
Q_DECLARE_METATYPE(MatchResult)
Q_DECLARE_METATYPE(MatchParams)


#endif // MATCHRESULT_H
