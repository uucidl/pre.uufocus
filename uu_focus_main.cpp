#include "uu_focus_main.hpp"

#include "uu_focus_effects.hpp"

static CommandMsg pop_command(UUFocusMainCoroutine* _program);
static void set(UUFocusMainCoroutine* _program, int step);
static void jump(UUFocusMainCoroutine* _program, int step);

namespace
{
struct CounterScope
{
    int *_counter;
    CounterScope(int* _counter) : _counter(_counter)
    {
        auto& counter = *_counter;
        ++counter;
    }

    ~CounterScope()
    {
        auto& counter = *_counter;
        --counter;
    }

};
} // unnamed namespace

// NOTE(Nicolas): main logic of the application.
CoroutineState uu_focus_main(UUFocusMainCoroutine* _program)
{
    auto& program = *_program;

    auto timer = program.timer_effect;
    auto audio = program.audio_effect;
    double const step_elapsed_micros =
        double(program.input.time_micros - program.step_micros);

    if (program.input.command.type == Command_application_stop) {
        pop_command(&program);
        if (timer_is_active(timer)) {
            audio_stop(audio);
            timer_stop(timer);
        }
        jump(&program, 200);
        return CoroutineState_Done;
    }

    if (program.entry_count > 0) return CoroutineState_ErrorRentry;

    CounterScope entry_counter(&program.entry_count);
    switch(/* resume */ program.step) {
        case 0:

        while(true) {
            timer_reset(timer);

            set(&program, 10); case 10:
            if (pop_command(&program).type != Command_timer_start) {
                return CoroutineState_Waiting;
            }

            timer_start(timer);
            audio_start(audio);

            set(&program, 20); case 20:
            while (timer_is_active(timer)) {
                auto const command = pop_command(&program);
                auto timeout = step_elapsed_micros >= 1e6;
                if (!timeout && !command.type) {
                    return CoroutineState_Waiting;
                }

                if (command.type == Command_timer_stop) {
                    audio_stop(audio);
                    timer_stop(timer);
                    timer_update_and_render(timer);
                    jump(&program, 0); /* reset */
                    return CoroutineState_Waiting;
                } else if(command.type == Command_timer_start) {
                    timer_reset(timer);
                    set(&program, 20); // reset timeout
                } else if (timeout) {
                    set(&program, 20); // reset timeout
                }
                timer_update_and_render(timer);
                return CoroutineState_Waiting;
            }
            timer_celebrate(timer);
            audio_stop(audio);
            timer_update_and_render(timer);
        }

        case 200:
        return CoroutineState_Done;
    }
    return CoroutineState_Done;
}

static CommandMsg pop_command(UUFocusMainCoroutine* _program)
{
    auto& program = *_program;
    auto command = program.input.command;
    program.input.command = {};
    return command;
}

static void set(UUFocusMainCoroutine* _program, int step)
{
    auto& program = *_program;
    program.step = step;
    program.step_micros = program.input.time_micros;
}

static void jump(UUFocusMainCoroutine* _program, int step)
{
    set(_program, step);
}

/*

Project
-------

- TODO(nicolas): application .ico for windows
- TODO(nicolas): foreground icon should be correct

UI, thoughts
------------

Goals:
- Looks classy,
- Good visibility 'timer running' vs 'timer stopped',
- Sound volume can be muted/lowered,
- Can't stop timer by accident
- Clear notifications:
    + Win32 Notification aren't very obtrusive, I missed
      one while working a cycle without sound on.



*/
