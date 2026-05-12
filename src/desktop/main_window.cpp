#include "desktop/main_window.hpp"

#include <QApplication>
#include <QAbstractItemView>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImage>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QScrollBar>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace {

QString formatFloat(float value, int precision = 3) {
    if (!std::isfinite(value)) {
        return "N/A";
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return QString::fromStdString(oss.str());
}

QImage matToImage(const cv::Mat& image) {
    if (image.empty()) {
        return {};
    }
    if (image.type() == CV_8UC3) {
        cv::Mat rgb;
        cv::cvtColor(image, rgb, cv::COLOR_BGR2RGB);
        return QImage(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step),
                      QImage::Format_RGB888).copy();
    }
    if (image.type() == CV_8UC1) {
        return QImage(image.data, image.cols, image.rows, static_cast<int>(image.step),
                      QImage::Format_Grayscale8).copy();
    }
    cv::Mat normalized;
    image.convertTo(normalized, CV_8U);
    return QImage(normalized.data, normalized.cols, normalized.rows, static_cast<int>(normalized.step),
                  QImage::Format_Grayscale8).copy();
}

QTableWidgetItem* item(const QString& text) {
    return new QTableWidgetItem(text);
}

} // namespace

MainWindow::MainWindow(AppConfig cfg,
                       MeasurementDatabase* database,
                       DesktopUser user,
                       QWidget* parent)
    : QMainWindow(parent),
      base_cfg_(std::move(cfg)),
      database_(database),
      user_(std::move(user)) {
    buildUi();
    connect(&poll_timer_, &QTimer::timeout, this, [this]() { pollPipeline(); });
    poll_timer_.start(40);
    refreshHistory();
}

MainWindow::~MainWindow() {
    stopMeasurement();
    if (pipeline_thread_.joinable()) {
        pipeline_thread_.join();
    }
    finalizeCurrentSession("STOPPED");
}

void MainWindow::closeEvent(QCloseEvent* event) {
    stopMeasurement();
    QMainWindow::closeEvent(event);
}

void MainWindow::buildUi() {
    setWindowTitle("Metal Metrology Production Console");
    setMinimumSize(1500, 920);
    resize(1600, 960);

    auto* shell = new QWidget(this);
    auto* root = new QHBoxLayout(shell);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* sidebar = new QFrame(shell);
    sidebar->setObjectName("Sidebar");
    sidebar->setFixedWidth(250);
    auto* side_layout = new QVBoxLayout(sidebar);
    side_layout->setContentsMargins(18, 22, 18, 18);
    side_layout->setSpacing(16);

    auto* brand = new QLabel("METAL\nMETROLOGY", sidebar);
    brand->setObjectName("AppTitle");
    brand->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    side_layout->addWidget(brand);

    auto* brand_sub = new QLabel("Industrial Vision System", sidebar);
    brand_sub->setObjectName("Muted");
    side_layout->addWidget(brand_sub);

    nav_list_ = new QListWidget(sidebar);
    nav_list_->addItem("实时测量");
    nav_list_->addItem("历史记录");
    nav_list_->addItem("系统设置");
    nav_list_->setCurrentRow(0);
    nav_list_->setFixedHeight(250);
    nav_list_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    nav_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    side_layout->addWidget(nav_list_);
    side_layout->addStretch(1);

    user_label_ = new QLabel(user_.username + " / " + user_.role, sidebar);
    user_label_->setObjectName("Muted");
    side_layout->addWidget(user_label_);

    auto* logout_button = new QPushButton("退出登录", sidebar);
    connect(logout_button, &QPushButton::clicked, this, [this]() {
        stopMeasurement();
        QApplication::quit();
    });
    side_layout->addWidget(logout_button);
    root->addWidget(sidebar);

    auto* main_area = new QWidget(shell);
    auto* main_layout = new QVBoxLayout(main_area);
    main_layout->setContentsMargins(22, 18, 22, 22);
    main_layout->setSpacing(16);

    auto* top_bar = new QFrame(main_area);
    top_bar->setObjectName("TopBar");
    auto* top_layout = new QHBoxLayout(top_bar);
    top_layout->setContentsMargins(18, 12, 18, 12);
    top_layout->setSpacing(12);
    status_label_ = new QLabel("本机生产测量控制台", top_bar);
    status_label_->setObjectName("SectionTitle");
    run_status_pill_ = new StatusPill(top_bar);
    run_status_pill_->setStatus("STOPPED", StatusPill::Tone::Idle);
    quality_pill_ = new StatusPill(top_bar);
    quality_pill_->setStatus("WAITING", StatusPill::Tone::Idle);
    top_layout->addWidget(status_label_, 1);
    top_layout->addWidget(run_status_pill_);
    top_layout->addWidget(quality_pill_);
    main_layout->addWidget(top_bar);

    content_stack_ = new QStackedWidget(main_area);
    content_stack_->addWidget(buildLivePage());
    history_tab_index_ = content_stack_->addWidget(buildHistoryPage());
    content_stack_->addWidget(buildSettingsPage());
    main_layout->addWidget(content_stack_, 1);

    connect(nav_list_, &QListWidget::currentRowChanged, this, [this](int index) {
        navigateTo(index);
    });

    root->addWidget(main_area, 1);
    setCentralWidget(shell);
    updateButtonState();
}

