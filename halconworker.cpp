#include "halconworker.h"

#include <QtMath>
#include <QDebug>

using namespace HalconCpp;


HalconWorker::HalconWorker(QObject *parent)
    : QObject{parent}{
    m_hvModelID.Clear();
}


HalconWorker::~HalconWorker(){
    clearCurrentModel();
}

void HalconWorker::saveModel(const QString& filePath){
    if(m_hvModelID.Length() == 0){
        emit threadLogInfo("保存失败：当前没有已训练好的模板");
        return;
    }

    try{
        // Halcon 算子：将模型写入磁盘 (.shm 文件)
        HalconCpp::WriteShapeModel(m_hvModelID,filePath.toLocal8Bit().constData());
        emit threadLogInfo("模板文件导出成功: " + filePath);
    }
    catch(HalconCpp::HException &e){
        emit threadLogInfo("模板导出异常："+QString::fromLocal8Bit(e.ErrorMessage().Text()));
    }
}


void HalconWorker::clearCurrentModel(){
    if (m_hvModelID.Length() > 0) {
        HalconCpp::ClearShapeModel(m_hvModelID);
        m_hvModelID.Clear();
    }
}


void HalconWorker::setMatchFactor(int matchFactorIndex){
    m_MatchFactor = matchFactorIndex;
}


