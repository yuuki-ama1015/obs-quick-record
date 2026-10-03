#include "quick-record-overlay.hpp"
#include "monitor-selector.hpp"
#include "region-selector.hpp"
#include "window-selector.hpp"
#include "settings.hpp"
#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#include <windows.h>

class QuickRecordOverlay::Surface : public QWidget {
public:
    QuickRecordOverlay &owner;
    MonitorInfo monitor;
    QLabel *label;
    Surface(QuickRecordOverlay &owner, MonitorInfo monitor)
        : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint), owner(owner), monitor(monitor)
    {
        setWindowTitle(text("Title") + " — " + monitor.device);
        setAttribute(Qt::WA_TranslucentBackground);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        auto *layout = new QVBoxLayout(this);
        auto *bar = new QHBoxLayout;
        bar->addStretch();
        auto button = [&](const char *key, auto action) {
            auto *b = new QPushButton(text(key));
            b->setMinimumHeight(40);
            b->setAutoDefault(false);
            connect(b, &QPushButton::clicked, &owner, action);
            bar->addWidget(b);
        };
        button("Region", [&owner] { owner.chooseMode(CaptureKind::Region); });
        button("Window", [&owner] { owner.chooseMode(CaptureKind::Window); });
        button("Monitor", [&owner] { owner.chooseMode(CaptureKind::Monitor); });
        button("Settings", [&owner] { emit owner.settingsRequested(); });
        button("Cancel", [&owner] { emit owner.canceled(); });
        bar->addStretch();
        layout->addLayout(bar);
        label = new QLabel;
        label->setAlignment(Qt::AlignCenter);
        label->setWordWrap(true);
        label->setStyleSheet("color:white; background:#252525; padding:12px;");
        label->setMaximumWidth(750);
        layout->addWidget(label, 0, Qt::AlignHCenter);
        layout->addStretch();
        setGeometry(monitor.screen->geometry()); // Qt logical pixels.
    }
    QRectF localRect(const QRect &physical) const
    {
        // Subtract the physical monitor origin BEFORE scaling to local logical pixels.
        return RegionSelector::toLogical(physical, monitor.physical, size());
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0,0,0,95));
        if (!owner.selected.physical.isEmpty()) {
            auto selection = localRect(owner.selected.physical);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.fillRect(selection, QColor(0,0,0,1)); // Nonzero alpha preserves mouse hit testing.
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            p.setPen(QPen(QColor("#ff5252"), 3));
            p.drawRect(selection.adjusted(1,1,-1,-1));
        }
    }
    QPoint physicalPoint(const QMouseEvent *e) const
    {
        return RegionSelector::toPhysical(e->position(), monitor.physical, size());
    }
    void mousePressEvent(QMouseEvent *e) override { if (e->button() == Qt::LeftButton) owner.press(physicalPoint(e)); }
    void mouseReleaseEvent(QMouseEvent *e) override { if (e->button() == Qt::LeftButton) owner.release(physicalPoint(e)); }
    void closeEvent(QCloseEvent *event) override
    {
        owner.hide();
        emit owner.canceled();
        event->accept();
    }
};
QuickRecordOverlay::QuickRecordOverlay()
{
    connect(&hoverTimer, &QTimer::timeout, this, &QuickRecordOverlay::hover);
    qApp->installEventFilter(this);
}
QuickRecordOverlay::~QuickRecordOverlay() { qApp->removeEventFilter(this); }
void QuickRecordOverlay::open()
{
    hide();
    surfaces.clear();
    monitors = MonitorSelector::enumerate();
    ready = false;
    selected = {};
    mode = CaptureKind::Region;
    dragging = false;
    status = text("SelectHint");
    for (const auto &monitor : monitors) {
        auto surface = std::make_unique<Surface>(*this, monitor);
        surface->show();
        surface->setCursor(Qt::CrossCursor);
        // Selection UI is hidden before preparing capture. Only the recording indicator needs display affinity.
        surfaces.push_back(std::move(surface));
    }
    if (surfaces.empty()) { emit canceled(); return; }
    auto *focus = surfaces.front().get();
    const auto position = MonitorSelector::cursor();
    for (auto &surface : surfaces) if (surface->monitor.physical.contains(position)) focus = surface.get();
    focus->raise(); focus->activateWindow(); focus->setFocus();
    hoverTimer.start(16);
    repaint();
}
void QuickRecordOverlay::hide()
{
    hoverTimer.stop();
    dragging = false;
    for (auto &surface : surfaces) surface->hide();
}
void QuickRecordOverlay::message(const QString &value) { status = value; repaint(); }
bool QuickRecordOverlay::eventFilter(QObject *object, QEvent *event)
{
    auto *widget = qobject_cast<QWidget *>(object);
    bool ours = false;
    for (const auto &surface : surfaces)
        if (surface->isVisible() && widget && widget->window() == surface.get()) ours = true;
    if (!ours || event->type() != QEvent::KeyPress) return false;
    auto *key = static_cast<QKeyEvent *>(event);
    if (key->isAutoRepeat()) return false;
    if (key->key() == Qt::Key_Escape) { emit canceled(); return true; }
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
        if (ready) emit confirmed();
        return true;
    }
    return false;
}
void QuickRecordOverlay::chooseMode(CaptureKind value)
{
    mode = value;
    ready = false;
    dragging = false;
    selected = {};
    status = text("SelectHint");
    emit selectionReset();
    for (auto &surface : surfaces) surface->setCursor(mode == CaptureKind::Region ? Qt::CrossCursor : Qt::ArrowCursor);
    repaint();
}
void QuickRecordOverlay::hover()
{
    if (ready) return;
    if (mode == CaptureKind::Monitor) selected = MonitorSelector::at(MonitorSelector::cursor(), monitors);
    else if (mode == CaptureKind::Window) selected = WindowSelector::at(MonitorSelector::cursor(), monitors);
    else if (mode == CaptureKind::Region && dragging) {
        selected = RegionSelector::between(dragStart, MonitorSelector::cursor(), monitors);
        status = QString("%1 × %2\n").arg(selected.physical.width()).arg(selected.physical.height()) + text("SelectHint");
    }
    repaint();
}
void QuickRecordOverlay::press(const QPoint &physical)
{
    ready = false;
    if (mode == CaptureKind::Region) { dragging = true; dragStart = physical; }
    emit selectionReset();
    hover();
}
void QuickRecordOverlay::release(const QPoint &physical)
{
    if (mode == CaptureKind::Region) selected = RegionSelector::between(dragStart, physical, monitors);
    else hover();
    dragging = false;
    if (!selected.valid()) {
        status = text(mode == CaptureKind::Region && selected.physical.width() >= 2 && selected.physical.height() >= 2 ? "CrossMonitor" : "SelectHint");
        repaint();
        return;
    }
    ready = true;
    status = selected.title + "\n" + QString("%1 × %2\n").arg(selected.physical.width()).arg(selected.physical.height()) + text("ReadyHint");
    repaint();
    emit selectionReady();
}
void QuickRecordOverlay::repaint()
{
    for (auto &surface : surfaces) { surface->label->setText(status); surface->update(); }
}
