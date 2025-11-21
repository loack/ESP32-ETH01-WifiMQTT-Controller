# Documentation de l'API MQTT

Ce document décrit l'interface MQTT pour le contrôleur d'I/O ESP32.

## 1. Configuration

Le nom de l'appareil (`<device_name>`) est utilisé comme base pour tous les sujets MQTT. Il est configurable via l'interface web et sa valeur par défaut est `esp32-eth01`.

## 2. Points d'Entrée (Contrôle de l'ESP32)

### 2.4. Pont Série (Serial Bridge)

Permet d'envoyer des messages série à un périphérique connecté (ex: robot KUKA) via MQTT.

- **Sujet :** `<device_name>/serial/send`
- **Méthode :** Publier
- **Payload (JSON) :**
  ```json
  {
    "message": "Commande KUKA VKRC2"
  }
  ```
- **Effet :** Le message est transmis sur le port série (Serial2) configuré. Un log TX est généré et visible via l'API REST ou l'UI web.

#### Synchronisation et logs
Chaque message reçu via MQTT et transmis sur le port série est journalisé (timestamp, direction TX, contenu). Les réponses reçues sur le port série peuvent être publiées en MQTT (voir ci-dessous).

### 2.1. Contrôle des Broches de Sortie

Pour commander une broche configurée en sortie.

- **Sujet :** `<device_name>/control/<pin_name>/set`
- **Méthode :** Publier
- **Payload (JSON) :**

  ```json
  {
    "state": 1,
    "exec_at": 1678886400,
    "exec_at_us": 500000
  }
  ```

- **Paramètres du payload :**
  - `state` (requis) : L'état désiré de la broche.
    - `1` : HIGH
    - `0` : LOW
  - `exec_at` (optionnel) : Timestamp UNIX (en secondes) pour une exécution programmée. Si omis, la commande est exécutée immédiatement.
  - `exec_at_us` (optionnel) : Microsecondes à ajouter au `exec_at` pour une synchronisation fine.

### 2.2. Synchronisation Temporelle

Permet de synchroniser l'horloge interne de l'ESP32 avec une source de temps maîtresse.

- **Sujet :** `esp32/time/sync`
- **Méthode :** Publier
- **Payload (JSON) :**

  ```json
  {
    "seconds": 1678886400,
    "us": 123456,
    "compensations": {
      "esp32-eth01": 500
    }
  }
  ```

- **Paramètres du payload :**
  - `seconds` (requis) : Timestamp UNIX (en secondes).
  - `us` (optionnel) : Microsecondes.
  - `compensations` (optionnel) : Un objet contenant des compensations de latence (en microsecondes) pour des appareils spécifiques.

### 2.3. Mesure de Latence (Ping)

Pour mesurer le temps d'aller-retour entre le maître et l'appareil.

- **Sujet :** `<device_name>/ping`
- **Méthode :** Publier
- **Payload :** N'importe quelle chaîne de caractères. Le payload sera renvoyé dans le message `pong`.

## 3. Points de Sortie (Données de l'ESP32)

### 3.4. Retour Série (Serial Bridge)

L'ESP32 publie les messages reçus sur le port série (RX) vers MQTT.

- **Sujet :** `<device_name>/serial/receive`
- **Méthode :** Message publié par l'ESP32.
- **Payload (JSON) :**
  ```json
  {
    "message": "Réponse KUKA VKRC2",
    "timestamp": "2025-11-21T14:32:10Z"
  }
  ```
- **Effet :** Permet au superviseur ou au robot maître de recevoir les réponses ou statuts du périphérique série.

### 3.5. Log Série (optionnel)

Pour audit ou debug, l'ESP32 peut publier périodiquement ou sur demande l'historique des échanges série.

- **Sujet :** `<device_name>/serial/log`
- **Méthode :** Message publié par l'ESP32.
- **Payload (JSON) :**
  ```json
  [
    { "timestamp": "2025-11-21T14:32:10Z", "direction": "TX", "message": "..." },
    { "timestamp": "2025-11-21T14:32:11Z", "direction": "RX", "message": "..." }
  ]
  ```
- **Effet :** Permet de suivre l'historique complet des échanges série (TX/RX).

### 3.1. État des Broches

L'ESP32 publie l'état de ses broches (entrées et sorties).

- **Sujet :** `<device_name>/status/<pin_name>`
- **Méthode :** Message publié par l'ESP32.
- **Payload :**
  - Pour les **entrées**, le payload est une simple chaîne de caractères : `"1"` (HIGH) ou `"0"` (LOW).
  - Pour les **sorties** (après une commande), le payload est un objet JSON :

    ```json
    {
      "state": 1,
      "timestamp": 1678886400,
      "us": 500123
    }
    ```

### 3.2. Disponibilité de l'Appareil

Indique si l'appareil est connecté au broker MQTT.

- **Sujet :** `<device_name>/availability`
- **Méthode :** Message publié par l'ESP32 (message retenu).
- **Payload :** `"online"`

### 3.3. Réponse à la Mesure de Latence (Pong)

Réponse à un message `ping`.

- **Sujet :** `<device_name>/pong`
- **Méthode :** Message publié par l'ESP32.
- **Payload (JSON) :**

  ```json
  {
    "ping_payload": "contenu_du_ping"
  }
  ```
