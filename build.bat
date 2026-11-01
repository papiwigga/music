@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul || (echo No se encuentra CMake. Abre "Developer Command Prompt for VS" desde el menu Inicio, ve a esta carpeta y ejecuta build.bat desde alli. & pause & exit /b 1)
where git >nul 2>nul || (echo Falta Git. Instalalo desde git-scm.com y vuelve a ejecutar este archivo. & pause & exit /b 1)

echo.
echo === 1/3 Preparando el proyecto (la primera vez descarga JUCE) ===
cmake -B build -A x64 || goto :error

echo.
echo === 2/3 Compilando el plugin VST3 (puede tardar 5-10 minutos) ===
cmake --build build --config Release --target AfinaVST_VST3 || goto :error

echo.
echo === 3/3 Compilando la version independiente ===
cmake --build build --config Release --target AfinaVST_Standalone || goto :error

if not exist dist mkdir dist
xcopy /E /I /Y "build\AfinaVST_artefacts\Release\VST3\AfinaVST.vst3" "dist\AfinaVST.vst3\" >nul
copy /Y "build\AfinaVST_artefacts\Release\Standalone\AfinaVST.exe" "dist\" >nul

echo.
echo LISTO. Se abre la carpeta "dist":
echo  - Copia la carpeta AfinaVST.vst3 a C:\Program Files\Common Files\VST3\
echo  - AfinaVST.exe es la version independiente para probar.
start "" "dist"
pause
exit /b 0

:error
echo.
echo HA FALLADO. Haz una captura o copia el ultimo mensaje de error y enviamelo.
pause
exit /b 1
