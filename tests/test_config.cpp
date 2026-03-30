#include "config.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

struct ScopedTempDir {
    std::filesystem::path path;

    explicit ScopedTempDir(std::string name)
        : path(std::filesystem::temp_directory_path() / ("hik_yoloobb_" + std::move(name))) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }

    ~ScopedTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

void writeTextFile(const std::filesystem::path& file, const std::string& content) {
    std::ofstream ofs(file);
    ofs << content;
    ofs.close();
}

bool containsText(const std::string& text, const std::string& token) {
    return text.find(token) != std::string::npos;
}

}  // namespace

bool TestConfigResolvesRelativePathsAndClampIntervals() {
    ScopedTempDir dir("config_ok");

    const auto model = dir.path / "model.onnx";
    const auto classes = dir.path / "classes.txt";
    const auto calib = dir.path / "calib.yaml";
    writeTextFile(model, "dummy");
    writeTextFile(classes, "part\n");
    writeTextFile(calib, "dummy\n");

    const auto cfg_path = dir.path / "system.yaml";
    writeTextFile(cfg_path,
                  "%YAML:1.0\n"
                  "app:\n"
                  "  input_source: \"\"\n"
                  "  onnx_model_path: \"model.onnx\"\n"
                  "  class_names_path: \"classes.txt\"\n"
                  "  calibration_file: \"calib.yaml\"\n"
                  "  infer_interval: 0\n"
                  "  measure_interval: -2\n"
                  "  render_interval: 0\n"
                  "  conf_threshold: 0.5\n"
                  "  nms_threshold: 0.4\n"
                  "  frame_width: 640\n"
                  "  frame_height: 480\n"
                  "  queue_size: 4\n"
                  "  caliper_length: 20\n"
                  "  caliper_half_width: 2\n"
                  "  gaussian_sigma: 1.2\n"
                  "  caliper_search_scale: 1.1\n");

    const AppConfig cfg = LoadConfig(cfg_path.string());

    if (cfg.infer_interval != 1 || cfg.measure_interval != 1 || cfg.render_interval != 1) {
        return false;
    }

    const auto expected_model = std::filesystem::weakly_canonical(model).string();
    const auto expected_classes = std::filesystem::weakly_canonical(classes).string();
    const auto expected_calib = std::filesystem::weakly_canonical(calib).string();

    return cfg.onnx_model_path == expected_model &&
           cfg.class_names_path == expected_classes &&
           cfg.calibration_file == expected_calib;
}

bool TestConfigThrowsOnInvalidThreshold() {
    ScopedTempDir dir("config_invalid_threshold");

    const auto cfg_path = dir.path / "bad.yaml";
    writeTextFile(cfg_path,
                  "%YAML:1.0\n"
                  "app:\n"
                  "  conf_threshold: 1.2\n"
                  "  nms_threshold: 0.3\n"
                  "  frame_width: 640\n"
                  "  frame_height: 480\n"
                  "  queue_size: 4\n"
                  "  infer_interval: 1\n"
                  "  measure_interval: 1\n"
                  "  render_interval: 1\n"
                  "  caliper_length: 20\n"
                  "  caliper_half_width: 2\n"
                  "  gaussian_sigma: 1.0\n"
                  "  caliper_search_scale: 1.0\n");

    try {
        (void)LoadConfig(cfg_path.string());
    } catch (const std::runtime_error& e) {
        return containsText(e.what(), "conf_threshold");
    }

    return false;
}

bool TestConfigThrowsWhenMissingAppNode() {
    ScopedTempDir dir("config_missing_app");

    const auto cfg_path = dir.path / "missing_app.yaml";
    writeTextFile(cfg_path, "%YAML:1.0\nnot_app:\n  key: value\n");

    try {
        (void)LoadConfig(cfg_path.string());
    } catch (const std::runtime_error& e) {
        return containsText(e.what(), "app 节点");
    }

    return false;
}