QWidget* MainWindow::buildLivePage() {
    auto* page = new QWidget(this);
    auto* root = new QHBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(16);

    auto* image_panel = new QFrame(page);
    image_panel->setObjectName("ImagePanel");
    auto* image_layout = new QVBoxLayout(image_panel);
    image_layout->setContentsMargins(14, 14, 14, 14);
    image_layout->setSpacing(10);

    auto* image_header = new QHBoxLayout();
    auto* image_title = new QLabel("实时视觉画面", image_panel);
    image_title->setObjectName("SectionTitle");
    auto* image_hint = new QLabel("OBB / Edge Points / Quality Overlay", image_panel);
    image_hint->setObjectName("Muted");
    image_header->addWidget(image_title);
    image_header->addStretch(1);
    image_header->addWidget(image_hint);
    image_layout->addLayout(image_header);

    image_label_ = new QLabel("等待启动测量", image_panel);
    image_label_->setAlignment(Qt::AlignCenter);
    image_label_->setMinimumSize(800, 600);
    image_label_->setStyleSheet(
        "QLabel { background:#02070d; color:#5f7489; border:1px solid #183047; "
        "border-radius:8px; font-size:18px; font-weight:700; }");
    image_layout->addWidget(image_label_, 1);
    root->addWidget(image_panel, 1);

    auto* side_panel = new QFrame(page);
    side_panel->setObjectName("Panel");
    side_panel->setFixedWidth(315);
    auto* side = new QVBoxLayout(side_panel);
    side->setContentsMargins(14, 14, 14, 14);
    side->setSpacing(10);

    auto* side_title = new QLabel("测量指标", side_panel);
    side_title->setObjectName("SectionTitle");
    side->addWidget(side_title);

    px_card_ = new MetricCard("像素距离 px", side_panel);
    mm_card_ = new MetricCard("毫米距离 mm", side_panel);
    sigma_card_ = new MetricCard("不确定度 sigma", side_panel);
    scans_card_ = new MetricCard("有效扫描线 scans", side_panel);
    quality_card_ = new MetricCard("质量状态", side_panel);
    mm_card_->setTone(MetricCard::Tone::Good);
    quality_card_->setSubtitle("等待测量");

    start_button_ = new QPushButton("开始", page);
    start_button_->setObjectName("PrimaryButton");
    stop_button_ = new QPushButton("停止", page);
    stop_button_->setObjectName("DangerButton");
    reconnect_button_ = new QPushButton("重新连接", page);
    clear_button_ = new QPushButton("清空显示", page);

    connect(start_button_, &QPushButton::clicked, this, [this]() { startMeasurement(); });
    connect(stop_button_, &QPushButton::clicked, this, [this]() { stopMeasurement(); });
    connect(reconnect_button_, &QPushButton::clicked, this, [this]() {
        stopMeasurement();
        startMeasurement();
    });
    connect(clear_button_, &QPushButton::clicked, this, [this]() {
        image_label_->setPixmap(QPixmap());
        image_label_->setText("显示已清空");
        px_card_->setValue("N/A");
        mm_card_->setValue("N/A");
        sigma_card_->setValue("N/A");
        scans_card_->setValue("N/A");
        quality_card_->setValue("N/A");
        quality_card_->setSubtitle("等待测量");
        quality_pill_->setStatus("WAITING", StatusPill::Tone::Idle);
    });

    side->addWidget(px_card_);
    side->addWidget(mm_card_);
    side->addWidget(sigma_card_);
    side->addWidget(scans_card_);
    side->addWidget(quality_card_);
    side->addSpacing(8);
    side->addWidget(start_button_);
    side->addWidget(stop_button_);
    side->addWidget(reconnect_button_);
    side->addWidget(clear_button_);
    side->addStretch(1);
    root->addWidget(side_panel);

    updateButtonState();
    return page;
}

