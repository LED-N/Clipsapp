# Composants tiers

Clipsapp utilise les bibliothèques partagées Qt 6.10.3 (Core, Gui, Widgets, Network), FFmpeg, les plugins Qt nécessaires à l'interface et à la connexion sécurisée de Windows, et les runtimes MinGW/GCC fournis avec les outils officiels Qt. Les bibliothèques partagées peuvent être remplacées par des versions compatibles ; les instructions de reconstruction de l'application sont dans le README du projet.

- **Qt** : copyright The Qt Company et contributeurs. Licences et attributions : <https://doc.qt.io/qt-6/licensing.html>. Sources correspondantes : <https://download.qt.io/archive/qt/6.10/6.10.3/submodules/>. Les textes LGPLv3 et GPLv3 sont fournis dans `third_party/licenses` (dépôt) et `runtime/licenses` (version portable).
- **GCC / libstdc++ / libgcc** : Free Software Foundation et contributeurs. Sources GCC 13.1 : <https://gcc.gnu.org/releases.html>. Le texte de l'exception pour les runtimes est fourni dans `GCC-Runtime-Exception.txt`, aux mêmes emplacements.
- **MinGW-w64 / winpthreads** : contributeurs MinGW-w64. Sources et attributions : <https://www.mingw-w64.org/> et <https://github.com/mingw-w64/mingw-w64>. Le texte fourni pour winpthreads est dans `Winpthreads-COPYING.txt`, aux mêmes emplacements.

La version portable contient également le manifeste SPDX de Qt, avec l'inventaire des composants, leurs attributions et les informations de licence fournies par Qt. Ce manifeste couvre le SDK Qtbase et peut citer des composants qui ne sont pas présents dans l'exécutable distribué.

**FFmpeg** : copyright les contributeurs FFmpeg et ceux des bibliothèques embarquées. Distribution LGPLv3 partagée BtbN `ffmpeg-n9.0.2-24-gfd5d616c29-win64-lgpl-shared-9.0`, release `autobuild-2026-10-09-14-16`. SHA-256 de l'archive : `1b5ebbf0a06def4602c8ac0e98f9987689a1bcfb56fc77dbeb33e38185ec97c6`. Les bibliothèques avcodec, avformat, avutil, swresample et swscale sont distribuées pour la capture et la lecture. L'exécutable `ffmpeg.exe`, avfilter et avdevice sont aussi distribués pour l'export dans un processus distinct. Aucun moteur OBS n'est embarqué.

- Sources FFmpeg correspondant au build : [commit fd5d616c29](https://github.com/FFmpeg/FFmpeg/tree/fd5d616c29).
- Archive binaire, sommes de contrôle et sources de construction : [release BtbN](https://github.com/BtbN/FFmpeg-Builds/releases/tag/autobuild-2026-10-09-14-16), [scripts et recettes BtbN](https://github.com/BtbN/FFmpeg-Builds).
- Licence : [FFmpeg Legal](https://ffmpeg.org/legal.html). La version portable contient `runtime/licenses/FFmpeg-LICENSE.txt` (LGPLv3), les textes GPLv3/LGPLv3 Qt et `runtime/licenses/FFmpeg-buildconf.txt` décrivant les options et bibliothèques du build. Le SDK complet peut être préparé avec le script bootstrap et son emplacement peut être changé via `CLIPSAPP_FFMPEG_ROOT` dans CMake.

Les déclarations minimales des interfaces Windows Graphics Capture suivent l'ABI publiée dans [Microsoft win32metadata](https://github.com/microsoft/win32metadata/tree/main/generation/WinSDK/RecompiledIdlHeaders). Les composants système Windows sont utilisés depuis l'installation de Windows.

