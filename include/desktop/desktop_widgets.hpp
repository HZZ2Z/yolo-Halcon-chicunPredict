#pragma once

#include <QFrame>
#include <QLabel>
#include <QWidget>

class QTimer;

class MetricCard : public QFrame {
public:
    enum class Tone {
        Neutral,
        Good,
        Warning,
        Danger
    };

    explicit MetricCard(const QString& title, QWidget* parent = nullptr);

    void setValue(const QString& value);
    void setSubtitle(const QString& subtitle);
    void setTone(Tone tone);

private:
    QLabel* title_ = nullptr;
    QLabel* value_ = nullptr;
    QLabel* subtitle_ = nullptr;
    Tone tone_ = Tone::Neutral;
};

class StatusPill : public QLabel {
public:
    enum class Tone {
        Idle,
        Good,
        Warning,
        Danger
    };

    explicit StatusPill(QWidget* parent = nullptr);
    void setStatus(const QString& text, Tone tone);
};

class AnimatedBackdropWidget : public QWidget {
public:
    explicit AnimatedBackdropWidget(QWidget* parent = nullptr);
    ~AnimatedBackdropWidget() override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QTimer* timer_ = nullptr;
    qreal phase_ = 0.0;
};
