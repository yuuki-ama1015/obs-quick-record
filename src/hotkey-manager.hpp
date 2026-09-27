#pragma once
#include <QObject>
#include <obs.h>
#include <functional>

class HotkeyManager : public QObject {
public:
    explicit HotkeyManager(QObject *parent, std::function<void()> action);
    ~HotkeyManager() override;
    void load(obs_data_t *settings);
    void save(obs_data_t *settings) const;
    void shutdown();
private:
    static void callback(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed);
    obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
    std::function<void()> action;
};