QWidget* MainWindow::buildHistoryPage() {
    auto* page = new QWidget(this);
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(14);

    auto* summary = new QFrame(page);
    summary->setObjectName("Panel");
    auto* summary_layout = new QHBoxLayout(summary);
    summary_layout->setContentsMargins(16, 12, 16, 12);
    auto* title = new QLabel("运行历史", summary);
    title->setObjectName("SectionTitle");
    auto* hint = new QLabel("一次运行保存为一条记录，双击可查看每一帧明细", summary);
    hint->setObjectName("Muted");
    summary_layout->addWidget(title);
    summary_layout->addStretch(1);
    summary_layout->addWidget(hint);
    root->addWidget(summary);

    auto* filter_panel = new QFrame(page);
    filter_panel->setObjectName("Panel");
    auto* filters = new QHBoxLayout();
    filters->setContentsMargins(14, 12, 14, 12);
    filter_panel->setLayout(filters);
    history_user_filter_ = new QLineEdit(page);
    history_user_filter_->setPlaceholderText("用户过滤，留空表示全部");
    history_quality_filter_ = new QComboBox(page);
    history_quality_filter_->addItem("全部", "");
    history_quality_filter_->addItem("已完成", "COMPLETED");
    history_quality_filter_->addItem("运行中", "RUNNING");
    history_quality_filter_->addItem("历史兼容", "LEGACY");
    history_limit_spin_ = new QSpinBox(page);
    history_limit_spin_->setRange(1, 10000);
    history_limit_spin_->setValue(500);
    auto* refresh_button = new QPushButton("刷新", page);
    auto* detail_button = new QPushButton("查看帧明细", page);
    auto* export_button = new QPushButton("导出 CSV", page);
    auto* delete_button = new QPushButton("删除选中", page);
    delete_button->setObjectName("DangerButton");
    auto* clear_all_button = new QPushButton("一键清空", page);
    clear_all_button->setObjectName("DangerButton");
    connect(refresh_button, &QPushButton::clicked, this, [this]() { refreshHistory(); });
    connect(detail_button, &QPushButton::clicked, this, [this]() {
        if (history_table_ && history_table_->currentRow() >= 0) {
            showSessionDetails(history_table_->currentRow());
        }
    });
    connect(export_button, &QPushButton::clicked, this, [this]() { exportHistory(); });
    connect(delete_button, &QPushButton::clicked, this, [this]() { deleteSelectedHistory(); });
    connect(clear_all_button, &QPushButton::clicked, this, [this]() { clearHistory(); });

    filters->addWidget(history_user_filter_);
    filters->addWidget(history_quality_filter_);
    filters->addWidget(history_limit_spin_);
    filters->addWidget(refresh_button);
    filters->addWidget(detail_button);
    filters->addWidget(export_button);
    filters->addWidget(delete_button);
    filters->addWidget(clear_all_button);
    root->addWidget(filter_panel);

    history_table_ = new QTableWidget(page);
    history_table_->setColumnCount(12);
    history_table_->setHorizontalHeaderLabels({
        "开始时间", "结束时间", "用户", "状态", "总帧", "OK帧", "低质量帧",
        "mean_px", "mean_mm", "mean_sigma", "mean_scans", "配置"
    });
    history_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    history_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    history_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    history_table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(history_table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        showSessionDetails(row);
    });
    auto* table_panel = new QFrame(page);
    table_panel->setObjectName("Panel");
    auto* table_layout = new QVBoxLayout(table_panel);
    table_layout->setContentsMargins(14, 14, 14, 14);
    table_layout->addWidget(history_table_, 1);
    root->addWidget(table_panel, 1);
    return page;
}

