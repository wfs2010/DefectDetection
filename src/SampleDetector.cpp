#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fstream>
#include <glog/logging.h>
#include <sys/stat.h>
//jsoncpp 相关的头文件
#include "reader.h"
#include "writer.h"
#include "value.h"
#include "SampleDetector.hpp"

static bool ifFileExists(const char *FileName)
{
    struct stat my_stat;
    return (stat(FileName, &my_stat) == 0);
}

static void convert_atlas(const std::string& modelName)
{
    std::string atcPath = "/usr/local/ev_sdk/src/convert_atlas.sh";
    size_t index = modelName.find_last_of(".");
    std::string outName = modelName.substr(0, index);
    std::string bashStr = std::string("bash " + atcPath + " " + modelName + " 5 " + outName + " " + std::string(aclrtGetSocName()));
    SDKLOG(INFO) << bashStr;
    system(bashStr.data());
}

SampleDetector::SampleDetector()
{
}

SampleDetector::~SampleDetector()
{
    UnInit();
}

STATUS SampleDetector::Init(const std::string &strModelName, float thresh)
{
    //如果已经初始化,则直接返回
    if (mInitialized)
    {
        SDKLOG(INFO) << "AlgoJugement instance is initied already!";
        return STATUS_SUCCESS;
    }

    // 初始化ACL资源
    STATUS status = InitAcl();
    if(status != STATUS_SUCCESS)
    {
        return ERROR_INITACL;
    }
    SDKLOG(INFO) << "succeed to init acl";
    //初始化模型管理实例
    std::string strOmName = strModelName;
    size_t sep_pos = strOmName.find_last_of(".");
    strOmName = strOmName.substr(0, sep_pos) + ".om";
    SDKLOG(INFO) << "strOmName" << strOmName;
    SDKLOG(INFO) << "strModelName" << strModelName;
    if (ifFileExists(strOmName.c_str()))
    {
        status = InitModel(strOmName);
    }
    else{
        convert_atlas(strModelName);
        status = InitModel(strOmName);
    }

    if(status != STATUS_SUCCESS)
    {
        return ERROR_INITMODEL;
    }
    SDKLOG(INFO) << "succeed to init model";
//    status = InitDvpp();
//    if(status != STATUS_SUCCESS)
//    {
//        return ERROR_INITDVPP;
//    }
    mInitialized = true;
    return STATUS_SUCCESS;
}

STATUS SampleDetector::InitAcl()
{
    mAclContext = nullptr;
    mAclStream = nullptr;
    uint32_t deviceCount;
    aclError ret = aclrtGetDeviceCount(&deviceCount);
    if (ret != ACL_ERROR_NONE)
    {
        SDKLOG(ERROR) << "No device found! aclError= " << ret;
        return ERROR_INITACL;
    }
    SDKLOG(INFO) << deviceCount << " devices found";

    // open device
    ret = aclrtSetDevice(mDeviceId);
    if (ret != ACL_ERROR_NONE)
    {
        SDKLOG(ERROR) << "Acl open device " << mDeviceId << " failed! aclError= " << ret;
        return ERROR_INITACL;
    }
    SDKLOG(INFO) << "Open device " << mDeviceId << " success";

    // create context (set current)
    ret = aclrtCreateContext(&mAclContext, mDeviceId);
    if (ret != ACL_ERROR_NONE)
    {
        SDKLOG(ERROR) << "acl create context failed! aclError= " << ret;
        return ERROR_INITACL;
    }
    SDKLOG(INFO) << "create context success";

    // set current context
    ret = aclrtSetCurrentContext(mAclContext);
    if (ret != ACL_ERROR_NONE)
    {
        SDKLOG(ERROR) << "acl set context failed! aclError= " << ret;
        return ERROR_INITACL;
    }
    SDKLOG(INFO) << "set context success";

    // create stream
    ret = aclrtCreateStream(&mAclStream);
    if (ret != ACL_ERROR_NONE)
    {
        SDKLOG(ERROR) << "acl create stream failed! aclError= " << ret;
        return ERROR_INITACL;
    }
    SDKLOG(INFO) << "create stream success";

    //获取当前应用程序运行在host还是device
    ret = aclrtGetRunMode(&mAclRunMode);
    if (ret != ACL_ERROR_NONE)
    {
        SDKLOG(ERROR) << "acl get run mode failed! aclError= " << ret;
        return ERROR_INITACL;
    }
    return STATUS_SUCCESS;
}

