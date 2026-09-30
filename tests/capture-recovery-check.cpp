#include "capture-controller.hpp"
#include "monitor-selector.hpp"
#include "window-selector.hpp"
#include <obs-frontend-api.h>
#include <QCoreApplication>
#include <cassert>
#include <iostream>

// Real libobs scenes and the production recovery path; only frontend selection is fake.
static obs_source_t *program = nullptr, *preview = nullptr;
static int restored = 0;
extern "C" {
obs_source_t *obs_frontend_get_current_scene() { return obs_source_get_ref(program); }
obs_source_t *obs_frontend_get_current_preview_scene() { return obs_source_get_ref(preview); }
obs_source_t *obs_frontend_get_current_transition() { return nullptr; }
void obs_frontend_set_current_scene(obs_source_t *source) { program = source; ++restored; }
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
void obs_frontend_recording_start() {}
void obs_frontend_recording_stop() {}
}
bool MonitorSelector::stillValid(const MonitorInfo &) { return true; }
bool WindowSelector::stillValid(const CaptureTarget &) { return true; }

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    assert(obs_startup("en-US", nullptr, nullptr));
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
    obs_source_remove(obs_scene_get_source(a));
    obs_source_remove(obs_scene_get_source(b));
    obs_scene_release(a);
    obs_scene_release(b);
    obs_shutdown();
    std::cout << "Saved capture recovery and unowned scene preservation passed\n";
}
