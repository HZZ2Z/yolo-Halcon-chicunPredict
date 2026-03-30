#include "app/pipeline_runner.hpp"
#include "config.hpp"
#include "logger.hpp"

int main(int argc, char** argv) {
    try {
        std::string config_path = "config/system.yaml";
        if (argc > 1) {
            config_path = argv[1];
        }

        AppConfig cfg = LoadConfig(config_path);
        logger::SetDebugEnabled(cfg.enable_diag_logs);
        return RunPipeline(cfg);
    } catch (const std::exception& e) {
        logger::Error(std::string("运行失败: ") + e.what());
        return 2;
    }
}
