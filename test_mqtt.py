#!/usr/bin/env python3
"""
Script de test MQTT avec broker intégré pour ESP32 IO Controller
Combine un broker MQTT simple et un client de test
"""

import paho.mqtt.client as mqtt
import threading
import time
import sys
import socket
import os
import platform
import json

# ========== CONFIGURATION ========== 
MQTT_PORT = 1883
DEVICE_NAME = "lilygo"  # Nom de l'appareil ESP32 (doit correspondre au nom configuré sur l'ESP32)
RELAY_NAMES = ["RelaisK1", "RelaisK2","RelaisK3","RelaisK4"]
RELAY_NAMES = ["RelaisK1", "RelaisK2"]

# Liste des devices pour les tests multi-ESP32
ALL_DEVICES = ["laser", "lilygo"]  # Ajouter vos ESP32 ici

# Dictionnaire pour suivre les commandes en attente de confirmation
pending_commands = {}

# Mesure de latence réseau - PAR DEVICE
device_latencies = {}  # device_name -> {'samples': [], 'avg_rtt_us': 0, 'avg_latency_us': 0, 'last_measurement': 0}
MAX_SAMPLES = 20

# Réduction nombre message affichage pour plus de clarté
TIME_MESSAGE_DISPLAY = False

# Tracker global des pings en attente
ping_tracker = {
    'ping_times': {}  # ping_id -> (time, device_name)
}

def get_device_latency(device_name):
    """Obtenir ou créer le tracker de latence pour un device"""
    if device_name not in device_latencies:
        device_latencies[device_name] = {
            'samples': [],
            'avg_rtt_us': 0,
            'avg_latency_us': 0,
            'last_measurement': 0
        }
    return device_latencies[device_name]

def get_local_ip():
    """Récupère l'adresse IP locale"""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except:
        return "127.0.0.1"
 
# ========== CLIENT MQTT ==========

def on_connect(client, userdata, flags, reason_code, properties):
    """Appelé lors de la connexion au broker"""
    if reason_code == 0:
        print(f"\n✓ Client connecté au broker MQTT")
        # S'abonner aux topics de statut de tous les relais
        status_topic = f"{DEVICE_NAME}/status/#"
        client.subscribe(status_topic)
        print(f"✓ Abonné à: {status_topic}")

        # S'abonner aux topics de disponibilité
        availability_topic = f"{DEVICE_NAME}/availability"
        client.subscribe(availability_topic)
        print(f"✓ Abonné à: {availability_topic}")
        
        # S'abonner au topic de temps commun
        client.subscribe("esp32/time/sync")
        print(f"✓ Abonné à: esp32/time/sync")
        
        # S'abonner au topic pong pour mesurer la latence
        pong_topic = f"{DEVICE_NAME}/pong"
        client.subscribe(pong_topic)
        print(f"✓ Abonné à: {pong_topic}")

        # S'abonner au topic de réception série
        serial_receive_topic = f"{DEVICE_NAME}/serial/receive"
        client.subscribe(serial_receive_topic)
        print(f"✓ Abonné à: {serial_receive_topic}\n")
    else:
        print(f"✗ Échec de connexion, code: {reason_code}")

