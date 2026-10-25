# Station de recharge automatique pour chien robot

Projet de BUT GEII (semestre 4, mai 2026), IUT de Cachan.

Une « niche » qui recharge la batterie d'un chien robot dès qu'il se pose sur ses plots de contact, quelle que soit son orientation. Un tableau de bord sur téléphone affiche la charge en temps réel.

## Le problème

Le robot peut se poser n'importe comment sur la station. On ne sait donc pas à l'avance quel plot touchera le pôle positif de sa batterie. Brancher le chargeur à l'aveugle risquerait une inversion de polarité.

## Comment ça marche

- **4 plots de contact**, chacun commandé par un interrupteur côté haut (+) et un interrupteur côté bas (−). Chaque plot peut ainsi être relié au + ou au − du chargeur.
- **Détection automatique de polarité** : l'ESP32 mesure la tension de chaque plot toutes les 50 ms. Le plot qui présente la tension de la batterie (entre 5 V et 36 V) est identifié comme pôle positif. La station y envoie le +, et le − sur les autres plots.
- **Mesure de la charge** : un capteur INA236 (bus I²C) mesure en continu la tension, le courant et la puissance.
- **Fin de charge** : quand le courant descend sous 50 mA (batterie pleine ou robot reparti), la station coupe la charge.

### Machine à états

| État | Rôle |
|---|---|
| Initial | Tous les plots sont coupés, la station attend le robot. |
| Temporisation | 2 s de stabilisation, puis analyse de la polarité. |
| Charge | Le + et le − sont envoyés vers les bons plots. |
| Charge terminée | La charge reste coupée tant que le robot est posé, pour éviter les cycles marche/arrêt. |
| Tension basse | Batterie sous 25 V : la charge est verrouillée (« batterie morte » sous 24 V). |
| Surtension | Plus de 36 V sur un plot : tout est coupé. Cet état a la priorité sur tous les autres. |

## Tableau de bord

L'ESP32 crée son propre point d'accès Wi-Fi et sert une page web, rafraîchie chaque seconde :

- la tension et l'état de chaque plot ;
- la tension, le courant, la puissance et la tension de shunt mesurés par l'INA236 ;
- une courbe en temps réel sur les 60 dernières secondes ;
- les statistiques de la session : durée, énergie transférée (Wh) et plots actifs ;
- des alertes visuelles et sonores, par exemple en cas de surintensité.

## Matériel et outils

- ESP32 (carte esp32dev), programmé avec PlatformIO et le framework Arduino
- Capteur de courant et de puissance INA236 (shunt de 5 mΩ, jusqu'à 9 A)
- Bibliothèques : ESPAsyncWebServer, AsyncTCP, INA236
- Tableau de bord en HTML, CSS et JavaScript, stocké dans la mémoire LittleFS de l'ESP32

## Organisation du code

| Fichier | Contenu |
|---|---|
| `src/main1.cpp` | Programme de l'ESP32 : mesures, machine à états, serveur web |
| `data/index.html` | Tableau de bord |
| `platformio.ini` | Configuration de la carte et des bibliothèques |

## Lancer le projet

1. Ouvrir le dossier avec PlatformIO (dans VS Code).
2. Envoyer le tableau de bord dans la mémoire de l'ESP32 : `pio run -t uploadfs`
3. Compiler et envoyer le programme : `pio run -t upload`
4. Se connecter au Wi-Fi `ESP32_Dashboard`, puis ouvrir `http://192.168.4.1` dans un navigateur.
