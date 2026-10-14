# Journal des versions

## 0.8.1

**Nouveautés**

- **Replay en 120 et 144 images par seconde**, pour suivre un écran rapide. Si la carte graphique ne suit pas, la barre du replay le signale.
- **Qualité constante** : un nouveau mode de débit donne à chaque scène ce qu'il lui faut, sous un débit maximal. Cinq niveaux, de « Légère » à « Quasi sans perte », et un préréglage **Maximale** pour garder des clips à retravailler.
- **Volumes habituels** : chaque piste (micro, jeu, Discord…) a son volume habituel, réglable dans **Réglages → Audio**. Il s'applique à la lecture, dans l'éditeur et dans les copies Discord ; l'enregistrement garde tout à plein niveau. Clipsapp le propose de lui-même quand vos exports vont toujours dans le même sens, et le bouton « Garder ces volumes par défaut » de l'éditeur le fait tout de suite.
- **Zone de notification** : fermer la fenêtre garde Clipsapp et son replay près de l'horloge. Un clic rouvre la fenêtre ; un clic droit propose de sauvegarder un clip, d'arrêter le replay ou de quitter. Cela se désactive dans **Réglages → Fichiers et alertes**.
- **Menu Démarrer** : Clipsapp propose au premier lancement de s'y ajouter ; l'option est aussi dans **Réglages → Fichiers et alertes**.
- **Icône** : l'application a la sienne dans l'Explorateur, la barre des tâches et le menu Démarrer.

**Améliorations**

- **Lecture fluide** : l'éditeur et le plein écran affichent les clips directement avec la carte graphique, à leur cadence, jusqu'à 144 images par seconde, même pendant qu'un jeu tourne. L'aperçu de l'éditeur est aussi plus net.
- **Mouvement plus régulier dans les clips** : chaque image du replay est prise à l'heure. Elle pouvait l'être en retard, ce qui donnait des à-coups.
- **Réglages avancés** : effort d'encodage « Rapide » ou « Soigné », débit jusqu'à 300 Mbit/s, et une infobulle sur chaque réglage. Ils restent repliés par défaut.
- **Mémoire du replay** : en qualité constante, l'estimation donne une fourchette selon le jeu, et ce que le plafond de débit permet au plus.

**Corrections**

- **Son des clips importés** : les clips enregistrés par d'autres logiciels, comme SteelSeries Moments, ne grésillent plus dans le lecteur et l'éditeur.
- **Image et son restent calés** quand l'encodeur ne suit pas : les images en trop sont abandonnées au lieu de ralentir le clip.
- **Une seule copie à la fois** : relancer Clipsapp pendant qu'il tourne ramène sa fenêtre au lieu d'en ouvrir une seconde.

## 0.8.0

Première version publique de Clipsapp.

- **Replay** : les dernières secondes de l'écran gardées en mémoire, enregistrées en clip d'un raccourci.
- **Bibliothèque** : clips rangés par jeu et par jour, favoris, recherche, renommage.
- **Lecture en plein écran**, pour regarder un clip ou le montrer sans l'envoyer.
- **Éditeur** : découpe, recadrage, volume de chaque piste de son, export.
- **Discord** : glisser un clip dans une conversation, ou en préparer une copie sous la limite de taille.
- **Mises à jour** proposées dans l'application et installées en deux clics.
