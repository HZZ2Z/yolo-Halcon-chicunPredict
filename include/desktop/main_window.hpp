#pragma once

#include "app/pipeline_runner.hpp"
#include "config.hpp"
#include "desktop/measurement_database.hpp"
#include "desktop/desktop_widgets.hpp"

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QStackedWidget>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVector>

#include <atomic>
#include <mutex>
#include <thread>

class QCloseEvent;
class QComboBox;

class MainWindow : public QMainWindow {
public:
    MainWindow(AppConfig cfg,
               MeasurementDatabase* database,
               DesktopUser user,
               QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    QWidget* buildLivePage();
    QWidget* buildHistoryPage();
    QWidget* buildSettingsPage();
    QWidget* makePanel(const QString& title, QWidget* content);
    void navigateTo(int index);

    void startMeasurement();
    void stopMeasurement();
    void pollPipeline();
    void refreshHistory();
    void exportHistory();
    void showSessionDetails(int row);
    void deleteSelectedHistory();
    void clearHistory();
    void updateButtonState();
    void addUserFromSettings();
    void finalizeCurrentSession(const QString& status);
    QList<qint64> selectedHistorySessionIds() const;

    AppConfig currentConfigFromUi() const;
    void displayFrame(const cv::Mat& image);
    void saveMeasurementIfNeeded(const OBBResult& tracked, const MeasurementResult& measurement);

    AppConfig base_cfg_;
    MeasurementDatabase* database_ = nullptr;
    DesktopUser user_;

    QLabel* image_label_ = nullptr;
    QLabel* status_label_ = nullptr;
    QLabel* user_label_ = nullptr;
    MetricCard* px_card_ = nullptr;
    MetricCard* mm_card_ = nullptr;
    MetricCard* sigma_card_ = nullptr;
    MetricCard* scans_card_ = nullptr;
    MetricCard* quality_card_ = nullptr;
    StatusPill* run_status_pill_ = nullptr;
    StatusPill* quality_pill_ = nullptr;
    QListWidget* nav_list_ = nullptr;
    QStackedWidget* content_stack_ = nullptr;
    int history_tab_index_ = -1;
    QPushButton* start_button_ = nullptr;
    QPushButton* stop_button_ = nullptr;
    QPushButton* reconnect_button_ = nullptr;
    QPushButton* clear_button_ = nullptr;

    QTableWidget* history_table_ = nullptr;
    QLineEdit* history_user_filter_ = nullptr;
    QComboBox* history_quality_filter_ = nullptr;
    QSpinBox* history_limit_spin_ = nullptr;

    QLineEdit* input_source_edit_ = nullptr;
    QSpinBox* max_frames_spin_ = nullptr;
    QSpinBox* display_width_spin_ = nullptr;
    QSpinBox* display_height_spin_ = nullptr;
    QLineEdit* new_user_edit_ = nullptr;
    QLineEdit* new_password_edit_ = nullptr;
    QComboBox* new_role_combo_ = nullptr;

    QTimer poll_timer_;
    std::thread pipeline_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};

    std::mutex latest_mutex_;
    cv::Mat latest_image_;
    OBBResult latest_tracked_;
    MeasurementResult latest_measurement_;
    bool latest_dirty_ = false;
    qint64 current_session_id_ = -1;
    QVector<qint64> history_session_ids_;
    uint64_t last_saved_measurement_frame_ = 0;
};
