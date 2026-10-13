[CmdletBinding()]
param(
    # Sends the release to GitHub once the archive is ready. Without it, the script stops
    # at the archive and says what is left to do by hand.
    [switch]$Publish
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskProject = Get-Content -LiteralPath (Join-Path $taskRoot 'CMakeLists.txt') -Raw -Encoding utf8
$taskVersion = [regex]::Match($taskProject, 'project\(Clipsapp VERSION ([0-9]+(\.[0-9]+){1,2})').Groups[1].Value
if (-not $taskVersion) { throw 'Version introuvable dans CMakeLists.txt.' }
$taskRepository = [regex]::Match($taskProject, 'set\(CLIPSAPP_UPDATE_REPOSITORY "([^"]*)"\)').Groups[1].Value
$taskTag = "v$taskVersion"

# The notes of the release are the section of CHANGELOG.md that carries its number.
$taskChangelog = Join-Path $taskRoot 'CHANGELOG.md'
if (-not (Test-Path -LiteralPath $taskChangelog)) { throw 'CHANGELOG.md est absent.' }
$taskSection = [regex]::Match((Get-Content -LiteralPath $taskChangelog -Raw -Encoding utf8), "(?ms)^## $([regex]::Escape($taskVersion))[^\r\n]*\r?\n(.*?)(?=^## |\z)")
if (-not $taskSection.Success -or -not $taskSection.Groups[1].Value.Trim()) { throw "Ajoutez une section « ## $taskVersion » à CHANGELOG.md : c'est ce que vos amis liront avant de mettre à jour." }

if ($Publish) {
    foreach ($taskTool in @('git', 'gh')) { if (-not (Get-Command $taskTool -ErrorAction SilentlyContinue)) { throw "$taskTool est requis pour publier." } }
    if (-not $taskRepository) { throw 'CLIPSAPP_UPDATE_REPOSITORY est vide dans CMakeLists.txt : cette version ne saurait pas où chercher ses mises à jour.' }
    Push-Location -LiteralPath $taskRoot
    try {
        if (git status --porcelain) { throw 'Des modifications ne sont pas enregistrées dans Git. Faites un commit, puis relancez.' }
        if (git tag --list $taskTag) { throw "La version $taskVersion a déjà été publiée. Augmentez le numéro dans CMakeLists.txt." }
    } finally { Pop-Location }
}

& (Join-Path $PSScriptRoot 'build-windows.ps1') -Test -Package
$taskDistribution = Join-Path $taskRoot 'dist\Clipsapp'
if (-not (Test-Path -LiteralPath (Join-Path $taskDistribution 'manifest.txt'))) { throw 'Le paquet est incomplet.' }

# One archive whose name never changes, holding one folder named Clipsapp: the folder a user keeps
# carries no version number, since the application updates itself in place. Windows' own tar writes the zip.
$taskArchive = Join-Path $taskRoot 'dist\Clipsapp-windows-x64.zip'
if (Test-Path -LiteralPath $taskArchive) { Remove-Item -LiteralPath $taskArchive -Force }
$taskTar = Join-Path $env:SystemRoot 'System32\tar.exe'
& $taskTar -a -c -f $taskArchive -C (Join-Path $taskRoot 'dist') 'Clipsapp'
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $taskArchive)) { throw "Création de l'archive impossible." }
if (-not ((& $taskTar -t -f $taskArchive) -contains 'Clipsapp/Clipsapp.exe')) { throw "L'archive ne contient pas le dossier Clipsapp et son exécutable." }
$taskNotes = Join-Path $taskRoot "dist\notes-$taskVersion.md"
[System.IO.File]::WriteAllText($taskNotes, $taskSection.Groups[1].Value.Trim() + "`n", (New-Object System.Text.UTF8Encoding $false))
$taskSize = [math]::Round((Get-Item -LiteralPath $taskArchive).Length / 1MB, 1)
Write-Output ''
Write-Output "Archive  : $taskArchive ($taskSize Mo)"
Write-Output "SHA-256  : $((Get-FileHash -LiteralPath $taskArchive -Algorithm SHA256).Hash.ToLower())"
Write-Output "Notes    : $taskNotes"
if (-not $taskRepository) { Write-Warning 'CLIPSAPP_UPDATE_REPOSITORY est vide dans CMakeLists.txt : cette version ne cherchera pas de mises à jour.' }

if (-not $Publish) {
    Write-Output ''
    Write-Output "Pour publier : relancez avec -Publish, ou créez la release $taskTag sur GitHub et joignez-y l'archive."
    return
}
Push-Location -LiteralPath $taskRoot
try {
    git push --set-upstream origin HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Envoi du code vers GitHub impossible.' }
    # The tag is created by GitHub on the commit that was just pushed, with the archive attached.
    gh release create $taskTag $taskArchive --title "Clipsapp $taskVersion" --notes-file $taskNotes
    if ($LASTEXITCODE -ne 0) { throw 'Création de la release impossible.' }
    git fetch --tags --quiet
} finally { Pop-Location }
Write-Output ''
Write-Output "Version $taskVersion publiée. Les copies de Clipsapp déjà installées la proposeront à leur prochain lancement."
