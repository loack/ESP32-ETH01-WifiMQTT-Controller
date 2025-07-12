#!/usr/bin/env python3
"""
ESP32 Relay Control Script
Simple Python script to control relays via HTTP API
"""

import requests
import sys
import time

# ESP32 Configuration
ESP32_IP = "192.168.1.11"
ESP32_PORT = 80
BASE_URL = f"http://{ESP32_IP}:{ESP32_PORT}"

def control_relay(relay_num, action):
    """
    Control a specific relay
    
    Args:
        relay_num (int): Relay number (1-4)
        action (str): 'start' or 'stop'
    
    Returns:
        bool: True if successful, False otherwise
    """
    if relay_num not in [1, 2, 3, 4]:
        print(f"Error: Relay number must be 1-4, got {relay_num}")
        return False
    
    if action.lower() not in ['start', 'stop']:
        print(f"Error: Action must be 'start' or 'stop', got '{action}'")
        return False
    
    url = f"{BASE_URL}/relay{relay_num}/{action.lower()}"
    
    try:
        print(f"Sending request to: {url}")
        response = requests.post(url, timeout=5)
        
        if response.status_code == 200:
            print(f"✅ Success: {response.text}")
            return True
        else:
            print(f"❌ Error: HTTP {response.status_code}")
            print(f"Response: {response.text}")
            return False
            
    except requests.exceptions.ConnectionError:
        print(f"❌ Error: Cannot connect to ESP32 at {ESP32_IP}")
        print("Check if:")
        print("  - ESP32 is powered on")
        print("  - Ethernet cable is connected")
        print("  - IP address is correct")
        return False
        
    except requests.exceptions.Timeout:
        print(f"❌ Error: Request timeout")
        return False
        
    except Exception as e:
        print(f"❌ Error: {e}")
        return False

def get_relay_status():
    """
    Get current status of all relays
    
    Returns:
        dict: Relay status or None if error
    """
    url = f"{BASE_URL}/status"
    
    try:
        print(f"Getting status from: {url}")
        response = requests.get(url, timeout=5)
        
        if response.status_code == 200:
            status = response.json()
            print("📊 Current Relay Status:")
            for i in range(1, 5):
                state = "ON" if status[f"relay{i}"] else "OFF"
                print(f"  Relay {i}: {state}")
            return status
        else:
            print(f"❌ Error: HTTP {response.status_code}")
            return None
            
    except Exception as e:
        print(f"❌ Error getting status: {e}")
        return None

def print_usage():
    """Print usage instructions"""
    print("\n🔌 ESP32 Relay Control Script")
    print("=" * 40)
    print("Usage:")
    print("  python relay_control.py <relay_num> <action>")
    print("  python relay_control.py status")
    print("")
    print("Arguments:")
    print("  relay_num: 1, 2, 3, or 4")
    print("  action:    start or stop")
    print("")
    print("Examples:")
    print("  python relay_control.py 1 start    # Turn ON relay 1")
    print("  python relay_control.py 2 stop     # Turn OFF relay 2")
    print("  python relay_control.py status     # Show all relay states")
    print("")
    print(f"ESP32 IP: {ESP32_IP}")
    print("")

def main():
    """Main function"""
    if len(sys.argv) == 1 or sys.argv[1] in ['-h', '--help', 'help']:
        print_usage()
        return
    
    # Check for status command
    if len(sys.argv) == 2 and sys.argv[1].lower() == 'status':
        get_relay_status()
        return
    
    # Check for relay control command
    if len(sys.argv) != 3:
        print("❌ Error: Wrong number of arguments")
        print_usage()
        return
    
    try:
        relay_num = int(sys.argv[1])
        action = sys.argv[2].lower()
        
        print(f"🎯 Target: Relay {relay_num} -> {action.upper()}")
        
        if control_relay(relay_num, action):
            print("✨ Command completed successfully!")
            
            # Show updated status after a short delay
            time.sleep(0.5)
            print("\n" + "="*30)
            get_relay_status()
        else:
            print("💥 Command failed!")
            sys.exit(1)
            
    except ValueError:
        print("❌ Error: Relay number must be an integer")
        print_usage()
        sys.exit(1)

if __name__ == "__main__":
    main()
