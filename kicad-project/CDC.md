# Cahier des Charges Technique — Carrier Board Modulaire ESP32

## Philosophie de Conception

**Principe directeur : la carte ne fait qu'assembler des modules.**

La carrier board n'intègre quasiment aucun composant discret complexe. Elle sert de socle d'interconnexion pour des modules préassemblés disponibles à bas coût (AliExpress, LCSC, etc.). Cette approche minimise le coût de fabrication du PCB (peu de composants à monter), simplifie la soudure manuelle (uniquement des pin headers et borniers THT), et facilite la maintenance (remplacement module par module).

---

## 1. Objectif du Projet

Concevoir une carrier board industrielle/domotique modulaire, robuste et entièrement assemblable à la main. Elle doit :

- Alimenter l'ensemble depuis une source 24V DC ou via PoE (Ethernet).
- Héberger le module **WT32-ETH01** (ESP32 + Ethernet intégré) pour le traitement et la connectivité.
- Étendre les I/O de l'ESP32 via un module dédié (CJMCU-2317).
- Gérer des entrées/sorties isolées 24V via des modules optocoupleurs.
- Piloter un driver de moteur pas-à-pas industriel (DM556).

---

## 2. Architecture de l'Alimentation

La carte accepte deux sources d'alimentation, protégées contre les conflits et l'inversion de polarité (or-ing par diodes Schottky).

### Source A — Alimentation Directe 24V DC

| Élément | Spécification |
|---|---|
| Connecteur | Bornier à vis, pas de 5.08 mm |
| Protection | Fusible réarmable PPTC + diode anti-inversion |

### Source B — Power over Ethernet (PoE 48V)

Un **module PoE Splitter** (type RT9400, Ag9900 ou équivalent, en version broches traversantes) est enfichable sur un slot dédié. Il extrait la tension du câble Ethernet (48V) et la convertit en tension intermédiaire (12V ou 5V) utilisable par le reste du circuit.

### Régulation Principale — Module Buck DC-DC MP1584EN (AliExpress)

Un slot femelle (broches au pas de 2.54 mm) accueille le **module Buck MP1584EN** disponible sur AliExpress (~0,50 €/pièce). Ce module abaisse la tension d'entrée (24V direct ou sortie du module PoE) pour fournir le **5V principal** à l'ensemble de la carte.

| Critère | Valeur |
|---|---|
| Tension de sortie | **5V** (réglable via potentiomètre) |
| Courant max | 3A |
| Fréquence de découpage | 1.5 MHz (faible ondulation) |
| Efficacité typique | ~90 % |