void HalconWorker::trainModel(const QString& imgPath, const HalconCpp::HObject& hRegion, const MatchParams& p) {
    MatchResult res;
    res.found = false;

    HTuple hv_Start, hv_End;
    HalconCpp::CountSeconds(&hv_Start);
    // 将模型 ID 声明放在 try 外面以便 catch 块清理或判断
    try {
        // 1. 合法性检查
        if (!hRegion.IsInitialized() || (long)hRegion.CountObj() == 0) {
            throw HException("Region is empty");
        }

        HTuple area, row_roi, col_roi;
        AreaCenter(hRegion, &area, &row_roi, &col_roi);
        if (area.D() < 10.0) { // 提高到 10 像素更稳健
            throw HException("Region area too small");
        }

        // 2. 环境清理与图像加载
        clearCurrentModel();

        HImage srcImg(imgPath.toLocal8Bit().data());//获取图片
        HImage grayImg;
        HTuple channels = srcImg.CountChannels();
        if(channels == 3){
            grayImg = srcImg.Rgb1ToGray();
        }
        else{
            grayImg = srcImg;
        }

        //HImage imgGauss = srcimg.GaussFilter(3);
        HImage procImg = grayImg.MedianImage("circle", 3, "mirrored").Emphasize(7,7,1.5);//中值滤波，去除高频噪点
        //HImage imgEmphasize = imgMedian.Emphasize(7, 7, 1.5);  //增强对比度

        //进行图片处理
        HImage reduced = procImg.ReduceDomain(hRegion);

        // 3. 对比度准备
        HTuple contrast;
        contrast[0] = p.contrastLow;
        contrast[1] = p.contrastHigh;
        contrast[2] = p.minComponentSize;

        // 2. 准备最小对比度（通常是一个固定小值）
        HTuple hv_MinContrast = 10; // 或者从 UI 传一个值，通常不建议设太大

        // 4. 创建模型 (各项异性)
        HTuple angleStart = qDegreesToRadians(p.angleStart);
        HTuple angleExtent = qDegreesToRadians(p.angleExtent);
        qDebug() << p.minComponentSize;
        // [优化]：此处使用参数中的 pyramidLevel   创建各向异性的Model算子
        if(m_MatchFactor == 0){
            CreateShapeModel(reduced, p.pyramidLevel, // 对应 MatchParams.numLevels
                                  angleStart, angleExtent, "auto",
                                  "auto", "use_polarity", contrast, hv_MinContrast, &m_hvModelID);
        }
        else if(m_MatchFactor == 1){
            CreateScaledShapeModel(reduced, p.pyramidLevel, // 对应 MatchParams.numLevels
                                  angleStart, angleExtent, "auto",
                                  p.scaleRMin, p.scaleRMax, "auto",
                                  "auto", "use_polarity", contrast, hv_MinContrast, &m_hvModelID);
        }
        else if(m_MatchFactor == 2){
            CreateAnisoShapeModel(reduced, p.pyramidLevel, // 对应 MatchParams.numLevels
                                  angleStart, angleExtent, "auto",
                                  p.scaleRMin, p.scaleRMax, "auto",
                                  p.scaleCMin, p.scaleCMax, "auto",
                                  "auto", "use_polarity", contrast, hv_MinContrast, &m_hvModelID);
        }

        if (m_hvModelID.Length() > 0) {
            // 获取原始轮廓用于预览 (始终以 0,0 为中心)
            HObject ho_ModelContours;
            GetShapeModelContours(&ho_ModelContours, m_hvModelID, 1);

            // 5. 立即自检 (在训练图上找一次)
            HTuple r, c, angle, score, SR = 1.0, SC = 1.0;
            if(m_MatchFactor == 0){
                FindShapeModel(procImg, m_hvModelID, angleStart, angleExtent,
                                    p.minScore, 1, 0.5, "least_squares", p.pyramidLevel, 0.9,
                                    &r, &c, &angle, &score);
            }
            else if(m_MatchFactor == 1){
                FindScaledShapeModel(procImg, m_hvModelID, angleStart, angleExtent,
                                    p.scaleRMin, p.scaleRMax,
                                    p.minScore, 1, 0.5, "least_squares", p.pyramidLevel, 0.9,
                                    &r, &c, &angle, &SR, &score);
                SC = SR;
            }
            else if(m_MatchFactor == 2){
                FindAnisoShapeModel(procImg, m_hvModelID, angleStart, angleExtent,
                                    p.scaleRMin, p.scaleRMax, p.scaleCMin, p.scaleCMax,
                                    p.minScore, 1, 0.5, "least_squares", p.pyramidLevel, 0.9,
                                    &r, &c, &angle, &SR, &SC, &score);
            }

            if (score.Length() > 0) {
                // [修改]：只有找着了才设为 found = true
                res.found = true;
                res.row = r[0].D();
                res.col = c[0].D();
                res.angle = angle[0].D();
                res.score = score[0].D();

                double scaleR = SR[0].D();
                double scaleC = SC[0].D();

                // 6. 执行轮廓仿射变换
                HTuple hv_HomMat2D;
                // VectorAngleToRigid(0, 0, 0, r[0], c[0], angle[0], &hv_HomMat2D);
                HalconCpp::HomMat2dIdentity(&hv_HomMat2D);
                HalconCpp::HomMat2dScale(hv_HomMat2D,scaleR,scaleC,0,0,&hv_HomMat2D);
                HalconCpp::HomMat2dRotate(hv_HomMat2D,res.angle,0,0,&hv_HomMat2D);
                HalconCpp::HomMat2dTranslate(hv_HomMat2D,res.row,res.col,&hv_HomMat2D);

                HalconCpp::AffineTransContourXld(ho_ModelContours, &res.modelContours, hv_HomMat2D);

                emit threadLogInfo("训练并自检成功");
            } else {
                // 建模成功但自检没找着（可能是 minScore 设太高）
                res.found = false;
                res.modelContours = ho_ModelContours; // 即使没找着，也可以把原始轮廓传回去看一眼
                emit threadLogInfo("警告：模板创建成功但自检匹配失败，请检查得分参数");
            }
        }

        // 记录耗时
        HalconCpp::CountSeconds(&hv_End);
        res.time = (hv_End.D() - hv_Start.D()) * 1000.0;

        emit modelTrained(res);

    } catch (HalconCpp::HException &e) {
        res.found = false;
        res.errorMsg = QString::fromLocal8Bit(e.ErrorMessage().Text());
        emit threadLogInfo("Worker Error: " + res.errorMsg);
        emit modelTrained(res);
    }
}


