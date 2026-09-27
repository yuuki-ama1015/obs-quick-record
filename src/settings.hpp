#pragma once
#include <QString>
#include <obs-module.h>
inline QString text(const char *key) { return QString::fromUtf8(obs_module_text(key)); }
enum class StartMode { Confirm, Immediate, Countdown };
struct Settings {
    StartMode startMode = StartMode::Confirm;
    bool cursor = true, rememberRegion = true, foregroundSafety = true, indicator = true, restoreScene = true;
    obs_data_t *data = nullptr;
    Settings();
    ~Settings();
    bool save();
    Settings(const Settings &) = delete;
    Settings &operator=(const Settings &) = delete;
};
