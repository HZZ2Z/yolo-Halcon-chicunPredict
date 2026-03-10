#pragma once

#include "types.hpp"

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

class CameraProvider {
public:
    CameraProvider(const std::string& source, int width, int height);
    ~CameraProvider();
    bool open();
    bool read(FrameData& out, uint64_t frame_id);

private:
    bool openMvs(int index);
    void closeMvs();

    std::string source_;
    int width_;
    int height_;
    cv::VideoCapture cap_;
    bool synthetic_mode_ = false;
    bool mvs_mode_ = false;

    void* mvs_handle_ = nullptr;
    bool mvs_initialized_ = false;
    std::vector<unsigned char> mvs_bgr_buffer_;
};
