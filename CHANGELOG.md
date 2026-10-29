# Journal des versions

## 0.8.5

**Nouveautés**

- **Compresseur** : il remplace « Préparer pour Discord » et va plus loin. Allégez vos clips sans perte visible, en comparant avant et après d'un glissement sur l'image, ou compressez-les pour Discord, à la taille de votre limite. Plusieurs clips, ou toute la bibliothèque, se compressent en arrière-plan. Barre latérale → Compresseur
- **Réglages de capture repensés** : régler sa capture est bien plus simple qu'avant : une résolution, une fréquence et un préréglage, Léger, Équilibré ou Haut. Réglages → Capture → Qualité

**Améliorations**

- **Son de l'enregistrement** : choisissez-le et réglez son volume, comme pour les clips et les screenshots.
- **Démarrage avec Windows** : Clipsapp peut maintenant s'ouvrir au premier plan, et plus seulement réduit près de l'horloge.
- **Premier lancement** : une étape Qualité, avec la taille de votre écran déjà proposée.
- **Jeu capturé** : il est toujours reconnu de lui-même, sinon les clips prennent le nom du profil ; le champ « Nom du jeu » disparaît.
- **Buffer du replay** : le disque est proposé dès que le replay prendrait trop de mémoire pour votre PC (un dixième de la mémoire, 2 Go au plus).
- **Suppression définitive** : chaque suppression peut aussi se passer de la corbeille, pour les clips, les enregistrements et les screenshots.
- **Export** : sa progression s'affiche au milieu de la fenêtre, puis propose d'ouvrir le nouveau clip ou de mettre l'original à la corbeille. Il ne s'active qu'une fois le clip réellement modifié.
- **Plein écran** : un clic sur l'image met en pause ou relance la lecture ; seuls Échap et la croix le ferment.
- **Clavier** : Espace sert à la lecture dans l'éditeur, et plus aucun bouton ne s'active au clavier par erreur.
- **Écran capturé** : il reste le même après une veille ou un écran rebranché, et la capture reprend d'elle-même à son retour.
- **Raccourcis** : les touches du pavé numérique et la ponctuation (« + », « é »…) peuvent servir de raccourci, et chaque raccourci peut être retiré.
- **Thèmes** : toutes les fenêtres, notifications et lecteurs suivent le thème choisi.
- **Éditeurs** : Exporter, Compresser et Mettre à la corbeille sont réunis en bas à droite, dans l'éditeur de clips comme dans celui des screenshots.
- **Glisser un clip** : survolez une application dans la barre des tâches, Discord par exemple, pour l'amener au premier plan et y déposer le fichier.
- **Partage** : un clip en HEVC ou en AV1, glissé ou copié vers Discord, propose une copie en H.264, lisible partout.

**Corrections**

- La barre de sélection des screenshots disait « clip sélectionné ».
- L'éditeur pouvait afficher « Lecture impossible : Immediate exit requested » ; un bouton « Recharger le clip » permet aussi de reprendre sans le fermer.
- Le replay ne démarrait pas sur les GTX 900 et 1000 (« Aucun encodeur matériel disponible ») : l'encodeur de Windows prend désormais le relais.
- Après avoir regardé un clip, Clipsapp gardait 200 à 300 Mo de mémoire jusqu'à sa fermeture.
- Un clip pouvait être marqué « Impossible de lire ce fichier » alors qu'il se lisait normalement.
- Dans l'éditeur de screenshots, le mode Dessiner empêchait de changer de mode, et « Ouvrir la nouvelle image » affichait « Image illisible ».

## 0.8.4

**Nouveautés**

- **Enregistrement** : enregistrez vos parties en entier, sans limite de durée. Un seul raccourci, **Ctrl + Alt + F9**, le lance et l'arrête, avec ou sans le replay à côté. Réglages → Capture → Enregistrement
- **Thèmes** : cinq thèmes prêts à l'emploi, dont un violet, ou le vôtre, avec vos couleurs et vos dégradés, à partager avec vos amis par un simple code. Le logo suit le thème. Réglages → Général → Thèmes
- **Buffer du replay sur disque** : un long replay sans prendre la mémoire du jeu. Clipsapp le propose quand il le faut. Réglages → Capture → Replay

**Améliorations**

- **Sources audio** : prises en compte dès leur premier son, sans vérification en continu.
- **Barre du replay** : le replay et l'enregistrement ont chacun leur barre, verte et rouge.
- **Réglages pendant la capture** : thèmes, raccourcis, dossier des clips, sons et volumes se modifient sans arrêter le replay ni l'enregistrement.

**Corrections**

- Une application mise à jour, comme Discord, reste enregistrée et exclue de « Tout, sauf exclusions ».
- Une application ouverte mais silencieuse n'est plus marquée « indisponible ».
- La liste des applications audio n'est plus vide quand Clipsapp démarre avant elles.
- En plein écran, l'image suit la taille de la fenêtre même en pause.

## 0.8.3

**Nouveautés**

