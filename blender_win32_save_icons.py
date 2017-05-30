"""
Render icons for the Microsoft Windows platform.
"""

import bpy

import contextlib
import os
import sys

def get_own_argv():
    argv = sys.argv

    if "--" not in argv:
        return []  # as if no args are passed

    return argv[argv.index("--") + 1:]


@contextlib.contextmanager
def fully_redirect_stdout(output):
    stdout = sys.stdout
    stdout_fd = stdout.fileno()
    # copy stdout_fd before it is overwritten
    #NOTE: `copied` is inheritable on Windows when duplicating a standard stream
    with os.fdopen(os.dup(stdout_fd), 'wb') as copied:
        stdout.flush()  # flush library buffers that dup2 knows nothing about
        os.dup2(output.fileno(), stdout_fd)
        try:
            yield stdout # allow code to be run with the redirected stdout
        finally:
            # restore stdout to its previous value
            #NOTE: dup2 makes stdout_fd inheritable unconditionally
            stdout.flush()
            os.dup2(copied.fileno(), stdout_fd)  # $ exec >&copied


def win32_save_icons(output_dir):
    with open(os.path.join(output_dir, 'blender_render.log'), 'w') as logfile:
        for scene in bpy.data.scenes:
            print("Scene", scene.name)
            for resolution in [16, 32, 48, 256]:
                filepath = os.path.abspath(os.path.join(output_dir, '%s_%spx' % (scene.name, resolution)))
                attrs = dict(
                 resolution_x = resolution,
                    resolution_y = resolution,
                    resolution_percentage = 100.0,
                    use_file_extension = True,
                    filepath=filepath
                )
                saved_attrs = { a: getattr(scene.render, a) for a in attrs.keys() }
                try:
                    for a, value in attrs.items(): setattr(scene.render, a, value)
                    with fully_redirect_stdout(logfile):
                        print("Rendering it")
                        bpy.ops.render.render(animation=False, write_still=True, use_viewport=False, layer="", scene=scene.name)
                    yield attrs['filepath']
                finally:
                    for a, value in saved_attrs.items(): setattr(scene.render, a, value)

def main():
    import argparse  # to parse options for us and print a nice help message

    # When --help or no args are given, print this help
    usage_text = (
            "Run blender in background mode with this script:"
            "  blender --background --python " + __file__ + " -- [options]"
            )

    parser = argparse.ArgumentParser(description=usage_text)

    # Example utility, add some text and renders or saves it (with options)
    # Possible types are: string, int, long, choice, float and complex.
    parser.add_argument("-o", "--output-dir", dest="output_dir", metavar='FILE',
            help="Save the generated files to the specified path")

    argv = get_own_argv()
    if not argv:
        parser.print_help()
        return

    args = parser.parse_args(argv)

    if not args.output_dir:
        print('Missing output dir')
        parser.print_help()
        return

    bpy.context.user_preferences.addons['cycles'].preferences.compute_device_type = 'CUDA'
    bpy.context.user_preferences.addons['cycles'].preferences.devices[0].use = True
    for output_filepath in win32_save_icons(args.output_dir):
        print("IMAGE\t%s" % output_filepath)

    print("batch job finished, exiting")



if __name__ == "__main__":
    main()


