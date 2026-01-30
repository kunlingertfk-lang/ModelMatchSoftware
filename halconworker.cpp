#include "halconworker.h"

#include <QtMath>
#include <QDebug>
#include <string>

using namespace HalconCpp;


HalconWorker::HalconWorker(QObject *parent)
    : QObject{parent}{
    m_hvModelID.Clear();
}


HalconWorker::~HalconWorker(){
    clearCurrentModel();
}



/*
void HalconWorker::process(const QString& imgPath, const HalconCpp::HObject& hRegion, const MatchParams& p) {
    MatchResult res;
    HTuple hv_Start, hv_End;
    HalconCpp::CountSeconds(&hv_Start);

    // 1. 将句柄声明在 try 外面，并初始化为空
    HTuple hv_ModelID;

    try {
        // 1. 拦截面积为 0 的情况，防止算子报错
        HTuple area, r, c;
        if (!hRegion.IsInitialized() || (long)hRegion.CountObj() == 0) {
            throw HException("Region is empty");
        }
        // 即使有对象，也要看面积
        AreaCenter(hRegion, &area, &r, &c);
        if (area.D() < 5.0) {
            throw HException("Region area too small for matching");
        }

        // 2. 只有面积 OK，才执行后续算子
        HImage ho_Image(imgPath.toLocal8Bit().constData());
        HImage ho_Reduced = ho_Image.ReduceDomain(hRegion);

        // 1. 准备对比度元组 [Low, High]
        HTuple hv_Contrast;
        hv_Contrast[0] = p.contrastLow;
        hv_Contrast[1] = p.contrastHigh;


        // --- 3. 建模 ---

        HTuple angleStart = p.angleStart*M_PI/180.0;
        HTuple angleExtent = p.angleExtent * M_PI / 180.0;

        // CreateShapeModel(ho_Reduced,"auto",angleStart,angleExtent,"auto",
        //                  "auto","use_polarity","auto","auto",&hv_ModelID);


        // 创建模型
        CreateAnisoShapeModel(ho_Reduced,
                p.pyramidLevel, angleStart, angleExtent, "auto",
                p.scaleRMin, p.scaleRMax, "auto",   //行缩放
                p.scaleCMin, p.scaleCMax, "auto",   //列缩放
                "auto", "use_polarity",
                hv_Contrast, p.minComponentSize, &hv_ModelID);

        // --- 4. 匹配 ---
        // 只有建模成功（句柄长度 > 0）才执行查找
        if (hv_ModelID.Length() > 0) {
            HTuple hv_Row, hv_Col, hv_Angle, hv_SR, hv_SC, hv_Score;
            // 建议：MinScore 设为 0.5 比较稳健，NumMatches 设为 1
            FindAnisoShapeModel(ho_Image,hv_ModelID,angleStart,angleExtent,
                p.scaleCMin,p.scaleCMax,p.scaleRMin,p.scaleRMax,p.minScore,
                1,0.5,"least_squares",0,0.9,
                &hv_Row,&hv_Col,&hv_Angle,&hv_SR,&hv_SC,&hv_Score);


            // FindShapeModel(ho_Image, hv_ModelID, 0, angleExtent, 0.5, 1, 0.5,
            //             "least_squares", 0, 0.9,
            //             &hv_Row, &hv_Col, &hv_Angle, &hv_Score);

            if (hv_Score.Length() > 0) {
                res.found = true;
                res.row = hv_Row[0].D();
                res.col = hv_Col[0].D();
                res.angle = hv_Angle[0].D();
                res.score = hv_Score[0].D();

                //获取并变换轮廓
                HalconCpp::HObject ho_ModelContours;
                HalconCpp::GetShapeModelContours(&ho_ModelContours, hv_ModelID, 1);

                //创建仿射变换矩阵，将轮廓从（0，0）移动并旋转到查找到的(row,col,angle)
                HTuple hv_HomMat2D;
                HalconCpp::VectorAngleToRigid(0,0,0,hv_Row[0],hv_Col[0],hv_Angle[0],&hv_HomMat2D);
                AffineTransContourXld(ho_ModelContours,&res.modelContours,hv_HomMat2D);
            }

        }

    } catch (HalconCpp::HException &e) {
        res.found = false;
        // 增加防御：如果异常消息为空，至少给个错误码
        std::string msg = e.ErrorMessage().Text();
        if (msg.empty()) msg = "Halcon Error Code: " + std::to_string(e.ErrorCode());
        res.errorMsg = QString::fromLocal8Bit(msg.c_str());
        qDebug() << "Worker 拦截异常:" << res.errorMsg;
    } catch (...) {
        res.found = false;
        res.errorMsg = "未知系统异常";
    }

    // --- 5. 句柄清理：这是防崩的关键 ---
    // 无论是否成功，只要 ModelID 产生过，就必须清理
    // if (hv_ModelID.Length() > 0) {
    //     ClearShapeModel(hv_ModelID);
    // }

    // --- 6. 统一计时并只发射一次信号 ---
    HalconCpp::CountSeconds(&hv_End);
    res.time = (hv_End.D() - hv_Start.D()) * 1000.0;
    emit modelTrained(res);
}*/