STATUS SampleDetector::InitModel(const std::string &strModelName)
{
    SDKLOG(INFO) << "load model " << strModelName;
    auto ret = aclmdlQuerySize(strModelName.c_str(), &mModelMSize, &mModelWSize);
    ret = aclrtMalloc(&mModelMptr, mModelMSize, ACL_MEM_MALLOC_HUGE_FIRST);
    ret = aclrtMalloc(&mModelWptr, mModelWSize, ACL_MEM_MALLOC_HUGE_FIRST);
    ret = aclmdlLoadFromFileWithMem(strModelName.c_str(), &mModelID, mModelMptr,mModelMSize, mModelWptr, mModelWSize);
    mModelDescPtr = aclmdlCreateDesc();
    ret = aclmdlGetDesc(mModelDescPtr, mModelID);
    // 创建模型输出的数据集结构
    mInputDatasetPtr = aclmdlCreateDataset();
    mOutputDatasetPtr = aclmdlCreateDataset();
    //获取模型的输入个数,为输入分配内存并创建输出数据集
    int iModelInputNum = aclmdlGetNumInputs(mModelDescPtr);
    SDKLOG(INFO) << "num of inputs " << iModelInputNum;
    {
    aclmdlGetInputDims(mModelDescPtr, 0, &m_input_dims);
    SDKLOG(INFO) << "input dim is : "<< m_input_dims.dims[0] << " " << m_input_dims.dims[1] << " " << m_input_dims.dims[2] << " " << m_input_dims.dims[3];
    size_t buffer_size = aclmdlGetInputSizeByIndex(mModelDescPtr, 0);
    mModelInputSize.width =  m_input_dims.dims[2];
    mModelInputSize.height =  m_input_dims.dims[3];
    ret = aclrtMalloc(&m_input_buffer, aclmdlGetInputSizeByIndex(mModelDescPtr, 0), ACL_MEM_MALLOC_NORMAL_ONLY);
    m_input_data_buffer = aclCreateDataBuffer(m_input_buffer, buffer_size);
    ret = aclmdlAddDatasetBuffer(mInputDatasetPtr, m_input_data_buffer);
    }

    //获取模型的输出个数,为输出分配内存并创建输出数据集
    mModelOutputNums = aclmdlGetNumOutputs(mModelDescPtr);
    SDKLOG(INFO) << "num of outputs " << mModelOutputNums;
    {
    size_t buffer_size = aclmdlGetOutputSizeByIndex(mModelDescPtr, 0);
    aclmdlGetOutputDims(mModelDescPtr, 0, &m_output_dims);
    SDKLOG(INFO) << "output dims is : " << m_output_dims.dims[0] << " " << m_output_dims.dims[1] << " " << m_output_dims.dims[2] << " " << m_output_dims.dims[3];
    if(mAclRunMode == ACL_HOST) m_output_buffer_host= new char[aclmdlGetOutputSizeByIndex(mModelDescPtr, 0)]();
    ret = aclrtMalloc(&m_output_buffer, buffer_size, ACL_MEM_MALLOC_NORMAL_ONLY);
    m_output_data_buffer = aclCreateDataBuffer(m_output_buffer, buffer_size);
    ret = aclmdlAddDatasetBuffer(mOutputDatasetPtr, m_output_data_buffer);
    }

    aclrtGetRunMode(&mAclRunMode);
    if(mAclRunMode == ACL_DEVICE)
    {
        SDKLOG(INFO) << "run in device mode";
    }
    else
    {
        SDKLOG(INFO) << "run in host mode";
    }

    //预处理功能
    if(mAclRunMode == ACL_DEVICE)
    {
        size_t single_chn_size = m_input_dims.dims[0] * m_input_dims.dims[2] * m_input_dims.dims[3] * sizeof(float);
        m_chw_wrappers.emplace_back(m_input_dims.dims[2], m_input_dims.dims[3], CV_32FC1, m_input_buffer);
        m_chw_wrappers.emplace_back(m_input_dims.dims[2], m_input_dims.dims[3], CV_32FC1, m_input_buffer + single_chn_size);
        m_chw_wrappers.emplace_back(m_input_dims.dims[2], m_input_dims.dims[3], CV_32FC1, m_input_buffer + 2 * single_chn_size);
    }
    return STATUS_SUCCESS;
}

