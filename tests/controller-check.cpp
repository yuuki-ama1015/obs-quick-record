#include "quick-record-controller.hpp"
#include "settings-window.hpp"
#include <QAction>
#include <QApplication>
#include <QEventLoop>
#include <cassert>
#include <cstring>
#include <iostream>

// Run the production controller, hotkey manager and settings dialog with real Qt
// timers. Only capture/overlay/frontend boundaries are fakes: no desktop recording,
// global key injection or persistent OBS settings are used by this test.
static struct {
    StartMode mode = StartMode::Confirm;
    QuickRecordOverlay *overlay = nullptr;
    obs_frontend_event_cb callback = nullptr;
    void *callbackData = nullptr;
    bool visible = false, allocated = false, recording = false, indicator = false;
    bool prepareOK = true, ready = true, startOK = true, emitStarted = true;
    bool otherOutput = false;
    int opens = 0, prepares = 0, starts = 0, stops = 0, cleanups = 0;
} qa;

static void event(obs_frontend_event value)
{
    assert(qa.callback);
    qa.callback(value, qa.callbackData);
}
static void wait(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}
static obs_hotkey_id toggleId()
{
    obs_hotkey_id result = OBS_INVALID_HOTKEY_ID;
    obs_enum_hotkeys([](void *data, obs_hotkey_id id, obs_hotkey_t *key) {
        if (std::strcmp(obs_hotkey_get_name(key), "obs-quick-record.toggle") == 0)
            *static_cast<obs_hotkey_id *>(data) = id;
        return true;
    }, &result);
    return result;
}
static void toggle()
{
    const auto id = toggleId();
    assert(id != OBS_INVALID_HOTKEY_ID);
    obs_hotkey_trigger_routed_callback(id, true);
    QCoreApplication::processEvents(); // Production callback queues to the GUI thread.
}
static void idle()
{
    assert(!qa.visible && !qa.allocated && !qa.indicator);
}
template<typename Check> static void scenario(StartMode mode, Check check)
{
    qa = {};
    qa.mode = mode;
    {
        QuickRecordController controller;
        toggle();
        assert(qa.visible && qa.opens == 1);
        check();
    }
    assert(!qa.callback && !qa.overlay);
    assert(toggleId() == OBS_INVALID_HOTKEY_ID);
    idle();
}

