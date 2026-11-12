#ifndef VARIANT_H
#define VARIANT_H
// Variante de firmware, choisie par l'environnement PlatformIO (voir VARIANTS.md).
//  FW_NET_ETH   : Ethernet WT32-ETH01 (ce depot ne cible que cette carte)
//  FW_IO_SIMPLE : E/S simples (moteur DM556 et MCP23017 absents, GPIO 4/14/15/32/33 libres)
//  FW_IO_24V    : carte 24 V (moteur DM556 + MCP23017)
//  FW_UDP_DEFAULT_ENABLED : UDP actif par defaut de configuration (cle fournie a l'execution)
//  FW_MQTT      : MQTT compile et active (0 = UDP seul)
#ifndef FW_VARIANT
#error "FW_VARIANT absent : compiler avec un environnement nomme (pio run -e sonde-eth-io)"
#endif
#if !defined(FW_NET_ETH) || !defined(FW_IO_SIMPLE) || !defined(FW_IO_24V) || !defined(FW_UDP_DEFAULT_ENABLED) || !defined(FW_MQTT)
#error "indicateurs FW_NET_ETH, FW_IO_SIMPLE, FW_IO_24V, FW_UDP_DEFAULT_ENABLED, FW_MQTT requis"
#endif
#if FW_NET_ETH != 1
#error "ce depot ne cible que le WT32-ETH01 (FW_NET_ETH=1) ; le WiFi seul est dans ESP32-WifiMQTTRelay"
#endif
#if (FW_IO_SIMPLE + FW_IO_24V) != 1
#error "choisir exactement un type d'E/S : FW_IO_SIMPLE ou FW_IO_24V"
#endif
#if !FW_UDP_DEFAULT_ENABLED && !FW_MQTT
#error "aucune liaison : activer MQTT (FW_MQTT=1) ou l'UDP (FW_UDP_DEFAULT_ENABLED=1)"
#endif
#define FW_NET_NAME "eth"
#if FW_IO_24V
#define FW_IO_NAME "24v"
#define FW_FEAT_MOTOR 1
#define FW_FEAT_MCP   1
#else
#define FW_IO_NAME "simple"
#define FW_FEAT_MOTOR 0
#define FW_FEAT_MCP   0
#endif
// Controle des broches par defaut du moteur : jamais sur le PHY Ethernet (LAN8720A) ni UART0
constexpr bool fwEthReserved(int p) { return p==0||p==1||p==3||p==17||p==18||p==19||p==21||p==22||p==23||p==25||p==26||p==27; }
#endif
