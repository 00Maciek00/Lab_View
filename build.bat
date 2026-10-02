@echo off
REM =============================================================================
REM  Lab_view v2.5 - Build Script / Skrypt Kompilacji
REM  Autor / Author: Maciej Sikorski
REM  Licencja / License: Apache 2.0
REM
REM  Pliki w folderze / Files required in this folder:
REM    Lab_view.cpp, Lab_view.rc, resource.h, lab_view.ico, languages\*.json
REM =============================================================================

setlocal enabledelayedexpansion
cd /d "%~dp0"

echo.
echo ================================================================================
echo  Lab View v2.5 - Build Script / Skrypt Kompilacji
echo ================================================================================
echo.

REM ----------------------------------------------------------------------------
REM  Sprawdzenie plikow zrodlowych / Check source files
REM ----------------------------------------------------------------------------
for %%F in (Lab_view.cpp Lab_view.rc resource.h lab_view.ico) do (
    if not exist "%%F" (
        echo [BLAD] Brakuje pliku / Missing file: %%F
        echo.
        pause
        exit /b 1
    )
)

if not exist "languages\*.json" (
    echo [UWAGA] Brak folderu languages\ z plikami .json - program nie bedzie mial tlumaczen!
    echo [WARN]  languages\ folder with .json files is missing - no translations will be available!
    echo.
)

REM =============================================================================
REM  KROK 1: Visual Studio
REM =============================================================================
echo [1/4] Sprawdzam Visual Studio... / Checking Visual Studio...

set "VCVARS="

REM --- Metoda 1: vswhere.exe ---------------------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "%VSWHERE%" (
    echo       Uzywam vswhere.exe... / Using vswhere.exe...
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
    )
)

REM --- Metoda 2: typowe sciezki (fallback) -------------------------------------
if not defined VCVARS (
    echo       vswhere nie znalazl instalacji, sprawdzam typowe sciezki...
    for %%Y in (18 2022 2019 2017) do (
        for %%E in (Enterprise Professional Community BuildTools) do (
            if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\%%Y\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\%%Y\%%E\VC\Auxiliary\Build\vcvars64.bat"
            if not defined VCVARS if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\%%Y\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\%%Y\%%E\VC\Auxiliary\Build\vcvars64.bat"
        )
    )
)

if not defined VCVARS (
    echo.
    echo [BLAD] Nie znaleziono Visual Studio! / Visual Studio not found!
    echo        Zainstaluj Visual Studio Community lub Build Tools z workloadem:
    echo        Install Visual Studio Community or Build Tools with workload:
    echo          "Desktop development with C++"
    echo        https://visualstudio.microsoft.com/downloads/
    echo.
    pause
    exit /b 1
)

echo       OK: %VCVARS%
call "%VCVARS%" >nul
if errorlevel 1 (
    echo [BLAD] vcvars64.bat zwrocil blad / returned error
    pause
    exit /b 1
)
echo       OK: Environment skonfigurowany / Environment configured
echo.

REM =============================================================================
REM  KROK 2: Zasoby / Resources
REM =============================================================================
echo [2/4] Kompilacja zasobow... / Compiling resources...

rc /nologo /fo Lab_view.res Lab_view.rc
if errorlevel 1 (
    echo.
    echo [BLAD] rc.exe zwrocil blad / rc.exe returned error
    pause
    exit /b 1
)
echo       OK: Lab_view.res
echo.

REM =============================================================================
REM  KROK 3: Kompilacja / Compile
REM =============================================================================
echo [3/4] Kompilacja Lab_view.cpp... / Compiling Lab_view.cpp...
echo.

cl /nologo /utf-8 /EHsc /O2 /MT /std:c++17 /DUNICODE /D_UNICODE Lab_view.cpp Lab_view.res /link /SUBSYSTEM:WINDOWS comctl32.lib shlwapi.lib ole32.lib oleaut32.lib windowscodecs.lib shell32.lib user32.lib gdi32.lib uuid.lib gdiplus.lib uxtheme.lib dwmapi.lib advapi32.lib /OUT:Lab_view.exe

if errorlevel 1 (
    echo.
    echo [BLAD] Kompilacja nieudana / Compilation failed!
    pause
    exit /b 1
)
echo.
echo       OK: Lab_view.exe skompilowany / compiled
echo.

REM =============================================================================
REM  KROK 4: Weryfikacja i czyszczenie / Verify and cleanup
REM =============================================================================
echo [4/4] Weryfikacja... / Verification...

if not exist "Lab_view.exe" (
    echo [BLAD] Lab_view.exe nie znaleziono!
    pause
    exit /b 1
)

set "SIZE=0"
for %%F in (Lab_view.exe) do set "SIZE=%%~zF"
echo       Rozmiar / Size: !SIZE! bajtow / bytes

del /Q Lab_view.res Lab_view.obj Lab_view.exp Lab_view.lib 2>nul

echo.
echo ================================================================================
echo  GOTOWE! / COMPLETE!   Lab_view.exe  (!SIZE! bytes)
echo ================================================================================
echo.
pause
endlocal