QWidget* MainWindow::buildSettingsPage() {
    auto* page = new QWidget(this);
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(14);

    auto* header = new QFrame(page);
    header->setObjectName("Panel");
    auto* header_layout = new QVBoxLayout(header);
    header_layout->setContentsMargins(16, 12, 16, 12);
    auto* title = new QLabel("系统设置", header);
    title->setObjectName("SectionTitle");
    auto* hint = new QLabel("仅提供显示和运行入口参数，核心测量算法仍由配置文件管理", header);
    hint->setObjectName("Muted");
    header_layout->addWidget(title);
    header_layout->addWidget(hint);
    root->addWidget(header);

    auto* runtime_box = new QGroupBox("运行设置（本次启动生效）", page);
    auto* form = new QFormLayout(runtime_box);
    input_source_edit_ = new QLineEdit(QString::fromStdString(base_cfg_.input_source), runtime_box);
    max_frames_spin_ = new QSpinBox(runtime_box);
    max_frames_spin_->setRange(-1, 10000000);
    max_frames_spin_->setValue(base_cfg_.max_frames);
    display_width_spin_ = new QSpinBox(runtime_box);
    display_width_spin_->setRange(320, 4096);
    display_width_spin_->setValue(base_cfg_.display_max_width);
    display_height_spin_ = new QSpinBox(runtime_box);
    display_height_spin_->setRange(240, 4096);
    display_height_spin_->setValue(base_cfg_.display_max_height);
    form->addRow("相机源", input_source_edit_);
    form->addRow("最大帧数", max_frames_spin_);
    form->addRow("显示宽度", display_width_spin_);
    form->addRow("显示高度", display_height_spin_);
    root->addWidget(runtime_box);

    if (user_.role == "admin") {
        auto* user_box = new QGroupBox("用户管理", page);
        auto* user_form = new QFormLayout(user_box);
        new_user_edit_ = new QLineEdit(user_box);
        new_password_edit_ = new QLineEdit(user_box);
        new_password_edit_->setEchoMode(QLineEdit::Password);
        new_role_combo_ = new QComboBox(user_box);
        new_role_combo_->addItems({"operator", "admin"});
        auto* add_button = new QPushButton("新增用户", user_box);
        connect(add_button, &QPushButton::clicked, this, [this]() { addUserFromSettings(); });
        user_form->addRow("用户名", new_user_edit_);
        user_form->addRow("密码", new_password_edit_);
        user_form->addRow("角色", new_role_combo_);
        user_form->addRow(add_button);
        root->addWidget(user_box);
    }

    root->addStretch(1);
    return page;
}

QWidget* MainWindow::makePanel(const QString& title, QWidget* content) {
    auto* panel = new QFrame(this);
    panel->setObjectName("Panel");
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14, 14, 14, 14);
    auto* label = new QLabel(title, panel);
    label->setObjectName("SectionTitle");
    layout->addWidget(label);
    layout->addWidget(content, 1);
    return panel;
}

void MainWindow::navigateTo(int index) {
    if (!content_stack_ || index < 0 || index >= content_stack_->count()) {
        return;
    }
    content_stack_->setCurrentIndex(index);
    if (index == history_tab_index_) {
        refreshHistory();
    }
}

void MainWindow::startMeasurement() {
    if (running_.load()) {
        return;
    }
    if (pipeline_thread_.joinable()) {
        pipeline_thread_.join();
    }

    AppConfig cfg = currentConfigFromUi();
    cfg.show_window = false;
    stop_requested_.store(false);
    running_.store(true);
    current_session_id_ = database_->startSession(user_.username,
                                                  "desktop-v1",
                                                  QString::fromStdString(cfg.calibration_file));
    last_saved_measurement_frame_ = 0;
    status_label_->setText("正在测量 / " + user_.username);
    if (run_status_pill_) {
        run_status_pill_->setStatus("RUNNING", StatusPill::Tone::Good);
    }
    updateButtonState();

    pipeline_thread_ = std::thread([this, cfg]() {
        PipelineCallbacks callbacks;
        callbacks.should_stop = [this]() { return stop_requested_.load(); };
        callbacks.on_frame = [this](const cv::Mat& image,
                                    const OBBResult& tracked,
                                    const MeasurementResult& measurement) {
            std::lock_guard<std::mutex> lock(latest_mutex_);
            latest_image_ = image.clone();
            latest_tracked_ = tracked;
            latest_measurement_ = measurement;
            latest_dirty_ = true;
        };
        RunPipeline(cfg, &callbacks);
        running_.store(false);
    });
}

