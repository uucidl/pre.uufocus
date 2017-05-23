#include "uu_focus_main.hpp"

#include <cassert>
#include <vector>

struct TimerEffect
{
    int on_count = 0;
    std::vector<std::string> actions; // trace
};

struct AudioEffect
{
    std::vector<std::string> actions;
};

struct Scenario
{
    Scenario(std::string name) { std::printf("TEST: %s\n", name.c_str()); }
    ~Scenario() { std::printf("TEST END\n\n"); };
};

static char const* CommandName(Command x);

template <typename BoundedRange>
std::size_t count_range(BoundedRange r, typename BoundedRange::value_type const& x)
{
    using namespace std;
    return count(begin(r), end(r), x);
}

int main(int argc, char** argv)
{
    auto const input = [](UUFocusMainCoroutine* _program, Command x) {
        auto &program = *_program;
        program.input.command.type = x;
        std::printf("INPUT: %s\n", CommandName(x));
    };

    {
        Scenario _("check program termination");
        TimerEffect timer;
        AudioEffect audio;
        UUFocusMainCoroutine program;
        {
            program = {};
            program.timer_effect = &timer;
            program.audio_effect = &audio;
        }

        input(&program, Command_application_stop);
        uu_focus_main(&program);
        auto last_step = program.step;
        uu_focus_main(&program);
        assert(program.step == last_step /* termination */);
        assert(timer.actions.empty()); // no effect should have occured
        assert(audio.actions.empty()); // no effect should have occured
    }

    {
        Scenario _("some trivial start; immediate stop");
        TimerEffect timer;
        AudioEffect audio;
        UUFocusMainCoroutine program;
        {
            program = {};
            program.timer_effect = &timer;
            program.audio_effect = &audio;
        }

        uu_focus_main(&program);
        uu_focus_main(&program);

        input(&program, Command_timer_start);
        uu_focus_main(&program);

        uu_focus_main(&program);

        input(&program, Command_timer_stop);
        uu_focus_main(&program);

        uu_focus_main(&program);
        assert(!timer.actions.empty());
        assert(timer.actions.back() == "timer reset");
        assert(count_range(timer.actions, "timer start")
               == count_range(timer.actions, "timer stop")
               == 1);
        assert(count_range(audio.actions, "audio start")
               == count_range(audio.actions, "audio stop")
               == 1);
        assert(count_range(timer.actions, "timer celebrate") == 0);
    }

    {
        Scenario _("the timer is updated at least once every second");
        TimerEffect timer;
        AudioEffect audio;
        UUFocusMainCoroutine program;
        {
            program = {};
            program.timer_effect = &timer;
            program.audio_effect = &audio;
        }

        input(&program, Command_timer_start);
        uu_focus_main(&program);

        program.input.time_micros += 200;
        uu_focus_main(&program);
        assert(count_range(timer.actions,
                     "timer update_and_render") == 0);

        program.input.time_micros += 500'000;
        uu_focus_main(&program);

        program.input.time_micros += 500'000;
        uu_focus_main(&program);

        assert(count_range(timer.actions,
                     "timer update_and_render") == 1);
        assert(timer.actions.size() == 3);

        program.input.time_micros += 200;
        uu_focus_main(&program);
        assert(timer.actions.size() == 3);
    }

    {
        Scenario _("start command resets the timer after start");
        TimerEffect timer;
        AudioEffect audio;
        UUFocusMainCoroutine program;
        {
            program = {};
            program.timer_effect = &timer;
            program.audio_effect = &audio;
        }

        input(&program, Command_timer_start);
        uu_focus_main(&program);

        input(&program, Command_timer_start);
        uu_focus_main(&program);

        {
            auto const last = timer.actions.end();
            auto timer_start = find(timer.actions.begin(), last, "timer start");
            assert(timer_start != last);
            auto timer_reset = find(timer_start, last, "timer reset");
            assert(timer_reset != last);
            assert(timer.on_count == 1);
            assert(count_range(timer.actions, "timer celebrate") == 0);
        }
    }

    {
        Scenario _("timer celebrates once done");
        TimerEffect timer;
        AudioEffect audio;
        UUFocusMainCoroutine program;
        {
            program = {};
            program.timer_effect = &timer;
            program.audio_effect = &audio;
        }

        input(&program, Command_timer_start);
        uu_focus_main(&program);

        --timer.on_count; // timer expires

        uu_focus_main(&program);

        assert(1 == count_range(timer.actions, "timer celebrate"));
    }
}

#include "uu_focus_main.cpp"

#include <cstdio>

static char const* CommandName(Command x)
{
#define E(expr) case expr: return #expr
    switch(x) {
        E(Command_null);
        E(Command_application_stop);
        E(Command_timer_start);
        E(Command_timer_stop);
    }
#undef E
    return "Command_<unknown>";
}


static void effect_log(std::vector<std::string>* _trace, std::string x)
{
    auto& trace = *_trace;
    trace.push_back(x);
    std::printf("EFFECT: %s\n", x.c_str());
}

void audio_start(AudioEffect* y)
{
    effect_log(&y->actions, "audio start");
}

void audio_stop(AudioEffect* y)
{
    effect_log(&y->actions, "audio stop");
}

void timer_start(TimerEffect* y)
{
    ++y->on_count;
    effect_log(&y->actions, "timer start");
}

void timer_stop(TimerEffect* y)
{
    --y->on_count;
    effect_log(&y->actions, "timer stop");
}

bool timer_is_active(TimerEffect* y)
{
    return y->on_count;
}

void timer_celebrate(TimerEffect* y)
{
    effect_log(&y->actions, "timer celebrate");
}

void timer_update_and_render(TimerEffect* y)
{
    effect_log(&y->actions, "timer update_and_render");
}

void timer_reset(TimerEffect* y)
{
    effect_log(&y->actions, "timer reset");
}

