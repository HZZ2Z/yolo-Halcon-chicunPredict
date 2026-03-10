#include "onnx_inferencer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>

#ifdef USE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

OnnxObbInferencer::OnnxObbInferencer(const std::string& model_path, float conf_thresh, float nms_thresh)
    : model_path_(model_path), conf_thresh_(conf_thresh), nms_thresh_(nms_thresh) {}

bool OnnxObbInferencer::init() {
#ifdef USE_ONNXRUNTIME
    if (model_path_.empty()) {
        std::cerr << "[ONNX] 模型路径为空" << std::endl;
        return false;
    }
    try {
        auto* env = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "metal_metrology");
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        OrtCUDAProviderOptions cuda_options{};
        cuda_options.device_id = 0;
        cuda_options.gpu_mem_limit = SIZE_MAX;
        cuda_options.arena_extend_strategy = 0;
        cuda_options.do_copy_in_default_stream = 1;
        try {
            options.AppendExecutionProvider_CUDA(cuda_options);
            std::cout << "[ONNX] CUDA Execution Provider 已启用" << std::endl;
        } catch (const Ort::Exception& e) {
            std::cerr << "[ONNX] CUDA Execution Provider 启用失败: " << e.what() << std::endl;
            std::cerr << "[ONNX] 当前配置要求 GPU 推理，程序退出。" << std::endl;
            delete env;
            return false;
        }

        auto* session = new Ort::Session(*env, model_path_.c_str(), options);

        Ort::AllocatorWithDefaultOptions allocator;
        auto input_name_ptr = session->GetInputNameAllocated(0, allocator);
        auto output_name_ptr = session->GetOutputNameAllocated(0, allocator);
        input_name_ = input_name_ptr.get();
        output_name_ = output_name_ptr.get();

        env_ = env;
        session_ = session;
        return true;
    } catch (const Ort::Exception& e) {
        std::cerr << "[ONNX] 初始化异常: " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "[ONNX] 初始化异常: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "[ONNX] 初始化异常: unknown error" << std::endl;
        return false;
    }
#else
    return false;
#endif
}

std::vector<OBBResult> OnnxObbInferencer::infer(const cv::Mat& image) {
#ifdef USE_ONNXRUNTIME
    if (!session_) {
        return {};
    }

    try {
        auto* session = reinterpret_cast<Ort::Session*>(session_);

        int in_h = model_input_h_ > 0 ? model_input_h_ : 1280;
        int in_w = model_input_w_ > 0 ? model_input_w_ : 1280;

        model_input_w_ = in_w;
        model_input_h_ = in_h;

        float scale = std::min(static_cast<float>(in_w) / std::max(1, image.cols),
                       static_cast<float>(in_h) / std::max(1, image.rows));
        int new_w = std::max(1, static_cast<int>(std::round(image.cols * scale)));
        int new_h = std::max(1, static_cast<int>(std::round(image.rows * scale)));
        int pad_w = std::max(0, in_w - new_w);
        int pad_h = std::max(0, in_h - new_h);
        int left = pad_w / 2;
        int right = pad_w - left;
        int top = pad_h / 2;
        int bottom = pad_h - top;

        letterbox_scale_ = scale;
        letterbox_pad_x_ = static_cast<float>(left);
        letterbox_pad_y_ = static_cast<float>(top);

        cv::Mat resized;
        cv::resize(image, resized, cv::Size(new_w, new_h), 0, 0, cv::INTER_LINEAR);
        cv::Mat padded;
        cv::copyMakeBorder(resized, padded, top, bottom, left, right, cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));
        cv::Mat blob = cv::dnn::blobFromImage(padded, 1.0 / 255.0, cv::Size(in_w, in_h), cv::Scalar(), true, false, CV_32F);

        const size_t chw_size = blob.total();
        if (input_buffer_.size() != chw_size) {
            input_buffer_.assign(chw_size, 0.0f);
        }
        std::memcpy(input_buffer_.data(), blob.ptr<float>(), chw_size * sizeof(float));

        std::array<int64_t, 4> input_shape = {1, 3, in_h, in_w};
        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(memory_info, input_buffer_.data(), input_buffer_.size(),
                                                                   input_shape.data(), input_shape.size());

        const char* input_names[] = {input_name_.c_str()};
        const char* output_names[] = {output_name_.c_str()};

        auto outputs = session->Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);
        auto& out0 = outputs[0];
        auto info = out0.GetTensorTypeAndShapeInfo();
        auto shape = info.GetShape();
        float* out_ptr = out0.GetTensorMutableData<float>();

        return rotatedNms(decodeOrtOutputs(image, out_ptr, shape));
    } catch (const Ort::Exception& e) {
        std::cerr << "[ONNX-infer] runtime exception: " << e.what() << std::endl;
        return {};
    } catch (const std::exception& e) {
        std::cerr << "[ONNX-infer] runtime exception: " << e.what() << std::endl;
        return {};
    } catch (...) {
        std::cerr << "[ONNX-infer] runtime exception: unknown" << std::endl;
        return {};
    }
