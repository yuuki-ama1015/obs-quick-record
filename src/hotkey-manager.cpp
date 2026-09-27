#include "hotkey-manager.hpp"
#include <obs-module.h>

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
        obs_key_combination_t key{INTERACT_COMMAND_KEY | INTERACT_SHIFT_KEY, OBS_KEY_R};
        obs_hotkey_load_bindings(id, &key, 1);
    }
}
void HotkeyManager::save(obs_data_t *settings) const
{
    if (id == OBS_INVALID_HOTKEY_ID) return;
    auto *bindings = obs_hotkey_save(id);
    obs_data_set_array(settings, "hotkey", bindings);
    obs_data_array_release(bindings);
}
