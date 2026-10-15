@echo off
rem Desinstalle la copie de Clipsapp qui se trouve dans ce dossier. Les clips ne sont jamais touches.
rem Clipsapp.exe --uninstall ferme l'application et retire ce qu'elle laisse hors de son dossier ;
rem ce script pose les questions, lit le resultat, puis supprime le dossier.
rem Ecrit ici en UTF-8, ce fichier est converti dans la page de codes 850 de la console quand le
rem paquet est prepare (scripts/build-windows.ps1) : en UTF-8, cmd se perd dans les fichiers qui
rem ont des accents et des "call", et la console ne lit plus ce qui est tape.
setlocal EnableExtensions DisableDelayedExpansion
title Désinstaller Clipsapp
set "APP=%~dp0"
set "APP=%APP:~0,-1%"
if not exist "%APP%\Clipsapp.exe" goto misplaced

echo.
echo  Désinstaller Clipsapp
echo  ---------------------
echo  Clipsapp va être fermé, puis retiré de ce PC :
echo    - son dossier : "%APP%"
echo    - son raccourci du menu Démarrer et son lancement avec Windows, s'il en a.
echo  Vos clips ne sont pas touchés.
echo.
rem La reponse se tape, puis Entree.
set "QUESTION= Continuer ? [O/N] "
call :ask
if not "%ANSWER%"=="O" goto cancelled

echo.
echo  Vos réglages (profils, sources audio, classement et favoris des clips) peuvent
echo  être gardés pour une prochaine installation de Clipsapp.
set "QUESTION= Les garder ? [O/N] "
call :ask
if "%ANSWER%"=="N" (
    set "KEEP="
    set "SETTINGS=Vos réglages ont été supprimés."
) else if "%ANSWER%"=="O" (
    set "KEEP=--keep-settings"
    set "SETTINGS=Vos réglages restent dans %LOCALAPPDATA%\Clipsapp, pour une prochaine installation."
) else goto cancelled

echo.
echo  Fermeture de Clipsapp et retrait de ses raccourcis...
start "" /wait "%APP%\Clipsapp.exe" --uninstall %KEEP%
set "CODE=%errorlevel%"
if "%CODE%"=="2" goto running
if "%CODE%"=="3" goto clips
if not "%CODE%"=="0" goto incomplete

echo  Suppression du dossier de Clipsapp...
rem Le dossier ne peut pas etre supprime tant que la console s'y trouve, ni tant que ce script
rem est en cours : "(goto)" termine le script, la suite de la ligne s'execute quand meme.
cd /d "%TEMP%"
(goto) 2>nul & rmdir /s /q "%APP%" & if exist "%APP%\" (echo. & echo  Certains fichiers n'ont pas pu être supprimés. Supprimez ce dossier à la main : & echo  "%APP%") else (echo. & echo  Clipsapp est désinstallé. %SETTINGS%) & echo. & pause

:running
echo.
echo  Clipsapp est encore ouvert. Il attend peut-être une réponse : des réglages non
echo  enregistrés, ou des copies Discord en cours. Répondez-y ou quittez-le (clic droit
echo  sur son icône près de l'horloge, puis Quitter), puis relancez ce script.
echo  Rien n'a été supprimé.
goto end

:clips
echo.
echo  Vos clips sont rangés dans le dossier de Clipsapp : le supprimer les effacerait.
echo  Choisissez un autre dossier des clips dans Réglages, Capture, déplacez-y
echo  vos clips, puis relancez ce script. Rien n'a été supprimé.
goto end

:incomplete
echo.
echo  Clipsapp n'a pas pu retirer toutes ses traces (code %CODE%) : son dossier est conservé.
echo  Relancez ce script ; si le problème revient, signalez-le dans l'onglet Issues du
echo  dépôt GitHub de Clipsapp.
goto end

:misplaced
echo.
echo  Ce script doit rester dans le dossier de Clipsapp, à côté de Clipsapp.exe.
goto end

rem Pose QUESTION jusqu'a une reponse O ou N, trois fois au plus ; sans reponse, ANSWER reste vide.
:ask
set "ANSWER="
for /l %%i in (1,1,3) do if not defined ANSWER call :askonce
goto :eof
:askonce
set "TYPED="
set /p "TYPED=%QUESTION%"
if not defined TYPED goto :eof
set "TYPED=%TYPED:~0,1%"
if /i "%TYPED%"=="O" set "ANSWER=O"
if /i "%TYPED%"=="N" set "ANSWER=N"
goto :eof

:cancelled
echo.
echo  Rien n'a été modifié.

:end
echo.
pause
exit /b 1
