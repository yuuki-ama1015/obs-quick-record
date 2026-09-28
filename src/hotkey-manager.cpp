#include "hotkey-manager.hpp"
#include <obs-module.h>
#include <vector>

static std::vector<obs_key_combination_t> bindingsFor(obs_hotkey_id id)
{
    struct State { obs_hotkey_id id; std::vector<obs_key_combination_t> bindings; } state{id, {}};
    obs_enum_hotkey_bindings([](void *data, size_t, obs_hotkey_binding_t *binding) {
        auto &state = *static_cast<State *>(data);
        if (obs_hotkey_binding_get_hotkey_id(binding) == state.id)
            state.bindings.push_back(obs_hotkey_binding_get_key_combination(binding));
        return true;
    }, &state);
    return state.bindings;
}

HotkeyManager::HotkeyManager(QObject *parent, std::function<void()> action)
    : QObject(parent), action(std::move(action))
{
    id = obs_hotkey_register_frontend("obs-quick-record.toggle", obs_module_text("Hotkey"), callback, this);
    blog(LOG_INFO, "OBS Quick Record: hotkey registered");
}
HotkeyManager::~HotkeyManager() { shutdown(); }
void HotkeyManager::shutdown()
{
    if (id != OBS_INVALID_HOTKEY_ID) obs_hotkey_unregister(id);
    id = OBS_INVALID_HOTKEY_ID;
}
void HotkeyManager::callback(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
    auto *self = static_cast<HotkeyManager *>(data);
    // OBS hotkey thread -> Qt GUI thread. Context destruction cancels queued calls.
    if (pressed) QMetaObject::invokeMethod(self, [self] { self->action(); }, Qt::QueuedConnection);
}
void HotkeyManager::load(obs_data_t *settings)
{
    if (obs_data_has_user_value(settings, "hotkey")) {
        auto *bindings = obs_data_get_array(settings, "hotkey");
        obs_hotkey_load(id, bindings);
        obs_data_array_release(bindings);
    } else {
        obs_key_combination_t key{INTERACT_ALT_KEY, OBS_KEY_R};
        obs_hotkey_load_bindings(id, &key, 1);
    }
}
obs_key_combination_t HotkeyManager::primaryBinding() const
{
    if (id == OBS_INVALID_HOTKEY_ID) return {0, OBS_KEY_NONE};
    const auto bindings = bindingsFor(id);
    return bindings.empty() ? obs_key_combination_t{0, OBS_KEY_NONE} : bindings.front();
}
void HotkeyManager::setPrimaryBinding(obs_key_combination_t binding)
{
    if (id == OBS_INVALID_HOTKEY_ID || binding.key == OBS_KEY_NONE) return;
    auto bindings = bindingsFor(id);
    if (bindings.empty()) bindings.push_back(binding);
    else bindings.front() = binding;
    obs_hotkey_load_bindings(id, bindings.data(), bindings.size());
    blog(LOG_INFO, "OBS Quick Record: hotkey changed");
}
void HotkeyManager::save(obs_data_t *settings) const
{
    if (id == OBS_INVALID_HOTKEY_ID) return;
    auto *bindings = obs_hotkey_save(id);
    obs_data_set_array(settings, "hotkey", bindings);
    obs_data_array_release(bindings);
}