Le **5V** alimente directement :
- Le **WT32-ETH01** via son pin 5V (régulateur interne → 3.3V pour l'ESP32)
- Les **modules relais et optocoupleurs** (VCC 5V)
- Le **level shifter TXS0108** (côté haute tension)

> Aucun régulateur n'est soudé directement sur le PCB : le slot permet de changer de module à tout moment, et le MP1584EN est remplaçable à 0,50 €.

---

## 3. Cœur du Système & Connectivité

### Module Microcontrôleur + Ethernet — WT32-ETH01 (Wireless-Tag)

Le module **WT32-ETH01** intègre sur un seul module le microcontrôleur ESP32, le PHY Ethernet LAN8720A et le connecteur RJ45. Cela élimine tout besoin d'un MagJack séparé ou d'un module LAN8720A additionnel, simplifiant considérablement le schéma et le routage.

| Caractéristique | Valeur |
|---|---|
| CPU | ESP32-D0WD dual-core 240 MHz |
| Ethernet | LAN8720A intégré + RJ45 on-board |
| WiFi / BT | 802.11 b/g/n + Bluetooth 4.2 |
| Alimentation | 5V (entrée) → 3.3V interne |
| Broches | 2 × 11 broches (22 au total) |
| **Pas des broches** | **⚠️ 2.0 mm — NON standard 2.54 mm** |

> **Point critique KiCad :** le footprint du WT32-ETH01 doit utiliser un pas de 2.0 mm. À vérifier impérativement sur la datasheet officielle (Wireless-Tag) avant de dessiner l'empreinte. Des embases femelles 1×11 au pas de 2.0 mm sont disponibles sur AliExpress.

Le PoE (si utilisé) reste géré en amont par le module PoE Splitter ; la tension intermédiaire est ensuite régulée par le module Buck MP1584EN (5V) avant d'alimenter le WT32-ETH01 via son pin 5V. Le régulateur interne du module produit le 3.3V pour l'ESP32 et le LAN8720A.

---

## 4. Extension des I/O — Module CJMCU-2317

L'ESP32 dispose d'un nombre limité de GPIO. Pour étendre les entrées/sorties disponibles, la carte intègre un slot pour le **module CJMCU-2317**, basé sur le MCP23017.

| Caractéristique | Valeur |
|---|---|
| Interface | I²C (SDA/SCL vers ESP32) |
| I/O ajoutées | 16 GPIO supplémentaires (2 × 8 bits) |
| Alimentation | 3.3V ou 5V selon module |
| Connexion | Pin headers femelles, pas de 2.54 mm |

Ce module est enfiché directement sur la carte sans aucune circuiterie externe nécessaire.

---

## 5. Entrées / Sorties Isolées 24V (Interface Industrielle)

Pour protéger l'ESP32 (logique 3.3V) des perturbations et surtensions du monde industriel (24V), toutes les interfaces 24V passent par des **modules optocoupleurs préassemblés**.

### Entrées 24V — Modules Optocoupleurs

Des modules optocoupleurs (type PC817 multi-canaux disponibles sur AliExpress) sont enfichés sur des slots dédiés. Chaque canal isole galvaniquement une entrée 24V et délivre un signal logique 3.3V compatible ESP32 ou CJMCU-2317.

- Connecteurs côté terrain : borniers à vis (pas de 3.5 mm ou 5.08 mm).
- Aucun composant discret nécessaire sur le PCB : le module gère en interne les résistances de limitation et le circuit optocoupleur.

### Sorties 24V — Modules Optocoupleurs / Relais

Pour les sorties vers des actionneurs 24V, des **modules relais** ou des **modules optocoupleurs de sortie** (avec transistor de puissance intégré) sont enfichés sur des slots. Le module reçoit un signal de commande 3.3V/5V depuis l'ESP32 ou le CJMCU-2317 et commute la charge 24V de façon isolée.

> Principe : le PCB ne porte aucun transistor, MOSFET ou relais discret. Tout est délégué aux modules.

---

## 6. Interface de Contrôle Moteur — Driver DM556

Le driver DM556 requiert trois signaux de commande : **PUL** (pas), **DIR** (direction), **ENA** (enable). Ses entrées optocouplées attendent des signaux à 5V (courant typique 10–15 mA), incompatibles directement avec les 3.3V de l'ESP32.

### Adaptation de niveau (Level Shifter)

Un **module level shifter bidirectionnel** (type TXS0108 ou équivalent, disponible en module préassemblé) est enfiché sur un slot dédié. Il convertit les signaux 3.3V de l'ESP32 en 5V pour le DM556.

**Câblage vers le DM556 (cathode commune) :**

| Signal DM556 | Connexion |
|---|---|
| PUL+, DIR+, ENA+ | Sortie 5V du level shifter |
| PUL−, DIR−, ENA− | GND |

Un bornier à vis dédié (6 broches, pas de 5.08 mm) regroupe les signaux vers le DM556.

---

## 7. Contraintes de Conception PCB

La carrier board est conçue pour une soudure manuelle au fer à souder classique, sans air chaud ni pâte à braser.

### Composants sur le PCB

| Type | Règle |
|---|---|
| Modules (WT32-ETH01, Buck MP1584EN, PoE Splitter, CJMCU-2317, optocoupleurs, relais, level shifter) | **Pin headers THT femelles** — pas 2.54 mm (sauf WT32-ETH01 : pas 2.0 mm) |
| Connecteurs terrain (borniers) | THT, pas de 3.5 ou 5.08 mm |
| Composants discrets résiduels (si nécessaires) | CMS taille **1206 minimum** |
| Diodes de protection, fusibles PPTC | THT de préférence |

### Règles de Routage

| Paramètre | Valeur |
|---|---|
| Pistes d'alimentation (24V, 5V, GND) | ≥ 1.5 mm |
| Pistes de signaux | 0.3 à 0.4 mm |
| Plan de masse | Face inférieure (Bottom layer), continu |
| Isolation 48V/24V ↔ logique 3.3V | ≥ 0.8 mm de clearance |

---

## 8. Prochaines Étapes dans KiCad

1. **Créer les symboles schématiques** pour chaque module (WT32-ETH01, MP1584EN, CJMCU-2317, modules optocoupleurs, module PoE Splitter, level shifter) — des rectangles avec le bon nombre de broches suffisent.
2. **Saisir le schéma (Eeschema)** en câblant les blocs conformément à ce CdC.
3. **Valider les empreintes PCB** : mesurer physiquement chaque module avant de dessiner le footprint, en particulier le WT32-ETH01 (pas 2.0 mm).
4. **Router le PCB** en respectant les règles de largeur de piste et de clearance définies ci-dessus.

---

## 9. Bill of Materials (BOM)

### 9.1 Modules Préassemblés

| Réf. | Désignation | Référence / Source | Qté | Prix unit. (indicatif) | Total |
|---|---|---|---|---|---|
| M1 | MCU + Ethernet | **WT32-ETH01** — Wireless-Tag / AliExpress | 1 | ~5,00 € | 5,00 € |
| M2 | Régulateur Buck 5V | **Module MP1584EN** 3A, réglé à 5V — AliExpress | 1 | ~0,50 € | 0,50 € |
| M3 | PoE Splitter | **Module RT9400 / Ag9900**, sortie 5V, broches THT — AliExpress | 1 | ~3,00 € | 3,00 € |
| M4 | Expandeur I/O I²C | **CJMCU-2317** (MCP23017) — AliExpress | 1 | ~1,50 € | 1,50 € |
| M5 | Optocoupleurs entrées 24V | **Module PC817 4 canaux** — AliExpress | 1–2 | ~0,80 € | ~1,60 € |
| M6 | Relais de sortie | **Module relais 4 canaux 5V** — AliExpress | 1 | ~1,50 € | 1,50 € |
| M7 | Level Shifter 3.3V → 5V | **Module TXS0108E** 8 canaux — AliExpress | 1 | ~0,60 € | 0,60 € |
| | | | | **Sous-total modules** | **~13,70 €** |

### 9.2 Connecteurs sur le PCB

| Réf. | Désignation | Référence type | Qté | Nbre de broches |
|---|---|---|---|---|
| J1 | Embase femelle WT32-ETH01 — côté A | **Pin Header Femelle 1×11, pas 2.0 mm** | 1 | 11 |
| J2 | Embase femelle WT32-ETH01 — côté B | **Pin Header Femelle 1×11, pas 2.0 mm** | 1 | 11 |
| J3 | Embase femelle Buck MP1584EN | Pin Header Femelle 1×4, pas 2.54 mm | 1 | 4 |
| J4 | Embase femelle module PoE Splitter | Pin Header Femelle 1×4 ou 1×6, pas 2.54 mm | 1 | 4–6 |
| J5 | Embase femelle CJMCU-2317 — rangée A | Pin Header Femelle 1×9, pas 2.54 mm | 1 | 9 |
| J6 | Embase femelle CJMCU-2317 — rangée B | Pin Header Femelle 1×9, pas 2.54 mm | 1 | 9 |
| J7 | Embase femelle module optocoupleurs | Pin Header Femelle 1×6, pas 2.54 mm | 1–2 | 6 |
| J8 | Embase femelle module relais (ctrl) | Pin Header Femelle 1×6, pas 2.54 mm | 1 | 6 |
| J9 | Embase femelle level shifter | Pin Header Femelle 1×10, pas 2.54 mm | 1 | 10 |

> Toutes les embases 2.54 mm sont des **pin headers femelles simples rangée générique** (vendus en barrettes de 40 broches à couper — ~0,10 €/barrette sur AliExpress).
> Les embases 2.0 mm pour le WT32-ETH01 sont disponibles séparément (barrette 1×40 pas 2.0 mm, ~0,15 € sur AliExpress).

### 9.3 Borniers à Vis (Connecteurs Terrain)

| Réf. | Désignation | Référence type | Qté | Prix unit. |
|---|---|---|---|---|
| BRN1 | Alimentation 24V DC | **KF301-2P**, pas 5.08 mm | 1 | ~0,10 € |
| BRN2 | Signaux DM556 (PUL±, DIR±, ENA±) | **KF301-6P** ou 2× KF301-3P, pas 5.08 mm | 1 | ~0,15 € |
| BRN3 | Entrées terrain 24V (par canal) | **KF301-2P**, pas 5.08 mm | × N canaux | ~0,10 € |
| BRN4 | Sorties terrain 24V (par canal) | **KF301-2P**, pas 5.08 mm | × N canaux | ~0,10 € |

> La série **KF301** (ou KCD3) est une référence ultra-courante au pas 5.08 mm, disponible à partir de 0,08 €/pièce sur AliExpress.

### 9.4 Composants Discrets Résiduels (PCB uniquement)

Ces composants sont les seuls à souder directement sur le PCB :

| Réf. | Désignation | Référence type | Qté | Boîtier |
|---|---|---|---|---|
| D1, D2 | Diodes Schottky or-ing (protection sources) | **1N5819** | 2 | DO-41 (THT) |
| F1 | Fusible réarmable PPTC 0.5 A | **MF-R050** ou équivalent | 1 | THT |
| R1, R2 | Résistances pull-up I²C SDA/SCL (4.7 kΩ) | Résistance 4.7 kΩ | 2 | 1206 (CMS) |

### 9.5 Coût Total Estimé par Carte

| Poste | Coût estimé |
|---|---|
| Modules préassemblés | ~13,70 € |
| Connecteurs (pin headers + borniers) | ~2,00 € |
| Composants discrets résiduels | ~0,50 € |
| PCB (5 pcs JLCPCB, 2 couches) | ~1,00 € / carte |
| **TOTAL** | **~17 €** |
