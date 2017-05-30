@if not defined BuildDir @set BuildDir=builds
@if not defined BuildObjDir @set BuildObjDir=%BuildDir%\obj
@if not exist %BuildObjDir% mkdir %BuildObjDir%
@if not exist %BuildDir% mkdir %BuildDir%

REM check syntax with clang
@set ClangWarnings=-Wall -Wextra -Wno-unused-parameter -Wmissing-prototypes
REM @clang -fsyntax-only win32_unit_uu_focus_main.cpp %ClangWarnings%
REM @clang -fsyntax-only uu_focus_main.cpp %ClangWarnings%
REM @clang -fsyntax-only test_unit_uu_focus_main.cpp %ClangWarnings%
REM @clang -fsyntax-only unit_make_ico.cpp -D_CRT_SECURE_NO_WARNINGS

REM Build tests:
cl -nologo -EHsc -Od -Z7 -W3 test_unit_uu_focus_main.cpp -Fo%BuildObjDir%\ ^
  -Fe%BuildDir%\test_uu_focus.exe
%BuildDir%\test_uu_focus.exe
@if errorlevel 1 (
	echo ERROR: test_uu_focus.exe
	exit /b %errorlevel%
)

REM Build program:
REM
set DllPrefixPath=%BuildDir%\uu_focus_ui
set DllPdbPath=%DllPrefixPath%_dll_%random%.pdb
echo --- WAITING FOR PDB > %DllPrefixPath%_dll.lock
cl -DUU_FOCUS_INTERNAL=1 ^
  -Fe%DllPrefixPath%.dll win32_unit_uu_focus_ui.cpp ^
  -Od -EHsc -Z7 -W3 -Fo%BuildObjDir%\ -nologo ^
  -LD -link -PDB:%DllPdbPath% ^
  -EXPORT:win32_uu_focus_ui_render

@if errorlevel 1 goto error_exit
@REM only remove lock file on success, to indicate we have a working dll
@attrib +R %DllPdbPath%
@del %DllPrefixPath%_dll_*.pdb > NUL 2> NUL
@attrib -R %DllPdbPath%
@del %DllPrefixPath%_dll.lock

rc.exe /nologo /fo %BuildDir%\uu_focus.res ^
   win32_unit_uu_focus_main.rc

cl -DUU_FOCUS_INTERNAL=1 ^
  -Fe%BuildDir%\uu_focus_dev.exe win32_unit_uu_focus_main.cpp ^
  %BuildDir%\uu_focus.res ^
  -Od -EHsc -Z7 -W3 -Fo%BuildObjDir%\ -nologo ^
  -link -PDB:%BuildDir%\uu_focus_dev.pdb

cl -DUU_FOCUS_INTERNAL=0 ^
  -Fe%BuildDir%\uu_focus_release.exe win32_unit_uu_focus_main.cpp ^
  %BuildDir%\uu_focus.res ^
  -O2 -EHsc -Z7 -W3 -Fo%BuildObjDir%\ -nologo ^
  -link -PDB:%BuildDir%\uu_focus_release.pdb

mt.exe -nologo -manifest %BuildDir%\uu_focus_release.exe.manifest -outputresource:%BuildDir%\uu_focus_release.exe;1

copy %BuildDir%\uu_focus_release.exe %BuildDir%\uu_focus.exe