void MainWindow::stopMeasurement() {
    stop_requested_.store(true);
    if (!running_.load() && pipeline_thread_.joinable()) {
        pipeline_thread_.join();
    }
    updateButtonState();
}

void MainWindow::pollPipeline() {
    if (!running_.load() && pipeline_thread_.joinable()) {
        pipeline_thread_.join();
        status_label_->setText("本机生产测量控制台");
        if (run_status_pill_) {
            run_status_pill_->setStatus("STOPPED", StatusPill::Tone::Idle);
        }
        finalizeCurrentSession("COMPLETED");
        updateButtonState();
        refreshHistory();
    }

    cv::Mat image;
    OBBResult tracked;
    MeasurementResult measurement;
    {
        std::lock_guard<std::mutex> lock(latest_mutex_);
        if (!latest_dirty_) {
            return;
        }
        image = latest_image_.clone();
        tracked = latest_tracked_;
        measurement = latest_measurement_;
        latest_dirty_ = false;
    }

    displayFrame(image);
    if (px_card_) {
        px_card_->setValue(formatFloat(measurement.pixel_distance, 6));
        px_card_->setSubtitle("pixel distance");
    }
    if (mm_card_) {
        mm_card_->setValue(formatFloat(measurement.world_distance_mm, 6));
        mm_card_->setSubtitle("world plane result");
    }
    if (sigma_card_) {
        sigma_card_->setValue(formatFloat(measurement.world_sigma_mm, 6));
        sigma_card_->setSubtitle("mm uncertainty");
        sigma_card_->setTone(measurement.quality_ok ? MetricCard::Tone::Good : MetricCard::Tone::Warning);
    }
    if (scans_card_) {
        scans_card_->setValue(QString::number(measurement.valid_scan_count));
        scans_card_->setSubtitle("valid scan lines");
        scans_card_->setTone(measurement.valid_scan_count >= 5 ? MetricCard::Tone::Good : MetricCard::Tone::Warning);
    }
    const QString quality = QString::fromStdString(measurement.quality_reason);
    if (quality_card_) {
        quality_card_->setValue(measurement.quality_ok ? "OK" : "LOW");
        quality_card_->setSubtitle(quality);
        quality_card_->setTone(measurement.quality_ok ? MetricCard::Tone::Good : MetricCard::Tone::Warning);
    }
    if (quality_pill_) {
        quality_pill_->setStatus(measurement.quality_ok ? "QUALITY OK" : "LOW QUALITY",
                                 measurement.quality_ok ? StatusPill::Tone::Good : StatusPill::Tone::Warning);
    }
    saveMeasurementIfNeeded(tracked, measurement);
}

void MainWindow::refreshHistory() {
    const QList<StoredRunSession> rows = database_->querySessions(
        history_user_filter_ ? history_user_filter_->text().trimmed() : QString(),
        history_quality_filter_ ? history_quality_filter_->currentData().toString() : QString(),
        history_limit_spin_ ? history_limit_spin_->value() : 500);

    if (!history_table_) {
        return;
    }
    history_session_ids_.clear();
    history_table_->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        const StoredRunSession& r = rows[row];
        history_session_ids_.push_back(r.id);
        history_table_->setItem(row, 0, item(r.started_at.toString("yyyy-MM-dd HH:mm:ss.zzz")));
        history_table_->setItem(row, 1, item(r.ended_at.isValid() ? r.ended_at.toString("yyyy-MM-dd HH:mm:ss.zzz") : "-"));
        history_table_->setItem(row, 2, item(r.username));
        history_table_->setItem(row, 3, item(r.status));
        history_table_->setItem(row, 4, item(QString::number(r.total_frames)));
        history_table_->setItem(row, 5, item(QString::number(r.ok_frames)));
        history_table_->setItem(row, 6, item(QString::number(r.low_quality_frames)));
        history_table_->setItem(row, 7, item(formatFloat(r.mean_px, 6)));
        history_table_->setItem(row, 8, item(formatFloat(r.mean_mm, 6)));
        history_table_->setItem(row, 9, item(formatFloat(r.mean_sigma, 6)));
        history_table_->setItem(row, 10, item(formatFloat(r.mean_scans, 3)));
        history_table_->setItem(row, 11, item(r.config_version));
    }
}

