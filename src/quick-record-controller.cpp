#include "quick-record-controller.hpp"
#include "settings-window.hpp"
#include <QAction>
#include <QMessageBox>
#include <windows.h>
QuickRecordController::QuickRecordController() : hotkey(this, [this] { toggle(); })
{
    hotkey.load(settings.data);
    connect(&overlay, &QuickRecordOverlay::selectionReset, this, [this] { countdown.stop(); state = QuickRecordState::Selecting; });
    connect(&prepareTimer, &QTimer::timeout, this, [this] {
        if (obs_frontend_recording_active()) { finish(); notify("AlreadyRecording"); return; }
        if (obs_frontend_streaming_active() || obs_frontend_replay_buffer_active() || obs_frontend_virtualcam_active()) { finish(); notify("OtherOutput"); return; }
        if (!capture.ready()) return;
        prepareTimer.stop();
        requested = true;
        if (!capture.start()) { finish(); notify("StartFailed"); return; }
        blog(LOG_INFO, "OBS Quick Record: recording requested");
    });
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
    frontendRegistered = true;
}
QuickRecordController::~QuickRecordController()
{
    shuttingDown = true;
    if (frontendRegistered) obs_frontend_remove_event_callback(frontendEvent, this);
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
    // Unregistration does not remove frontend events already queued to Qt.
    if (shuttingDown) return;
    if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
        CaptureController::removeStaleScene();
    } else if (event == OBS_FRONTEND_EVENT_RECORDING_STARTING && !requested) {
        externalRecording = true;
        if (state != QuickRecordState::Idle || pending) finish();
    } else if (event == OBS_FRONTEND_EVENT_RECORDING_STARTED && pending && requested) {
        pending = false;
        state = QuickRecordState::Recording;
        startTimeout.stop();
        if (settings.indicator) indicator.start(overlay.target().monitor);
        const auto &target = overlay.target();
        obs_data_set_string(settings.data, "lastMonitor", target.monitor.id.toUtf8().constData());
        if (settings.rememberRegion && target.kind == CaptureKind::Region) {
            auto *region = obs_data_create();
            obs_data_set_int(region, "x", target.physical.x());
            obs_data_set_int(region, "y", target.physical.y());
            obs_data_set_int(region, "width", target.physical.width());
            obs_data_set_int(region, "height", target.physical.height());
            obs_data_set_int(region, "monitorIndex", target.monitor.index);
            obs_data_set_string(region, "monitorId", target.monitor.id.toUtf8().constData());
            obs_data_set_obj(settings.data, "lastRegion", region);
            obs_data_release(region);
        }
        hotkey.save(settings.data);
        settings.save();
        blog(LOG_INFO, "OBS Quick Record: recording started");
    } else if (event == OBS_FRONTEND_EVENT_RECORDING_STOPPED) {
        externalRecording = false;
        blog(LOG_INFO, "OBS Quick Record: recording stopped");
        finish();
    } else if (event == OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN || event == OBS_FRONTEND_EVENT_EXIT) {
        shuttingDown = true;
        // OBS dispatches callbacks in reverse order, so removing this callback here is safe.
        // Its frontend API is destroyed before module_unload.
        if (frontendRegistered) obs_frontend_remove_event_callback(frontendEvent, this);
        frontendRegistered = false;
        hotkey.save(settings.data);
        hotkey.shutdown();
        settings.save();
        delete settingsWindow;
        finish();
    } else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING) {
        if (state == QuickRecordState::Recording) capture.stop();
        else finish();
    } else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED && !pending && state == QuickRecordState::Idle) {
        CaptureController::removeStaleScene();
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
    if (pending) { if (requested) capture.stop(); else finish(); return; }
    if (externalRecording || obs_frontend_recording_active()) { notify("AlreadyRecording"); return; }
    if (obs_frontend_streaming_active() || obs_frontend_replay_buffer_active() || obs_frontend_virtualcam_active()) { notify("OtherOutput"); return; }
    if (settings.foregroundSafety && GetAncestor(GetForegroundWindow(), GA_ROOTOWNER) == obs_frontend_get_main_window_handle()) return;
    if (state != QuickRecordState::Idle) return;
    if (settingsWindow && settingsWindow->isVisible()) return;
    state = QuickRecordState::Selecting;
    overlay.open();
    if (state != QuickRecordState::Selecting) { notify("NoMonitor"); return; }
    blog(LOG_INFO, "OBS Quick Record: selector opened");
}
void QuickRecordController::begin()
{
    if (pending || state != QuickRecordState::ReadyToRecord) return;
    if (externalRecording || obs_frontend_recording_active()) { finish(); notify("AlreadyRecording"); return; }
    if (obs_frontend_streaming_active() || obs_frontend_replay_buffer_active() || obs_frontend_virtualcam_active()) { finish(); notify("OtherOutput"); return; }
    overlay.hide();
    pending = true;
    if (!capture.prepare(overlay.target(), settings.cursor)) { finish(); notify("StartFailed"); return; }
    prepareTimer.start(50);
    startTimeout.start(10000);
    blog(LOG_INFO, "OBS Quick Record: capture preparation started");
}
void QuickRecordController::showSettings()
{
    if (shuttingDown || pending || state == QuickRecordState::Recording) return;
    finish();
    if (!settingsWindow) {
        settingsWindow = new SettingsWindow(settings, hotkey);
        settingsWindow->setAttribute(Qt::WA_DeleteOnClose);
        connect(settingsWindow, &QDialog::accepted, this, [this] { hotkey.save(settings.data); settings.save(); });
    }
    settingsWindow->show(); settingsWindow->raise(); settingsWindow->activateWindow();
}
void QuickRecordController::finish()
{
    startTimeout.stop();
    prepareTimer.stop();
    countdown.stop();
    overlay.hide();
    indicator.stop();
    capture.cleanup(settings.restoreScene);
    requested = false;
    pending = false;
    state = QuickRecordState::Idle;
}
