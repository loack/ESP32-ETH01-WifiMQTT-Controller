# Variantes de firmware (WT32-ETH01)

Les variantes se choisissent a la compilation par l'environnement PlatformIO. Elles selectionnent ce qui existait deja
(moteur DM556, MCP23017, liaison UDP, MQTT) ; aucun module n'a ete recree.

| Environnement | E/S | Moteur / MCP23017 | UDP par defaut | MQTT | Usage |
|---|---|---|---|---|---|
| `sonde-eth-io` | simple | absents (GPIO 4/14/15/32/33 libres, I2C non demarre) | actif (cle a l'execution) | compile, actif, desactivable | sonde-touch |
| `sonde-eth-io-udp-seul` | simple | absents | actif | non compile | sonde sans repli MQTT |
| `io-devicenet-eth-24v` | 24 V | presents (moteur actif par defaut) | actif (cle a l'execution) | compile, actif, desactivable | io-devicenet-minus |

Indicateurs (`build_flags`) : `FW_VARIANT`, `FW_NET_ETH`, `FW_IO_SIMPLE` / `FW_IO_24V`, `FW_UDP_DEFAULT_ENABLED`, `FW_MQTT`.
Controles a la compilation (`src/variant.h`, `config.h`) : un seul type d'E/S ; au moins une liaison (MQTT ou UDP) ;
reseau Ethernet seulement ; broches moteur hors PHY Ethernet/UART0 et distinctes.

Comportement
- UDP « actif par defaut » = `enabled=true` dans la configuration par defaut ; la liaison ne demarre que si une cle est configuree
  (`POST /api/udp`, jamais de cle dans le code). Sans cle : inerte.
- E/S simple : `motorEnabled` est force a faux au chargement ; `mcpIoBegin()` et `mcpEnsureDefaultIOsRegistered()` ne font rien
  (la liste d'E/S n'est pas modifiee) ; `POST /api/motor/config` avec `enabled=true` est refuse.
- MQTT : `FW_MQTT=1` et reglage persistant `mqttActive` (defaut vrai) via `POST /api/config` `{"mqttActive":false}` (redemarrage,
  sans reflasher) ; `GET /api/info` indique `mqttActive`. MQTT desactive : plus aucune connexion au broker, la sonde n'emet que par UDP.
  Retour arriere : `{"mqttActive":true}` puis redemarrage.
- Version : `GET /api/info` (variante, commit, date, indicateurs, memoire, place OTA) et message retenu MQTT `<device>/info`.

Compilation : `pio run -e <env>` ; tests natifs : `pio test -e native`. Mise a jour : OTA par `/update` (firmware seul), jamais le
systeme de fichiers ni un effacement. L'ancien environnement `wt32-eth01` est remplace par `io-devicenet-eth-24v`.
