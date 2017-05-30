REM # Build long-lasting assets into assets/
REM #
@if not defined BuildDir @set BuildDir=builds
@if not defined BuildObjDir @set BuildObjDir=%BuildDir%\obj
@if not exist %BuildObjDir% mkdir %BuildObjDir%
@if not exist %BuildDir% mkdir %BuildDir%

@if not defined BlenderExe set BlenderExe=blender.exe
@if not exist %BlenderExe% (
	@echo Missing: %BlenderExe% i.e. BlenderExe
	exit /b 1
)

REM Build icons:
%BlenderExe% --background --factory-startup ^
  assets\focus.blend ^
  --python blender_win32_save_icons.py ^
  --verbose 0 ^
  -- --output-dir %BuildObjDir%

REM Build tools:
cl /Fe:%BuildDir%/make_ico.exe /Z7 /DEBUG unit_make_ico.cpp ^
   /Fo%BuildObjDir%\ /nologo /EHsc

REM convert pngs to .ico file for windows
%BuildDir%\make_ico.exe --output %BuildDir%\focus.ico ^
    %BuildObjDir%\Icon3_16px.png ^
    %BuildObjDir%\Icon3_32px.png ^
    %BuildObjDir%\Icon3_48px.png ^
    %BuildObjDir%\Icon3_256px.png
copy %BuildDir%\focus.ico assets\focus.ico
