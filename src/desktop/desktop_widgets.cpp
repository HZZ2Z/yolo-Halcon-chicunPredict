#include "desktop/desktop_widgets.hpp"

#include "desktop/desktop_theme.hpp"

#include <QColor>
#include <QFont>
#include <QGridLayout>
#include <QLinearGradient>
#include <QPainter>
#include <QPen>
#include <QRadialGradient>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace {

QString toneColor(MetricCard::Tone tone) {
    switch (tone) {
    case MetricCard::Tone::Good:
        return "#35f2b7";
    case MetricCard::Tone::Warning:
        return "#ffb84d";
    case MetricCard::Tone::Danger:
        return "#ff6b7a";
    case MetricCard::Tone::Neutral:
    default:
        return "#38d7ef";
    }
}

QString pillStyle(StatusPill::Tone tone) {
    QString bg = "#132435";
    QString fg = "#aebfd0";
    QString border = "#30465c";
    if (tone == StatusPill::Tone::Good) {
        bg = "#0b332d";
        fg = "#56f0c1";
        border = "#1f8a72";
    } else if (tone == StatusPill::Tone::Warning) {
        bg = "#3b2a0e";
        fg = "#ffc15c";
        border = "#a97823";
    } else if (tone == StatusPill::Tone::Danger) {
        bg = "#3a161b";
        fg = "#ff8490";
        border = "#9a3b45";
    }
    return QString("QLabel { background:%1; color:%2; border:1px solid %3; "
                   "border-radius:12px; padding:5px 12px; font-weight:700; }")
        .arg(bg, fg, border);
}

} // namespace

MetricCard::MetricCard(const QString& title, QWidget* parent)
    : QFrame(parent) {
    setObjectName("StyledCard");
    setStyleSheet(DesktopTheme::cardStyle("#223142"));
    setMinimumHeight(88);
    setMaximumHeight(104);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(4);

    title_ = new QLabel(title, this);
    title_->setObjectName("MetricTitle");
    value_ = new QLabel("N/A", this);
    value_->setObjectName("MetricValue");
    subtitle_ = new QLabel("-", this);
    subtitle_->setObjectName("MetricSubtitle");

    QFont value_font = value_->font();
    value_font.setPointSize(18);
    value_font.setBold(true);
    value_->setFont(value_font);

    layout->addWidget(title_);
    layout->addWidget(value_);
    layout->addStretch(1);
    layout->addWidget(subtitle_);
    setTone(Tone::Neutral);
}

void MetricCard::setValue(const QString& value) {
    value_->setText(value);
}

void MetricCard::setSubtitle(const QString& subtitle) {
    subtitle_->setText(subtitle);
}

void MetricCard::setTone(Tone tone) {
    tone_ = tone;
    const QString color = toneColor(tone_);
    value_->setStyleSheet(QString("QLabel#MetricValue { color:%1; background:transparent; border:0; padding:0; }").arg(color));
    setStyleSheet(DesktopTheme::cardStyle(color));
}

StatusPill::StatusPill(QWidget* parent)
    : QLabel(parent) {
    setAlignment(Qt::AlignCenter);
    setStatus("IDLE", Tone::Idle);
}

void StatusPill::setStatus(const QString& text, Tone tone) {
    setText(text);
    setStyleSheet(pillStyle(tone));
}

AnimatedBackdropWidget::AnimatedBackdropWidget(QWidget* parent)
    : QWidget(parent),
      timer_(new QTimer(this)) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    connect(timer_, &QTimer::timeout, this, [this]() {
        phase_ += 0.018;
        if (phase_ > 1000.0) {
            phase_ = 0.0;
        }
        update();
    });
    timer_->start(33);
}

AnimatedBackdropWidget::~AnimatedBackdropWidget() = default;

void AnimatedBackdropWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient bg(rect().topLeft(), rect().bottomRight());
    bg.setColorAt(0.0, QColor("#06101b"));
    bg.setColorAt(0.48, QColor("#0a1d2f"));
    bg.setColorAt(1.0, QColor("#02060b"));
    painter.fillRect(rect(), bg);

    const int grid = 42;
    const qreal drift = std::fmod(phase_ * 24.0, static_cast<qreal>(grid));
    painter.setPen(QPen(QColor(39, 115, 143, 70), 1));
    for (int x = -grid; x < width() + grid; x += grid) {
        painter.drawLine(QPointF(x + drift, 0), QPointF(x - width() * 0.18 + drift, height()));
    }
    for (int y = -grid; y < height() + grid; y += grid) {
        painter.drawLine(QPointF(0, y + drift), QPointF(width(), y - height() * 0.08 + drift));
    }

    QLinearGradient sweep(0, 0, width(), 0);
    sweep.setColorAt(0.0, QColor(0, 214, 239, 0));
    sweep.setColorAt(0.5, QColor(0, 214, 239, 90));
    sweep.setColorAt(1.0, QColor(0, 214, 239, 0));
    painter.setPen(QPen(QBrush(sweep), 2.0));
    const qreal y = std::fmod(phase_ * 190.0, static_cast<qreal>(height() + 180)) - 90.0;
    painter.drawLine(QPointF(-80, y), QPointF(width() + 80, y + height() * 0.16));

    painter.setPen(Qt::NoPen);
    for (int i = 0; i < 28; ++i) {
        const qreal t = phase_ + i * 0.71;
        const qreal x = std::fmod(i * 97.0 + std::sin(t) * 40.0 + phase_ * 38.0,
                                  static_cast<qreal>(width() + 80)) - 40.0;
        const qreal py = std::fmod(i * 53.0 + std::cos(t * 0.7) * 28.0,
                                   static_cast<qreal>(height() + 60)) - 30.0;
        QColor dot = (i % 3 == 0) ? QColor(255, 184, 77, 120) : QColor(56, 215, 239, 115);
        painter.setBrush(dot);
        painter.drawEllipse(QPointF(x, py), 2.0, 2.0);
    }

    QRadialGradient glow(QPointF(width() * 0.72, height() * 0.22), width() * 0.72);
    glow.setColorAt(0.0, QColor(0, 214, 239, 55));
    glow.setColorAt(0.42, QColor(0, 214, 239, 18));
    glow.setColorAt(1.0, QColor(0, 0, 0, 0));
    painter.fillRect(rect(), glow);
}
