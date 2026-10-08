#include "recording-quality.hpp"
#include "settings.hpp"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTemporaryDir>
#include <cassert>
#include <iostream>
#include <util/bmem.h>

// Real libobs encoder properties, references and settings I/O. No GPU, capture,
// profile file, or encoding is needed; only frontend state/video info is fake.
static config_t *profile = nullptr;
static obs_output_t *output = nullptr;
static obs_video_info video{};
static int busy = 0;
static QString settingsFile;
extern "C" {
config_t *obs_frontend_get_profile_config() { return profile; }
obs_output_t *obs_frontend_get_recording_output() { return obs_output_get_ref(output); }
bool obs_frontend_recording_active() { return busy == 1; }
bool obs_frontend_streaming_active() { return busy == 2; }
bool obs_frontend_replay_buffer_active() { return busy == 3; }
bool obs_frontend_virtualcam_active() { return busy == 4; }
bool obs_get_video_info(obs_video_info *info) { *info = video; return true; }
obs_module_t *obs_current_module() { return reinterpret_cast<obs_module_t *>(1); }
char *obs_module_get_config_path(obs_module_t *, const char *) { return bstrdup(settingsFile.toUtf8().constData()); }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    assert(obs_startup("en-US", nullptr, nullptr));
    assert(config_open_string(&profile, "[Output]\nMode=Simple\n[SimpleOutput]\nRecQuality=Small\n") == CONFIG_SUCCESS);
    QTemporaryDir temp;
    assert(temp.isValid()); settingsFile = temp.filePath("settings.json");
    { Settings settings; assert(!settings.economy); settings.economy = true; assert(settings.save()); }
    { Settings settings; assert(settings.economy); settings.economy = false; assert(settings.save()); }
    { Settings settings; assert(!settings.economy); }

    obs_encoder_info info{};
    info.id = "qa-video"; info.type = OBS_ENCODER_VIDEO; info.codec = "h264";
    info.get_name = [](void *) { return "Quality test encoder"; };
    info.create = [](obs_data_t *, obs_encoder_t *encoder) -> void * { return encoder; };
    info.destroy = [](void *) {};
    info.encode = [](void *, encoder_frame *, encoder_packet *, bool *) { return true; };
    obs_register_encoder(&info);
    auto *encoder = obs_video_encoder_create("qa-video", "simple_video_recording", nullptr, nullptr);
    assert(encoder);
    obs_output_info oi{};
    oi.id = "qa-recording"; oi.flags = OBS_OUTPUT_ENCODED | OBS_OUTPUT_VIDEO;
    oi.get_name = [](void *) { return "Quality test output"; };
    oi.create = [](obs_data_t *, obs_output_t *o) -> void * { return o; };
    oi.destroy = [](void *) {};
    oi.start = [](void *) { return true; }; oi.stop = [](void *, uint64_t) {};
    oi.encoded_packet = [](void *, encoder_packet *) {};
    obs_register_output(&oi);
    output = obs_output_create("qa-recording", "test recording", nullptr, nullptr);
    assert(output);
    video.output_width = 1920; video.output_height = 1080; video.fps_num = 60; video.fps_den = 1;
    {
        RecordingQuality quality;
        assert(quality.apply(false) && !obs_encoder_scaling_enabled(encoder));
        assert(quality.apply(true)); // Before the first OBS recording: no encoder media or output attachment yet.
        assert(obs_encoder_scaling_enabled(encoder));
        assert(obs_encoder_get_frame_rate_divisor(encoder) == 2);
        assert(quality.restore());
        assert(!obs_encoder_scaling_enabled(encoder) && obs_encoder_get_frame_rate_divisor(encoder) == 1);
        assert(obs_encoder_get_scale_type(encoder) == OBS_SCALE_DISABLE);
    }
    video_output_info vi{};
    vi.name = "Quality CPU video"; vi.format = VIDEO_FORMAT_NV12;
    vi.fps_num = 60; vi.fps_den = 1; vi.width = 1920; vi.height = 1080;
    vi.cache_size = 2; vi.colorspace = VIDEO_CS_709; vi.range = VIDEO_RANGE_PARTIAL;
    video_t *media = nullptr;
    assert(video_output_open(&media, &vi) == VIDEO_OUTPUT_SUCCESS);
    obs_encoder_set_video(encoder, media);
    obs_output_set_video_encoder(output, encoder);
    {
        RecordingQuality quality;
        for (auto fps : {24U, 25U, 30U, 50U, 60U, 120U}) {
            video.fps_num = fps;
            assert(quality.apply(true));
            assert(obs_encoder_get_width(encoder) == 1280 && obs_encoder_get_height(encoder) == 720);
            assert(obs_encoder_get_frame_rate_divisor(encoder) == (fps + 29) / 30);
            assert(quality.restore());
            assert(obs_encoder_get_width(encoder) == 1920 && obs_encoder_get_height(encoder) == 1080);
        }
        video.fps_num = 60000; video.fps_den = 1001;
        assert(quality.apply(true) && obs_encoder_get_frame_rate_divisor(encoder) == 2);
        assert(quality.restore());
        assert(obs_encoder_set_frame_rate_divisor(encoder, 4));
        obs_encoder_set_gpu_scale_type(encoder, OBS_SCALE_LANCZOS);
        assert(quality.apply(true) && obs_encoder_get_frame_rate_divisor(encoder) == 4);
        assert(quality.restore());
        assert(obs_encoder_get_frame_rate_divisor(encoder) == 4 && obs_encoder_get_scale_type(encoder) == OBS_SCALE_LANCZOS);
        for (busy = 1; busy <= 4; ++busy) assert(!quality.apply(true));
        busy = 0;
        for (auto mode : {"Stream", "Lossless", ""}) {
            config_set_string(profile, "SimpleOutput", "RecQuality", mode);
            assert(!quality.apply(true));
        }
        config_set_string(profile, "SimpleOutput", "RecQuality", "HQ");
        config_set_string(profile, "Output", "Mode", "Advanced");
        assert(!quality.apply(true));
        config_set_string(profile, "Output", "Mode", "Simple");
        obs_encoder_set_scaled_size(encoder, 640, 480);
        assert(!quality.apply(true) && obs_encoder_get_width(encoder) == 640);
        obs_encoder_set_scaled_size(encoder, 0, 0);
        auto *other = obs_video_encoder_create("qa-video", "other", nullptr, nullptr);
        obs_output_set_video_encoder(output, other);
        assert(!quality.apply(true));
        obs_output_set_video_encoder(output, encoder);
        obs_encoder_release(other);
        video.output_width = 1080; video.output_height = 1920;
        assert(quality.apply(true));
        assert(obs_encoder_get_width(encoder) == 720 && obs_encoder_get_height(encoder) == 1280);
        // Destructor is also a rollback path for failed start / unload.
    }
    assert(!obs_encoder_scaling_enabled(encoder) && obs_encoder_get_frame_rate_divisor(encoder) == 4);
    assert(obs_encoder_get_scale_type(encoder) == OBS_SCALE_LANCZOS);
    assert(recordingEconomySize({800, 600}) == QSize(800, 600));
    assert(recordingEconomySize({2560, 1440}) == QSize(1280, 720));
    assert(recordingEconomySize({1600, 1200}) == QSize(960, 720));
    assert(recordingEconomySize({1920, 800}) == QSize(1280, 532));
    assert(recordingEconomySize({641, 481}) == QSize(640, 480));
    obs_output_release(output);
    obs_encoder_release(encoder);
    video_output_close(media);
    config_close(profile);
    obs_shutdown();
    std::cout << "720p bounds, rational fps, encoder rollback, unsupported modes and settings persistence passed\n";
}
