[CmdletBinding()]
param([switch]$Test, [switch]$Package)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskCmake = Join-Path $taskRoot '.tools\bin\cmake.exe'
$taskCompilerBin = Join-Path $taskRoot '.tools\Tools\mingw1310_64\bin'
$taskQtBin = Join-Path $taskRoot '.tools\bin'
$taskFfmpegRoot = Join-Path $taskRoot '.tools\ffmpeg\ffmpeg-n9.0.2-24-gfd5d616c29-win64-lgpl-shared-9.0'
if (-not (Test-Path -LiteralPath $taskCmake)) { throw 'Lancez scripts/bootstrap-windows.ps1 pour préparer les outils locaux.' }
$taskPreviousPath = $env:PATH
$taskPreviousQpa = $env:QT_QPA_PLATFORM
Push-Location -LiteralPath $taskRoot
try {
    $env:PATH = "$taskCompilerBin;$taskQtBin;$taskFfmpegRoot\bin;$taskPreviousPath"
    & $taskCmake --preset windows-local
    if ($LASTEXITCODE -ne 0) { throw 'Configuration CMake échouée.' }
    & $taskCmake --build --preset windows-local --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Compilation échouée.' }
    if ($Test) {
        & (Join-Path $taskQtBin 'ctest.exe') --preset windows-local
        if ($LASTEXITCODE -ne 0) { throw 'Tests du moteur échoués.' }
        $env:QT_QPA_PLATFORM = 'windows'
        # A fresh folder each time: the first-launch page is part of what is checked.
        $taskSmokeRoot = Join-Path $taskRoot 'build\windows\smoke'
        if (Test-Path -LiteralPath $taskSmokeRoot) { Remove-Item -LiteralPath $taskSmokeRoot -Recurse -Force }
        $taskDataDirectory = Join-Path $taskSmokeRoot 'config'
        $taskPreviewDirectory = Join-Path $taskSmokeRoot 'previews'
        $taskArguments = @('--smoke-test', '--data-dir', ('"{0}"' -f $taskDataDirectory), '--previews-dir', ('"{0}"' -f $taskPreviewDirectory))
        $taskSmokeProcess = Start-Process -FilePath (Join-Path $taskRoot 'build\windows\clipsapp.exe') -ArgumentList $taskArguments -WindowStyle Hidden -Wait -PassThru
        if ($taskSmokeProcess.ExitCode -ne 0) { throw 'Vérification du démarrage échouée.' }
        $taskReportPath = Join-Path $taskDataDirectory 'smoke-report.json'
        if (-not (Test-Path -LiteralPath $taskReportPath) -or -not (Get-Content -LiteralPath $taskReportPath -Raw -Encoding utf8 | ConvertFrom-Json).passed) { throw 'Rapport de démarrage absent ou invalide.' }
        Get-Content -LiteralPath $taskReportPath -Encoding utf8
    }
    if ($Package) {
        $taskDistribution = Join-Path $taskRoot 'dist\Clipsapp'
        # The package is rebuilt from nothing: what it holds is exactly what this run put in it.
        if (Test-Path -LiteralPath $taskDistribution) {
            if (-not [System.IO.Path]::GetFullPath($taskDistribution).StartsWith([System.IO.Path]::GetFullPath((Join-Path $taskRoot 'dist')) + '\', [System.StringComparison]::OrdinalIgnoreCase)) { throw 'Distribution directory outside the project.' }
            Remove-Item -LiteralPath $taskDistribution -Recurse -Force
        }
        New-Item -ItemType Directory -Path $taskDistribution -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $taskRoot 'build\windows\clipsapp.exe') -Destination (Join-Path $taskDistribution 'Clipsapp.exe') -Force
        foreach ($taskMediaPattern in @('avcodec-*.dll', 'avformat-*.dll', 'avutil-*.dll', 'swresample-*.dll', 'swscale-*.dll', 'avfilter-*.dll', 'avdevice-*.dll')) {
            Get-ChildItem -LiteralPath (Join-Path $taskFfmpegRoot 'bin') -Filter $taskMediaPattern | Copy-Item -Destination $taskDistribution -Force
        }
        Copy-Item -LiteralPath (Join-Path $taskFfmpegRoot 'bin\ffmpeg.exe') -Destination $taskDistribution -Force
        & (Join-Path $taskQtBin 'windeployqt.exe') --release --compiler-runtime --no-translations --skip-plugin-types generic,networkinformation --no-system-d3d-compiler --no-system-dxc-compiler (Join-Path $taskDistribution 'Clipsapp.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Préparation de la version portable échouée.' }
        # The update check talks to GitHub through Windows' own TLS (Schannel): the other backends are not shipped.
        $taskDistributionPrefix = [System.IO.Path]::GetFullPath($taskDistribution).TrimEnd('\') + '\'
        foreach ($taskObsoleteName in @('generic', 'networkinformation', 'tls\qopensslbackend.dll', 'tls\qcertonlybackend.dll', 'D3Dcompiler_47.dll')) {
            $taskObsoletePath = Join-Path $taskDistribution $taskObsoleteName
            if (Test-Path -LiteralPath $taskObsoletePath) {
                $taskResolvedObsolete = (Resolve-Path -LiteralPath $taskObsoletePath).Path
                if (-not $taskResolvedObsolete.StartsWith($taskDistributionPrefix, [System.StringComparison]::OrdinalIgnoreCase)) { throw 'Generated dependency outside distribution directory.' }
                Remove-Item -LiteralPath $taskResolvedObsolete -Recurse -Force
            }
        }
        if (-not (Test-Path -LiteralPath (Join-Path $taskDistribution 'tls\qschannelbackend.dll'))) { throw 'Le module TLS de Windows est absent du paquet : la recherche de mises à jour ne fonctionnerait pas.' }
        foreach ($taskDocument in @('README.md', 'CHANGELOG.md', 'LICENSE', 'THIRD_PARTY.md')) {
            if (Test-Path -LiteralPath (Join-Path $taskRoot $taskDocument)) { Copy-Item -LiteralPath (Join-Path $taskRoot $taskDocument) -Destination $taskDistribution -Force }
        }
        $taskNoticesDirectory = Join-Path $taskDistribution 'third_party'
        New-Item -ItemType Directory -Path $taskNoticesDirectory -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $taskRoot 'third_party\licenses') -Destination $taskNoticesDirectory -Recurse -Force
        Copy-Item -LiteralPath (Join-Path $taskRoot '.tools\sbom\qtbase-6.10.3.spdx.json') -Destination $taskNoticesDirectory -Force
        Copy-Item -LiteralPath (Join-Path $taskFfmpegRoot 'LICENSE.txt') -Destination (Join-Path $taskNoticesDirectory 'FFmpeg-LICENSE.txt') -Force
        $taskBuildconfProcess = Start-Process -FilePath (Join-Path $taskFfmpegRoot 'bin\ffmpeg.exe') -ArgumentList '-buildconf' -RedirectStandardError (Join-Path $taskNoticesDirectory 'FFmpeg-buildconf.txt') -RedirectStandardOutput (Join-Path $taskRoot 'build\windows\ffmpeg-buildconf-stdout.txt') -WindowStyle Hidden -Wait -PassThru
        if ($taskBuildconfProcess.ExitCode -ne 0) { throw 'Inventaire FFmpeg impossible.' }
        # The list of the package's files. It marks a published copy, which alone looks for updates,
        # and lets an update remove the files a later version no longer ships.
        $taskManifestPath = Join-Path $taskDistribution 'manifest.txt'
        $taskManifest = @(Get-ChildItem -LiteralPath $taskDistribution -Recurse -File | ForEach-Object { $_.FullName.Substring($taskDistributionPrefix.Length).Replace('\', '/') }) + 'manifest.txt' | Sort-Object -Unique
        [System.IO.File]::WriteAllLines($taskManifestPath, [string[]]$taskManifest, (New-Object System.Text.UTF8Encoding $false))
        Write-Output "Version portable : $taskDistribution\Clipsapp.exe"
    }
} finally {
    $env:PATH = $taskPreviousPath
    $env:QT_QPA_PLATFORM = $taskPreviousQpa
    Pop-Location
}
