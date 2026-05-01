#pragma once

#include "types.hpp"

#include <array>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

class OnnxObbInferencer {
public:
    OnnxObbInferencer(const std::string& model_path, float conf_thresh, float nms_thresh);
    ~OnnxObbInferencer();
    bool init();
    std::vector<OBBResult> infer(const cv::Mat& image);

private:
    std::vector<OBBResult> decodeMock(const cv::Mat& image) const;
    std::vector<OBBResult> decodeOrtOutputs(const cv::Mat& image, const float* output_data,
                                            const std::vector<int64_t>& output_shape) const;
    std::vector<OBBResult> rotatedNms(const std::vector<OBBResult>& input) const;

    std::string model_path_;
    float conf_thresh_;
    float nms_thresh_;
    int model_input_w_ = 1280;
    int model_input_h_ = 1280;
    float letterbox_scale_ = 1.0f;
    float letterbox_pad_x_ = 0.0f;
    float letterbox_pad_y_ = 0.0f;
    std::vector<float> input_buffer_;

#ifdef USE_ONNXRUNTIME
    void* env_ = nullptr;
    void* session_ = nullptr;
    std::string input_name_;
    std::string output_name_;
#endif
};
