#include "quick-record-controller.hpp"
#include "settings-window.hpp"
#include <QAction>
#include <QApplication>
#include <QEventLoop>
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
#include <obs-module.h>
#include <util/bmem.h>
#include <QTemporaryDir>
#include <QFile>
#include <QCloseEvent>
#include <QMessageBox>
#include <QThread>
static bool moduleEnabled = false;
static obs_output_t *testOutput = nullptr;
extern "C" obs_module_t *obs_current_module() { return moduleEnabled ? reinterpret_cast<obs_module_t *>(1) : nullptr; }

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
    bool targetLost = false;
    bool otherOutput = false, replay = false, virtualCamera = false;
    bool outputMissing = false;
    bool saveFails = false;
    int saves = 0;
    bool stopInsideSignal = false;
    int configReads = 0;
    int opens = 0, prepares = 0, starts = 0, stops = 0, cleanups = 0, staleCleanups = 0;
    QWidget *main = nullptr;
    QString requestPath;
} qa;
class MainWindow : public QWidget {
public:
    int closes = 0;
    void closeEvent(QCloseEvent *event) override {
        assert(!qa.visible && !qa.allocated && !qa.indicator && !qa.recording);
        ++closes; event->accept();
    }
};

static void event(obs_frontend_event value)
{
    assert(qa.callback);
    qa.callback(value, qa.callbackData);
    if (value == OBS_FRONTEND_EVENT_RECORDING_STOPPED && QThread::currentThread() == qApp->thread()) QCoreApplication::processEvents();
}
static void stopped(int code = OBS_OUTPUT_SUCCESS)
{
    auto signal = [code] {
        calldata_t params{};
        calldata_set_int(&params, "code", code);
        calldata_set_string(&params, "last_error", code ? "test recording failure" : "");
        signal_handler_signal(obs_output_get_signal_handler(testOutput), "stop", &params);
        calldata_free(&params);
    };
    if (qa.stopInsideSignal) { signal(); QCoreApplication::processEvents(); return; }
    std::thread(signal).join();
    qa.recording = false;
    event(OBS_FRONTEND_EVENT_RECORDING_STOPPED);
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
void *obs_frontend_get_main_window() { return qa.main; }
char *obs_module_get_config_path(obs_module_t *, const char *) { ++qa.configReads; return bstrdup(qa.requestPath.toUtf8().constData()); }
obs_output_t *obs_frontend_get_recording_output() { return qa.outputMissing ? nullptr : obs_output_get_ref(testOutput); }
void obs_frontend_add_event_callback(obs_frontend_event_cb callback, void *data)
{ assert(!qa.callback); qa.callback = callback; qa.callbackData = data; }
void obs_frontend_remove_event_callback(obs_frontend_event_cb callback, void *data)
{ assert(qa.callback == callback && qa.callbackData == data); qa.callback = nullptr; }
bool obs_frontend_recording_active() { return qa.recording; }
bool obs_frontend_streaming_active() { return qa.otherOutput; }
bool obs_frontend_replay_buffer_active() { return qa.replay; }
bool obs_frontend_virtualcam_active() { return qa.virtualCamera; }
}
Settings::Settings() : startMode(qa.mode), foregroundSafety(false), data(obs_data_create()) {}
Settings::~Settings() { obs_data_release(data); }
bool Settings::save() { ++qa.saves; return !qa.saveFails; }
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
void CaptureController::removeStaleScene() { ++qa.staleCleanups; }
bool CaptureController::prepare(const CaptureTarget &target, bool)
{ assert(target.valid()); ++qa.prepares; qa.allocated = true; return qa.prepareOK; }
CaptureReadiness CaptureController::ready()
{ return qa.targetLost ? CaptureReadiness::Failed : qa.ready ? CaptureReadiness::Ready : CaptureReadiness::Waiting; }
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
{ ++qa.stops; stopped(); }
void CaptureController::cleanup(bool) { ++qa.cleanups; qa.allocated = false; }

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    assert(obs_startup("en-US", nullptr, nullptr));
    obs_output_info outputInfo{};
    outputInfo.id = "qa-output";
    outputInfo.flags = OBS_OUTPUT_ENCODED; // No video subsystem in this isolated controller test.
    outputInfo.encoded_packet = [](void *, encoder_packet *) {};
    outputInfo.get_name = [](void *) { return "test recording"; };
    outputInfo.create = [](obs_data_t *, obs_output_t *output) -> void * { return output; };
    outputInfo.destroy = [](void *) {};
    outputInfo.start = [](void *) { return true; };
    outputInfo.stop = [](void *, uint64_t) {};
    obs_register_output(&outputInfo);
    testOutput = obs_output_create("qa-output", "qa recording output", nullptr, nullptr);
    assert(testOutput);
    signal_handler_connect(obs_output_get_signal_handler(testOutput), "stop", [](void *, calldata_t *) {
        if (qa.stopInsideSignal) {
            qa.recording = false;
            qa.callback(OBS_FRONTEND_EVENT_RECORDING_STOPPED, qa.callbackData);
        }
    }, nullptr); // Simulate OBS's earlier stop subscriber, before our result observer.
    // Ignore physical hotkeys; only explicit routed callbacks drive these checks.
    obs_hotkey_enable_callback_rerouting(true);
    for (int mode = 0; mode < 8; ++mode) {
        qa = {};
        MainWindow main;
        QTemporaryDir requests;
        qa.main = &main; qa.requestPath = requests.filePath("launch-request.txt");
        QFile request(qa.requestPath); assert(request.open(QIODevice::WriteOnly));
        request.write(QByteArray::number(QCoreApplication::applicationPid()) + (mode == 4 ? "" : "\nexit-after-capture"));
        request.close();
        moduleEnabled = true;
        {
            QuickRecordController controller;
            event(OBS_FRONTEND_EVENT_FINISHED_LOADING);
            if (mode == 6) toggle(); // User selected before the launcher request timer fired.
            wait(650);
            assert(qa.visible && !QFile::exists(qa.requestPath));
            assert(request.open(QIODevice::WriteOnly)); request.write("invalid"); request.close();
            if (mode == 1 || mode == 5 || mode == 7) {
                qa.outputMissing = mode == 7;
                emit qa.overlay->selectionReady(); emit qa.overlay->confirmed(); wait(150);
                assert(qa.recording);
                stopped(mode == 5 ? OBS_OUTPUT_NO_SPACE : OBS_OUTPUT_SUCCESS);
                if (mode != 1) {
                    bool notified = false;
                    for (auto *widget : QApplication::topLevelWidgets()) {
                        auto *box = qobject_cast<QMessageBox *>(widget);
                        if (box && box->text() == "RecordingFailed") { notified = true; box->close(); }
                    }
                    assert(notified); // Even a tray-only OBS error must retain the process after the notification closes.
                }
            } else {
                if (mode == 2) { main.show(); main.hide(); }
                if (mode == 3) { qa.otherOutput = true; event(OBS_FRONTEND_EVENT_STREAMING_STARTING); qa.otherOutput = false; }
                emit qa.overlay->canceled();
            }
            idle(); wait(650);
            assert(main.closes == (mode <= 1 || mode == 6 ? 1 : 0));
            assert(qa.configReads == 1); // Request path is computed once, watcher stops after consumption.
            assert(QFile::exists(qa.requestPath)); // Later requests are not polled by an already-running OBS.
        }
        moduleEnabled = false;
    }
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
        stopped(); // OBS/another plugin stops it.
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
        qa.stopInsideSignal = true;
        emit qa.overlay->selectionReady(); wait(150);
        stopped();
        idle();
        for (auto *widget : QApplication::topLevelWidgets()) {
            auto *box = qobject_cast<QMessageBox *>(widget);
            assert(!box || box->text() != "RecordingFailed");
        }
    });
    scenario(StartMode::Immediate, [] {
        qa.startOK = false;
        emit qa.overlay->selectionReady();
        wait(150);
        idle();
        assert(qa.starts == 1);
    });
    scenario(StartMode::Immediate, [] {
        qa.ready = false;
        emit qa.overlay->selectionReady();
        wait(150);
        assert(qa.allocated && qa.starts == 0);
        qa.targetLost = true; // Deleted scene item / changed window while warming.
        wait(150);
        idle();
        assert(qa.starts == 0);
        toggle();
        assert(qa.opens == 2);
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
    for (int output = 0; output < 3; ++output) {
        scenario(StartMode::Confirm, [output] {
            emit qa.overlay->selectionReady();
            qa.otherOutput = output == 0;
            qa.replay = output == 1;
            qa.virtualCamera = output == 2;
            emit qa.overlay->confirmed(); // Another output started during selection.
            idle();
            assert(qa.prepares == 0);
            toggle(); // Still blocked from Idle, without touching the other output.
            idle();
            assert(qa.opens == 1 && qa.stops == 0);
        });
        scenario(StartMode::Immediate, [output] {
            qa.ready = false;
            emit qa.overlay->selectionReady();
            wait(150);
            assert(qa.allocated && qa.starts == 0);
            qa.otherOutput = output == 0;
            qa.replay = output == 1;
            qa.virtualCamera = output == 2;
            wait(150); // Output started while waiting for capture frames.
            idle();
            assert(qa.starts == 0 && qa.stops == 0);
            qa.otherOutput = qa.replay = qa.virtualCamera = false;
            qa.ready = true;
            wait(150);
            assert(qa.starts == 0); // Canceled preparation cannot start later.
            toggle();
            assert(qa.opens == 2 && qa.visible);
        });
    }
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
    scenario(StartMode::Confirm, [] {
        // Already queued work can outlive callback/hotkey unregistration.
        obs_hotkey_trigger_routed_callback(toggleId(), true);
        std::thread([] {
            event(OBS_FRONTEND_EVENT_FINISHED_LOADING);
            event(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED);
            event(OBS_FRONTEND_EVENT_RECORDING_STOPPED);
            event(OBS_FRONTEND_EVENT_EXIT);
        }).join();
        event(OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN);
        const int cleanups = qa.cleanups;
        QCoreApplication::processEvents();
        assert(qa.staleCleanups == 0 && qa.cleanups == cleanups);
        assert(qa.opens == 1 && !qa.callback);
        idle();
    });
    scenario(StartMode::Confirm, [] {
        assert(qa.saves == 1);
        event(OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN);
        assert(qa.saves == 2);
    });
    assert(qa.saves == 2); // Successful shutdown does not save again on unload.
    scenario(StartMode::Confirm, [] {
        qa.saveFails = true;
        event(OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN);
        qa.saveFails = false;
    });
    assert(qa.saves == 3); // Failed shutdown save can still retry on unload.
    obs_output_release(testOutput);
    obs_shutdown();
    std::cout << "confirm/countdown/immediate, cancellation, failure, external stop and shutdown passed\n";
}
