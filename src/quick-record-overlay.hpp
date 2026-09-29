#pragma once
#include <QObject>
#include <QTimer>
#include <memory>
#include <vector>
#include "capture-region.hpp"
class QuickRecordOverlay : public QObject {
    Q_OBJECT
public:
    QuickRecordOverlay();
    ~QuickRecordOverlay() override;
    void open();
    void hide();
    void message(const QString &message);
    const CaptureTarget &target() const { return selected; }
signals:
    void confirmed();
    void canceled();
    void settingsRequested();
    void selectionReady();
    void selectionReset();
protected:
    bool eventFilter(QObject *, QEvent *) override;
private:
    class Surface;
    friend class Surface;
    void chooseMode(CaptureKind);
    void hover();
    void press(const QPoint &physical);
    void release(const QPoint &physical);
    void repaint();
    std::vector<MonitorInfo> monitors;
    std::vector<std::unique_ptr<Surface>> surfaces;
    CaptureTarget selected;
    CaptureKind mode = CaptureKind::Monitor;
    QTimer hoverTimer;
    QString status;
    bool ready = false;
    bool dragging = false;
    QPoint dragStart;
};