/*
void HalconWorker::matchOnly(const QString& imgPath, const MatchResult& p, int itemIndex){
    MatchResult res;
    res.itemIndex = itemIndex; // 携带索引，方便 UI 知道是哪张图算完了

    HTuple hv_Start, hv_End;
    CountSeconds(&hv_Start);

    try {
        if (m_hvModelID.Length() == 0) throw HException("请先创建模板");

        HImage ho_Image(imgPath.toLocal8Bit().constData());

        HTuple hv_Row, hv_Col, hv_Angle, hv_SR, hv_SC, hv_Score;
        // 极速匹配
        HTuple angleStart = 360.0*p.angleStart / 180.0;
        HTuple angleExtent = 360.0*p.angleExtent / 180.0;
        FindAnisoShapeModel(ho_Image, m_hvModelID,
                            angleStart,angleExtent,
                            p.scaleRMin, p.scaleRMax, p.scaleCMin, p.scaleCMax,
                            p.minScore, 1, 0.5, "least_squares", 0, 0.9,
                            &hv_Row, &hv_Col, &hv_Angle, &hv_SR, &hv_SC, &hv_Score);

        if (hv_Score.Length() > 0) {
            res.found = true;
            res.row = hv_Row[0].D();
            res.col = hv_Col[0].D();
            res.angle = hv_Angle[0].D();
            res.score = hv_Score[0].D();

            // 获取结果轮廓
            HObject ho_Contours;
            GetShapeModelContours(&ho_Contours, m_hvModelID, 1);
            HTuple hv_HomMat;
            VectorAngleToRigid(0, 0, 0, hv_Row[0], hv_Col[0], hv_Angle[0], &hv_HomMat);
            AffineTransContourXld(ho_Contours, &res.modelContours, hv_HomMat);
        }
    } catch (HException &e) {
        res.found = false;
        res.errorMsg = QString::fromLocal8Bit(e.ErrorMessage().Text());
    }

    CountSeconds(&hv_End);
    res.time = (hv_End.D() - hv_Start.D()) * 1000.0;
    emit resultReady(res);
}
*/




void HalconWorker::clearCurrentModel(){
    if (m_hvModelID.Length() > 0) {
        HalconCpp::ClearShapeModel(m_hvModelID);
        m_hvModelID.Clear();
    }
}


