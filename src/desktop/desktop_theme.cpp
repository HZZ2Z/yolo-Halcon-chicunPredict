#include "desktop/desktop_theme.hpp"

#include <QApplication>
#include <QFont>

namespace DesktopTheme {

void apply(QApplication& app) {
    QFont font = app.font();
    font.setFamily("Noto Sans CJK SC");
    font.setPointSize(10);
    app.setFont(font);
    app.setStyleSheet(appStyleSheet());
}

QString cardStyle(const QString& border_color) {
    const QString border = border_color.isEmpty() ? "#223142" : border_color;
    return QString(
        "QFrame#StyledCard {"
        "background: rgba(17, 27, 39, 236);"
        "border: 1px solid %1;"
        "border-radius: 10px;"
        "}"
    ).arg(border);
}

QString appStyleSheet() {
    return QStringLiteral(R"(
        QMainWindow, QDialog, QWidget {
            background: #07111d;
            color: #dce8f5;
        }
        QLabel {
            color: #dce8f5;
            background: transparent;
        }
        QFrame#Sidebar {
            background: #081521;
            border-right: 1px solid #173146;
        }
        QFrame#TopBar, QFrame#Panel, QFrame#ImagePanel {
            background: rgba(12, 23, 35, 238);
            border: 1px solid #1e3549;
            border-radius: 10px;
        }
        QLabel#AppTitle {
            color: #f6fbff;
            font-size: 21px;
            font-weight: 700;
        }
        QLabel#SectionTitle {
            color: #f3f9ff;
            font-size: 17px;
            font-weight: 700;
        }
        QLabel#Muted {
            color: #7d92a8;
        }
        QLabel#MetricTitle {
            color: #88a0b7;
            font-size: 13px;
            font-weight: 600;
        }
        QLabel#MetricValue {
            background: transparent;
            border: 0;
            padding: 0;
            font-size: 18px;
            font-weight: 800;
        }
        QLabel#MetricSubtitle {
            color: #70879e;
            background: transparent;
            border: 0;
            padding: 0;
            font-size: 12px;
        }
        QPushButton {
            background: #102337;
            border: 1px solid #244764;
            border-radius: 8px;
            color: #e8f4ff;
            padding: 9px 14px;
            font-weight: 600;
        }
        QPushButton:hover {
            background: #15324d;
            border-color: #2d90bd;
        }
        QPushButton:pressed {
            background: #0b1c2d;
        }
        QPushButton:disabled {
            color: #607283;
            background: #0b1723;
            border-color: #162638;
        }
        QPushButton#PrimaryButton {
            background: #00a7c7;
            border-color: #36d7ef;
            color: #04111c;
        }
        QPushButton#PrimaryButton:hover {
            background: #15bfdc;
        }
        QPushButton#DangerButton {
            background: #3b1f22;
            border-color: #8b3a40;
            color: #ffd6d9;
        }
        QLineEdit, QSpinBox, QComboBox {
            background: #0a1a29;
            border: 1px solid #29445b;
            border-radius: 8px;
            color: #eff8ff;
            padding: 8px 10px;
            selection-background-color: #00a7c7;
        }
        QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
            border-color: #38d7ef;
        }
        QGroupBox {
            background: rgba(12, 23, 35, 238);
            border: 1px solid #1e3549;
            border-radius: 10px;
            margin-top: 18px;
            padding: 14px;
            font-weight: 700;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 6px;
            color: #8edff0;
        }
        QTableWidget {
            background: #091724;
            alternate-background-color: #0d1d2c;
            gridline-color: #183047;
            border: 1px solid #20384f;
            border-radius: 8px;
            color: #dce8f5;
        }
        QTableWidget::item {
            padding: 6px;
        }
        QTableWidget::item:selected {
            background: #0f6076;
            color: #ffffff;
        }
        QHeaderView::section {
            background: #102338;
            color: #9eddf0;
            border: 0;
            border-right: 1px solid #1f3a52;
            padding: 8px;
            font-weight: 700;
        }
        QListWidget {
            background: transparent;
            border: 0;
            outline: 0;
        }
        QListWidget::item {
            color: #9bb2c7;
            padding: 14px 16px;
            border-radius: 8px;
            margin: 3px 8px;
            font-weight: 600;
            font-size: 15px;
        }
        QListWidget::item:selected {
            background: #10314a;
            color: #eaffff;
            border-left: 3px solid #00d6ef;
        }
        QListWidget::item:hover {
            background: #0d2539;
        }
        QScrollBar:vertical {
            background: #07111d;
            width: 10px;
        }
        QScrollBar::handle:vertical {
            background: #254158;
            border-radius: 5px;
        }
    )");
}

} // namespace DesktopTheme
