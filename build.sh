#!/bin/sh
set -e

mkdir -p builds
c++ -std=c++14 -Wall -Wextra test_unit_uu_focus_main.cpp -o builds/test_uu_focus
printf "PROGRAM\t%s\ttest\n" builds/test_uu_focus
builds/test_uu_focus > /dev/null

c++ -o builds/uu_focus_dev \
    -std=c++14 \
    macos_unit_uu_focus_main.mm \
    -DUU_FOCUS_INTERNAL=1 \
    -g \
    -framework AppKit \
    -framework CoreAudio
printf "PROGRAM\t%s\n" builds/uu_focus_dev

c++ -o builds/uu_focus_release \
    -std=c++14 \
    macos_unit_uu_focus_main.mm \
    -DUU_FOCUS_INTERNAL=0 \
    -g \
    -framework AppKit \
    -framework CoreAudio
printf "PROGRAM\t%s\n" builds/uu_focus_release

# bundle app:
rm -rf builds/UUFocus.app
mkdir -p builds/UUFocus.app/Contents/MacOS
cp macos-Info.plist builds/UUFocus.app/Contents/Info.plist
cp builds/uu_focus_release builds/UUFocus.app/Contents/MacOS/uu_focus
printf "MACOS_APP\t%s\n" builds/UUFocus.app
