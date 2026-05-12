#include "config.hpp"
#include "desktop/desktop_theme.hpp"
#include "desktop/desktop_widgets.hpp"
#include "desktop/main_window.hpp"
#include "desktop/measurement_database.hpp"

#include <QApplication>
#include <QDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <exception>

namespace {

class SetupAdminDialog : public QDialog {
public:
    explicit SetupAdminDialog(MeasurementDatabase* database, QWidget* parent = nullptr)
        : QDialog(parent), database_(database) {
        setWindowTitle("初始化管理员");
        resize(980, 600);
        auto* root = new QHBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto* backdrop = new AnimatedBackdropWidget(this);
        backdrop->setMinimumWidth(520);
        auto* hero = new QVBoxLayout(backdrop);
        hero->setContentsMargins(42, 44, 42, 44);
        hero->addStretch(1);
        auto* product = new QLabel("METAL METROLOGY", backdrop);
        product->setObjectName("AppTitle");
        auto* desc = new QLabel("Local industrial vision measurement console", backdrop);
        desc->setObjectName("Muted");
        auto* line = new QLabel("Create the first administrator to unlock production access.", backdrop);
        line->setStyleSheet("color:#dffbff;font-size:18px;font-weight:700;");
        hero->addWidget(product);
        hero->addSpacing(8);
        hero->addWidget(desc);
        hero->addSpacing(24);
        hero->addWidget(line);
        hero->addStretch(1);
        root->addWidget(backdrop, 1);

        auto* right = new QWidget(this);
        auto* right_layout = new QVBoxLayout(right);
        right_layout->setContentsMargins(42, 42, 42, 42);
        right_layout->addStretch(1);

        auto* card = new QFrame(right);
        card->setObjectName("StyledCard");
        card->setStyleSheet(DesktopTheme::cardStyle("#2b7f95"));
        auto* card_layout = new QVBoxLayout(card);
        card_layout->setContentsMargins(28, 26, 28, 26);
        card_layout->setSpacing(14);
        auto* title = new QLabel("初始化管理员", card);
        title->setObjectName("SectionTitle");
        auto* hint = new QLabel("首次启动需要创建本机管理员账号", card);
        hint->setObjectName("Muted");
        card_layout->addWidget(title);
        card_layout->addWidget(hint);

        auto* form = new QFormLayout();
        username_ = new QLineEdit("admin", this);
        password_ = new QLineEdit(this);
        password_->setEchoMode(QLineEdit::Password);
        confirm_ = new QLineEdit(this);
        confirm_->setEchoMode(QLineEdit::Password);
        form->addRow("用户名", username_);
        form->addRow("密码", password_);
        form->addRow("确认密码", confirm_);
        card_layout->addLayout(form);

        auto* create_button = new QPushButton("创建管理员", card);
        create_button->setObjectName("PrimaryButton");
        auto* cancel_button = new QPushButton("退出", card);
        connect(create_button, &QPushButton::clicked, this, [this]() { createAdmin(); });
        connect(cancel_button, &QPushButton::clicked, this, &QDialog::reject);
        card_layout->addWidget(create_button);
        card_layout->addWidget(cancel_button);
        right_layout->addWidget(card);
        right_layout->addStretch(1);
        root->addWidget(right);
    }

private:
    void createAdmin() {
        if (username_->text().trimmed().isEmpty() || password_->text().isEmpty()) {
            QMessageBox::warning(this, "创建失败", "用户名和密码不能为空");
            return;
        }
        if (password_->text() != confirm_->text()) {
            QMessageBox::warning(this, "创建失败", "两次密码不一致");
            return;
        }
        if (!database_->createUser(username_->text(), password_->text(), "admin")) {
            QMessageBox::warning(this, "创建失败", "管理员创建失败");
            return;
        }
        accept();
    }