def on_message(client, userdata, msg):
    """Appelé lors de la réception d'un message"""
    receipt_time = time.time()
    topic = msg.topic
    payload = msg.payload.decode()
    
    # Gérer les réponses pong pour mesurer la latence
    if topic.endswith("/pong"):
        try:
            # Extraire le nom du device depuis le topic
            device_name = topic.split('/')[0]
            data = json.loads(payload)
            ping_payload = data.get("ping_payload")
            
            if ping_payload and ping_payload in ping_tracker['ping_times']:
                ping_time, tracked_device = ping_tracker['ping_times'].pop(ping_payload)
                if device_name != tracked_device:
                    return  # Réponse d'un autre device
                    
                rtt = (receipt_time - ping_time) * 1000000  # en microsecondes
                
                # Obtenir le tracker pour ce device
                dev_latency = get_device_latency(device_name)
                
                # Ajouter à l'échantillon
                dev_latency['samples'].append(rtt)
                if len(dev_latency['samples']) > MAX_SAMPLES:
                    dev_latency['samples'].pop(0)
                
                # Calculer le RTT moyen et la latence (unidirectionnelle = RTT / 2)
                avg_rtt = sum(dev_latency['samples']) / len(dev_latency['samples'])
                dev_latency['avg_rtt_us'] = int(avg_rtt)
                dev_latency['avg_latency_us'] = int(avg_rtt / 2)
                
                # Afficher le RTT individuel (pas la moyenne) pour voir les vraies valeurs
                rtt_ms = rtt / 1000.0
                latency_ms = rtt_ms / 2.0
                
                # N'afficher que pendant les tests de qualité (sinon trop verbeux)
                # Les pings automatiques toutes les 30s sont silencieux
                if ping_payload.startswith("measure_"):
                    print(f"    ✓ [{device_name}] RTT: {rtt_ms:.2f}ms | Latence: {latency_ms:.2f}ms")
        except (json.JSONDecodeError, KeyError):
            pass
        return

    # Gérer les messages de statut JSON
    status_prefix = f"{DEVICE_NAME}/status/"
    if topic.startswith(status_prefix):
        relay_name = topic[len(status_prefix):]
        try:
            # Essayer de parser comme JSON d'abord
            data = json.loads(payload)
            
            # Si c'est un objet JSON avec state et timestamp (outputs/relais)
            if isinstance(data, dict):
                state = data.get("state")
                esp_timestamp = data.get("timestamp")
                esp_us = data.get("us", 0)  # Microsecondes (0 par défaut)

                if state is None or esp_timestamp is None:
                    print(f"📨 Message de statut incomplet reçu pour {relay_name}: {payload}")
                    return

                state_str = "ON" if state == 1 else "OFF"
                print(f"📨 Statut reçu pour {relay_name}: {state_str} (ESP time: {esp_timestamp}.{esp_us:06d})")

                # Vérifier si une commande était en attente pour ce relais
                if relay_name in pending_commands:
                    command_info = pending_commands.pop(relay_name)
                    
                    if command_info['type'] == 'immediate':
                        send_time = command_info['time']
                        latency = (receipt_time - send_time) * 1000
                        print(f"   └── ⏱️  Latence de la commande immédiate: {latency:.3f} ms")
                    
                    elif command_info['type'] == 'scheduled':
                        exec_at_sec = command_info['exec_at_sec']
                        exec_at_us = command_info['exec_at_us']
                        
                        # Calculer le délai en microsecondes
                        expected_time_us = (exec_at_sec * 1000000) + exec_at_us
                        actual_time_us = (esp_timestamp * 1000000) + esp_us
                        delay_us = actual_time_us - expected_time_us
                        delay_ms = delay_us / 1000.0
                        
                        print(f"   └── 🗓️  Commande programmée exécutée:")
                        print(f"        - Heure demandée : {exec_at_sec}.{exec_at_us:06d}")
                        print(f"        - Heure exécution: {esp_timestamp}.{esp_us:06d}")
                        print(f"        - Décalage       : {delay_ms:.3f} ms ({delay_us} µs)")
            
            # Si c'est juste un nombre (inputs)
            elif isinstance(data, int):
                state_str = "HIGH" if data == 1 else "LOW"
                print(f"📨 Input {relay_name}: {state_str}")

        except (json.JSONDecodeError, KeyError):
            # Gérer les anciens messages ou les messages mal formés
            print(f"📨 Message (non-JSON ou mal formé) reçu: {topic} = {payload}")

    # Gérer les autres messages (disponibilité, etc.)
    elif topic.endswith("/serial/receive"):
        try:
            data = json.loads(payload)
            print(f"🖥️  SERIAL RX via MQTT: {data.get('message')}")
        except json.JSONDecodeError:
            print(f"🖥️  SERIAL RX (raw): {payload}")
    
    #on affiche les messages de sync uniquement si demandé
    if topic.endswith("/time/sync"):
        if TIME_MESSAGE_DISPLAY:
            print(f"⏰ Timestamp sync reçu: {payload}")
    else:
        print(f"📨 Message reçu: {topic} = {payload}")


def on_disconnect(client, userdata, disconnect_flags, reason_code, properties):
    """Appelé lors de la déconnexion"""
    if reason_code != 0:
        print(f"⚠ Déconnexion inattendue, code: {reason_code}")

def on_publish(client, userdata, mid, reason_code=None, properties=None):
    """Appelé quand un message est publié"""
    pass  # Silencieux pour ne pas polluer la console

