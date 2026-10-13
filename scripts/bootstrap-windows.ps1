[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$taskProjectRoot = Split-Path -Parent $PSScriptRoot
$taskToolsRoot = Join-Path $taskProjectRoot '.tools'
$taskArchivesRoot = Join-Path $taskToolsRoot 'archives'
New-Item -ItemType Directory -Path $taskArchivesRoot -Force | Out-Null
$taskSevenZip = Get-Command 7z.exe -ErrorAction SilentlyContinue
if ($taskSevenZip) { $taskSevenZip = $taskSevenZip.Source }
elseif (Test-Path -LiteralPath 'C:\Program Files\7-Zip\7z.exe') { $taskSevenZip = 'C:\Program Files\7-Zip\7z.exe' }
else { throw '7-Zip est requis pour extraire les archives officielles Qt. Installez 7-Zip puis relancez ce script.' }
$taskCurl = Join-Path $env:SystemRoot 'System32\curl.exe'
if (-not (Test-Path -LiteralPath $taskCurl)) { throw 'curl.exe est requis.' }

function Install-TaskArchive {
    param([string]$Repository, [string]$Package, [string]$Version, [string]$Archive, [string]$CheckPath)
    if (Test-Path -LiteralPath (Join-Path $taskToolsRoot $CheckPath)) { return }
    $taskArchiveUrl = "$Repository/$Package/$Version$Archive"
    $taskArchivePath = Join-Path $taskArchivesRoot "$Version$Archive"
    $taskHashPath = "$taskArchivePath.sha1"
    Write-Output "Preparation : $Archive"
    & $taskCurl --fail --location --silent --show-error --retry 2 --output $taskHashPath "$taskArchiveUrl.sha1"
    if ($LASTEXITCODE -ne 0) { throw "Telechargement du checksum impossible : $taskArchiveUrl" }
    $taskExpectedHash = [regex]::Match((Get-Content -LiteralPath $taskHashPath -Raw), '[a-fA-F0-9]{40}').Value
    if (-not $taskExpectedHash) { throw "Checksum officiel invalide : $Archive" }
    if (-not (Test-Path -LiteralPath $taskArchivePath) -or (Get-FileHash -LiteralPath $taskArchivePath -Algorithm SHA1).Hash -ne $taskExpectedHash) {
        & $taskCurl --fail --location --silent --show-error --retry 2 --output $taskArchivePath $taskArchiveUrl
        if ($LASTEXITCODE -ne 0) { throw "Telechargement impossible : $taskArchiveUrl" }
    }
    if ((Get-FileHash -LiteralPath $taskArchivePath -Algorithm SHA1).Hash -ne $taskExpectedHash) { throw "Verification de l'archive echouee : $Archive" }
    & $taskSevenZip x $taskArchivePath "-o$taskToolsRoot" -y -bso0 -bsp0
    if ($LASTEXITCODE -ne 0) { throw "Extraction impossible : $Archive" }
}

Install-TaskArchive 'https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/tools_mingw1310' 'qt.tools.win64_mingw1310' '13.1.0-202407240918' 'mingw1310.7z' 'Tools\mingw1310_64\bin\g++.exe'
Install-TaskArchive 'https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/tools_cmake' 'qt.tools.cmake' '3.30.5-202502251255' 'windows64_x64_cmake_tools.7z' 'bin\cmake.exe'
Install-TaskArchive 'https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/tools_ninja' 'qt.tools.ninja' '1.12.1-202502251256' 'ninja_win64.7z' 'ninja.exe'
Install-TaskArchive 'https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6103/qt6_6103' 'qt.qt6.6103.win64_mingw' '6.10.3-0-202603310407' 'qtbase-Windows-Windows_11_24H2-Mingw-Windows-Windows_11_24H2-X86_64.7z' 'bin\Qt6Widgets.dll'
$taskFfmpegVersion = 'ffmpeg-n9.0.2-24-gfd5d616c29-win64-lgpl-shared-9.0'
$taskFfmpegCheck = Join-Path $taskToolsRoot "ffmpeg\$taskFfmpegVersion\include\libavcodec\avcodec.h"
if (-not (Test-Path -LiteralPath $taskFfmpegCheck)) {
    $taskFfmpegArchive = Join-Path $taskArchivesRoot 'ffmpeg-lgpl-shared.zip'
    $taskFfmpegHash = '1b5ebbf0a06def4602c8ac0e98f9987689a1bcfb56fc77dbeb33e38185ec97c6'
    if (-not (Test-Path -LiteralPath $taskFfmpegArchive) -or (Get-FileHash -LiteralPath $taskFfmpegArchive -Algorithm SHA256).Hash -ne $taskFfmpegHash) {
        & $taskCurl --fail --location --silent --show-error --retry 2 --output $taskFfmpegArchive "https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-10-09-14-16/$taskFfmpegVersion.zip"
        if ($LASTEXITCODE -ne 0) { throw 'Téléchargement FFmpeg impossible.' }
    }
    if ((Get-FileHash -LiteralPath $taskFfmpegArchive -Algorithm SHA256).Hash -ne $taskFfmpegHash) { throw 'Vérification SHA-256 FFmpeg échouée.' }
    & $taskSevenZip x $taskFfmpegArchive ('-o' + (Join-Path $taskToolsRoot 'ffmpeg')) -y -bso0 -bsp0
    if ($LASTEXITCODE -ne 0) { throw 'Extraction FFmpeg impossible.' }
}
Write-Output 'Outils prepares dans .tools ; aucun PATH systeme modifie.'
