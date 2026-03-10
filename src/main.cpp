#include "calibration.hpp"
#include "camera_provider.hpp"
#include "config.hpp"
#include "onnx_inferencer.hpp"
#include "subpixel_caliper.hpp"
#include "thread_safe_queue.hpp"
#include "tracker_ekf.hpp"
#include "types.hpp"

#include <iostream>
#include <atomic>
#include <cmath>
#include <thread>
#include <deque>

int main(int argc, char** argv) {
    try {
        std::string config_path = "config/system.yaml";
        if (argc > 1) {
            config_path = argv[1];
        }

        AppConfig cfg = LoadConfig(config_path);

        CameraProvider camera(cfg.input_source, cfg.frame_width, cfg.frame_height);
        if (!camera.open()) {
            std::cerr << "相机/视频源打开失败: " << cfg.input_source << std::endl;
            std::cerr << "提示: 若系统无 /dev/video*，说明设备可能不是 UVC 直出。" << std::endl;
            std::cerr << "请先运行 scripts/check_camera.sh 诊断；若为 Hikrobot 工业相机，建议接入 MVS SDK 采集。"
                      << std::endl;
            return 1;
        }

        CalibrationMapper calibration;
        const bool calibration_loaded = (!cfg.calibration_file.empty() &&
                                         calibration.load(cfg.calibration_file,
                                                          cfg.frame_width,
                                                          cfg.frame_height,
                                                          cfg.focal_length_mm,
                                                          cfg.pixel_size_um));
        if (!calibration_loaded) {
            std::cerr << "警告: 标定文件加载失败" << std::endl;
        }
        if (cfg.strict_calibration && !calibration.canMeasureInMm()) {
            std::cerr << "错误: strict_calibration=1 但当前标定无法完成毫米换算。" << std::endl;
            std::cerr << "HALCON 模式下请提供有效的相机参数(.cal)与位姿文件(.dat)。"
                      << std::endl;
            return 1;
        }

        OnnxObbInferencer inferencer(cfg.onnx_model_path, cfg.conf_threshold, cfg.nms_threshold);
        if (!inferencer.init()) {
            std::cerr << "ONNX 初始化失败：请确认已使用 USE_ONNXRUNTIME=ON 编译，且模型路径正确。" << std::endl;
            return 1;
        }

        SubpixelCaliper caliper(cfg.caliper_length,
                    cfg.caliper_half_width,
                    cfg.gaussian_sigma,
                    cfg.caliper_search_scale,
                    cfg.use_obb_adaptive_caliper,
                    cfg.measure_long_edge);
        ObbTracker tracker;

        ThreadSafeQueue<FrameData> queue(cfg.queue_size);

        std::atomic<bool> stop_requested(false);

        std::thread producer([&]() {
            uint64_t fid = 0;
            while (!stop_requested.load()) {
                if (cfg.max_frames > 0 && fid >= static_cast<uint64_t>(cfg.max_frames)) {
                    break;
                }
                FrameData frame;
                if (!camera.read(frame, fid)) {
                    break;
                }
                queue.push(std::move(frame));
                ++fid;
            }
            queue.close();
        });

        std::thread consumer([&]() {
            uint64_t processed_frames = 0;
            MeasurementResult last_measurement;
            std::deque<float> px_history;
            std::deque<float> mm_history;
            constexpr size_t smooth_window = 5;

            auto medianValue = [](const std::deque<float>& values) -> float {
                if (values.empty()) {
                    return 0.0f;
                }
                std::vector<float> sorted(values.begin(), values.end());
                std::sort(sorted.begin(), sorted.end());
                const size_t n = sorted.size();
                if (n % 2 == 1) {
                    return sorted[n / 2];
                }
                return 0.5f * (sorted[n / 2 - 1] + sorted[n / 2]);
            };

            while (true) {
                auto frame_opt = queue.pop();
                if (!frame_opt.has_value()) {
                    break;
                }
                FrameData frame = std::move(frame_opt.value());
                ++processed_frames;

                frame.image = calibration.undistort(frame.image);

                std::optional<OBBResult> best;
                const bool do_infer = ((processed_frames - 1) % static_cast<uint64_t>(cfg.infer_interval) == 0);
                if (do_infer) {
                    auto detections = inferencer.infer(frame.image);
                    if (!detections.empty()) {
                        best = *std::max_element(detections.begin(), detections.end(), [](const OBBResult& a, const OBBResult& b) {
                            return a.confidence < b.confidence;
                        });
                    }
                }

                OBBResult tracked = tracker.update(best);
                MeasurementResult mr;
                const bool do_measure = ((processed_frames - 1) % static_cast<uint64_t>(cfg.measure_interval) == 0);
                if (do_measure && tracked.class_id >= 0 && tracked.confidence > 0.0f &&
                    tracked.rrect.size.width > 1.0f && tracked.rrect.size.height > 1.0f) {
                    mr = caliper.measure(frame, tracked);
                }

                if (mr.valid) {
                    if (calibration.canMeasureInMm()) {
                        cv::Point2f w0 = calibration.pixelToWorld(mr.left_edge_px);
                        cv::Point2f w1 = calibration.pixelToWorld(mr.right_edge_px);
                        const float mm = cv::norm(w1 - w0);
                        if (std::isfinite(mm) && mm > 0.0f) {
                            mr.world_distance_mm = mm;
                        } else {
                            mr.world_distance_mm = -1.0f;
                        }
                    } else {
                        mr.world_distance_mm = -1.0f;
                    }

                    px_history.push_back(mr.pixel_distance);
                    if (px_history.size() > smooth_window) {
                        px_history.pop_front();
                    }
                    mr.pixel_distance = medianValue(px_history);

                    if (mr.world_distance_mm > 0.0f && std::isfinite(mr.world_distance_mm)) {
                        mm_history.push_back(mr.world_distance_mm);
                        if (mm_history.size() > smooth_window) {
                            mm_history.pop_front();
                        }
                        mr.world_distance_mm = medianValue(mm_history);
                    }

                    last_measurement = mr;

                }

                if (mr.valid) {
                    last_measurement = mr;
                }

                if (cfg.show_window) {
                    if ((processed_frames - 1) % static_cast<uint64_t>(cfg.render_interval) != 0) {
                        continue;
                    }
                    cv::Mat& vis = frame.image;
                    if (tracked.class_id >= 0 && tracked.confidence > 0.0f &&
                        tracked.rrect.size.width > 1.0f && tracked.rrect.size.height > 1.0f) {
                        cv::Point2f pts[4];
                        tracked.rrect.points(pts);
                        for (int i = 0; i < 4; ++i) {
                            cv::line(vis, pts[i], pts[(i + 1) % 4], cv::Scalar(0, 255, 0), 2);
                        }
                    }
                    if (last_measurement.valid) {
                        cv::circle(vis, last_measurement.left_edge_px, 3, cv::Scalar(255, 0, 0), -1);
                        cv::circle(vis, last_measurement.right_edge_px, 3, cv::Scalar(0, 0, 255), -1);
                    }
                    std::string px_text = last_measurement.valid ? ("px=" + std::to_string(last_measurement.pixel_distance)) : "px=N/A";
                    cv::putText(vis, px_text, cv::Point(20, 40), cv::FONT_HERSHEY_SIMPLEX, 0.8,
                                cv::Scalar(0, 255, 255), 2);
                    std::string mm_text = calibration.canMeasureInMm() ? ("mm=" + std::to_string(last_measurement.world_distance_mm))
                                                                      : "mm=N/A (need calibration)";
                    cv::putText(vis, mm_text, cv::Point(20, 75), cv::FONT_HERSHEY_SIMPLEX, 0.8,
                                cv::Scalar(255, 255, 0), 2);
                    cv::imshow("metal_metrology", vis);
                    int key = cv::waitKey(1);
                    if (key == 27 || key == 'q') {
                        stop_requested.store(true);
                        queue.close();
                        break;
                    }
                }
            }
        });

        producer.join();
        consumer.join();

        std::cout << "处理完成。" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "运行失败: " << e.what() << std::endl;
        return 2;
    }
}
