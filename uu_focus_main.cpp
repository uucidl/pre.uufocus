#include "uu_focus_main.hpp"

#include "uu_focus_effects.hpp"

static void pop_command(UUFocusMainCoroutine* _program);
static void set(UUFocusMainCoroutine* _program, int step);
static void jump(UUFocusMainCoroutine* _program, int step);

// NOTE(Nicolas): main logic of the application.
void uu_focus_main(UUFocusMainCoroutine* _program)
{
    auto& program = *_program;

    CommandMsg const& command = program.input.command;
    double const step_elapsed_micros =
        double(program.input.time_micros - program.step_micros);
    auto timer = program.timer_effect;
    auto audio = program.audio_effect;

    if (command.type == Command_application_stop) {
        if (timer_is_active(timer)) {
            audio_stop(audio);
            timer_stop(timer);
        }
        pop_command(&program);
        jump(&program, 200);
        goto yield;
    }

    switch(/* resume */ program.step) {
        while(true) {
            case 0: set(&program, 0);
            timer_reset(timer);
            /* fallthrough */

            case 10: set(&program, 10);
            if (command.type != Command_timer_start) {
                pop_command(&program);
                goto yield;
            }
            pop_command(&program);
            timer_start(timer);
            audio_start(audio);
            /* fallthrough */

            case 20: set(&program, 20); {
                auto timeout = step_elapsed_micros >= 1e6;
                if (timer_is_active(timer)) {
                    if (command.type) {
                        CommandMsg const& command = program.input.command;
                        if (command.type == Command_timer_stop) {
                            audio_stop(audio);
                            timer_stop(timer);
                            pop_command(&program);
                        } else if (command.type == Command_timer_start) {
                            timer_reset(timer);
                            timer_update_and_render(timer);
                            pop_command(&program);
                        }
                    } else if(timeout) {
                        timer_update_and_render(timer);
                        goto yield; // wait
                    } else {
                        goto yield; // wait
                    }
                } else {
                    pop_command(&program);
                    timer_celebrate(timer);
                    timer_update_and_render(timer);
                }
            }
            /* fallthrough */
        }

        case 200:
        /* application end */
        goto yield;
    }

yield: /* return to caller */
    return;
}

static void pop_command(UUFocusMainCoroutine* _program)
{
    auto &program = *_program;
    program.input.command = {};
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
