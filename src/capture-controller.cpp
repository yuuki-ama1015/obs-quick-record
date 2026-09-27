#include "capture-controller.hpp"
#include "monitor-selector.hpp"
#include <obs-frontend-api.h>
#include <obs-module.h>

static constexpr const char *sceneName = "__obs_quick_record_internal__";
static constexpr const char *ownerKey = "obs-quick-record-owned-v1";
static bool owned(obs_source_t *source)
{
    auto *data = obs_source_get_settings(source);
    const bool result = obs_data_get_bool(data, ownerKey);
    obs_data_release(data);
    return result;
}
CaptureController::~CaptureController() { cleanup(); }
void CaptureController::removeStaleScene()
{
    auto *old = obs_get_source_by_name(sceneName);
    if (!old) return;
    if (owned(old)) obs_source_remove(old);
    obs_source_release(old);
}
bool CaptureController::prepare(const CaptureTarget &selection, bool cursor)
{
    if (scene || obs_frontend_recording_active() || !selection.valid() || !MonitorSelector::stillValid(selection.monitor)) return false;
    auto *collision = obs_get_source_by_name(sceneName);
    if (collision) { obs_source_release(collision); return false; }
    target = selection;
    previous = obs_frontend_get_current_scene();
    if (!previous) return false;
    auto *data = obs_data_create();
    obs_data_set_string(data, "monitor_id", target.monitor.id.toUtf8().constData());
    obs_data_set_int(data, "method", 0);
    obs_data_set_bool(data, "capture_cursor", cursor);
    source = obs_source_create_private("monitor_capture", "__obs_quick_record_capture__", data);
    obs_data_release(data);
    if (!source) { cleanup(); return false; }
    auto *props = obs_source_properties(source);
    auto *list = obs_properties_get(props, "monitor_id");
    bool listed = false;
    for (size_t i = 0; list && i < obs_property_list_item_count(list); ++i)
        if (target.monitor.id == QString::fromUtf8(obs_property_list_item_string(list, i))) listed = true;
    obs_properties_destroy(props);
    if (!listed) { cleanup(); return false; }
    scene = obs_scene_create(sceneName);
    if (!scene) { cleanup(); return false; }
    auto *sceneData = obs_source_get_settings(obs_scene_get_source(scene));
    obs_data_set_bool(sceneData, ownerKey, true);
    obs_data_release(sceneData);
    auto *item = obs_scene_add(scene, source);
    if (!item) { cleanup(); return false; }
    obs_video_info video{};
    if (!obs_get_video_info(&video)) { cleanup(); return false; }
    vec2 bounds{static_cast<float>(video.base_width), static_cast<float>(video.base_height)};
    obs_sceneitem_set_alignment(item, OBS_ALIGN_TOP | OBS_ALIGN_LEFT);
    obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_STRETCH);
    obs_sceneitem_set_bounds(item, &bounds);
    settled.invalidate();
    return true;
}
bool CaptureController::ready()
{
    if (!scene || !source) return false;
    auto *sceneSource = obs_scene_get_source(scene);
    if (!switched) {
        // obs_scene_create emits a signal; frontend adds its list item on a queued GUI call.
        obs_frontend_source_list scenes{};
        obs_frontend_get_scenes(&scenes);
        bool listed = false;
        for (size_t i = 0; i < scenes.sources.num; ++i) if (scenes.sources.array[i] == sceneSource) listed = true;
        obs_frontend_source_list_free(&scenes);
        if (!listed) return false;
        auto *transition = obs_frontend_get_current_transition();
        bool active = transition && obs_transition_is_active(transition);
        obs_source_release(transition);
        if (active) return false;
        obs_frontend_set_current_scene(sceneSource);
        switched = true;
    }
    auto *current = obs_frontend_get_current_scene();
    bool currentMatches = current == sceneSource;
    obs_source_release(current);
    auto *transition = obs_frontend_get_current_transition();
    bool active = transition && obs_transition_is_active(transition);
    obs_source_release(transition);
    if (!currentMatches || active || obs_source_get_width(source) == 0 || obs_source_get_height(source) == 0) {
        settled.invalidate();
        return false;
    }
    if (!settled.isValid()) settled.start();
    return settled.elapsed() >= 250;
}
bool CaptureController::start()
{
    if (obs_frontend_recording_active()) return false;
    obs_frontend_recording_start();
    return true;
}
void CaptureController::stop() { obs_frontend_recording_stop(); }
void CaptureController::cleanup(bool restore)
{
    if (scene) {
        auto *current = obs_frontend_get_current_scene();
        const bool ours = current == obs_scene_get_source(scene);
        obs_source_release(current);
        if (previous && !obs_source_removed(previous) && (restore || ours) && switched) {
            // Finish the transition before removing either scene. Do not change transition settings.
            auto *transition = obs_frontend_get_current_transition();
            if (transition) obs_transition_set(transition, previous);
            obs_source_release(transition);
            obs_frontend_set_current_scene(previous);
            blog(LOG_INFO, "OBS Quick Record: previous scene restored");
        }
        obs_source_remove(obs_scene_get_source(scene));
        obs_scene_release(scene);
        scene = nullptr;
    }
    obs_source_release(source); source = nullptr;
    obs_source_release(previous); previous = nullptr;
    switched = false;
}
