#include "settings.hpp"
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <util/bmem.h>
static QString settingsPath()
{
    char *path = obs_module_config_path("settings.json");
    QString result = QString::fromUtf8(path);
    bfree(path);
    return result;
}
Settings::Settings()
{
    data = obs_data_create_from_json_file(settingsPath().toUtf8().constData());
    if (!data) data = obs_data_create();
    for (auto key : {"cursor", "rememberRegion", "foregroundSafety", "indicator", "restoreScene"}) obs_data_set_default_bool(data, key, true);
    auto mode = obs_data_get_int(data, "startMode");
    startMode = mode >= 0 && mode <= 2 ? static_cast<StartMode>(mode) : StartMode::Confirm;
    cursor = obs_data_get_bool(data, "cursor");
    rememberRegion = obs_data_get_bool(data, "rememberRegion");
    foregroundSafety = obs_data_get_bool(data, "foregroundSafety");
    indicator = obs_data_get_bool(data, "indicator");
    restoreScene = obs_data_get_bool(data, "restoreScene");
}
Settings::~Settings() { obs_data_release(data); }
bool Settings::save()
{
    obs_data_set_int(data, "startMode", static_cast<int>(startMode));
    obs_data_set_bool(data, "cursor", cursor);
    obs_data_set_bool(data, "rememberRegion", rememberRegion);
    obs_data_set_bool(data, "foregroundSafety", foregroundSafety);
    obs_data_set_bool(data, "indicator", indicator);
    obs_data_set_bool(data, "restoreScene", restoreScene);
    auto path = settingsPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    const QByteArray json(obs_data_get_json(data));
    bool ok = file.open(QIODevice::WriteOnly) && file.write(json) == json.size() && file.commit();
    if (!ok) blog(LOG_ERROR, "OBS Quick Record: settings save failed");
    return ok;
}