    MeasurementDatabase* database_ = nullptr;
    QLineEdit* username_ = nullptr;
    QLineEdit* password_ = nullptr;
    QLineEdit* confirm_ = nullptr;
};

class LoginDialog : public QDialog {
public:
    explicit LoginDialog(MeasurementDatabase* database, QWidget* parent = nullptr)
        : QDialog(parent), database_(database) {
        setWindowTitle("登录");
        resize(980, 600);
        auto* root = new QHBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto* backdrop = new AnimatedBackdropWidget(this);
        backdrop->setMinimumWidth(540);
        auto* hero = new QVBoxLayout(backdrop);
        hero->setContentsMargins(46, 48, 46, 48);
        hero->addStretch(1);
        auto* title = new QLabel("METAL METROLOGY", backdrop);
        title->setObjectName("AppTitle");
        auto* subtitle = new QLabel("Precision measurement. Local data. Production ready.", backdrop);
        subtitle->setObjectName("Muted");
        auto* status = new QLabel("VISION MEASUREMENT CONSOLE", backdrop);
        status->setStyleSheet("color:#64efff;font-size:15px;font-weight:800;letter-spacing:0px;");
        hero->addWidget(status);
        hero->addSpacing(18);
        hero->addWidget(title);
        hero->addSpacing(10);
        hero->addWidget(subtitle);
        hero->addStretch(1);
        root->addWidget(backdrop, 1);

        auto* right = new QWidget(this);
        auto* right_layout = new QVBoxLayout(right);
        right_layout->setContentsMargins(42, 42, 42, 42);
        right_layout->addStretch(1);

        auto* card = new QFrame(right);
        card->setObjectName("StyledCard");
        card->setStyleSheet(DesktopTheme::cardStyle("#2b7f95"));
        auto* card_layout = new QVBoxLayout(card);
        card_layout->setContentsMargins(30, 28, 30, 28);
        card_layout->setSpacing(14);
        auto* login_title = new QLabel("本机登录", card);
        login_title->setObjectName("SectionTitle");
        auto* login_hint = new QLabel("请输入授权账号进入测量系统", card);
        login_hint->setObjectName("Muted");
        card_layout->addWidget(login_title);
        card_layout->addWidget(login_hint);

        auto* form = new QFormLayout();
        username_ = new QLineEdit(this);
        password_ = new QLineEdit(this);
        password_->setEchoMode(QLineEdit::Password);
        form->addRow("用户名", username_);
        form->addRow("密码", password_);
        card_layout->addLayout(form);

        auto* login_button = new QPushButton("登录系统", card);
        login_button->setObjectName("PrimaryButton");
        auto* cancel_button = new QPushButton("退出", card);
        connect(login_button, &QPushButton::clicked, this, [this]() { authenticate(); });
        connect(cancel_button, &QPushButton::clicked, this, &QDialog::reject);
        card_layout->addWidget(login_button);
        card_layout->addWidget(cancel_button);
        right_layout->addWidget(card);
        right_layout->addStretch(1);
        root->addWidget(right);
    }

    const DesktopUser& user() const {
        return user_;
    }

private:
    void authenticate() {
        if (!database_->authenticate(username_->text(), password_->text(), user_)) {
            QMessageBox::warning(this, "登录失败", "用户名或密码错误");
            return;
        }
        accept();
    }

    MeasurementDatabase* database_ = nullptr;
    QLineEdit* username_ = nullptr;
    QLineEdit* password_ = nullptr;
    DesktopUser user_;
};

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    DesktopTheme::apply(app);

    try {
        std::string config_path = "config/system.yaml";
        if (argc > 1) {
            config_path = argv[1];
        }
        AppConfig cfg = LoadConfig(config_path);

        MeasurementDatabase database;
        if (!database.open("data/metrology.db")) {
            QMessageBox::critical(nullptr, "启动失败", "无法打开本地数据库 data/metrology.db");
            return 2;
        }

        if (!database.hasUsers()) {
            SetupAdminDialog setup(&database);
            if (setup.exec() != QDialog::Accepted) {
                return 1;
            }
        }

        LoginDialog login(&database);
        if (login.exec() != QDialog::Accepted) {
            return 1;
        }

        MainWindow window(cfg, &database, login.user());
        window.show();
        return app.exec();
    } catch (const std::exception& e) {
        QMessageBox::critical(nullptr, "启动失败", e.what());
        return 2;
    }
}
