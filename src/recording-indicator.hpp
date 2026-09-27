#pragma once
#include <QLabel>
#include <QTimer>
#include <QElapsedTimer>
#include "capture-region.hpp"
class RecordingIndicator : public QLabel {
public:
    RecordingIndicator();
    void start(const MonitorInfo &monitor);
    void stop();
private:
    QTimer timer;
    QElapsedTimer elapsed;
};