# ========== FONCTIONS DE CONTRÔLE ==========
def set_relay(client, relay_name, state, exec_at_sec=None, exec_at_us=None):
    """Active ou désactive un relais, immédiatement ou de manière programmée"""
    topic = f"{DEVICE_NAME}/control/{relay_name}/set"
    
    payload_data = {"state": 1 if state else 0}
    if exec_at_sec is not None:
        payload_data["exec_at"] = exec_at_sec
        payload_data["exec_at_us"] = exec_at_us if exec_at_us is not None else 0
    
    payload = json.dumps(payload_data)
    
    # Enregistrer les informations sur la commande pour le calcul de la latence/délai
    if exec_at_sec is not None:
        pending_commands[relay_name] = {
            'type': 'scheduled', 
            'exec_at_sec': exec_at_sec,
            'exec_at_us': exec_at_us if exec_at_us is not None else 0
        }
    else:
        pending_commands[relay_name] = {'type': 'immediate', 'time': time.time()}

    result = client.publish(topic, payload, qos=1)
    
    if result.rc == mqtt.MQTT_ERR_SUCCESS:
        action = "ON" if state else "OFF"
        if exec_at_sec is not None:
            exec_time_str = time.strftime('%H:%M:%S', time.localtime(exec_at_sec))
            exec_us = exec_at_us if exec_at_us is not None else 0
            print(f"✓ Commande programmée envoyée: {relay_name} -> {action} à {exec_time_str}.{exec_us:06d}")
        else:
            print(f"✓ Commande immédiate envoyée: {relay_name} -> {action}")
    else:
        print(f"✗ Erreur lors de l'envoi de la commande")
        # Si l'envoi échoue, retirer la commande des commandes en attente
        pending_commands.pop(relay_name, None)

def turn_on(client, relay_name):
    """Active un relais immédiatement"""
    set_relay(client, relay_name, True)

def turn_off(client, relay_name):
    """Désactive un relais immédiatement"""
    set_relay(client, relay_name, False)

def schedule_toggle(client, relay_name, delay_seconds=5):
    """Programme l'activation d'un relais dans le futur avec précision microseconde"""
    current_time = time.time()
    exec_time = current_time + delay_seconds
    
    exec_seconds = int(exec_time)
    exec_us = int((exec_time - exec_seconds) * 1000000)
    
    print(f"\n🗓️ Programmation de {relay_name} pour s'activer dans {delay_seconds} secondes...")
    exec_time_str = time.strftime('%H:%M:%S', time.localtime(exec_seconds))
    print(f"   Exécution prévue: {exec_time_str}.{exec_us:06d}")
    
    set_relay(client, relay_name, True, exec_at_sec=exec_seconds, exec_at_us=exec_us)


def toggle_relay(client, relay_name, delay=2):
    """Fait basculer un relais ON puis OFF avec un délai"""
    print(f"\n🔄 Test toggle {relay_name}...")
    turn_on(client, relay_name)
    time.sleep(delay)
    turn_off(client, relay_name)

