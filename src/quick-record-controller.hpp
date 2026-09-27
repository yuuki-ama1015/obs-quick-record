#pragma once
#include <QObject>
#include <QTimer>
#include <obs-frontend-api.h>
#include "capture-controller.hpp"
#include "hotkey-manager.hpp"
#include "settings.hpp"
#include "quick-record-overlay.hpp"
#include <QPointer>
class QAction;
class SettingsWindow;

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
    void begin();
    void showSettings();
    void notify(const char *key);
    QuickRecordState state = QuickRecordState::Idle;
    bool pending = false;
    bool shuttingDown = false;
    Settings settings;
    CaptureController capture;
    HotkeyManager hotkey;
    QTimer startTimeout;
    QTimer countdown;
    int seconds = 0;
    QuickRecordOverlay overlay;
    QPointer<SettingsWindow> settingsWindow;
    QPointer<QAction> toolsAction;
};
