#pragma once
#include <QObject>
#include <QSize>
#include <QTimer>
#include <algorithm>
#include <cstring>
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/config-file.h>

inline QSize recordingEconomySize(QSize size)
{
    const QSize limit = size.width() >= size.height() ? QSize(1280, 720) : QSize(720, 1280);
    if (size.width() > limit.width() || size.height() > limit.height())
        size.scale(limit, Qt::KeepAspectRatio);
    return {size.width() & ~1, size.height() & ~1}; // Encoder pixels, even dimensions; never upscale.
}

// OBS 32.2.2 SimpleOutput keeps these per-encoder properties during StartRecording.
// Never edit the profile, output resolution, encoder settings JSON, or video subsystem.
class RecordingQuality : public QObject {
public:
    RecordingQuality()
    {
        connect(&retry, &QTimer::timeout, this, [this] { restore(); });
    }
    ~RecordingQuality() override
    {
        restore();
        // OBS may still own an active encoder during shutdown. Do not reconfigure it.
        obs_encoder_release(encoder);
    }
    bool apply(bool economy)
    {
        if (!restore()) return false;
        if (!economy) return true;
        if (obs_frontend_recording_active() || obs_frontend_streaming_active() ||
            obs_frontend_replay_buffer_active() || obs_frontend_virtualcam_active()) return false;
        auto *profile = obs_frontend_get_profile_config();
        if (!profile) return false;
        const char *mode = config_get_string(profile, "Output", "Mode");
        const char *quality = config_get_string(profile, "SimpleOutput", "RecQuality");
        if (!mode || std::strcmp(mode, "Simple") || !quality ||
            (std::strcmp(quality, "Small") && std::strcmp(quality, "HQ"))) return false;
        obs_video_info video{};
        if (!obs_get_video_info(&video) || video.output_width < 2 || video.output_height < 2 ||
            !video.fps_num || !video.fps_den) return false;
        const auto size = recordingEconomySize({int(video.output_width), int(video.output_height)});
        if (size.width() < 2 || size.height() < 2) return false;
        auto *candidate = obs_get_encoder_by_name("simple_video_recording"); // Owned ref; SimpleOutput.cpp.
        if (!candidate) return false;
        auto *output = obs_frontend_get_recording_output();
        auto *attached = output ? obs_output_get_video_encoder(output) : nullptr; // Borrowed ref.
        const bool usable = output && !obs_output_active(output) && (!attached || attached == candidate) &&
                            !obs_encoder_active(candidate) && !obs_encoder_scaling_enabled(candidate);
        obs_output_release(output);
        if (!usable) { obs_encoder_release(candidate); return false; }
        const auto divisor = obs_encoder_get_frame_rate_divisor(candidate);
        const auto scale = obs_encoder_get_scale_type(candidate);
        const uint64_t denominator = uint64_t(video.fps_den) * 30;
        const auto limited = std::max(divisor, uint32_t((video.fps_num + denominator - 1) / denominator));
        if (!divisor || !obs_encoder_set_frame_rate_divisor(candidate, limited)) {
            obs_encoder_release(candidate); return false;
        }
        encoder = candidate;
        originalDivisor = divisor;
        originalScale = scale;
        obs_encoder_set_scaled_size(encoder, uint32_t(size.width()), uint32_t(size.height()));
        obs_encoder_set_gpu_scale_type(encoder, OBS_SCALE_BICUBIC);
        blog(LOG_INFO, "OBS Quick Record: capacity mode %dx%d frame divisor=%u",
             size.width(), size.height(), limited);
        return true;
    }
    bool restore()
    {
        if (!encoder) return true;
        // An external replay buffer can keep the recording encoder active after STOPPED.
        // Wait for inactivity instead of altering a running output.
        if (obs_encoder_active(encoder) || !obs_encoder_set_frame_rate_divisor(encoder, originalDivisor)) {
            if (!retry.isActive()) retry.start(500);
            return false;
        }
        retry.stop();
        obs_encoder_set_scaled_size(encoder, 0, 0);
        obs_encoder_set_gpu_scale_type(encoder, originalScale);
        obs_encoder_release(encoder);
        encoder = nullptr;
        blog(LOG_INFO, "OBS Quick Record: recording encoder size and frame rate restored");
        return true;
    }
private:
    obs_encoder_t *encoder = nullptr;
    uint32_t originalDivisor = 1;
    obs_scale_type originalScale = OBS_SCALE_DISABLE;
    QTimer retry;
};