void MainWindow::exportHistory() {
    const int selected_row = history_table_ ? history_table_->currentRow() : -1;
    const QString path = QFileDialog::getSaveFileName(
        this, "导出测量记录", "measurement_records.csv", "CSV (*.csv)");
    if (path.isEmpty()) {
        return;
    }
    bool ok = false;
    if (selected_row >= 0 && selected_row < history_session_ids_.size()) {
        ok = database_->exportSessionFramesCsv(path, history_session_ids_[selected_row]);
    } else {
        ok = database_->exportSessionsCsv(path,
                                          history_user_filter_->text().trimmed(),
                                          history_quality_filter_->currentData().toString(),
                                          history_limit_spin_->value());
    }
    if (!ok) {
        QMessageBox::warning(this, "导出失败", "无法写入 CSV 文件");
    }
}

QList<qint64> MainWindow::selectedHistorySessionIds() const {
    QList<qint64> ids;
    if (!history_table_) {
        return ids;
    }
    const QModelIndexList selected = history_table_->selectionModel()->selectedRows();
    for (const QModelIndex& index : selected) {
        const int row = index.row();
        if (row >= 0 && row < history_session_ids_.size()) {
            const qint64 id = history_session_ids_[row];
            if (id > 0 && !ids.contains(id)) {
                ids.push_back(id);
            }
        }
    }
    return ids;
}

void MainWindow::deleteSelectedHistory() {
    const QList<qint64> ids = selectedHistorySessionIds();
    if (ids.isEmpty()) {
        QMessageBox::information(this, "未选择记录", "请先选择要删除的历史记录。");
        return;
    }
    if (ids.contains(current_session_id_)) {
        QMessageBox::warning(this, "无法删除", "当前正在运行的测量记录不能删除，请先停止测量。");
        return;
    }

    const QString text = ids.size() == 1
        ? "确定删除选中的 1 条运行记录及其所有帧明细吗？"
        : "确定删除选中的 " + QString::number(ids.size()) + " 条运行记录及其所有帧明细吗？";
    if (QMessageBox::question(this, "确认删除", text) != QMessageBox::Yes) {
        return;
    }
    if (!database_->deleteSessions(ids)) {
        QMessageBox::warning(this, "删除失败", "数据库删除失败，请稍后重试。");
        return;
    }
    refreshHistory();
}

void MainWindow::clearHistory() {
    if (current_session_id_ > 0 || running_.load()) {
        QMessageBox::warning(this, "无法清空", "测量运行中不能清空历史记录，请先停止测量。");
        return;
    }
    if (QMessageBox::question(this,
                              "确认清空",
                              "确定清空全部历史运行记录和所有帧明细吗？此操作不可恢复。")
        != QMessageBox::Yes) {
        return;
    }
    if (!database_->deleteAllSessions()) {
        QMessageBox::warning(this, "清空失败", "数据库清空失败，请稍后重试。");
        return;
    }
    refreshHistory();
}

void MainWindow::showSessionDetails(int row) {
    if (row < 0 || row >= history_session_ids_.size()) {
        return;
    }
    const qint64 session_id = history_session_ids_[row];
    const QList<StoredMeasurement> frames = database_->querySessionFrames(session_id);

    QDialog dialog(this);
    dialog.setWindowTitle("运行记录帧明细 #" + QString::number(session_id));
    dialog.resize(1200, 720);
    auto* root = new QVBoxLayout(&dialog);
    auto* table = new QTableWidget(&dialog);
    table->setColumnCount(13);
    table->setHorizontalHeaderLabels({
        "frame", "时间", "用户", "px", "raw_mm", "mm", "sigma", "scans",
        "quality", "cx", "cy", "angle", "配置"
    });
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    table->verticalScrollBar()->setStyleSheet(
        "QScrollBar:vertical { background:#07111d; width:22px; margin:0; border-radius:10px; }"
        "QScrollBar::handle:vertical { background:#3f6682; min-height:54px; border-radius:10px; }"
        "QScrollBar::handle:vertical:hover { background:#58a6c6; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; background:transparent; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }");
    table->setRowCount(frames.size());
    for (int i = 0; i < frames.size(); ++i) {
        const StoredMeasurement& f = frames[i];
        table->setItem(i, 0, item(QString::number(f.frame_id)));
        table->setItem(i, 1, item(f.timestamp.toString("yyyy-MM-dd HH:mm:ss.zzz")));
        table->setItem(i, 2, item(f.username));
        table->setItem(i, 3, item(formatFloat(f.px, 6)));
        table->setItem(i, 4, item(formatFloat(f.raw_mm, 6)));
        table->setItem(i, 5, item(formatFloat(f.mm, 6)));
        table->setItem(i, 6, item(formatFloat(f.sigma, 6)));
        table->setItem(i, 7, item(QString::number(f.scans)));
        table->setItem(i, 8, item(f.quality));
        table->setItem(i, 9, item(formatFloat(f.cx, 3)));
        table->setItem(i, 10, item(formatFloat(f.cy, 3)));
        table->setItem(i, 11, item(formatFloat(f.angle, 3)));
        table->setItem(i, 12, item(f.config_version));
    }
    root->addWidget(table);
    dialog.exec();
}

