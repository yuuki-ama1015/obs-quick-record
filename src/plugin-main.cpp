#include <obs-module.h>
#include "quick-record-controller.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-quick-record", "en-US")
static QuickRecordController *controller = nullptr;

bool obs_module_load(void)
{
    controller = new QuickRecordController;
    blog(LOG_INFO, "OBS Quick Record: plugin loaded");
    return true;
}

void obs_module_unload(void)
{
    delete controller;
    controller = nullptr;
    blog(LOG_INFO, "OBS Quick Record: plugin unloaded");
}