void HalconWorker::matchBatch(const QStringList& imgPaths, const MatchParams& p) {
    if (m_hvModelID.Length() == 0) {
        emit threadLogInfo("模型句柄为空或不存在");
        return;
    }

    try{
        // 变换轮廓
        HalconCpp::HObject baseContours;
        HalconCpp::GetShapeModelContours(&baseContours, m_hvModelID, 1);

        for (int i = 0; i < imgPaths.size(); ++i) {
            QVector<MatchResult> res;
            try {
                // HalconCpp::HTuple start; //算法开始起始时间
                // HalconCpp::CountSeconds(&start);

                HalconCpp::HImage img(imgPaths[i].toLocal8Bit().data());
                HTuple r, c, angle, score, SR = 1.0, SC = 1.0;

                if(m_MatchFactor == 0){
                    FindShapeModel(img, m_hvModelID,
                                   qDegreesToRadians(p.angleStart), qDegreesToRadians(p.angleExtent),
                                   p.minScore, p.matchNum, 0.5, "least_squares", p.pyramidLevel, 0.9,
                                   &r, &c, &angle, &score);
                }
                else if(m_MatchFactor == 1){
                    FindScaledShapeModel(img, m_hvModelID,
                                         qDegreesToRadians(p.angleStart), qDegreesToRadians(p.angleExtent),
                                         p.scaleRMin, p.scaleRMax,
                                         p.minScore, p.matchNum, 0.5, "least_squares", p.pyramidLevel, 0.9,
                                         &r, &c, &angle, &SR, &score);
                    SC = SR;
                }
                else if(m_MatchFactor == 2){
                    FindAnisoShapeModel(img, m_hvModelID,
                                        qDegreesToRadians(p.angleStart), qDegreesToRadians(p.angleExtent),
                                        p.scaleRMin, p.scaleRMax, p.scaleCMin, p.scaleCMax,
                                        p.minScore, p.matchNum, 0.5, "least_squares", p.pyramidLevel, 0.9,
                                         &r, &c, &angle, &SR, &SC, &score);
                }

                int numFound = score.Length();
                if (numFound > 0) {
                    for(int j = 0;j < numFound; ++j){
                        MatchResult onrRes;
                        onrRes.found = true;
                        onrRes.row = r[j].D(); onrRes.col = c[j].D(); onrRes.angle = angle[j].D();
                        onrRes.score = score[j].D();
                        double scaleR = (m_MatchFactor) ? SR[j].D() : 1.0;
                        double scaleC = (m_MatchFactor) ? SC[j].D() : 1.0;


                        HalconCpp::HTuple homMat;
                        HalconCpp::HomMat2dIdentity(&homMat); // 建立基础矩阵

                        /*刚体变换矩阵根据起始点(R1,C1,θ1)(R1,C1,θ1) 和 目标点 (R2,C2,θ2)(R2,C2,θ2)自动计算出一个包含平移和旋转的矩阵。它不支持缩放（Scaling）。*/
                        // 适用场景：普通的形状匹配（只有位置和角度变化，物体大小绝对不变）。
                        // HalconCpp::VectorAngleToRigid(0, 0, 0, r[0], c[0], a[0], &homMat);

                        // 2. 先缩放轮廓（针对中心 0,0）
                        HalconCpp::HomMat2dScale(homMat,scaleR,scaleC,0,0,&homMat);
                        // 3. 再旋转
                        HomMat2dRotate(homMat, onrRes.angle, 0, 0, &homMat);

                        // 4. 最后平移到目标像素位置
                        HomMat2dTranslate(homMat, onrRes.row, onrRes.col, &homMat);

                        HalconCpp::AffineTransContourXld(baseContours, &onrRes.modelContours, homMat);
                        res.push_back(onrRes);
                    }
                }
                // HalconCpp::HTuple end;
                // HalconCpp::CountSeconds(&end);
                // res.time = (end.D() - start.D()) * 1000.0;

            } catch (HalconCpp::HException &e) {
                res[0].found = false;
                res[0].errorMsg = QString("图像 %1 读取或匹配异常").arg(i);
            }
            emit batchRowReady(i, res); // 发回单行结果
        }
    }catch(...){
        // 处理获取基础轮廓失败等极少数情况
    }
    emit batchFinished();
}























