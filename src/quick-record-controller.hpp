#pragma once
#include <QObject>
#include <QTimer>
#include <obs-frontend-api.h>
#include "capture-controller.hpp"
#include "hotkey-manager.hpp"
#include "settings.hpp"
#include "quick-record-overlay.hpp"
#include "recording-indicator.hpp"
#include <QPointer>
#include <QElapsedTimer>
#include <atomic>
#include "launcher-session.hpp"
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
    static void recordingStopped(void *, calldata_t *);
    void watchRecording();
    void unwatchRecording();
    obs_output_t *recordingOutput = nullptr;
    std::atomic<int> recordingResult{OBS_OUTPUT_ERROR};
    QuickRecordState state = QuickRecordState::Idle;
    bool pending = false;
    bool shuttingDown = false;
    bool shutdownSettingsSaved = false;
    bool frontendRegistered = false;
    LauncherSession launcherSession;
    Settings settings;
    CaptureController capture;
    HotkeyManager hotkey;
    QTimer startTimeout;
    QTimer countdown;
    QTimer prepareTimer;
    QTimer launcherTimer;
    QElapsedTimer launcherWait;
    QString launcherRequestPath;
    bool requested = false;
    bool externalRecording = false;
    RecordingIndicator indicator;
    int seconds = 0;
    QuickRecordOverlay overlay;
    QPointer<SettingsWindow> settingsWindow;
    QPointer<QAction> toolsAction;
};
