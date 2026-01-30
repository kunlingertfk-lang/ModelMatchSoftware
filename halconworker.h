#ifndef HALCONWORKER_H
#define HALCONWORKER_H

#include <QObject>
#include <HalconCpp.h>
#include "MatchResult.h"

using namespace HalconCpp;



class HalconWorker : public QObject
{
    Q_OBJECT
public:
    explicit HalconWorker(QObject *parent = nullptr);
    ~HalconWorker();


private:
    HTuple m_hvModelID;


public slots:

    /**
     * @brief 1. 训练/创建模板
     * 由“参数页”调节 Slider 或点击“创建”按钮触发
     */
    void trainModel(const QString& imgPath, const HalconCpp::HObject& hRegion, const MatchParams& p);


    /**
     * @brief 2. 单张快速匹配
     * 用于用户手动切换图片或点击“当前图检测”时调用
     */
    // void matchSingle(const QString& imgPath, const MatchParams& p);

    /**
     * @brief 3. 批量匹配全部
     * 用于“应用页”点击“检测所有”按钮。内部循环调用模型句柄，不重复建模。
     */
    void matchBatch(const QStringList& imgPaths, const MatchParams& p);





    //  1：学习模板（只在点击“创建”或“学习”时调用一次）
    //void process(const QString& imgPath, const HalconCpp::HObject& hRegion,const MatchParams& p); // 核心处理函数

    //  2：快速匹配（应用页批量检测时调用，不再创建模型，直接查找）
    // void matchOnly(const QString& imgPath, const MatchResult& p, int itemIndex);

    /*
     * @brief 释放当前内存中的模型
     */
    void clearCurrentModel();

signals:

    // 训练成功后发射，包含原始模型轮廓 (用于 UI 预览)
    void modelTrained(MatchResult res);

    // 单张匹配成功后发射
    void resultReady(MatchResult res);

    // 批量匹配中，每算完一张发射一次，带索引 index 方便 UI 对应表格行
    void batchRowReady(int index, MatchResult res);

    // 整个队列处理完毕
    void batchFinished();

    void threadLogInfo(QString text);
};

#endif // HALCONWORKER_H
