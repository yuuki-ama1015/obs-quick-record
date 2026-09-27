#include "quick-record-controller.hpp"
#include <obs-module.h>
#include <windows.h>

QuickRecordController::QuickRecordController() : hotkey(this, [this] { toggle(); })
{
    auto *settings = obs_data_create();
    hotkey.load(settings);
    obs_data_release(settings);
    startTimeout.setSingleShot(true);
    connect(&startTimeout, &QTimer::timeout, this, [this] {
        if (pending && !obs_frontend_recording_active()) finish();
    });
    obs_frontend_add_event_callback(frontendEvent, this);
}
QuickRecordController::~QuickRecordController()
{
    shuttingDown = true;
    obs_frontend_remove_event_callback(frontendEvent, this);
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
    }
}
void QuickRecordController::toggle()
{
    if (shuttingDown) return;
    if (state == QuickRecordState::Recording) { capture.stop(); return; }
    if (pending || GetForegroundWindow() == obs_frontend_get_main_window_handle()) return;
    pending = true;
    if (!capture.start()) { finish(); return; }
    startTimeout.start(10000);
    blog(LOG_INFO, "OBS Quick Record: recording requested");
}
void QuickRecordController::finish()
{
    startTimeout.stop();
    capture.cleanup();
    pending = false;
    state = QuickRecordState::Idle;
}