void MainWindow::updateButtonState() {
    const bool running = running_.load();
    if (start_button_) {
        start_button_->setEnabled(!running);
    }
    if (stop_button_) {
        stop_button_->setEnabled(running);
    }
    if (reconnect_button_) {
        reconnect_button_->setEnabled(!running);
    }
    if (run_status_pill_) {
        run_status_pill_->setStatus(running ? "RUNNING" : "STOPPED",
                                    running ? StatusPill::Tone::Good : StatusPill::Tone::Idle);
    }
}

void MainWindow::addUserFromSettings() {
    if (!new_user_edit_ || !new_password_edit_ || !new_role_combo_) {
        return;
    }
    if (new_user_edit_->text().trimmed().isEmpty() || new_password_edit_->text().isEmpty()) {
        QMessageBox::warning(this, "新增失败", "用户名和密码不能为空");
        return;
    }
    if (!database_->createUser(new_user_edit_->text(),
                               new_password_edit_->text(),
                               new_role_combo_->currentText())) {
        QMessageBox::warning(this, "新增失败", "用户创建失败，可能用户名已存在");
        return;
    }
    new_user_edit_->clear();
    new_password_edit_->clear();
    QMessageBox::information(this, "新增成功", "用户已创建");
}

void MainWindow::finalizeCurrentSession(const QString& status) {
    if (current_session_id_ > 0) {
        database_->finishSession(current_session_id_, status);
        current_session_id_ = -1;
    }
}

AppConfig MainWindow::currentConfigFromUi() const {
    AppConfig cfg = base_cfg_;
    if (input_source_edit_) {
        cfg.input_source = input_source_edit_->text().toStdString();
    }
    if (max_frames_spin_) {
        cfg.max_frames = max_frames_spin_->value();
    }
    if (display_width_spin_) {
        cfg.display_max_width = display_width_spin_->value();
    }
    if (display_height_spin_) {
        cfg.display_max_height = display_height_spin_->value();
    }
    return cfg;
}

void MainWindow::displayFrame(const cv::Mat& image) {
    const QImage qimg = matToImage(image);
    if (qimg.isNull()) {
        return;
    }
    image_label_->setPixmap(QPixmap::fromImage(qimg).scaled(
        image_label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::saveMeasurementIfNeeded(const OBBResult& tracked,
                                         const MeasurementResult& measurement) {
    if (!measurement.valid || current_session_id_ <= 0 ||
        measurement.frame_id == last_saved_measurement_frame_) {
        return;
    }
    last_saved_measurement_frame_ = measurement.frame_id;

    StoredMeasurement record;
    record.session_id = current_session_id_;
    record.frame_id = static_cast<qint64>(measurement.frame_id);
    record.timestamp = QDateTime::currentDateTime();
    record.username = user_.username;
    record.px = measurement.pixel_distance;
    record.raw_mm = measurement.raw_world_distance_mm;
    record.mm = measurement.world_distance_mm;
    record.sigma = measurement.world_sigma_mm;
    record.scans = measurement.valid_scan_count;
    record.quality = QString::fromStdString(measurement.quality_reason);
    record.cx = tracked.rrect.center.x;
    record.cy = tracked.rrect.center.y;
    record.angle = tracked.rrect.angle;
    record.config_version = "desktop-v1";
    record.calibration_file = QString::fromStdString(currentConfigFromUi().calibration_file);
    database_->insertMeasurement(record);
}