def synchronized_toggle_all_devices(client, relay_name="RelaisK1", delay_seconds=3):
    """Active un relais sur TOUS les ESP32 de manière synchronisée"""
    print(f"\n🎬 TEST DE SYNCHRONISATION MULTI-ESP32")
    print(f"{'='*60}")
    print(f"Relais ciblé: {relay_name}")
    print(f"Devices: {', '.join(ALL_DEVICES)}")
    print(f"Délai avant exécution: {delay_seconds} secondes")
    print(f"{'='*60}\n")
    
    # Vérifier que tous les devices ont une compensation mesurée
    devices_ready = []
    for device in ALL_DEVICES:
        if device in device_latencies and device_latencies[device]['avg_latency_us'] > 0:
            devices_ready.append(device)
            comp_ms = device_latencies[device]['avg_latency_us'] / 1000.0
            print(f"  ✓ {device:12s} - Compensation: {comp_ms:6.2f} ms")
        else:
            print(f"  ⚠️  {device:12s} - Pas de compensation disponible (mesure en cours...)")
    
    if len(devices_ready) < len(ALL_DEVICES):
        print(f"\n⚠️  Attendez que tous les devices aient une compensation mesurée")
        print(f"   ({len(devices_ready)}/{len(ALL_DEVICES)} prêts)")
        return
    
    # Calculer l'heure d'exécution synchronisée
    current_time = time.time()
    exec_time = current_time + delay_seconds
    exec_seconds = int(exec_time)
    exec_us = int((exec_time - exec_seconds) * 1000000)
    
    exec_time_str = time.strftime('%H:%M:%S', time.localtime(exec_seconds))
    print(f"\n⏰ Heure d'exécution synchronisée: {exec_time_str}.{exec_us:06d}")
    print(f"\n📤 Envoi des commandes programmées...\n")
    
    # Envoyer la commande à tous les devices
    for device in ALL_DEVICES:
        topic = f"{device}/control/{relay_name}/set"
        payload_data = {
            "state": 1,  # ON
            "exec_at": exec_seconds,
            "exec_at_us": exec_us
        }
        payload = json.dumps(payload_data)
        
        result = client.publish(topic, payload, qos=1)
        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            print(f"  ✓ Commande envoyée à {device}")
        else:
            print(f"  ✗ Échec d'envoi à {device}")
    
    print(f"\n⏳ Attente de l'exécution ({delay_seconds}s)...")
    print(f"🎥 FILMEZ MAINTENANT pour vérifier la synchronisation !\n")
    
    # Attendre l'exécution + marge
    time.sleep(delay_seconds + 2)
    
    # Éteindre tous les relais
    print(f"\n📤 Extinction des relais...\n")
    for device in ALL_DEVICES:
        topic = f"{device}/control/{relay_name}/set"
        payload_data = {"state": 0}  # OFF
        payload = json.dumps(payload_data)
        client.publish(topic, payload, qos=1)
        print(f"  ✓ {device} éteint")
    
    print(f"\n{'='*60}")
    print(f"✓ Test de synchronisation terminé")
    print(f"{'='*60}\n")

def send_serial_message_via_mqtt(client, message):
    """Envoie un message à l'ESP32 pour qu'il le transmette sur son port série."""
    if not client.is_connected():
        print("⚠ Client MQTT non connecté")
        return

    topic = f"{DEVICE_NAME}/serial/send"
    payload = json.dumps({"message": message})
    
    result = client.publish(topic, payload, qos=1)
    
    if result.rc == mqtt.MQTT_ERR_SUCCESS:
        print(f"✓ Message série envoyé via MQTT: \"{message}\"")
    else:
        print(f"✗ Erreur lors de l'envoi du message série via MQTT")


# ========== MENU INTERACTIF ==========

def show_menu():
    """Affiche le menu des commandes"""
    print("\n" + "="*50)
    print("COMMANDES DISPONIBLES:")
    print("="*50)
    for i, relay in enumerate(RELAY_NAMES, 1):
        print(f"{i}. Activer {relay}")
        print(f"{i+len(RELAY_NAMES)}. Désactiver {relay}")
    
    offset = len(RELAY_NAMES) * 2
    print(f"{offset+1}. Toggle tous les relais")
    print(f"{offset+2}. Test séquentiel")
    print(f"{offset+3}. Activer {RELAY_NAMES[0]} dans 5 secondes")
    print(f"{offset+4}. Publier timestamp maintenant")
    print(f"{offset+5}. Mesurer la compensation réseau")
    print(f"{offset+6}. 🎬 TEST SYNC MULTI-ESP32 (laser + lilygo)")
    print(f"{offset+7}. 🔄 Changer de device")
    print(f"{offset+8}. Envoyer message série à l'ESP32")
    print("0. Quitter")
    print("="*50)

def test_sequence(client):
    """Test séquentiel de tous les relais"""
    print("\n🧪 Début du test séquentiel...")
    for relay in RELAY_NAMES:
        print(f"\n→ Test de {relay}")
        toggle_relay(client, relay, delay=1.5)
        time.sleep(0.5)
    print("\n✓ Test séquentiel terminé")

def toggle_all(client):
    """Active puis désactive tous les relais"""
    print("\n🔄 Activation de tous les relais...")
    for relay in RELAY_NAMES:
        turn_on(client, relay)
        time.sleep(0.2)
    
    time.sleep(2)
    
    print("\n🔄 Désactivation de tous les relais...")
    for relay in RELAY_NAMES:
        turn_off(client, relay)
        time.sleep(0.2)

