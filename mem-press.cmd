@echo off
setlocal EnableExtensions
rem Native Windows launcher. Uses MSVC when available, otherwise gcc/clang.
set "ROOT=%~dp0"
set "CACHE=%LOCALAPPDATA%\mem-press"
if not defined LOCALAPPDATA set "CACHE=%TEMP%\mem-press"
if not exist "%CACHE%" mkdir "%CACHE%" >nul 2>&1
set "EXE=%CACHE%\mem-press.exe"
if not exist "%EXE%" goto build
for /f %%T in ('powershell -NoProfile -Command "$s=Get-Item ''%ROOT%mem-press.c'';$e=Get-Item ''%EXE%'';if($s.LastWriteTimeUtc -gt $e.LastWriteTimeUtc){1}else{0}"') do if %%T==1 goto build
:run
"%EXE%" %*
exit /b %ERRORLEVEL%
:build
where cl >nul 2>&1
if not errorlevel 1 (
  cl /nologo /O2 /W4 /D_CRT_SECURE_NO_WARNINGS /Fe:"%EXE%" "%ROOT%mem-press.c" Psapi.lib
  if not errorlevel 1 goto run
)
where gcc >nul 2>&1
if not errorlevel 1 (
  gcc -O2 -Wall -Wextra -o "%EXE%" "%ROOT%mem-press.c" -lpsapi
  if not errorlevel 1 goto run
)
where clang >nul 2>&1
if not errorlevel 1 (
  clang -O2 -Wall -Wextra -o "%EXE%" "%ROOT%mem-press.c" -lpsapi
  if not errorlevel 1 goto run
)
echo mem-press: install MSVC, gcc, or clang and retry 1>&2
exit /b 127
