@echo off
chcp 65001 >nul
setlocal
echo Installation de Mahali...
echo.

if not exist "C:\mahali" mkdir "C:\mahali"
echo Copie des fichiers...
xcopy "%~dp0*" "C:\mahali\" /E /I /H /K /Y >nul

echo Création du raccourci...
powershell -ExecutionPolicy Bypass -File "%~dp0CreateShortcut.ps1"

echo.
echo Installation terminée.
echo Lancement de Mahali...
start "" "C:\mahali\mahali-desktop.exe"
echo.
echo Vous pouvez fermer cette fenêtre.
pause >nul
