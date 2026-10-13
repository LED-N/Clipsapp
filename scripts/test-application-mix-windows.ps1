[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskBuild = Join-Path $taskRoot 'build\windows'
$taskProbe = Join-Path $taskBuild 'clipsapp_application_mix_probe.exe'
if (-not (Test-Path -LiteralPath $taskProbe)) { throw 'Build the application and its manual probes first.' }
$taskOutput = Join-Path $taskRoot 'build\probes\application-mix'
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskGame = Join-Path $taskOutput 'Game-tone.exe'
$taskSeparate = Join-Path $taskOutput 'Separate-tone.exe'
Copy-Item -LiteralPath $taskProbe -Destination $taskGame -Force
Copy-Item -LiteralPath $taskProbe -Destination $taskSeparate -Force
$taskReady = Join-Path $taskOutput 'ready'
if (Test-Path -LiteralPath $taskReady) { Remove-Item -LiteralPath $taskReady -Force }
$taskPreviousPath = $env:PATH
$taskProcesses = @()
try {
    $env:PATH = "$taskBuild;$taskRoot\.tools\bin;$taskRoot\.tools\Tools\mingw1310_64\bin;$taskPreviousPath"
    $taskArguments = @('--excluded', ('"{0}"' -f $taskSeparate), '--output-dir', ('"{0}"' -f $taskOutput))
    $taskRecorder = Start-Process -FilePath $taskProbe -ArgumentList $taskArguments -WindowStyle Hidden -PassThru
    $taskProcesses += $taskRecorder
    $taskTimeout = [System.Diagnostics.Stopwatch]::StartNew()
    while (-not (Test-Path -LiteralPath $taskReady) -and -not $taskRecorder.HasExited -and $taskTimeout.ElapsedMilliseconds -lt 6000) { Start-Sleep -Milliseconds 50 }
    if (-not (Test-Path -LiteralPath $taskReady)) { throw 'Automatic audio initialization failed; inspect the numeric report.' }
    $taskTimeline = [System.Diagnostics.Stopwatch]::StartNew()
    $taskProcesses += Start-Process -FilePath $taskSeparate -ArgumentList '--tone', '550', '--duration', '7900' -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 600
    $taskFirstGame = Start-Process -FilePath $taskGame -ArgumentList '--tone', '330', '--duration', '2900' -WindowStyle Hidden -PassThru
    $taskProcesses += $taskFirstGame
    $taskFirstGame.WaitForExit()
    if ($taskFirstGame.ExitCode -ne 0) { throw 'First generated game tone failed.' }
    while ($taskTimeline.ElapsedMilliseconds -lt 4300) { Start-Sleep -Milliseconds 50 }
    $taskProcesses += Start-Process -FilePath $taskGame -ArgumentList '--tone', '330', '--duration', '3500' -WindowStyle Hidden -PassThru
    foreach ($taskProcess in $taskProcesses) {
        if (-not $taskProcess.WaitForExit(15000)) { throw 'Generated audio check timed out.' }
        if ($taskProcess.ExitCode -ne 0) { throw 'Audio probe failed; inspect build/probes/application-mix/report.json.' }
    }
    Get-Content -LiteralPath (Join-Path $taskOutput 'report.json') -Encoding utf8
} finally {
    foreach ($taskProcess in $taskProcesses) { if (-not $taskProcess.HasExited) { $taskProcess.Kill(); $taskProcess.WaitForExit(3000) | Out-Null } }
    $env:PATH = $taskPreviousPath
}
