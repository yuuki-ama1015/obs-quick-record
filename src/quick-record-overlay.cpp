#include "quick-record-overlay.hpp"
#include "settings.hpp"
#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
QuickRecordOverlay::QuickRecordOverlay() : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    auto *layout = new QVBoxLayout(this);
    auto *bar = new QHBoxLayout;
    bar->addStretch();
    for (const char *key : {"Region", "Window", "Monitor", "Settings", "Cancel"}) {
        auto *button = new QPushButton(text(key));
        button->setMinimumHeight(40);
        button->setAutoDefault(false);
        bar->addWidget(button);
        if (QString::fromLatin1(key) == "Settings") connect(button, &QPushButton::clicked, this, &QuickRecordOverlay::settingsRequested);
        else if (QString::fromLatin1(key) == "Cancel") connect(button, &QPushButton::clicked, this, &QuickRecordOverlay::canceled);
    }
    bar->addStretch();
    layout->addLayout(bar);
    label = new QLabel(text("SelectHint"));
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet("color:white; background:#252525; padding:12px;");
    layout->addWidget(label, 0, Qt::AlignHCenter);
    layout->addStretch();
}
void QuickRecordOverlay::open()
{
    setGeometry(QApplication::primaryScreen()->geometry());
    label->setText(text("SelectHint"));
    show(); raise(); activateWindow(); setFocus();
}
void QuickRecordOverlay::message(const QString &value) { label->setText(value); }
void QuickRecordOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat()) return;
    if (event->key() == Qt::Key_Escape) emit canceled();
    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) emit confirmed();
    else QWidget::keyPressEvent(event);
}
void QuickRecordOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) { message(text("ReadyHint")); emit selectionReady(); }
}
void QuickRecordOverlay::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 90));
}
