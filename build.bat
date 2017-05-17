@set BuildDir=builds
@set BuildObjDir=%BuildDir%\obj
@if not exist %BuildDir% mkdir %BuildDir%
@if not exist %BuildObjDir% mkdir %BuildObjDir%

REM check syntax with clang
@set ClangWarnings=-Wall -Wextra -Wno-unused-parameter -Wmissing-prototypes
clang -fsyntax-only win32_unit_uu_focus_main.cpp %ClangWarnings%
clang -fsyntax-only uu_focus_main.cpp %ClangWarnings%
clang -fsyntax-only test_unit_uu_focus_main.cpp %ClangWarnings%


REM Build tests:
cl -nologo -EHsc -Od -Z7 -W3 test_unit_uu_focus_main.cpp -Fo%BuildObjDir%\ ^
  -Fe%BuildDir%\test_uu_focus.exe
%BuildDir%\test_uu_focus.exe
@if errorlevel 1 (
	echo ERROR: test_uu_focus.exe
	exit /b %errorlevel%
)

REM Build program:
cl -nologo -Od -Z7 -W3 win32_unit_uu_focus_main.cpp -Fo%BuildObjDir%\ -Fe%BuildDir%\uu_focus.exe

