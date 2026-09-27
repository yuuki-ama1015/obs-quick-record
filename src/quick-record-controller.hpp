#pragma once
#include <QObject>
#include <QTimer>
#include <obs-frontend-api.h>
#include "capture-controller.hpp"
#include "hotkey-manager.hpp"

enum class QuickRecordState { Idle, Selecting, ReadyToRecord, Recording };
class QuickRecordController : public QObject {
public:
    QuickRecordController();
    ~QuickRecordController() override;
private:
    static void frontendEvent(obs_frontend_event, void *);
    void onEvent(obs_frontend_event);
    void toggle();
    void finish();
    QuickRecordState state = QuickRecordState::Idle;
    bool pending = false;
    bool shuttingDown = false;
    CaptureController capture;
    HotkeyManager hotkey;
    QTimer startTimeout;
};
