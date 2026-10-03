#pragma once
#include "capture-region.hpp"
#include <obs.h>
#include <QElapsedTimer>
enum class CaptureReadiness { Waiting, Ready, Failed };
class CaptureController {
public:
    ~CaptureController();
    bool prepare(const CaptureTarget &target, bool cursor);
    CaptureReadiness ready();
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
    obs_sceneitem_t *item = nullptr; // Owned ref; external removal must not invalidate it.
    bool cropped = false;
    bool warming = false;
    QElapsedTimer settled;
};
