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
#include <atomic>
#include <memory>
#include "launcher-session.hpp"
#include "launcher-request-watcher.hpp"
#include "recording-quality.hpp"
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
    RecordingQuality recordingQuality;
    HotkeyManager hotkey;
    QTimer startTimeout;
    QTimer countdown;
    QTimer prepareTimer;
    LauncherRequestWatcher launcherRequests;
    bool requested = false;
    bool externalRecording = false;
    std::unique_ptr<RecordingIndicator> indicator;
    int seconds = 0;
    QuickRecordOverlay overlay;
    QPointer<SettingsWindow> settingsWindow;
    QPointer<QAction> toolsAction;
};
