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
        # The folder holds the executable, the uninstall script, and "runtime" with everything else.
        $taskRuntime = Join-Path $taskDistribution 'runtime'
        New-Item -ItemType Directory -Path $taskRuntime -Force | Out-Null
        # windeployqt gathers the libraries beside the executable it is given: the executable waits in
        # "runtime" for that, then takes its place at the root.
        Copy-Item -LiteralPath (Join-Path $taskRoot 'build\windows\clipsapp.exe') -Destination (Join-Path $taskRuntime 'Clipsapp.exe') -Force
        foreach ($taskMediaPattern in @('avcodec-*.dll', 'avformat-*.dll', 'avutil-*.dll', 'swresample-*.dll', 'swscale-*.dll', 'avfilter-*.dll', 'avdevice-*.dll')) {
            Get-ChildItem -LiteralPath (Join-Path $taskFfmpegRoot 'bin') -Filter $taskMediaPattern | Copy-Item -Destination $taskRuntime -Force
        }
        Copy-Item -LiteralPath (Join-Path $taskFfmpegRoot 'bin\ffmpeg.exe') -Destination $taskRuntime -Force
        & (Join-Path $taskQtBin 'windeployqt.exe') --release --compiler-runtime --no-translations --skip-plugin-types generic,networkinformation --no-system-d3d-compiler --no-system-dxc-compiler (Join-Path $taskRuntime 'Clipsapp.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Préparation de la version portable échouée.' }
        $taskExecutable = Join-Path $taskDistribution 'Clipsapp.exe'
        Move-Item -LiteralPath (Join-Path $taskRuntime 'Clipsapp.exe') -Destination $taskExecutable
        # The update check talks to GitHub through Windows' own TLS (Schannel): the other backends are not shipped.
        $taskDistributionPrefix = [System.IO.Path]::GetFullPath($taskDistribution).TrimEnd('\') + '\'
        foreach ($taskObsoleteName in @('generic', 'networkinformation', 'tls\qopensslbackend.dll', 'tls\qcertonlybackend.dll', 'D3Dcompiler_47.dll')) {
            $taskObsoletePath = Join-Path $taskRuntime $taskObsoleteName
            if (Test-Path -LiteralPath $taskObsoletePath) {
                $taskResolvedObsolete = (Resolve-Path -LiteralPath $taskObsoletePath).Path
                if (-not $taskResolvedObsolete.StartsWith($taskDistributionPrefix, [System.StringComparison]::OrdinalIgnoreCase)) { throw 'Generated dependency outside distribution directory.' }
                Remove-Item -LiteralPath $taskResolvedObsolete -Recurse -Force
            }
        }
        if (-not (Test-Path -LiteralPath (Join-Path $taskRuntime 'tls\qschannelbackend.dll'))) { throw 'Le module TLS de Windows est absent du paquet : la recherche de mises à jour ne fonctionnerait pas.' }
        # Windows looks for the libraries of an executable beside it. Declared as a private assembly in
        # the manifest of the executable, they are found in "runtime" instead; the plugins of Qt are
        # found there by the application itself (src/main.cpp).
        $taskLibraries = @(Get-ChildItem -LiteralPath $taskRuntime -Filter '*.dll' -File | Sort-Object Name | ForEach-Object { "  <file name=`"$($_.Name)`"/>" })
        $taskAssembly = '<assemblyIdentity type="win32" name="runtime" version="1.0.0.0" processorArchitecture="amd64"/>'
        [System.IO.File]::WriteAllText((Join-Path $taskRuntime 'runtime.manifest'), (@('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>', '<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">', "  $taskAssembly") + $taskLibraries + '</assembly>' -join "`r`n") + "`r`n", (New-Object System.Text.UTF8Encoding $false))
        $taskExecutableManifest = @('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>', '<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">',
            '  <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3"><security><requestedPrivileges><requestedExecutionLevel level="asInvoker" uiAccess="false"/></requestedPrivileges></security></trustInfo>',
            "  <dependency><dependentAssembly>$taskAssembly</dependentAssembly></dependency>", '</assembly>') -join "`r`n"
        # Only the published executable carries it: the one in build\windows runs beside its libraries.
        if (-not ('ClipsappManifest' -as [type])) {
            Add-Type -TypeDefinition @'
using System; using System.ComponentModel; using System.Runtime.InteropServices;
public static class ClipsappManifest {
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern IntPtr BeginUpdateResource(string file, bool deleteExisting);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool UpdateResource(IntPtr update, IntPtr type, IntPtr name, ushort language, byte[] data, uint size);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool EndUpdateResource(IntPtr update, bool discard);
    // RT_MANIFEST, identifier 1: the manifest Windows reads when it starts the executable.
    public static void Embed(string file, byte[] manifest) {
        IntPtr update = BeginUpdateResource(file, false); if (update == IntPtr.Zero) throw new Win32Exception();
        if (!UpdateResource(update, (IntPtr)24, (IntPtr)1, 0, manifest, (uint)manifest.Length)) { int error = Marshal.GetLastWin32Error(); EndUpdateResource(update, true); throw new Win32Exception(error); }
        if (!EndUpdateResource(update, false)) throw new Win32Exception();
    }
}
'@
        }
        [ClipsappManifest]::Embed([System.IO.Path]::GetFullPath($taskExecutable), [System.Text.Encoding]::UTF8.GetBytes($taskExecutableManifest))
        # What uninstalls this folder ships inside it: it also removes what the application left outside.
        # Written in UTF-8, it is shipped in the code page of the console, where cmd reads it right.
        $taskUninstall = [System.IO.File]::ReadAllText((Join-Path $taskRoot 'resources\Desinstaller Clipsapp.bat'), [System.Text.Encoding]::UTF8) -replace "`r?`n", "`r`n"
        [System.IO.File]::WriteAllText((Join-Path $taskDistribution 'Desinstaller Clipsapp.bat'), $taskUninstall, [System.Text.Encoding]::GetEncoding(850))
        # The licenses go with the libraries they cover. README and CHANGELOG stay on the project's page:
        # the application shows the notes of a new version from GitHub, not from its folder.
        Copy-Item -LiteralPath (Join-Path $taskRoot 'THIRD_PARTY.md') -Destination $taskRuntime -Force
        $taskNoticesDirectory = Join-Path $taskRuntime 'licenses'
        New-Item -ItemType Directory -Path $taskNoticesDirectory -Force | Out-Null
        Get-ChildItem -LiteralPath (Join-Path $taskRoot 'third_party\licenses') -File | Copy-Item -Destination $taskNoticesDirectory -Force
        Copy-Item -LiteralPath (Join-Path $taskRoot '.tools\sbom\qtbase-6.10.3.spdx.json') -Destination $taskNoticesDirectory -Force
        Copy-Item -LiteralPath (Join-Path $taskFfmpegRoot 'LICENSE.txt') -Destination (Join-Path $taskNoticesDirectory 'FFmpeg-LICENSE.txt') -Force
        $taskBuildconfProcess = Start-Process -FilePath (Join-Path $taskFfmpegRoot 'bin\ffmpeg.exe') -ArgumentList '-buildconf' -RedirectStandardError (Join-Path $taskNoticesDirectory 'FFmpeg-buildconf.txt') -RedirectStandardOutput (Join-Path $taskRoot 'build\windows\ffmpeg-buildconf-stdout.txt') -WindowStyle Hidden -Wait -PassThru
        if ($taskBuildconfProcess.ExitCode -ne 0) { throw 'Inventaire FFmpeg impossible.' }
        # The list of the package's files. It marks a published copy, which alone looks for updates,
        # and lets an update remove the files a later version no longer ships.
        $taskManifestPath = Join-Path $taskRuntime 'manifest.txt'
        $taskManifest = @(Get-ChildItem -LiteralPath $taskDistribution -Recurse -File | ForEach-Object { $_.FullName.Substring($taskDistributionPrefix.Length).Replace('\', '/') }) + 'runtime/manifest.txt' | Sort-Object -Unique
        [System.IO.File]::WriteAllLines($taskManifestPath, [string[]]$taskManifest, (New-Object System.Text.UTF8Encoding $false))
        $taskRootEntries = @(Get-ChildItem -LiteralPath $taskDistribution -Force | ForEach-Object Name | Sort-Object)
        if (($taskRootEntries -join '|') -ne (@('Clipsapp.exe', 'Desinstaller Clipsapp.bat', 'runtime') -join '|')) { throw "La racine du paquet contient autre chose que prévu : $($taskRootEntries -join ', ')" }
        # Started as a user would: from its folder alone, none of the tools of the build on the path.
        $taskPackageCheck = Join-Path $taskRoot 'build\windows\package-check'
        if (Test-Path -LiteralPath $taskPackageCheck) { Remove-Item -LiteralPath $taskPackageCheck -Recurse -Force }
        $taskBuildPath = $env:PATH; $taskBuildQpa = $env:QT_QPA_PLATFORM
        try {
            $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\Wbem"; $env:QT_QPA_PLATFORM = 'windows'
            $taskPackageProcess = Start-Process -FilePath $taskExecutable -ArgumentList @('--smoke-test', '--data-dir', ('"{0}"' -f $taskPackageCheck)) -WindowStyle Hidden -Wait -PassThru
        } finally { $env:PATH = $taskBuildPath; $env:QT_QPA_PLATFORM = $taskBuildQpa }
        $taskPackageReport = Join-Path $taskPackageCheck 'smoke-report.json'
        if ($taskPackageProcess.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $taskPackageReport) -or -not (Get-Content -LiteralPath $taskPackageReport -Raw -Encoding utf8 | ConvertFrom-Json).passed) { throw "La version portable ne démarre pas seule (code $($taskPackageProcess.ExitCode))." }
        Write-Output "Version portable : $taskExecutable (démarrage vérifié hors des outils de compilation)"
    }
} finally {
    $env:PATH = $taskPreviousPath
    $env:QT_QPA_PLATFORM = $taskPreviousQpa
    Pop-Location
}