STATUS SampleDetector::UnInit()
{
    if(mInitialized == false)
    {
        return STATUS_SUCCESS;
    }
    SDKLOG(INFO) << "in uninit func";
    // 释放输入数据集
    if(mInputDatasetPtr != nullptr)
    {
        aclmdlDestroyDataset(mInputDatasetPtr);
        mInputDatasetPtr = nullptr;
    }
    // 释放输出数据集
    if(mOutputDatasetPtr != nullptr)
    {
        aclmdlDestroyDataset(mOutputDatasetPtr);
        mOutputDatasetPtr = nullptr;
    }
    if (m_input_buffer!= nullptr)
    {
        aclrtFree(m_input_buffer);
        m_input_buffer = nullptr;
    }
    if (m_output_buffer != nullptr)
    {
        aclrtFree(m_output_buffer);
        m_output_buffer = nullptr;
    }
    if (m_input_data_buffer != nullptr){
        aclDestroyDataBuffer(m_input_data_buffer);
        m_input_data_buffer = nullptr;
    }
    if(m_output_data_buffer != nullptr)
    {
        aclDestroyDataBuffer(m_output_data_buffer);
        m_input_data_buffer = nullptr;
    }
    if (m_output_buffer_host != nullptr)
    {
        delete m_output_buffer_host;
        m_output_buffer_host = nullptr;
    }

    //释放存放模型的相关资源
    if(mModelMptr != nullptr)
    {
        aclrtFree(mModelMptr);
        mModelMptr = nullptr;
    }
    if(mModelWptr != nullptr)
    {
        aclrtFree(mModelWptr);
        mModelWptr = nullptr;
    }
    if(mModelDescPtr != nullptr)
    {
        aclmdlDestroyDesc(mModelDescPtr);
        mModelDescPtr = nullptr;
    }
    aclmdlUnload(mModelID);

    aclrtDestroyStream(mAclStream);
    aclrtDestroyContext(mAclContext);
    mInitialized = false;
}

STATUS SampleDetector::ProcessImage(const cv::Mat &inFrame, std::vector<BoxInfo> &result, float thresh)
{
    // set current context
    auto ret = aclrtSetCurrentContext(mAclContext);
    if (ret != ACL_ERROR_NONE)
    {
        SDKLOG(ERROR) << "acl set context failed! aclError= " << ret;
        return ERROR_INITACL;
    }

    mThresh = thresh;
    mInputHeight = inFrame.cols;
    mInputWidth = inFrame.rows;
    //预处理

    cv::Mat in_mat = inFrame.clone();
    //等比例缩放
    float r = std::min(mModelInputSize.width / static_cast<float>(in_mat.rows), mModelInputSize.height / static_cast<float>(in_mat.cols));
    cv::Size new_size = cv::Size{static_cast<int>(in_mat.cols * r), static_cast<int>(in_mat.rows * r)};
    SDKLOG(INFO) << "model input: "<< mModelInputSize.width  << mModelInputSize.height;
    SDKLOG(INFO) << "cv::Size " << new_size.width << new_size.height  << r;
    SDKLOG(INFO) << "cv::Size inMat " << in_mat.rows << in_mat.cols ;
    cv::Mat resized_mat;
    cv::resize(in_mat, resized_mat, new_size);

    if(m_board.empty())
    {
        m_board = cv::Mat(mModelInputSize, CV_8UC3, cv::Scalar(114, 114, 114));
    }
    resized_mat.copyTo(m_board(cv::Rect{0, 0, resized_mat.cols, resized_mat.rows}));

    //色阈转换BGR2RGB
    cv::cvtColor(m_board, m_board, cv::COLOR_BGR2RGB);
    //转浮点型归一化，hwc2chw
    m_board.convertTo(m_normalized_mat, CV_32FC3, 1/255.);
    // std::vector<cv::Mat> m_chw_wrappers;
    cv::split(m_normalized_mat, m_chw_wrappers);

    if(mAclRunMode == ACL_HOST)
    {
        size_t single_chn_size = m_chw_wrappers[0].rows * m_chw_wrappers[0].cols * sizeof(float);
        aclrtMemcpy(m_input_buffer, single_chn_size, m_chw_wrappers[0].data, single_chn_size, ACL_MEMCPY_HOST_TO_DEVICE);
        aclrtMemcpy((char*)m_input_buffer + single_chn_size, single_chn_size, m_chw_wrappers[1].data, single_chn_size, ACL_MEMCPY_HOST_TO_DEVICE);
        aclrtMemcpy((char*)m_input_buffer + 2 * single_chn_size, single_chn_size, m_chw_wrappers[2].data, single_chn_size, ACL_MEMCPY_HOST_TO_DEVICE);
    }

    SDKLOG(INFO) << "before inference";
    //运行模型推理
    STATUS status = doInference();
    SDKLOG(INFO) << "after inference";

    //解析yolo层的输出
    float scale = std::min(m_input_dims.dims[3] / (in_mat.cols * 1.0), m_input_dims.dims[2] / (in_mat.rows * 1.0));
    std::vector<BoxInfo> detBoxes{};
    void* obuf = nullptr;
    if(mAclRunMode == ACL_HOST)
    {
        size_t cp_size = m_output_dims.dims[0] * m_output_dims.dims[1] * m_output_dims.dims[2] * sizeof(float);
        aclrtMemcpy(m_output_buffer_host, cp_size, m_output_buffer,  cp_size, ACL_MEMCPY_DEVICE_TO_HOST);
        obuf = m_output_buffer_host;
    }
    else
    {
        obuf = m_output_buffer;
    }
    decode_outputs((float*)obuf, detBoxes, scale, in_mat.cols, in_mat.rows);
    runNms(detBoxes);
    result = detBoxes;
    return STATUS_SUCCESS;
}


