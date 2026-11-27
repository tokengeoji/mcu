@echo off
setlocal
set "APP=%~dp0app.py"
where py >nul 2>nul
if not errorlevel 1 (
  py -3 -c "import sys, tkinter; assert sys.version_info >= (3, 10)" >nul 2>nul
  if not errorlevel 1 goto launch_py
)
set "PYTHON_EXE=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if exist "%PYTHON_EXE%" goto launch_exe
for /f "delims=" %%P in ('where python.exe 2^>nul ^| findstr /i /v "\\WindowsApps\\"') do (
  "%%P" -c "import sys, tkinter; assert sys.version_info >= (3, 10)" >nul 2>nul
  if not errorlevel 1 (
    set "PYTHON_EXE=%%P"
    goto launch_exe
  )
)
echo This mapper needs Python 3.10 or later with Tcl/Tk.
echo Install Python for Windows, then run this file again.
pause
exit /b 1

:launch_py
py -3 -B "%APP%"
goto done

:launch_exe
"%PYTHON_EXE%" -B "%APP%"

:done
if not errorlevel 1 exit /b 0
pause
exit /b 1
