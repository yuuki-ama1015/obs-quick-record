#include "capture-controller.hpp"
#include "monitor-selector.hpp"
#include "window-selector.hpp"
#include <obs-frontend-api.h>
#include <QCoreApplication>
#include <QThread>
#include <cassert>
#include <iostream>

// Real libobs scenes and the production recovery path; only frontend selection is fake.
static obs_source_t *program = nullptr, *preview = nullptr;
static int restored = 0;
static bool duplicateTitle = false;
static bool hooked = false, wrongWindow = false;
static obs_source_t *transition = nullptr;
static int starts = 0;
extern "C" {
obs_source_t *obs_frontend_get_current_scene() { return obs_source_get_ref(program); }
obs_source_t *obs_frontend_get_current_preview_scene() { return obs_source_get_ref(preview); }
obs_source_t *obs_frontend_get_current_transition() { return obs_source_get_ref(transition); }
void obs_frontend_set_current_scene(obs_source_t *source)
{ program = source; if (transition) obs_transition_set(transition, source); ++restored; }
void obs_frontend_set_current_preview_scene(obs_source_t *source) { preview = source; }
void obs_frontend_get_scenes(obs_frontend_source_list *list)
{
    obs_enum_scenes([](void *data, obs_source_t *source) {
        auto *list = static_cast<obs_frontend_source_list *>(data);
        auto *ref = obs_source_get_ref(source);
        da_push_back(list->sources, &ref);
        return true;
    }, list);
}
bool obs_frontend_recording_active() { return false; }
void obs_frontend_recording_start() { ++starts; }
void obs_frontend_recording_stop() {}
bool obs_get_video_info(obs_video_info *video)
{ video->base_width = 1920; video->base_height = 1080; return true; }
}
bool MonitorSelector::stillValid(const MonitorInfo &) { return true; }
bool WindowSelector::stillValid(const CaptureTarget &) { return true; }
QString WindowSelector::encode(const QString &title, const QString &windowClass, const QString &exe)
{ return title + ":" + windowClass + ":" + exe; }

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    assert(obs_startup("en-US", nullptr, nullptr));
    // Fake only the capture driver; scene/item ownership uses real libobs.
    obs_source_info capture{};
    capture.id = "monitor_capture";
    capture.type = OBS_SOURCE_TYPE_INPUT;
    capture.output_flags = OBS_SOURCE_VIDEO;
    capture.get_name = [](void *) { return "QA capture"; };
    capture.create = [](obs_data_t *, obs_source_t *source) -> void * {
        proc_handler_add(obs_source_get_proc_handler(source),
            "void get_hooked(out bool hooked, out string title, out string class, out string executable)",
            [](void *, calldata_t *data) {
                calldata_set_bool(data, "hooked", hooked);
                calldata_set_string(data, "title", "Same");
                calldata_set_string(data, "class", wrongWindow ? "OtherClass" : "Class");
                calldata_set_string(data, "executable", "target.exe");
            }, nullptr);
        static int token; return &token;
    };
    capture.destroy = [](void *) {};
    capture.get_width = [](void *) -> uint32_t { return 640; };
    capture.get_height = [](void *) -> uint32_t { return 480; };
    capture.get_properties = [](void *) {
        auto *props = obs_properties_create();
        auto *monitor = obs_properties_add_list(props, "monitor_id", "Monitor", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
        obs_property_list_add_string(monitor, "QA monitor", "test-monitor");
        auto *window = obs_properties_add_list(props, "window", "Window", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
        obs_property_list_add_string(window, "Selected", "Same:Class:target.exe");
        if (duplicateTitle) obs_property_list_add_string(window, "Other app", "same:OtherClass:other.exe");
        return props;
    };
    obs_register_source(&capture);
    capture.id = "window_capture";
    obs_register_source(&capture);
    capture.id = "qa_transition";
    capture.type = OBS_SOURCE_TYPE_TRANSITION;
    capture.get_width = nullptr;
    capture.get_height = nullptr;
    capture.audio_render = [](void *, uint64_t *, obs_source_audio_mix *, uint32_t, size_t, size_t) { return false; };
    capture.video_render = [](void *, gs_effect_t *) {};
    obs_register_source(&capture);
    auto *a = obs_scene_create("User A");
    auto *b = obs_scene_create("User B");
    assert(a && b);
    for (int scenario = 0; scenario < 5; ++scenario) {
        auto *scene = obs_scene_create("__obs_quick_record_internal__");
        assert(scene);
        auto *source = obs_scene_get_source(scene);
        auto *settings = obs_source_get_settings(source);
        obs_data_set_bool(settings, "obs-quick-record-owned-v1", scenario != 4);
        if (scenario == 0)
            obs_data_set_string(settings, "obs-quick-record-previous-scene-uuid", obs_source_get_uuid(obs_scene_get_source(b)));
        obs_data_release(settings);
        program = scenario == 3 ? obs_scene_get_source(b) : source;
        preview = scenario == 2 ? nullptr : obs_scene_get_source(a);
        restored = 0;
        CaptureController::removeStaleScene();
        if (scenario == 4) {
            assert(!obs_source_removed(source) && program == source && restored == 0);
        } else {
            assert(obs_source_removed(source));
            assert(program != source && obs_scene_from_source(program));
            if (scenario == 0 || scenario == 3) assert(program == obs_scene_get_source(b));
            if (scenario == 1) assert(program == obs_scene_get_source(a));
            assert(restored == (scenario == 3 ? 0 : 1));
        }
        program = preview = nullptr;
        obs_source_remove(source);
        obs_scene_release(scene);
    }
    CaptureTarget target;
    target.monitor.id = "test-monitor";
    target.monitor.physical = {0, 0, 640, 480};
    target.physical = {10, 20, 100, 80};
    target.kind = CaptureKind::Region;
    program = obs_scene_get_source(a);
    {
        CaptureController controller;
        assert(controller.prepare(target, true));
        auto *source = obs_get_source_by_name("__obs_quick_record_internal__");
        assert(source);
        // Remove during warm-up. Controller's own item ref must keep ready()
        // safe, and readiness must fail instead of waiting or starting.
        obs_scene_enum_items(obs_scene_from_source(source), [](obs_scene_t *, obs_sceneitem_t *item, void *) {
            obs_sceneitem_remove(item);
            return false;
        }, nullptr);
        assert(controller.ready() == CaptureReadiness::Failed);
        assert(!controller.start());
        controller.cleanup();
        assert(obs_source_removed(source));
        obs_source_release(source);
    }
    target.kind = CaptureKind::Window;
    target.windowValue = "Same:Class:target.exe";
    {
        CaptureController controller;
        duplicateTitle = true;
        assert(!controller.prepare(target, true)); // Same title, different class/exe is still ambiguous.
        duplicateTitle = false;
        assert(controller.prepare(target, true));
        assert(controller.ready() == CaptureReadiness::Waiting); // Dimensions alone do not prove acquisition.
        hooked = true;
        wrongWindow = true;
        assert(controller.ready() == CaptureReadiness::Waiting);
        assert(program == obs_scene_get_source(a)); // Wrong class must not switch Program.
        wrongWindow = false;
        transition = obs_source_create_private("qa_transition", "QA transition", nullptr);
        assert(transition);
        assert(controller.ready() == CaptureReadiness::Waiting);
        QThread::msleep(260);
        assert(controller.ready() == CaptureReadiness::Ready);
        assert(controller.start() && starts == 1);
        controller.cleanup();
        assert(program == obs_scene_get_source(a));
        obs_source_release(transition);
        transition = nullptr;
    }
    program = preview = nullptr;
    obs_source_remove(obs_scene_get_source(a));
    obs_source_remove(obs_scene_get_source(b));
    obs_scene_release(a);
    obs_scene_release(b);
    obs_shutdown();
    std::cout << "Saved capture recovery and unowned scene preservation passed\n";
}