def publish_time_now(client):
    """Publie le timestamp immédiatement avec précision microseconde"""
    if client.is_connected():
        current_time = time.time()
        seconds = int(current_time)
        microseconds = int((current_time - seconds) * 1000000)
        
        payload = json.dumps({
            "seconds": seconds,
            "us": microseconds
        })
        
        topic = "esp32/time/sync"
        client.publish(topic, payload, qos=1)
        
        time_str = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(seconds))
        print(f"\u23f0 Timestamp publié manuellement: {seconds}.{microseconds:06d} ({time_str}.{microseconds:06d})")
    else:
        print("⚠ Client MQTT non connecté")

def measure_compensation(client):
    """Mesure la compensation réseau en envoyant plusieurs pings"""
    if not client.is_connected():
        print("⚠ Client MQTT non connecté")
        return
    
    print(f"\n🔬 Mesure de la compensation réseau pour [{DEVICE_NAME}]...")
    print("Envoi de 10 pings espacés pour mesurer la latence réseau...\n")
    
    # Obtenir le tracker pour ce device
    dev_latency = get_device_latency(DEVICE_NAME)
    
    # Nettoyer les anciens échantillons pour une mesure propre
    old_samples = dev_latency['samples'].copy()
    dev_latency['samples'].clear()
    
    # Envoyer 10 pings avec espacement suffisant
    for i in range(10):
        ping_id = f"measure_{int(time.time() * 1000000)}_{i}"
        ping_tracker['ping_times'][ping_id] = (time.time(), DEVICE_NAME)
        client.publish(f"{DEVICE_NAME}/ping", ping_id)
        print(f"  Ping {i+1}/10...", end='', flush=True)
        time.sleep(0.3)  # 300ms entre chaque ping pour éviter la congestion
        # La réponse s'affichera sur la même ligne grâce au callback
    
    # Attendre les dernières réponses
    print("\n\nAttente des dernières réponses...")
    time.sleep(1.5)
    
    # Analyser les résultats
    if len(dev_latency['samples']) > 0:
        rtts_ms = [rtt / 1000.0 for rtt in dev_latency['samples']]
        
        # Trier pour calculer les percentiles
        rtts_sorted = sorted(rtts_ms)
        n = len(rtts_sorted)
        median_rtt = rtts_sorted[n//2]
        p25_rtt = rtts_sorted[n//4]
        p75_rtt = rtts_sorted[(3*n)//4]
        
        # Filtrer les outliers (> 3x la médiane)
        rtts_filtered = [rtt for rtt in rtts_ms if rtt < median_rtt * 3]
        
        if len(rtts_filtered) == 0:
            rtts_filtered = rtts_ms  # Fallback si tous filtrés
        
        avg_rtt = sum(rtts_filtered) / len(rtts_filtered)
        min_rtt = min(rtts_filtered)
        max_rtt = max(rtts_filtered)
        jitter = max_rtt - min_rtt
        latency_ms = avg_rtt / 2
        
        # Calculer l'écart-type pour voir la stabilité
        variance = sum((rtt - avg_rtt)**2 for rtt in rtts_filtered) / len(rtts_filtered)
        std_dev = variance ** 0.5
        
        print(f"\n{'='*55}")
        print(f"📊 RÉSULTATS DE MESURE ({len(rtts_filtered)}/{len(rtts_ms)} échantillons)")
        print(f"{'='*55}")
        print(f"  RTT moyen:         {avg_rtt:.2f} ms")
        print(f"  RTT médiane:       {median_rtt:.2f} ms")
        print(f"  RTT min/max:       {min_rtt:.2f} / {max_rtt:.2f} ms")
        print(f"  Percentile 25/75:  {p25_rtt:.2f} / {p75_rtt:.2f} ms")
        print(f"  Jitter (max-min):  {jitter:.2f} ms")
        print(f"  Écart-type:        {std_dev:.2f} ms")
        print(f"  Latence unidirect: {latency_ms:.2f} ms")
        
        if len(rtts_filtered) < len(rtts_ms):
            outliers = len(rtts_ms) - len(rtts_filtered)
            outlier_values = [rtt for rtt in rtts_ms if rtt >= median_rtt * 3]
            print(f"\n  ⚠️  {outliers} outlier(s) ignoré(s): {', '.join(f'{v:.1f}ms' for v in outlier_values)}")
        
        print(f"\n🎯 Incertitude de synchronisation: ±{avg_rtt:.1f} ms")
        print(f"{'='*55}")
        
        # Évaluation basée sur le RTT complet, pas la latence
        if avg_rtt < 5:
            quality = "✅ EXCELLENTE"
            desc = "Précision sub-5ms - idéal pour synchronisation temps réel"
        elif avg_rtt < 15:
            quality = "✅ TRÈS BONNE"
            desc = "Précision ~15ms - bon pour la plupart des applications"
        elif avg_rtt < 30:
            quality = "✓ BONNE"
            desc = "Précision ~30ms - acceptable"
        elif avg_rtt < 50:
            quality = "⚠️  MOYENNE"
            desc = "Précision ~50ms - vérifier la qualité WiFi"
        else:
            quality = "❌ FAIBLE"
            desc = "Précision >50ms - problème réseau probable"
        
        print(f"  {quality}")
        print(f"  {desc}")
        
        # Avertissement si jitter ou écart-type élevé
        if std_dev > avg_rtt * 0.4:
            print(f"  ⚠️  Écart-type élevé ({std_dev:.1f}ms) - réseau instable")
        elif jitter > avg_rtt * 0.6:
            print(f"  ⚠️  Jitter élevé ({jitter:.1f}ms) - latence variable")
        
        print(f"{'='*55}\n")
        
        # Restaurer les échantillons (combiner ancien et nouveau pour moyenne mobile)
        dev_latency['samples'] = old_samples + dev_latency['samples']
        if len(dev_latency['samples']) > MAX_SAMPLES:
            dev_latency['samples'] = dev_latency['samples'][-MAX_SAMPLES:]
    else:
        print("\n❌ Aucune réponse reçue. Vérifiez la connexion MQTT.")
        # Restaurer les anciens échantillons
        dev_latency['samples'] = old_samples

def switch_device(client):
    """Permet de changer le device actuellement contrôlé"""
    global DEVICE_NAME
    
    print(f"\n{'='*60}")
    print("CHANGEMENT DE DEVICE")
    print(f"{'='*60}")
    print(f"Device actuel: {DEVICE_NAME}")
    print(f"Devices disponibles: {', '.join(ALL_DEVICES)}")
    
    new_device = input("\nNom du nouveau device: ").strip()
    
    if not new_device:
        print("❌ Nom vide, annulation")
        return
    
    old_device = DEVICE_NAME
    DEVICE_NAME = new_device
    
    # Se désabonner des anciens topics
    client.unsubscribe(f"{old_device}/status/#")
    client.unsubscribe(f"{old_device}/availability")
    client.unsubscribe(f"{old_device}/pong")
    
    # S'abonner aux nouveaux topics
    client.subscribe(f"{DEVICE_NAME}/status/#")
    client.subscribe(f"{DEVICE_NAME}/availability")
    client.subscribe(f"{DEVICE_NAME}/pong")
    
    print(f"\n✓ Device changé: {old_device} → {DEVICE_NAME}")
    print(f"✓ Abonné aux nouveaux topics de {DEVICE_NAME}")
    
    # Initialiser le tracker de latence pour ce device si nécessaire
    get_device_latency(DEVICE_NAME)

def publish_time(client):
    """Publie le timestamp actuel avec précision microseconde et mesure la latence"""
    while True:
        if client.is_connected():
            current_loop_time = time.time()
            
            # Mesurer la latence toutes les 30 secondes pour chaque device connu
            for device_name in list(device_latencies.keys()):
                dev_latency = device_latencies[device_name]
                if current_loop_time - dev_latency['last_measurement'] > 30:
                    # Envoyer un ping pour mesurer la latence
                    ping_id = f"{device_name}_{int(current_loop_time * 1000000)}"
                    ping_tracker['ping_times'][ping_id] = (current_loop_time, device_name)
                    ping_topic = f"{device_name}/ping"
                    client.publish(ping_topic, ping_id)
                    dev_latency['last_measurement'] = current_loop_time
            
            # Obtenir le temps avec microsecondes
            current_time = time.time()
            seconds = int(current_time)
            microseconds = int((current_time - seconds) * 1000000)
            
            # Créer le payload JSON avec microsecondes + compensations par device
            payload_data = {
                "seconds": seconds,
                "us": microseconds,
                "compensations": {}  # device_name -> latency_us
            }
            
            # Ajouter la compensation pour chaque device
            for device_name, dev_latency in device_latencies.items():
                if dev_latency['avg_latency_us'] > 0:
                    payload_data["compensations"][device_name] = dev_latency['avg_latency_us']
            
            payload = json.dumps(payload_data)
            
            topic = "esp32/time/sync"  # Topic commun à tous les ESP32
            client.publish(topic, payload, qos=1)  # QoS 1 pour garantir la livraison
            
            time_str = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(seconds))
            
            if TIME_MESSAGE_DISPLAY:
                # Afficher avec info de latence si disponible
                if device_latencies:
                    compensations_str = ", ".join([f"{dev}:{lat['avg_latency_us']/1000:.1f}ms" 
                                                for dev, lat in device_latencies.items() 
                                                if lat['avg_latency_us'] > 0])
                    if compensations_str:
                        print(f"⏰ Sync: {seconds}.{microseconds:06d} | Compensations: [{compensations_str}]")
                    else:
                        print(f"⏰ Sync: {seconds}.{microseconds:06d} (mesure latence en cours...)")
                else:
                    print(f"⏰ Sync: {seconds}.{microseconds:06d} (aucun device détecté)")
        
        time.sleep(10)  # Synchroniser toutes les 10 secondes

def restart_mosquitto():
    """Redémarre le service Mosquitto pour s'assurer qu'il est bien lancé."""
    if platform.system() == "Windows":
        print("\n🔄 Tentative de redémarrage du service Mosquitto sur Windows...")
        try:
            # Arrêter le service
            result_stop = os.system("net stop mosquitto > nul 2>&1")
            if result_stop == 0:
                print("   - Service Mosquitto arrêté.")
            
            time.sleep(2) # Attendre un peu

            # Démarrer le service
            result_start = os.system("net start mosquitto > nul 2>&1")
            if result_start == 0:
                print("   - Service Mosquitto démarré.")
                print("✓ Le service Mosquitto semble avoir redémarré avec succès.")
                time.sleep(3) # Laisse le temps au broker de s'initialiser
                return True
            else:
                print("✗ Impossible de démarrer le service Mosquitto.")
                print("  -> Assurez-vous que le script est exécuté avec les droits d'administrateur.")
                return False

        except Exception as e:
            print(f"✗ Une erreur est survenue lors de la tentative de redémarrage: {e}")
            return False
    else:
        # Pour info, si le script est utilisé sur un autre OS
        print("\nℹ️  Le redémarrage automatique de Mosquitto n'est implémenté que pour Windows.")
        return True

def check_and_restart_mosquitto(host, port):
    """Vérifie si le broker est accessible, sinon tente de le redémarrer."""
    if platform.system() != "Windows":
        print("\nℹ️  La vérification/redémarrage de Mosquitto n'est implémenté que pour Windows.")
        return

    print(f"\n🔍 Vérification du broker à l'adresse {host}:{port}...")
    try:
        with socket.create_connection((host, port), timeout=2):
            print("✓ Le broker MQTT est accessible.")
            return
    except (ConnectionRefusedError, socket.timeout, OSError):
        print("✗ Le broker MQTT ne répond pas. Tentative de redémarrage...")
        if not restart_mosquitto():
            input("\nAppuyez sur Entrée pour continuer malgré l'échec du redémarrage...")
        else:
            # Petite pause pour laisser le temps au broker de s'initialiser complètement
            time.sleep(3)

# ========== PROGRAMME PRINCIPAL ========== 
def main():
    """Fonction principale"""
    global DEVICE_NAME
    
    print("="*60)
    print("ESP32 IO Controller - Script de Test MQTT")
    print("="*60)
    
    local_ip = get_local_ip()
    
    # Vérifier si le broker est en ligne, sinon le redémarrer
    check_and_restart_mosquitto(local_ip, MQTT_PORT)

    print(f"\n✅ L'adresse IP de ce PC est: {local_ip}")
    
    # Demander le nom du device à contrôler
    print("\n" + "="*60)
    print("SÉLECTION DE L'APPAREIL")
    print("="*60)
    device_input = input(f"Nom de l'appareil ESP32 (par défaut: '{DEVICE_NAME}'): ").strip()
    if device_input:
        DEVICE_NAME = device_input
        print(f"✓ Appareil sélectionné: {DEVICE_NAME}")
    else:
        print(f"✓ Utilisation de l'appareil par défaut: {DEVICE_NAME}")
    
    print("\n" + "="*60)
    print("📋 CONFIGURATION REQUISE POUR L'ESP32")
    print("="*60)
    print("Assurez-vous que votre ESP32 est configuré avec les paramètres suivants:")
    print(f"  - MQTT Server: \"{local_ip}\"")
    print(f"  - MQTT Port:   {MQTT_PORT}")
    print(f"  - Nom appareil: \"{DEVICE_NAME}\"") 
    print(f"\n(Votre ESP32 doit être sur le même réseau Wi-Fi que ce PC)")
    print("="*60)
    
    broker_address = local_ip
   
    
    try:
        print(f"\n🔗 Tentative de connexion au broker: {broker_address}:{MQTT_PORT}...")
        
        # Créer le client MQTT
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="ESP32_Test_Client")
        
        # Configurer les callbacks
        client.on_connect = on_connect
        client.on_message = on_message
        client.on_disconnect = on_disconnect
        client.on_publish = on_publish
        
        # Connexion au broker
        client.connect(broker_address, MQTT_PORT, 60)
        
        # Démarrer la boucle réseau en arrière-plan
        client.loop_start()
        
        # Démarrer le thread pour la publication de l'heure
        time_thread = threading.Thread(target=publish_time, args=(client,), daemon=True)
        time_thread.start()

        # Attendre que la connexion soit établie
        time.sleep(1)
        
        # Menu interactif
        while True:
            show_menu()
            try:
                choice = input("\nChoisissez une option: ").strip()
                
                if choice == "0":
                    print("\n👋 Au revoir!")
                    break
                
                choice = int(choice)
                num_relays = len(RELAY_NAMES)
                
                # Activer un relais
                if 1 <= choice <= num_relays:
                    turn_on(client, RELAY_NAMES[choice-1])
                
                # Désactiver un relais
                elif num_relays+1 <= choice <= num_relays*2:
                    turn_off(client, RELAY_NAMES[choice-num_relays-1])
                
                # Toggle tous
                elif choice == num_relays*2 + 1:
                    toggle_all(client)
                
                # Test séquentiel
                elif choice == num_relays*2 + 2:
                    test_sequence(client)

                # Commande programmée
                elif choice == num_relays*2 + 3:
                    schedule_toggle(client, RELAY_NAMES[0], delay_seconds=5)
                
                # Publier timestamp maintenant
                elif choice == num_relays*2 + 4:
                    publish_time_now(client)
                
                # Mesurer la compensation réseau
                elif choice == num_relays*2 + 5:
                    measure_compensation(client)
                
                # Test de synchronisation multi-ESP32
                elif choice == num_relays*2 + 6:
                    synchronized_toggle_all_devices(client)
                
                # Changer de device
                elif choice == num_relays*2 + 7:
                    switch_device(client)
                # Envoyer message série à l'ESP32
                elif choice == num_relays*2 + 8:
                    msg = input("Message à envoyer via Serial2: ").strip()
                    if msg:
                        send_serial_message_via_mqtt(client, msg)
                
                else:
                    print("❌ Option invalide")
                
                time.sleep(0.3)
                
            except ValueError:
                print("❌ Veuillez entrer un nombre")
            except KeyboardInterrupt:
                print("\n\n👋 Interruption utilisateur")
                break
    
    except ConnectionRefusedError:
        print(f"\n❌ IMPOSSIBLE DE SE CONNECTER AU BROKER {broker_address}:{MQTT_PORT}")
        print("\n💡 Solutions:")
        print("   1. Assurez-vous que Mosquitto est bien démarré sur ce PC.")
        print("   2. Vérifiez que votre pare-feu ne bloque pas le port 1883.")
        print("   3. Essayez de redémarrer Mosquitto.")
    except Exception as e:
        print(f"❌ Une erreur inattendue est survenue: {e}")
    
    finally:
        # Nettoyer et fermer la connexion
        print("\nFermeture de la connexion...")
        try:
            client.loop_stop()
            client.disconnect()
        except:
            pass

if __name__ == "__main__":
    main()
