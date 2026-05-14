#!/bin/sh
# Post-install hook run by `meson install`.
# Removes stale wayland-kbdstate copies left by an earlier /usr/local install,
# so the panel doesn't load a shadowed module.
set -eu

# Staging into a package (DESTDIR build): no system-wide side effects.
if [ -n "${DESTDIR:-}" ]; then
    exit 0
fi

prefix="${MESON_INSTALL_PREFIX:-/usr}"

# Never clean the very prefix we just installed into.
if [ "$prefix" = "/usr/local" ]; then
    echo "wayland-kbdstate: installed under $prefix"
    exit 0
fi

removed=0
drop() {
    if [ -e "$1" ]; then
        rm -rf "$1"
        removed=1
    fi
}

drop /usr/local/share/xfce4/panel/plugins/wayland-kbdstate.desktop
drop /usr/local/share/xfce4/panel/plugins/wayland-kbdstate
# libdir varies across distros (lib, lib64, lib/x86_64-linux-gnu, ...).
for so in /usr/local/lib/xfce4/panel/plugins/libwayland-kbdstate.so \
          /usr/local/lib/*/xfce4/panel/plugins/libwayland-kbdstate.so; do
    drop "$so"
done

if [ "$removed" -eq 1 ]; then
    echo "wayland-kbdstate: removed stale copies from /usr/local"
fi

echo "wayland-kbdstate: installed under $prefix"
echo "wayland-kbdstate: restart the panel with -> meson compile -C build restart-panel"
echo "wayland-kbdstate:                    (or -> ninja -C build restart-panel)"
