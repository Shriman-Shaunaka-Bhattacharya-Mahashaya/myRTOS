#!/bin/bash
set -e

echo "=========================================================="
echo " Starting myRTOS Browser-Based Simulation Environment"
echo "=========================================================="

export DISPLAY=:0

# 1. Start Xvfb (Virtual In-Memory Framebuffer Screen)
echo "[1/4] Starting Xvfb virtual display on :0 (1280x820)..."
Xvfb :0 -screen 0 1280x820x24 -ac &
sleep 1

# 2. Start minimal window manager (Openbox)
echo "[2/4] Starting window manager..."
openbox &
sleep 1

# 3. Start x11vnc server capturing the virtual screen
echo "[3/4] Starting x11vnc VNC server on port 5900..."
x11vnc -display :0 -forever -nopw -shared -rfbport 5900 -quiet &
sleep 1

# 4. Start noVNC WebSockets proxy (streams screen to web browser)
echo "[4/4] Starting noVNC browser proxy on http://localhost:8080/vnc.html..."
websockify --web /usr/share/novnc 8080 localhost:5900 &
sleep 1

echo "=========================================================="
echo " myRTOS Interactive Dashboard is LIVE!"
echo " Open your browser to: http://localhost:8080/vnc.html"
echo "=========================================================="

# Launch the Dear ImGui simulation dashboard
exec /app/build/myrtos_dashboard