#else
    (void)image;
    return {};
#endif
}

std::vector<OBBResult> OnnxObbInferencer::decodeMock(const cv::Mat& image) const {
    std::vector<OBBResult> out;

    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0.8);

    cv::Mat bin;
    cv::threshold(gray, bin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    for (const auto& contour : contours) {
        if (cv::contourArea(contour) < 500.0) {
            continue;
        }
        auto rr = cv::minAreaRect(contour);
        OBBResult r;
        r.rrect = rr;
        r.class_id = 0;
        r.confidence = 0.8f;
        out.push_back(r);
    }
    return out;
}

std::vector<OBBResult> OnnxObbInferencer::decodeOrtOutputs(const cv::Mat& image,
                                                           const float* output_data,
                                                           const std::vector<int64_t>& output_shape) const {
    std::vector<OBBResult> out;
    if (!output_data || output_shape.size() != 3) {
        return out;
    }

    const int64_t n = output_shape[1];
    const int64_t d = output_shape[2];
    if (n <= 0 || d != 7) {
        return out;
    }

    float inv_scale = 1.0f / std::max(1e-6f, letterbox_scale_);
    for (int64_t i = 0; i < n; ++i) {
        const float* row = output_data + i * d;
        float conf = row[4];
        if (conf < conf_thresh_) {
            continue;
        }

        float cx = (row[0] - letterbox_pad_x_) * inv_scale;
        float cy = (row[1] - letterbox_pad_y_) * inv_scale;
        float w = row[2] * inv_scale;
        float h = row[3] * inv_scale;

        cx = std::clamp(cx, 0.0f, static_cast<float>(image.cols - 1));
        cy = std::clamp(cy, 0.0f, static_cast<float>(image.rows - 1));
        w = std::clamp(w, 2.0f, static_cast<float>(image.cols));
        h = std::clamp(h, 2.0f, static_cast<float>(image.rows));

        OBBResult r;
        r.rrect.center = cv::Point2f(cx, cy);
        r.rrect.size = cv::Size2f(w, h);
        r.rrect.angle = rad2deg(row[6]);
        r.class_id = static_cast<int>(std::round(row[5]));
        r.confidence = conf;
        out.push_back(r);
    }

    return out;
}

std::vector<OBBResult> OnnxObbInferencer::rotatedNms(const std::vector<OBBResult>& input) const {
    if (input.size() <= 1) {
        return input;
    }

    std::vector<OBBResult> sorted = input;
    std::sort(sorted.begin(), sorted.end(), [](const OBBResult& a, const OBBResult& b) {
        return a.confidence > b.confidence;
    });

    std::vector<OBBResult> kept;
    std::vector<char> removed(sorted.size(), 0);

    auto iou_rotated = [](const cv::RotatedRect& a, const cv::RotatedRect& b) {
        std::vector<cv::Point2f> inter;
        int code = cv::rotatedRectangleIntersection(a, b, inter);
        if (code == cv::INTERSECT_NONE || inter.empty()) {
            return 0.0f;
        }
        float inter_area = std::abs(cv::contourArea(inter));
        float union_area = a.size.area() + b.size.area() - inter_area;
        if (union_area <= 1e-6f) {
            return 0.0f;
        }
        return inter_area / union_area;
    };

    for (size_t i = 0; i < sorted.size(); ++i) {
        if (removed[i]) {
            continue;
        }
        kept.push_back(sorted[i]);
        for (size_t j = i + 1; j < sorted.size(); ++j) {
            if (removed[j]) {
                continue;
            }
            if (sorted[i].class_id != sorted[j].class_id) {
                continue;
            }
            float iou = iou_rotated(sorted[i].rrect, sorted[j].rrect);
            if (iou > nms_thresh_) {
                removed[j] = 1;
            }
        }
    }

    return kept;
}
