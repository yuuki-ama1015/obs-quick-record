#include "quick-record-controller.hpp"
#include "settings-window.hpp"
#include <QAction>
#include <QMessageBox>
#include <windows.h>
QuickRecordController::QuickRecordController() : hotkey(this, [this] { toggle(); })
{
    hotkey.load(settings.data);
    startTimeout.setSingleShot(true);
    connect(&startTimeout, &QTimer::timeout, this, [this] {
        if (pending && !obs_frontend_recording_active()) { finish(); notify("StartFailed"); }
    });
    connect(&overlay, &QuickRecordOverlay::canceled, this, [this] { if (!pending) finish(); });
    connect(&overlay, &QuickRecordOverlay::settingsRequested, this, &QuickRecordController::showSettings);
    connect(&overlay, &QuickRecordOverlay::confirmed, this, [this] {
        if (state == QuickRecordState::ReadyToRecord && !countdown.isActive()) begin();
    });
    connect(&overlay, &QuickRecordOverlay::selectionReady, this, [this] {
        state = QuickRecordState::ReadyToRecord;
        countdown.stop();
        if (settings.startMode == StartMode::Immediate) begin();
        else if (settings.startMode == StartMode::Countdown) {
            seconds = 3;
            overlay.message(text("Counting").arg(seconds));
            countdown.start(1000);
        }
    });
    connect(&countdown, &QTimer::timeout, this, [this] {
        if (--seconds == 0) { countdown.stop(); begin(); }
        else overlay.message(text("Counting").arg(seconds));
    });
    toolsAction = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("Settings")));
    connect(toolsAction, &QAction::triggered, this, &QuickRecordController::showSettings);
    obs_frontend_add_event_callback(frontendEvent, this);
}
QuickRecordController::~QuickRecordController()
{
    shuttingDown = true;
    obs_frontend_remove_event_callback(frontendEvent, this);
    hotkey.save(settings.data);
    settings.save();
    delete settingsWindow;
    delete toolsAction;
    finish();
}
void QuickRecordController::frontendEvent(obs_frontend_event event, void *data)
{
    auto *self = static_cast<QuickRecordController *>(data);
    QMetaObject::invokeMethod(self, [self, event] { self->onEvent(event); }, Qt::AutoConnection);
}
void QuickRecordController::onEvent(obs_frontend_event event)
{
    if (event == OBS_FRONTEND_EVENT_RECORDING_STARTED && pending) {
        pending = false;
        state = QuickRecordState::Recording;
        startTimeout.stop();
        blog(LOG_INFO, "OBS Quick Record: recording started");
    } else if (event == OBS_FRONTEND_EVENT_RECORDING_STOPPED) {
        blog(LOG_INFO, "OBS Quick Record: recording stopped");
        finish();
    } else if (event == OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN || event == OBS_FRONTEND_EVENT_EXIT) {
        shuttingDown = true;
        finish();
    } else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING) {
        finish();
    }
}
void QuickRecordController::notify(const char *key)
{
    blog(LOG_WARNING, "OBS Quick Record: %s", key);
    if (shuttingDown) return;
    auto *box = new QMessageBox(QMessageBox::Information, text("Title"), text(key), QMessageBox::Ok);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowFlag(Qt::WindowStaysOnTopHint);
    box->show();
    connect(this, &QObject::destroyed, box, [box] { delete box; });
}
void QuickRecordController::toggle()
{
    if (shuttingDown) return;
    if (state == QuickRecordState::Recording) { capture.stop(); return; }
    if (pending) return;
    if (obs_frontend_recording_active()) { notify("AlreadyRecording"); return; }
    if (settings.foregroundSafety && GetAncestor(GetForegroundWindow(), GA_ROOTOWNER) == obs_frontend_get_main_window_handle()) return;
    if (state != QuickRecordState::Idle) return;
    state = QuickRecordState::Selecting;
    overlay.open();
    blog(LOG_INFO, "OBS Quick Record: selector opened");
}
void QuickRecordController::begin()
{
    if (pending || state != QuickRecordState::ReadyToRecord) return;
    if (obs_frontend_recording_active()) { finish(); notify("AlreadyRecording"); return; }
    overlay.hide();
    pending = true;
    if (!capture.start()) { finish(); notify("StartFailed"); return; }
    startTimeout.start(10000);
    blog(LOG_INFO, "OBS Quick Record: recording requested");
}
void QuickRecordController::showSettings()
{
    if (shuttingDown || pending || state == QuickRecordState::Recording) return;
    finish();
    if (!settingsWindow) {
        settingsWindow = new SettingsWindow(settings);
        settingsWindow->setAttribute(Qt::WA_DeleteOnClose);
        connect(settingsWindow, &QDialog::accepted, this, [this] { hotkey.save(settings.data); settings.save(); });
    }
    settingsWindow->show(); settingsWindow->raise(); settingsWindow->activateWindow();
}
void QuickRecordController::finish()
{
    startTimeout.stop();
    countdown.stop();
    overlay.hide();
    capture.cleanup();
    pending = false;
    state = QuickRecordState::Idle;
}
