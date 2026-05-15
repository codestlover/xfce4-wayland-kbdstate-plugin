# xfce4-wayland-kbdstate-plugin

An xfce4-panel plugin that shows the current keyboard layout as a flag, built for
native Wayland.

The stock `xfce4-xkb-plugin` relies on X11 / libxklavier and does nothing under a
native Wayland session. This plugin instead reads keyboard state directly through
the Wayland `wl_seat` / `wl_keyboard` protocol and `xkbcommon`.

## Compatibility

The plugin uses only the core Wayland protocol (`wl_seat` / `wl_keyboard`) and
`xkbcommon` — nothing compositor-specific — so it is not tied to any particular
Wayland compositor.

The panel itself must run as a native Wayland client (`GDK_BACKEND=wayland`).

Testing status across Wayland compositors that host `xfce4-panel`:

| Compositor   | Family              | Status      |
| ------------ | ------------------- | ----------- |
| **labwc**    | wlroots · stacking  | ✅ Tested    |
| Wayfire      | wlroots · stacking  | ❔ Untested  |
| Hikari       | wlroots · stacking  | ❔ Untested  |
| Sway         | wlroots · tiling    | ❔ Untested  |
| Hyprland     | wlroots · tiling    | ❔ Untested  |
| River        | wlroots · tiling    | ❔ Untested  |
| niri         | wlroots · tiling    | ❔ Untested  |
| dwl          | wlroots · tiling    | ❔ Untested  |
| Cage         | wlroots · kiosk     | ❔ Untested  |
| cosmic-comp  | Smithay             | ❔ Untested  |

All listed compositors implement `wlr-layer-shell`, which `xfce4-panel` uses to
dock. GNOME (Mutter) is not listed because it does not implement layer-shell;
full desktops like KDE Plasma normally use their own panel. Reports for any
untested compositor are welcome.

## Features

- Shows the active keyboard layout as a small flag icon, with a text fallback for
  layouts that have no bundled flag.
- Neon `#ff4d55` glow around the flag while Caps Lock is on.
- Written in C++20, no X11 dependency.
- Styling lives in a separate `wayland-kbdstate.css`, loaded at runtime — it can be
  tweaked without rebuilding the plugin.
- meson-driven install, including a helper target to relaunch the panel as a
  native Wayland client.
- Bundled flags: `us`, `gb`, `ru`, `ua`, `de`, `fr`, `es`, `it`, `pl`, `cz`, `tr`,
  `nl`.

## Requirements

A C++20 compiler, `meson`, `ninja`, `pkg-config`, and the development packages for
`gtk+-3.0`, `libxfce4panel-2.0`, `libxfce4ui-2`, `wayland-client` and `xkbcommon`.

**Debian / Ubuntu**

```bash
sudo apt install build-essential g++ meson ninja-build pkg-config \
  libgtk-3-dev libxfce4panel-2.0-dev libxfce4ui-2-dev \
  libwayland-dev libxkbcommon-dev
```

**Fedora**

```bash
sudo dnf install gcc-c++ meson ninja-build pkgconf-pkg-config \
  gtk3-devel xfce4-panel-devel libxfce4ui-devel \
  wayland-devel libxkbcommon-devel
```

**Arch Linux**

```bash
sudo pacman -S --needed base-devel meson ninja \
  gtk3 xfce4-panel libxfce4ui wayland libxkbcommon
```

**openSUSE**

```bash
sudo zypper install gcc-c++ meson ninja pkgconf-pkg-config \
  gtk3-devel xfce4-panel-devel libxfce4ui-devel \
  wayland-devel libxkbcommon-devel
```

On other distributions, install the equivalents of the packages listed above.

## Build & install

```bash
meson setup build --prefix=/usr
meson compile -C build
sudo meson install -C build
```

`meson install` installs the plugin module, the flag images, the stylesheet and
the `.desktop` file.

Then restart `xfce4-panel` as a native Wayland client (from inside your Wayland
session, **without** `sudo`):

```bash
xfce4-panel -q
setsid -f env GDK_BACKEND=wayland xfce4-panel >/dev/null 2>&1
```

If a `build/` directory is left over from an older checkout, recreate it:
`rm -rf build` (or `meson setup --wipe build`).

## Limitations

Click-to-switch from the panel is not implemented yet; it is planned for later and
would require a compositor-side helper.

## License

GPL-2.0-or-later — see [LICENSE](LICENSE). The bundled flag icons are trivial
geometric renderings of national flags and carry no separate licensing.