- **Screenshots** : un raccourci (**Ctrl + Alt + F11** par défaut) prend l'écran ou la fenêtre du replay en qualité native, que le replay tourne ou non. Les images sont rangées par jeu, comme les clips, dans le dossier « Screenshots » de la bibliothèque, et ont leur propre catégorie sous « Clips », avec favoris, recherche, renommage et classement. Un clic ou **Espace** les affiche en plein écran ; les **flèches gauche et droite** passent à l'image précédente ou suivante. Un éditeur permet de les recadrer, d'y **dessiner** (pinceau, flèche, cadre, ellipse ; six couleurs, trois épaisseurs, annuler avec Ctrl + Z) et d'enregistrer le résultat dans une copie à côté de l'originale, qui ne change pas. Elles se copient aussi pour être collées dans Discord, ou se mettent à la corbeille. Un son, au choix et réglable, confirme chaque screenshot.
- **Screenshot depuis l'éditeur de clips** : le bouton appareil photo, sur l'image, garde l'image affichée en qualité d'origine, avec le cadrage choisi, parmi les screenshots du jeu.
- **Volumes par jeu** : Clipsapp retient vos volumes jeu par jeu. Si vous baissez souvent le son du jeu sur ARC Raiders, il vous propose de l'appliquer à tous les clips ARC Raiders, sans toucher à ceux des autres jeux. Le bouton « Garder ces volumes » de l'éditeur les garde pour le jeu du clip ; la liste et un bouton « Oublier » sont dans **Réglages → Audio**.
- **Démarrage automatique**, dans **Réglages → Général**, désactivé tant que vous ne le choisissez pas :
  - **Lancer Clipsapp à l'ouverture de la session Windows**. Il s'ouvre discrètement, dans la zone de notification.
  - **Replay automatique** : dès le lancement de Clipsapp, ou seulement quand un de vos jeux est ouvert. Les jeux se choisissent parmi les applications ouvertes, ou par leur programme. Le replay s'arrête 30 secondes après la fermeture du dernier jeu, pour laisser le temps de sauvegarder la fin. Arrêté à la main pendant une partie, il ne redémarre pas avant la prochaine ouverture du jeu. Il enregistre l'écran choisi dans Capture, ou l'écran principal si c'est une fenêtre qui y est choisie.
- **Quoi de neuf** : au premier lancement après une mise à jour, une fenêtre présente ce qu'apporte la nouvelle version. Un clic sur le numéro de version, en bas à gauche, la rouvre, avec les notes des versions précédentes.
- **Les clips ajoutés, renommés ou supprimés dans l'Explorateur** apparaissent ou disparaissent aussitôt dans Clipsapp, sans rien actualiser.
- **Désinstallation en un double-clic** : `Desinstaller Clipsapp.bat`, dans le dossier de Clipsapp, ferme l'application, retire son raccourci du menu Démarrer et son lancement avec Windows, puis supprime le dossier. Il propose de garder vos réglages pour une prochaine installation. Vos clips ne sont jamais touchés ; s'ils sont rangés dans le dossier de Clipsapp, le script s'arrête sans rien supprimer.
- **Copier le diagnostic**, dans **Réglages → Général** : Clipsapp tient un petit journal sur votre PC, et ce bouton le copie avec la description de la machine, pour le joindre à un signalement. Le nom de votre dossier personnel en est retiré, et rien n'est envoyé.

**Améliorations**

- **Un micro débranché n'arrête plus le replay** : sa piste reste silencieuse, la barre du replay le signale, et le son revient dès que le micro est rebranché. Une source qui suit le périphérique par défaut suit aussi ses changements. Il en va de même pour une sortie audio, et pour une application dont le son ne peut pas être suivi.
- **Bibliothèque plus rapide à l'ouverture** : les miniatures et la durée des clips sont gardées d'un lancement à l'autre ; un clip n'est lu qu'une fois.
- Après un plantage, Clipsapp le signale au lancement suivant et garde un rapport sur le PC.
- **Son de sauvegarde au choix** : sept sons (deux notes, carillon, pièce, goutte, bulle, déclencheur photo, bip), un volume réglable et un bouton « Écouter » pour les essayer, dans **Réglages → Général**.
- **Réglages réorganisés** : le dossier des clips, renommé « Dossier des clips et captures d'écran », rejoint l'onglet **Capture**, sous le replay ; l'onglet « Fichiers et alertes » devient **Général**. Les infobulles des réglages à plusieurs choix (codec, encodeur, mode de débit, effort, niveau de qualité) présentent chaque choix sur sa ligne, son nom en gras.
- **Dossier de Clipsapp épuré** : il ne contient plus que `Clipsapp.exe`, le script de désinstallation et un dossier `runtime` qui regroupe tout le reste. La mise à jour depuis la 0.8.2 fait le rangement d'elle-même. Le README reste sur la page GitHub ; le journal des versions s'ouvre depuis Clipsapp.
- **Réglages rangés dans un seul dossier** : `%LOCALAPPDATA%\Clipsapp` au lieu de `%LOCALAPPDATA%\Clipsapp\Clipsapp`. Ils y sont déplacés au premier lancement, sans rien perdre.

**Corrections et retraits**

- **Le recadrage à la souris refonctionne** dans l'éditeur : depuis l'affichage direct de la 0.8.1, on ne pouvait plus tracer ni redimensionner le cadre sur l'image, seulement le régler par les valeurs précises.
- **Les boutons et les bandeaux de l'éditeur restent à leur place** : « Cadrer / Résultat » pouvaient se retrouver dans la barre du replay, et le bandeau d'un clip exporté passer sous l'image.
- La barre latérale n'affiche plus les jeux que sous la catégorie ouverte, Clips ou Screenshots.
- Intel Quick Sync n'est plus proposé : il renvoyait une erreur au démarrage du replay. Un profil qui l'avait choisi passe en « Automatique » (NVIDIA ou AMD).
- Un message d'avertissement de Qt revenait dans le journal après l'ouverture de l'éditeur (« must be a top level window »).

## 0.8.2

**Correction**

- **« Quitter » ferme vraiment Clipsapp** : en 0.8.1, quitter depuis l'icône de la zone de notification laissait le programme tourner en arrière-plan, sans fenêtre ni icône, avec son replay s'il était actif. Si cela vous est arrivé, fermez « Clipsapp » une fois dans le Gestionnaire des tâches, ou redémarrez le PC.

**Nouveauté**

- `Clipsapp.exe --quit` ferme la copie de Clipsapp en cours, comme « Quitter » dans le menu de son icône.

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
