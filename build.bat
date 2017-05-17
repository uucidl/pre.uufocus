set BuildDir=builds
set BuildObjDir=%BuildDir%\obj
mkdir %BuildDir%
mkdir %BuildObjDir%
cl win32_unit_uu_focus_main.cpp -Fo%BuildObjDir%\ -Fe%BuildDir%\uu_focus.exe


