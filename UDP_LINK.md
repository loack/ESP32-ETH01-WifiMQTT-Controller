# Liaison UDP directe (OMUDP v1)

Module optionnel, **desactive par defaut**, qui s'ajoute a MQTT (inchange) : un PC maitre pilote les sorties et recoit les fronts d'entree en UDP, pour reduire la latence. Fichiers : `src/udp_proto.*` (trames, HMAC-SHA-256 tronque, sans Arduino), `src/udp_safety.*` (validation des GPIO, deadman, broche de securite, sans Arduino), `src/udp_link.*` (sockets, taches, REST).

- Port UDP 47100 par defaut. Trames signees (HMAC-SHA-256 tronque a 8 octets) avec une cle maitre de 32 octets **fournie a l'execution** (`POST /api/udp`, champ `key` en 64 caracteres hexadecimaux, stockee en NVS, jamais renvoyee). Aucune cle dans le code ni dans ce depot.
- Configuration : `GET /api/udp` (lecture), `POST /api/udp` (ecriture, en-tete `X-Admin-Password`). Champs : `enabled`, `port`, `masterHost`, `deviceId`, `key`, `allowInsecure` (banc seulement), `statePeriodMs`, `lossMs`, `outputs[]` (`pin`, `safe`, `deadmanMs`), `safety` (`enabled`, `pin`, `activeLevel`, `mode`, `delayMs`, `periodMs`).
- **Broche de securite** (desactivee par defaut) : sortie de defaut ou de battement declenchee par la perte du battement de vie du PC. GPIO acceptes : 4, 13, 14 s'ils sont libres ; les GPIO Ethernet (RMII), flash, strapping, UART0, I2C et entrees seules sont refuses. **Ce n'est pas une fonction de securite** : un arret d'urgence cable independant reste obligatoire.
- Tests des primitives sur PC : `pio test -e native`. Compilation : `pio run`.
- Specification complete : voir le depot de documentation du projet (T-042-protocole-udp).
