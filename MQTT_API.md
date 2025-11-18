# Documentation de l'API MQTT

Ce document décrit l'interface MQTT pour le contrôleur d'I/O ESP32.

## 1. Configuration

Le nom de l'appareil (`<device_name>`) est utilisé comme base pour tous les sujets MQTT. Il est configurable via l'interface web et sa valeur par défaut est `esp32-eth01`.

## 2. Points d'Entrée (Contrôle de l'ESP32)

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
