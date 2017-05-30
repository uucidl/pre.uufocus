// @language: c++14
#include "uu_focus_effects.hpp"
#include "uu_focus_effects_types.hpp"

#include "uu_focus_platform.hpp"

#include <cmath>
#include <random>

static double global_audio_amp_target = 0.0;
static uint64_t global_audio_fade_remaining_samples = 0;

static void set_main_fade(double target, uint64_t duration_micros)
{
    global_audio_amp_target = target;
    uint64_t micros_per_sample = 1'000'000/48000;
    global_audio_fade_remaining_samples = duration_micros/micros_per_sample;
}

static void audio_fade_in(uint64_t duration_micros)
{
    set_main_fade(1.0, duration_micros);
}

static void audio_fade_out(uint64_t duration_micros)
{
    set_main_fade(0.0, duration_micros);
}

void audio_start(AudioEffect*)
{
    audio_fade_in(1'000'000);
}

void audio_stop(AudioEffect*)
{
    audio_fade_out(1'000'000);
}

static double
db_to_amp (double volume_in_db)
{
    return pow (exp (volume_in_db), log (10.0) / 20.0);
}

static constexpr double TAU = 6.2831853071795864769252;

static double white_noise_step()
{
    static std::random_device rd;
    static std::mt19937 g(rd());
    static std::uniform_real_distribution<> d(-1.0, 1.0);
    return d(g);
}

#if UU_FOCUS_INTERNAL
static void reference_tone_n(float* stereo_frames, int frame_count)
{
    static const auto reference_hz = 1000;
    static const auto reference_amp = db_to_amp(-20.0);
    static double phase;

    double phase_delta = reference_hz / 48000.0;
    for (int i = 0; i < frame_count; ++i) {
        float y = float(reference_amp * std::sin(TAU*phase));
        stereo_frames[2*i] = stereo_frames[2*i + 1] = y;
        phase += phase_delta;
    }
}
#endif

struct PinkNoiseState
{
    double b0, b1, b2, b3, b4, b5, b6;
    double pink;
};

static void pink_noise_step(PinkNoiseState *_s, double const white)
{
    // Filter by Paul Kellet (pk3 = (Black))
    // paul.kellett@maxim.abel.co.uk
    //
    // Filter to make pink noise from white  (updated March 2000)
    // ------------------------------------
    //
    // This is an approximation to a -10dB/decade filter using a weighted sum
    // of first order filters. It is accurate to within +/-0.05dB above 9.2Hz
    // (44100Hz sampling rate). Unity gain is at Nyquist, but can be adjusted
    // by scaling the numbers at the end of each line.
    //
    // If 'white' consists of uniform random numbers, such as those generated
    // by the rand() function, 'pink' will have an almost gaussian level
    // distribution.

    auto& s = *_s;
    s.b0 = 0.99886 * s.b0 + white * 0.0555179;
    s.b1 = 0.99332 * s.b1 + white * 0.0750759;
    s.b2 = 0.96900 * s.b2 + white * 0.1538520;
    s.b3 = 0.86650 * s.b3 + white * 0.3104856;
    s.b4 = 0.55000 * s.b4 + white * 0.5329522;
    s.b5 = -0.7616 * s.b5 - white * 0.0168980;
    s.pink = s.b0 + s.b1 + s.b2 + s.b3 + s.b4 + s.b5 + s.b6 + white * 0.5362;
    s.b6 = white * 0.115926;
}

static void pink_noise_n(PinkNoiseState *_s, float* stereo_frames, int frame_count)
{
    auto& s = *_s;
    for (int i = 0; i < frame_count; ++i) {
        auto white = white_noise_step();
        pink_noise_step(&s, white);
        auto y = float(s.pink);
        stereo_frames[2*i] = stereo_frames[2*i + 1] = y;
    }
}

void audio_thread_render(AudioEffect*, float* stereo_samples, int sample_count)
{
    static double amp = 0.0;
    auto& fade_remaining_samples = global_audio_fade_remaining_samples;
    auto const amp_target = global_audio_amp_target;

    double amp_inc = 0.0;
    if (fade_remaining_samples != 0) {
        amp_inc = double(amp_target - amp) / fade_remaining_samples;
    }

    if (fade_remaining_samples == 0 && amp_target == 0.0) {
        memset(stereo_samples, 0, sample_count * 2 * sizeof(float));
    } else {
#if UU_FOCUS_INTERNAL
        reference_tone_n(stereo_samples, sample_count);
#else
        auto pink_noise_amp = db_to_amp(-26);
        static PinkNoiseState pink = {};
        pink_noise_n(&pink, stereo_samples, sample_count);
        for (int i = 0; i < sample_count; ++i) {
            float y = float(pink_noise_amp);
            stereo_samples[2*i] *= y;
            stereo_samples[2*i + 1] *= y;
        }
#endif
        for (int i = 0; i < sample_count; ++i) {
            auto y = amp;
            stereo_samples[2*i] *= float(y);
            stereo_samples[2*i + 1] *= float(y);
            if (fade_remaining_samples == 0) {
                amp = amp_target;
            } else if (fade_remaining_samples > 0) {
                amp += amp_inc;
                --fade_remaining_samples;
            }
        }
    }
}

static int const default_duration_s =
#if UU_FOCUS_INTERNAL
  5
#else
  25*60
#endif
  ;

TimerEffect* timer_make(Platform* platform)
{
    auto _timer = new TimerEffect;
    auto &timer = *_timer;
    timer = {};
    timer.platform = platform;
    return &timer;
}

void timer_start(TimerEffect* _timer)
{
    auto& timer = *_timer;
    ++timer.on_count;
    timer_reset(&timer);
    timer_update_and_render(&timer);
}

void timer_stop(TimerEffect* _timer)
{
    auto& timer = *_timer;
    --timer.on_count;
    timer_update_and_render(&timer);
}

bool timer_is_active(TimerEffect* _timer)
{
    auto& timer = *_timer;
    return timer.on_count > 0 && timer.now_micros < timer.end_micros;
}

void timer_update_and_render(TimerEffect* _timer)
{
    auto& timer = *_timer;
    platform_render_async(timer.platform);
}

void timer_reset(TimerEffect* _timer)
{
    auto& timer = *_timer;
    timer.end_micros = timer.now_micros + default_duration_s*1'000'000;
    timer_update_and_render(&timer);
}

void timer_celebrate(TimerEffect* _timer)
{
    auto& timer = *_timer;
    timer.on_count = 0;
    timer_update_and_render(&timer);
    platform_notify(timer.platform, ui_text("Focus time over! Congrats."));
}
