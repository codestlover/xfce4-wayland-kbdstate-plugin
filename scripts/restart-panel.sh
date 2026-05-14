#!/bin/sh
# Restart xfce4-panel as a native Wayland client so the plugin can talk to the
# compositor. Run as your own user (no sudo), inside the Wayland session.
set -eu

if [ -z "${WAYLAND_DISPLAY:-}" ]; then
    echo "restart-panel: WAYLAND_DISPLAY is not set -- refusing to restart the panel" >&2
    echo "restart-panel: outside a Wayland session. Run this from your Wayland desktop." >&2
    exit 1
fi

if ! command -v xfce4-panel >/dev/null 2>&1; then
    echo "restart-panel: xfce4-panel not found in PATH" >&2
    exit 1
fi

xfce4-panel -q >/dev/null 2>&1 || true
sleep 1
setsid -f env GDK_BACKEND=wayland xfce4-panel >/dev/null 2>&1

echo "restart-panel: xfce4-panel relaunched with GDK_BACKEND=wayland"