void HalconWorker::trainModel(const QString& imgPath, const HalconCpp::HObject& hRegion, const MatchParams& p) {
    clearCurrentModel();
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
        HImage img(imgPath.toLocal8Bit().data());
        HImage reduced = img.ReduceDomain(hRegion);

        // 3. 对比度准备
        HTuple contrast;
        contrast[0] = p.contrastLow;
        contrast[1] = p.contrastHigh;

        // 4. 创建模型 (各项异性)
        HTuple angleStart = qDegreesToRadians(p.angleStart);
        HTuple angleExtent = qDegreesToRadians(p.angleExtent);

        // [优化]：此处使用参数中的 pyramidLevel
        CreateAnisoShapeModel(reduced, p.pyramidLevel, // 对应 MatchParams.numLevels
                              angleStart, angleExtent, "auto",
                              p.scaleRMin, p.scaleRMax, "auto",
                              p.scaleCMin, p.scaleCMax, "auto",
                              "auto", "use_polarity", contrast, p.minComponentSize, &m_hvModelID);

        if (m_hvModelID.Length() > 0) {
            // 获取原始轮廓用于预览 (始终以 0,0 为中心)
            HObject ho_ModelContours;
            GetShapeModelContours(&ho_ModelContours, m_hvModelID, 1);

            // 5. 立即自检 (在训练图上找一次)
            HTuple r, c, angle, SR, SC, score;
            FindAnisoShapeModel(img, m_hvModelID, angleStart, angleExtent,
                                p.scaleRMin, p.scaleRMax, p.scaleCMin, p.scaleCMax,
                                p.minScore, 1, 0.5, "least_squares", 0, 0.9,
                                &r, &c, &angle, &SR, &SC, &score);

            if (score.Length() > 0) {
                // [修改]：只有找着了才设为 found = true
                res.found = true;
                res.row = r[0].D();
                res.col = c[0].D();
                res.angle = angle[0].D();
                res.score = score[0].D();

                // 6. 执行轮廓仿射变换
                HTuple hv_HomMat2D;
                VectorAngleToRigid(0, 0, 0, r[0], c[0], angle[0], &hv_HomMat2D);
                AffineTransContourXld(ho_ModelContours, &res.modelContours, hv_HomMat2D);

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
            MatchResult res;
            try {
                HalconCpp::HTuple start;
                HalconCpp::CountSeconds(&start);

                HalconCpp::HImage img(imgPaths[i].toLocal8Bit().data());
                HalconCpp::HTuple r, c, a, sr, sc, score;

                HalconCpp::FindAnisoShapeModel(img, m_hvModelID,
                                               qDegreesToRadians(p.angleStart), qDegreesToRadians(p.angleExtent),
                                               p.scaleCMin, p.scaleCMax, p.scaleRMin, p.scaleRMax,
                                               p.minScore, 1, 0.5, "least_squares", 0, 0.9,
                                               &r, &c, &a, &sr, &sc, &score);

                if (score.Length() > 0) {
                    res.found = true;
                    res.row = r[0].D(); res.col = c[0].D(); res.angle = a[0].D();
                    res.score = score[0].D();
                    double scaleR = sr[0].D(), scaleC = sc[0].D();


                    HalconCpp::HTuple homMat;
                    HalconCpp::HomMat2dIdentity(&homMat); // 建立基础矩阵

                    /*刚体变换矩阵根据起始点(R1,C1,θ1)(R1,C1,θ1) 和 目标点 (R2,C2,θ2)(R2,C2,θ2)自动计算出一个包含平移和旋转的矩阵。它不支持缩放（Scaling）。*/
                    // 适用场景：普通的形状匹配（只有位置和角度变化，物体大小绝对不变）。
                    // HalconCpp::VectorAngleToRigid(0, 0, 0, r[0], c[0], a[0], &homMat);

                     // 2. 先缩放轮廓（针对中心 0,0）
                    HalconCpp::HomMat2dScale(homMat,scaleR,scaleC,0,0,&homMat);
                    // 3. 再旋转
                    HomMat2dRotate(homMat, res.angle, 0, 0, &homMat);

                    // 4. 最后平移到目标像素位置
                    HomMat2dTranslate(homMat, res.row, res.col, &homMat);

                    HalconCpp::AffineTransContourXld(baseContours, &res.modelContours, homMat);
                }
                HalconCpp::HTuple end;
                HalconCpp::CountSeconds(&end);
                res.time = (end.D() - start.D()) * 1000.0;

            } catch (HalconCpp::HException &e) {
                res.found = false;
                res.errorMsg = QString("图像 %1 读取或匹配异常").arg(i);
            }
            emit batchRowReady(i, res); // 发回单行结果
        }
    }catch(...){
        // 处理获取基础轮廓失败等极少数情况
    }
    emit batchFinished();
}























