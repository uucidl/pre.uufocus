#include "uu_focus_main.hpp"

#include "uu_focus_effects.hpp"

static CommandMsg pop_command(UUFocusMainCoroutine* _program);
static void set(UUFocusMainCoroutine* _program, int step);
static void jump(UUFocusMainCoroutine* _program, int step);

// NOTE(Nicolas): main logic of the application.
void uu_focus_main(UUFocusMainCoroutine* _program)
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
        goto yield;
    }

    switch(/* resume */ program.step) {
        case 0:

        while(true) {
            timer_reset(timer);

            set(&program, 10); case 10:
            if (pop_command(&program).type != Command_timer_start) {
                goto yield;
            }

            timer_start(timer);
            audio_start(audio);

            set(&program, 20); case 20: {
                auto const command = pop_command(&program);
                auto timeout = step_elapsed_micros >= 1e6;
                if (timer_is_active(timer)) {
                    if (command.type) {
                        if (command.type == Command_timer_stop) {
                            audio_stop(audio);
                            timer_stop(timer);
                            jump(&program, 0); /* reset */
                        } else if (command.type == Command_timer_start) {
                            timer_reset(timer);
                            timer_update_and_render(timer);
                        }
                    } else if(timeout) {
                        timer_update_and_render(timer);
                        // reset and wait:
                        set(&program, 20);
                    }
                    goto yield;
                }

                timer_celebrate(timer);
                timer_update_and_render(timer);
            }
        }

        case 200:
        /* application end */
        goto yield;
    }

yield: /* return to caller */
    return;
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
