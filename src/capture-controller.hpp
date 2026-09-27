#pragma once
#include "capture-region.hpp"
#include <obs.h>
#include <QElapsedTimer>
class CaptureController {
public:
    ~CaptureController();
    bool prepare(const CaptureTarget &target, bool cursor);
    bool ready();
    bool start();
    void stop();
    void cleanup(bool restore = true);
    static void removeStaleScene();
private:
    CaptureTarget target;
    obs_scene_t *scene = nullptr;
    obs_source_t *source = nullptr;
    obs_source_t *previous = nullptr;
    bool switched = false;
    QElapsedTimer settled;
};
