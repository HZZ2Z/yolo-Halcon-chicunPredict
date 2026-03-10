#include "camera_provider.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>

#ifdef USE_MVS
#include "MvCameraControl.h"
#endif

CameraProvider::CameraProvider(const std::string& source, int width, int height)
    : source_(source), width_(width), height_(height) {}

CameraProvider::~CameraProvider() {
    closeMvs();
}

#ifdef USE_MVS
bool CameraProvider::openMvs(int index) {
    if (index < 0) {
        index = 0;
    }

    int nRet = MV_CC_Initialize();
    if (nRet != MV_OK) {
        return false;
    }
    mvs_initialized_ = true;

    MV_CC_DEVICE_INFO_LIST dev_list;
    std::memset(&dev_list, 0, sizeof(dev_list));
    nRet = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE | MV_GENTL_CAMERALINK_DEVICE |
                                 MV_GENTL_CXP_DEVICE | MV_GENTL_XOF_DEVICE,
                             &dev_list);
    if (nRet != MV_OK || dev_list.nDeviceNum == 0) {
        closeMvs();
        return false;
    }

    if (static_cast<unsigned int>(index) >= dev_list.nDeviceNum) {
        closeMvs();
        return false;
    }

    nRet = MV_CC_CreateHandle(&mvs_handle_, dev_list.pDeviceInfo[index]);
    if (nRet != MV_OK || !mvs_handle_) {
        closeMvs();
        return false;
    }

    nRet = MV_CC_OpenDevice(mvs_handle_);
    if (nRet != MV_OK) {
        closeMvs();
        return false;
    }

    if (dev_list.pDeviceInfo[index]->nTLayerType == MV_GIGE_DEVICE) {
        int nPacketSize = MV_CC_GetOptimalPacketSize(mvs_handle_);
        if (nPacketSize > 0) {
            MV_CC_SetIntValueEx(mvs_handle_, "GevSCPSPacketSize", nPacketSize);
        }
    }

    MV_CC_SetEnumValue(mvs_handle_, "TriggerMode", 0);
    if (width_ > 0) {
        MV_CC_SetIntValueEx(mvs_handle_, "Width", width_);
    }
    if (height_ > 0) {
        MV_CC_SetIntValueEx(mvs_handle_, "Height", height_);
    }

    nRet = MV_CC_StartGrabbing(mvs_handle_);
    if (nRet != MV_OK) {
        closeMvs();
        return false;
    }

    mvs_mode_ = true;
    return true;
}

void CameraProvider::closeMvs() {
    if (mvs_handle_) {
        MV_CC_StopGrabbing(mvs_handle_);
        MV_CC_CloseDevice(mvs_handle_);
        MV_CC_DestroyHandle(mvs_handle_);
        mvs_handle_ = nullptr;
    }
    if (mvs_initialized_) {
        MV_CC_Finalize();
        mvs_initialized_ = false;
    }
    mvs_mode_ = false;
}
#else
bool CameraProvider::openMvs(int) {
    return false;
}

void CameraProvider::closeMvs() {}
#endif

bool CameraProvider::open() {
    if (source_.empty()) {
        synthetic_mode_ = true;
        return true;
    }

    if (source_.rfind("mvs:", 0) == 0) {
        int index = 0;
        std::string idx = source_.substr(4);
        if (!idx.empty()) {
            index = std::stoi(idx);
        }
        return openMvs(index);
    }

    bool is_numeric = !source_.empty() && std::all_of(source_.begin(), source_.end(), ::isdigit);
    if (is_numeric) {
        cap_.open(std::stoi(source_));
    } else {
        cap_.open(source_);
    }

    if (!cap_.isOpened()) {
        if (is_numeric) {
            int index = std::stoi(source_);
            if (openMvs(index)) {
                return true;
            }
        }
        return false;
    }

    if (width_ > 0) {
        cap_.set(cv::CAP_PROP_FRAME_WIDTH, width_);
    }
    if (height_ > 0) {
        cap_.set(cv::CAP_PROP_FRAME_HEIGHT, height_);
    }
    return true;
}

bool CameraProvider::read(FrameData& out, uint64_t frame_id) {
    out.frame_id = frame_id;
    out.timestamp = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count()) /
                     1000.0;

    if (synthetic_mode_) {
        out.image = cv::Mat::zeros(height_, width_, CV_8UC3);
        cv::Point center(width_ / 2 + static_cast<int>(100 * std::sin(frame_id * 0.03)),
                         height_ / 2 + static_cast<int>(40 * std::cos(frame_id * 0.02)));
        cv::Size size(220, 60);
        float angle = static_cast<float>(std::fmod(frame_id * 0.8, 180.0));
        cv::RotatedRect rr(center, size, angle);
        cv::Point2f pts[4];
        rr.points(pts);
        for (int i = 0; i < 4; ++i) {
            cv::line(out.image, pts[i], pts[(i + 1) % 4], cv::Scalar(230, 230, 230), 2);
        }
        cv::circle(out.image, center, 3, cv::Scalar(0, 255, 255), -1);
        return true;
    }

    if (mvs_mode_) {
#ifdef USE_MVS
        MV_FRAME_OUT frame;
        std::memset(&frame, 0, sizeof(frame));
        int nRet = MV_CC_GetImageBuffer(mvs_handle_, &frame, 1000);
        if (nRet != MV_OK) {
            return false;
        }

        unsigned int w = frame.stFrameInfo.nExtendWidth;
        unsigned int h = frame.stFrameInfo.nExtendHeight;
        unsigned int need = w * h * 3 + 2048;
        if (mvs_bgr_buffer_.size() < need) {
            mvs_bgr_buffer_.resize(need);
        }

        MV_CC_PIXEL_CONVERT_PARAM stConvertParam;
        std::memset(&stConvertParam, 0, sizeof(stConvertParam));
        stConvertParam.nWidth = w;
        stConvertParam.nHeight = h;
        stConvertParam.pSrcData = frame.pBufAddr;
        stConvertParam.nSrcDataLen = frame.stFrameInfo.nFrameLenEx;
        stConvertParam.enSrcPixelType = frame.stFrameInfo.enPixelType;
        stConvertParam.enDstPixelType = PixelType_Gvsp_BGR8_Packed;
        stConvertParam.pDstBuffer = mvs_bgr_buffer_.data();
        stConvertParam.nDstBufferSize = need;

        nRet = MV_CC_ConvertPixelType(mvs_handle_, &stConvertParam);
        if (nRet != MV_OK) {
            MV_CC_FreeImageBuffer(mvs_handle_, &frame);
            return false;
        }

        cv::Mat bgr(static_cast<int>(h), static_cast<int>(w), CV_8UC3, mvs_bgr_buffer_.data());
        out.image = bgr.clone();

        MV_CC_FreeImageBuffer(mvs_handle_, &frame);
        return true;
#else
        return false;
#endif
    }

    return cap_.read(out.image);
}
