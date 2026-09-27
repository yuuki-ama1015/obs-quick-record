#include "capture-controller.hpp"
#include <obs-frontend-api.h>
bool CaptureController::start()
{
    if (obs_frontend_recording_active()) return false;
    obs_frontend_recording_start();
    return true;
}
void CaptureController::stop() { obs_frontend_recording_stop(); }
void CaptureController::cleanup() {}