STATUS SampleDetector::doInference()
{
    auto ret = aclmdlExecute(mModelID, mInputDatasetPtr, mOutputDatasetPtr);
    return STATUS_SUCCESS;
}


static float BoxIOU(const cv::Rect2d &b1, const cv::Rect2d &b2)
{
    cv::Rect2d inter = b1 & b2;
    return inter.area() / (b1.area() + b2.area() - inter.area());
}

void SampleDetector::runNms(std::vector<BoxInfo> & vecBoxObjs)
{
    std::sort(vecBoxObjs.begin(), vecBoxObjs.end(), [](const BoxInfo &b1, const BoxInfo &b2){return b1.score > b2.score;});
    for (int i = 0; i < vecBoxObjs.size(); ++i)
    {
        if (vecBoxObjs[i].score == 0)
        {
            continue;
        }
        for (int j = i + 1; j < vecBoxObjs.size(); ++j)
        {
            if (vecBoxObjs[j].score == 0)
            {
                continue;
            }
            cv::Rect pos1{vecBoxObjs[i].x1, vecBoxObjs[i].y1, vecBoxObjs[i].x2-vecBoxObjs[i].x1, vecBoxObjs[i].y2- vecBoxObjs[i].y1};
            cv::Rect pos2{vecBoxObjs[j].x1, vecBoxObjs[j].y1, vecBoxObjs[j].x2-vecBoxObjs[j].x1, vecBoxObjs[j].y2- vecBoxObjs[j].y1};
            if (BoxIOU(pos1, pos2) >= 0.45 )
            {
                vecBoxObjs[j].score = 0;
            }
        }
    }
    for (auto iter = vecBoxObjs.begin(); iter != vecBoxObjs.end(); ++iter)
    {
        if (iter->score < 0.01)
        {
            vecBoxObjs.erase(iter);
            --iter;
        }
    }
}

void SampleDetector::decode_outputs(float* buffer,
    std::vector<BoxInfo>& objects,
    float scale,
    const int img_w,
    const int img_h)
{
    SDKLOG(INFO) << "scale:" << scale << "img_w: " << img_w << "img_h:" << img_h;
    std::vector<BoxInfo> proposals;
    int box_num = m_output_dims.dims[1];
    int class_num = m_output_dims.dims[2] - 5;

    for(int i = 0; i < box_num; ++i)
    {
        int index = i * (class_num + 5);

        if(buffer[index + 4] > mThresh)
        {
            float x = buffer[index] / scale;
            float y = buffer[index + 1] / scale;
            float w = buffer[index + 2] / scale;
            float h = buffer[index + 3] / scale;
            float* max_cls_pos = std::max_element(buffer + index + 5, buffer + index + 5 + class_num);

            if((*max_cls_pos) * buffer[index+4] > mThresh)
            {
                cv::Rect box{x- w / 2, y - h / 2, w, h};
                box = box & cv::Rect(0, 0, img_w-1, img_h-1);
                if( box.area() > 0)
                {
                    BoxInfo box_info = { box.x, box.y, box.x + box.width, box.y + box.height, (*max_cls_pos) * buffer[index+4], max_cls_pos - (buffer + index + 5)};
                    objects.push_back(box_info);
                }
            }
        }
    }
}
