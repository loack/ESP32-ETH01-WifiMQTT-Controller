#!/bin/bash
# ESP32 Relay Control Examples

echo "🔌 ESP32 Relay Control Test Script"
echo "=================================="

# Make sure the script is executable
chmod +x relay_control.py

echo "📊 Getting current status..."
python3 relay_control.py status

echo ""
echo "🔥 Testing Relay 1..."
echo "Turning relay 1 ON..."
python3 relay_control.py 1 start
sleep 2

echo "Turning relay 1 OFF..."
python3 relay_control.py 1 stop
sleep 2

echo ""
echo "⚡ Testing Relay 2..."
echo "Turning relay 2 ON..."
python3 relay_control.py 2 start
sleep 2

echo "Turning relay 2 OFF..."
python3 relay_control.py 2 stop

echo ""
echo "📊 Final status..."
python3 relay_control.py status

echo ""
echo "✅ Test completed!"
