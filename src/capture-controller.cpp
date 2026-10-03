#include "capture-controller.hpp"
#include "monitor-selector.hpp"
#include "window-selector.hpp"
#include <obs-frontend-api.h>
#include <obs-module.h>

static constexpr const char *sceneName = "__obs_quick_record_internal__";
static constexpr const char *ownerKey = "obs-quick-record-owned-v1";
static constexpr const char *previousKey = "obs-quick-record-previous-scene-uuid";
static void restoreProgram(obs_source_t *previous)
{
    auto *transition = obs_frontend_get_current_transition();
    if (transition) obs_transition_set(transition, previous);
    obs_source_release(transition);
    obs_frontend_set_current_scene(previous);
    // Refresh Studio Mode labels without swapping our removed scene into Preview.
    auto *preview = obs_frontend_get_current_preview_scene();
    if (preview) obs_frontend_set_current_preview_scene(preview);
    obs_source_release(preview);
}
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
    if (owned(old)) {
        auto *current = obs_frontend_get_current_scene();
        const bool needsRestore = !current || QString::fromUtf8(obs_source_get_name(current)) == sceneName;
        obs_source_release(current);
        if (needsRestore) {
            auto *data = obs_source_get_settings(old);
            auto *previous = obs_get_source_by_uuid(obs_data_get_string(data, previousKey));
            obs_data_release(data);
            if (previous == old || (previous && !obs_scene_from_source(previous))) {
                obs_source_release(previous);
                previous = nullptr;
            }
            // Older saved scenes have no UUID. Prefer the unchanged Studio Preview,
            // then another user scene, rather than deleting the active Program.
            if (!previous) previous = obs_frontend_get_current_preview_scene();
            if (previous == old) { obs_source_release(previous); previous = nullptr; }
            if (!previous) {
                obs_frontend_source_list scenes{};
                obs_frontend_get_scenes(&scenes);
                for (size_t i = 0; i < scenes.sources.num; ++i) {
                    if (scenes.sources.array[i] != old) { previous = obs_source_get_ref(scenes.sources.array[i]); break; }
                }
                obs_frontend_source_list_free(&scenes);
            }
            if (previous) {
                restoreProgram(previous);
                blog(LOG_INFO, "OBS Quick Record: recovered previous scene after interrupted capture");
            }
            obs_source_release(previous);
        }
        obs_source_remove(old);
    }
    obs_source_release(old);
}
bool CaptureController::prepare(const CaptureTarget &selection, bool cursor)
{
    if (scene || obs_frontend_recording_active() || !selection.valid() || !MonitorSelector::stillValid(selection.monitor)) return false;
    auto *collision = obs_get_source_by_name(sceneName);
    if (collision) { obs_source_release(collision); return false; }
    target = selection;
    const bool window = target.kind == CaptureKind::Window;
    if (window && !WindowSelector::stillValid(target)) {
        blog(LOG_WARNING, "OBS Quick Record: selected window changed or OBS resolves a different HWND");
        return false;
    }
    previous = obs_frontend_get_current_scene();
    if (!previous) return false;
    auto *data = obs_data_create();
    obs_data_set_string(data, "monitor_id", target.monitor.id.toUtf8().constData());
    obs_data_set_int(data, "method", 0);
    obs_data_set_bool(data, "capture_cursor", cursor);
    if (window) {
        obs_data_set_string(data, "window", target.windowValue.toUtf8().constData());
        obs_data_set_bool(data, "cursor", cursor);
        obs_data_set_bool(data, "client_area", true);
        obs_data_set_bool(data, "capture_audio", false);
        obs_data_set_int(data, "priority", 1); // WINDOW_PRIORITY_TITLE, see window-helpers.h.
    }
    source = obs_source_create_private(window ? "window_capture" : "monitor_capture", "__obs_quick_record_capture__", data);
    obs_data_release(data);
    if (!source) { cleanup(); return false; }
    auto *props = obs_source_properties(source);
    auto *list = obs_properties_get(props, window ? "window" : "monitor_id");
    bool listed = false;
    int matchingTitles = 0;
    for (size_t i = 0; list && i < obs_property_list_item_count(list); ++i) {
        const auto value = QString::fromUtf8(obs_property_list_item_string(list, i));
        if ((window ? target.windowValue : target.monitor.id) == value) listed = true;
        if (window && value.section(':', 0, 0).compare(target.windowValue.section(':', 0, 0), Qt::CaseInsensitive) == 0)
            ++matchingTitles;
    }
    obs_properties_destroy(props);
    if (window && matchingTitles != 1) {
        blog(LOG_WARNING, "OBS Quick Record: window title is ambiguous; refusing capture");
        cleanup(); return false;
    }
    if (!listed) { cleanup(); return false; }
    scene = obs_scene_create(sceneName);
    if (!scene) { cleanup(); return false; }
    auto *sceneData = obs_source_get_settings(obs_scene_get_source(scene));
    obs_data_set_bool(sceneData, ownerKey, true);
    obs_data_set_string(sceneData, previousKey, obs_source_get_uuid(previous));
    obs_data_release(sceneData);
    item = obs_scene_add(scene, source);
    if (!item) { cleanup(); return false; }
    obs_sceneitem_addref(item);
    obs_video_info video{};
    if (!obs_get_video_info(&video)) { cleanup(); return false; }
    vec2 bounds{static_cast<float>(video.base_width), static_cast<float>(video.base_height)};
    obs_sceneitem_set_alignment(item, OBS_ALIGN_TOP | OBS_ALIGN_LEFT);
    obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_STRETCH);
    obs_sceneitem_set_bounds(item, &bounds);
    // Warm capture before switching: Studio Mode may snapshot the scene's item transforms.
    obs_source_inc_showing(source);
    warming = true;
    settled.invalidate();
    cropped = false;
    return true;
}
static bool windowCaptureMatches(obs_source_t *source, const CaptureTarget &target)
{
    // OBS 32.2.2 Window Capture's public get_hooked procedure reports the
    // acquired title/class/executable, including Studio Mode's capture copy.
    calldata_t data{};
    const bool called = proc_handler_call(obs_source_get_proc_handler(source), "get_hooked", &data);
    const bool matches = called && calldata_bool(&data, "hooked") &&
        WindowSelector::encode(QString::fromUtf8(calldata_string(&data, "title")),
                               QString::fromUtf8(calldata_string(&data, "class")),
                               QString::fromUtf8(calldata_string(&data, "executable"))) == target.windowValue;
    calldata_free(&data);
    return matches;
}
CaptureReadiness CaptureController::ready()
{
    if (!scene || !source || !item) return CaptureReadiness::Failed;
    auto *sceneSource = obs_scene_get_source(scene);
    if (obs_source_removed(sceneSource) || obs_source_removed(source) || obs_sceneitem_get_scene(item) != scene)
        return CaptureReadiness::Failed;
    if (target.kind == CaptureKind::Window && !WindowSelector::stillValid(target)) return CaptureReadiness::Failed;
    if (!obs_source_get_width(source) || !obs_source_get_height(source)) return CaptureReadiness::Waiting;
    if (target.kind == CaptureKind::Window && !windowCaptureMatches(source, target)) return CaptureReadiness::Waiting;
    if (!cropped && target.kind == CaptureKind::Region) {
        // Display Capture pixels must agree with the selected physical monitor geometry.
        if (obs_source_get_width(source) != static_cast<uint32_t>(target.monitor.physical.width()) ||
            obs_source_get_height(source) != static_cast<uint32_t>(target.monitor.physical.height())) return CaptureReadiness::Waiting;
        const auto local = target.physical.translated(-target.monitor.physical.topLeft());
        obs_sceneitem_crop crop{};
        crop.left = local.x(); crop.top = local.y();
        crop.right = target.monitor.physical.width() - local.x() - local.width();
        crop.bottom = target.monitor.physical.height() - local.y() - local.height();
        obs_sceneitem_set_crop(item, &crop); // OBS source pixels, not Qt logical pixels.
        cropped = true;
        blog(LOG_INFO, "OBS Quick Record: region selected x=%d y=%d w=%d h=%d", target.physical.x(), target.physical.y(), target.physical.width(), target.physical.height());
    }
    if (!switched) {
        // obs_scene_create emits a signal; frontend adds its list item on a queued GUI call.
        obs_frontend_source_list scenes{};
        obs_frontend_get_scenes(&scenes);
        bool listed = false;
        for (size_t i = 0; i < scenes.sources.num; ++i) if (scenes.sources.array[i] == sceneSource) listed = true;
        obs_frontend_source_list_free(&scenes);
        if (!listed) return CaptureReadiness::Waiting;
        auto *transition = obs_frontend_get_current_transition();
        bool active = transition && obs_transition_is_active(transition);
        obs_source_release(transition);
        if (active) return CaptureReadiness::Waiting;
        obs_frontend_set_current_scene(sceneSource);
        switched = true;
    }
    auto *current = obs_frontend_get_current_scene();
    bool currentMatches = current == sceneSource;
    obs_source_release(current);
    auto *transition = obs_frontend_get_current_transition();
    bool active = transition && obs_transition_is_active(transition);
    auto *program = transition ? obs_transition_get_active_source(transition) : nullptr;
    bool programReady = false;
    if (program && QString::fromUtf8(obs_source_get_name(program)) == sceneName) {
        // Studio Mode's private copy can contain a separate Window Capture instance.
        struct Check { const CaptureTarget &target; bool ready = false; } check{target};
        obs_scene_enum_items(obs_scene_from_source(program), [](obs_scene_t *, obs_sceneitem_t *item, void *data) {
            auto &check = *static_cast<Check *>(data);
            auto *capture = obs_sceneitem_get_source(item);
            check.ready = obs_source_get_width(capture) && obs_source_get_height(capture) &&
                (check.target.kind != CaptureKind::Window || windowCaptureMatches(capture, check.target));
            return false; // Our scene has exactly one capture item.
        }, &check);
        programReady = check.ready;
    }
    obs_source_release(program);
    obs_source_release(transition);
    if (!currentMatches || active || !programReady) {
        settled.invalidate();
        return CaptureReadiness::Waiting;
    }
    if (!settled.isValid()) settled.start();
    return settled.elapsed() >= 250 ? CaptureReadiness::Ready : CaptureReadiness::Waiting;
}
bool CaptureController::start()
{
    if (obs_frontend_recording_active() || ready() != CaptureReadiness::Ready) return false;
    obs_frontend_recording_start();
    return true;
}
void CaptureController::stop() { obs_frontend_recording_stop(); }
void CaptureController::cleanup(bool restore)
{
    obs_sceneitem_release(item);
    item = nullptr;
    if (scene) {
        auto *current = obs_frontend_get_current_scene();
        const bool ours = current == obs_scene_get_source(scene);
        obs_source_release(current);
        if (previous && !obs_source_removed(previous) && (restore || ours) && switched) {
            // Finish the transition before removing either scene. Do not change transition settings.
            restoreProgram(previous);
            blog(LOG_INFO, "OBS Quick Record: previous scene restored");
        }
        obs_source_remove(obs_scene_get_source(scene));
        obs_scene_release(scene);
        scene = nullptr;
    }
    if (warming) obs_source_dec_showing(source);
    warming = false;
    obs_source_release(source); source = nullptr;
    obs_source_release(previous); previous = nullptr;
    switched = false;
}
