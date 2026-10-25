#!/bin/bash
set -euo pipefail
export DISPLAY=:99
children=()
cleanup() {
    trap - EXIT INT TERM
    if ((${#children[@]})); then
        kill "${children[@]}" 2>/dev/null || true
        wait "${children[@]}" 2>/dev/null || true
    fi
}
trap cleanup EXIT
trap 'exit 0' INT TERM
rm -f /tmp/.X99-lock /tmp/.X11-unix/X99
Xvfb :99 -screen 0 1280x820x24 -ac +extension GLX +render -noreset &
children+=("$!")
for ((attempt=0;attempt<100;attempt++)); do
    [[ -S /tmp/.X11-unix/X99 ]] && break
    kill -0 "${children[0]}" 2>/dev/null || { echo 'Virtual display failed'; exit 1; }
    sleep 0.1
done
[[ -S /tmp/.X11-unix/X99 ]] || { echo 'Virtual display startup timed out'; exit 1; }
openbox &
children+=("$!")
x11vnc -display :99 -forever -nopw -shared -localhost -rfbport 5900 -quiet &
children+=("$!")
# Wait for VNC before offering a browser connection.
for ((attempt=0;attempt<100;attempt++)); do
    if (echo > /dev/tcp/127.0.0.1/5900) 2>/dev/null; then break; fi
    sleep 0.1
done
websockify --heartbeat=30 --web /app/web 8080 localhost:5900 &
children+=("$!")
/app/build/myrtos_dashboard "$@" &
children+=("$!")
echo 'myRTOS: http://localhost:8080/'
# A failed dependency ends the container instead of leaving a frozen browser page.
wait -n "${children[@]}"
