<p align="center">
  <img src=".github/banner.png" alt="Clipsapp — vos meilleurs moments, enregistrés et rangés sur votre PC" width="100%">
</p>

# Clipsapp

Clipsapp garde en mémoire les dernières secondes de votre écran pendant que vous jouez. Un raccourci les enregistre en clip ; l'application sert ensuite à les retrouver, les regarder, les couper et les envoyer sur Discord.

Il existe pour une raison simple : faire ces deux choses dans une seule application légère. Le replay, à la manière d'OBS, et le rangement des clips sur le PC, à la manière de SteelSeries Moments, sans suite logicielle à installer autour, sans compte à créer et sans télémétrie.

## Télécharger

**[Télécharger Clipsapp pour Windows](https://github.com/LED-N/Clipsapp/releases/latest/download/Clipsapp-windows-x64.zip)**

1. Ouvrez l'archive et glissez le dossier `Clipsapp` où vous voulez, par exemple dans `Documents`. Évitez `Program Files`.
2. Lancez `Clipsapp.exe`, dans ce dossier.
3. Au premier lancement, Windows peut afficher « Windows a protégé votre ordinateur » : cliquez sur **Informations complémentaires**, puis **Exécuter quand même**.

Clipsapp pose ensuite quatre questions (dossier des clips, écran ou fenêtre, sons, raccourci), et le replay peut tourner.

## Ce que fait Clipsapp

- **Replay** : de 15 secondes à 10 minutes gardées en mémoire, enregistrées d'un raccourci, **Ctrl + Alt + F10** par défaut. Jusqu'en 4K à 60 images par seconde.
- **Son en pistes séparées** : micro, jeu et Discord, pour régler ou couper chacun après coup.
- **Bibliothèque** : clips rangés par jeu et par jour, favoris, recherche, renommage.
- **Lecture en plein écran**, pour regarder un clip ou le montrer sans l'envoyer.
- **Éditeur** : découpe, recadrage, volume de chaque piste, export. L'original est conservé.
- **Discord** : glisser un clip dans une conversation, ou en préparer une copie sous la limite de taille.

## À savoir

- **Système** : Windows 11, 64 bits. Clipsapp est développé et essayé avec une carte graphique NVIDIA ; il n'a pas encore été essayé avec une carte AMD.
- **Rien d'autre à installer** : tout ce dont Clipsapp a besoin est dans son dossier. Pour le désinstaller, supprimez ce dossier.
- **Mises à jour** : Clipsapp signale une nouvelle version à son lancement et l'installe en deux clics, sans toucher aux clips ni aux réglages. Cela se désactive dans **Réglages → Fichiers et alertes**.
- **Vie privée** : vos clips et vos réglages ne quittent pas votre PC. La seule connexion de Clipsapp est cette recherche de mises à jour auprès de GitHub, qui ne contient que son numéro de version.
- **Application non signée** : c'est la raison de l'avertissement de Windows au premier lancement.

Un problème ou une idée ? Écrivez dans l'onglet [Issues](https://github.com/LED-N/Clipsapp/issues), avec la version affichée en bas à gauche de Clipsapp.

## Compiler depuis les sources

Deux scripts suffisent sous Windows. Ils installent leurs outils (Qt 6, MinGW, CMake, FFmpeg) dans le dossier du projet, sans rien changer au système ; [7-Zip](https://www.7-zip.org/) doit être installé.

```powershell
.\scripts\bootstrap-windows.ps1
.\scripts\build-windows.ps1 -Test -Package
```

L'application prête à l'emploi se trouve ensuite dans `dist\Clipsapp`. Les composants tiers et leurs licences sont listés dans [THIRD_PARTY.md](THIRD_PARTY.md).
