#!/bin/sh
mkdir -p builds
c++ -std=c++14 -Wall -Wextra test_unit_uu_focus_main.cpp -o builds/test_uu_focus
builds/test_uu_focus