extern "C" {
const char *obs_module_text(const char *key) { return std::strcmp(key, "Counting") ? key : "Counting %1"; }
void *obs_frontend_add_tools_menu_qaction(const char *) { return new QAction; }
void *obs_frontend_get_main_window_handle() { return nullptr; }
void obs_frontend_add_event_callback(obs_frontend_event_cb callback, void *data)
{ assert(!qa.callback); qa.callback = callback; qa.callbackData = data; }
void obs_frontend_remove_event_callback(obs_frontend_event_cb callback, void *data)
{ assert(qa.callback == callback && qa.callbackData == data); qa.callback = nullptr; }
bool obs_frontend_recording_active() { return qa.recording; }
bool obs_frontend_streaming_active() { return qa.otherOutput; }
bool obs_frontend_replay_buffer_active() { return false; }
bool obs_frontend_virtualcam_active() { return false; }
}
Settings::Settings() : startMode(qa.mode), foregroundSafety(false), data(obs_data_create()) {}
Settings::~Settings() { obs_data_release(data); }
bool Settings::save() { return true; }
class QuickRecordOverlay::Surface {};
QuickRecordOverlay::QuickRecordOverlay() { qa.overlay = this; }
QuickRecordOverlay::~QuickRecordOverlay() { qa.overlay = nullptr; }
void QuickRecordOverlay::open()
{
    qa.visible = true; ++qa.opens;
    selected.kind = CaptureKind::Region;
    selected.monitor.id = "test-monitor";
    selected.physical = QRect(10, 20, 640, 480);
}
void QuickRecordOverlay::hide() { qa.visible = false; }
void QuickRecordOverlay::message(const QString &) {}
bool QuickRecordOverlay::eventFilter(QObject *, QEvent *) { return false; }
RecordingIndicator::RecordingIndicator() = default;
void RecordingIndicator::start(const MonitorInfo &) { qa.indicator = true; }
void RecordingIndicator::stop() { qa.indicator = false; }
CaptureController::~CaptureController() { cleanup(); }
void CaptureController::removeStaleScene() {}
bool CaptureController::prepare(const CaptureTarget &target, bool)
{ assert(target.valid()); ++qa.prepares; qa.allocated = true; return qa.prepareOK; }
bool CaptureController::ready() { return qa.ready; }
bool CaptureController::start()
{
    ++qa.starts;
    if (qa.startOK && qa.emitStarted) {
        event(OBS_FRONTEND_EVENT_RECORDING_STARTING);
        qa.recording = true;
        event(OBS_FRONTEND_EVENT_RECORDING_STARTED);
    }
    return qa.startOK;
}
void CaptureController::stop()
{ ++qa.stops; qa.recording = false; event(OBS_FRONTEND_EVENT_RECORDING_STOPPED); }
void CaptureController::cleanup(bool) { ++qa.cleanups; qa.allocated = false; }

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    assert(obs_startup("en-US", nullptr, nullptr));
    // Ignore physical hotkeys; only explicit routed callbacks drive these checks.
    obs_hotkey_enable_callback_rerouting(true);
    scenario(StartMode::Confirm, [] {
        emit qa.overlay->confirmed(); // Enter without a selection cannot start.
        assert(qa.prepares == 0);
        emit qa.overlay->selectionReady();
        assert(qa.prepares == 0);
        emit qa.overlay->confirmed();
        wait(150);
        assert(qa.starts == 1 && qa.recording && qa.indicator);
        toggle(); // Manual stop uses the same global toggle.
        assert(qa.stops == 1);
        idle();
        toggle(); // STOPPED must leave the controller reusable.
        assert(qa.opens == 2 && qa.visible);
    });
    for (int cancel = 0; cancel < 3; ++cancel) {
        scenario(StartMode::Countdown, [cancel] {
            emit qa.overlay->selectionReady();
            emit qa.overlay->confirmed(); // Enter does not bypass a countdown.
            wait(1100);
            assert(qa.prepares == 0);
            if (cancel == 0) emit qa.overlay->canceled();
            if (cancel == 1) emit qa.overlay->selectionReset();
            if (cancel == 2) emit qa.overlay->settingsRequested();
            wait(2300); // Beyond the original countdown deadline.
            assert(qa.prepares == 0 && qa.starts == 0);
            if (cancel != 1) idle();
        });
    }
    scenario(StartMode::Countdown, [] {
        emit qa.overlay->selectionReady();
        wait(3500);
        assert(qa.starts == 1 && qa.recording);
        qa.recording = false;
        event(OBS_FRONTEND_EVENT_RECORDING_STOPPED); // OBS/another plugin stops it.
        idle();
    });
    scenario(StartMode::Immediate, [] {
        emit qa.overlay->selectionReady();
        assert(qa.prepares == 1);
        wait(150);
        assert(qa.starts == 1);
        toggle();
        idle();
    });
    scenario(StartMode::Confirm, [] {
        qa.prepareOK = false; // Closed target/source creation failure.
        emit qa.overlay->selectionReady();
        emit qa.overlay->confirmed();
        idle();
        assert(qa.starts == 0 && qa.cleanups > 0);
        qa.prepareOK = true;
        toggle();
        assert(qa.opens == 2);
    });
    scenario(StartMode::Immediate, [] {
        qa.startOK = false;
        emit qa.overlay->selectionReady();
        wait(150);
        idle();
        assert(qa.starts == 1);
    });
    scenario(StartMode::Immediate, [] {
        qa.emitStarted = false; // Frontend accepts request but encoder never starts.
        emit qa.overlay->selectionReady();
        wait(10700);
        idle();
        assert(qa.starts == 1);
        toggle();
        assert(qa.opens == 2);
    });
    scenario(StartMode::Confirm, [] {
        emit qa.overlay->selectionReady();
        event(OBS_FRONTEND_EVENT_RECORDING_STARTING); // External recording wins.
        qa.recording = true;
        idle();
        toggle();
        assert(qa.prepares == 0 && qa.stops == 0 && qa.opens == 1);
        qa.recording = false;
        event(OBS_FRONTEND_EVENT_RECORDING_STOPPED);
    });
    scenario(StartMode::Confirm, [] {
        emit qa.overlay->selectionReady();
        qa.otherOutput = true;
        emit qa.overlay->confirmed();
        idle();
        assert(qa.prepares == 0);
    });
    for (int state = 0; state < 4; ++state) {
        scenario(StartMode::Countdown, [state] {
            if (state == 1) emit qa.overlay->selectionReady();
            if (state == 2) emit qa.overlay->settingsRequested();
            if (state == 3) {
                emit qa.overlay->selectionReady();
                qa.ready = false;
                wait(3300); // Capture allocated, waiting for source frames.
                assert(qa.allocated);
            }
            event(OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN);
            assert(!qa.callback && toggleId() == OBS_INVALID_HOTKEY_ID);
            idle();
            wait(3400);
            assert(qa.starts == 0); // No late countdown/preparation callbacks.
        });
    }
    obs_shutdown();
    std::cout << "confirm/countdown/immediate, cancellation, failure, external stop and shutdown passed\n";
}
