# SPDX-FileCopyrightText: 2026 codestlover
# SPDX-License-Identifier: GPL-2.0-or-later

project(
  'xfce4-wayland-kbdstate-plugin',
  ['c', 'cpp'],
  version: '0.9.0',
  license: 'GPL-2.0-or-later',
  meson_version: '>=0.56.0',
  default_options: [
    'c_std=c11',
    'cpp_std=c++20',
    'warning_level=2',
  ],
)

gtk = dependency('gtk+-3.0', required: true)
xfce_panel = dependency('libxfce4panel-2.0', required: true)
wayland_client = dependency('wayland-client', required: true)
xkbcommon = dependency('xkbcommon', required: true)

plugin_name = 'wayland-kbdstate'
plugin_module_dir = join_paths(get_option('libdir'), 'xfce4', 'panel', 'plugins')
plugin_desktop_dir = join_paths(get_option('datadir'), 'xfce4', 'panel', 'plugins')
plugin_data_dir = join_paths(get_option('datadir'), 'xfce4', 'panel', 'plugins', plugin_name)
plugin_flags_dir = join_paths(plugin_data_dir, 'flags')

shared_module(
  plugin_name,
  [
    'src/wayland-kbdstate.cpp',
    'src/plugin-entry.c',
  ],
  dependencies: [gtk, xfce_panel, wayland_client, xkbcommon],
  cpp_args: [
    '-DFLAGS_DIR="' + plugin_flags_dir + '"',
    '-DSTYLE_DIR="' + plugin_data_dir + '"',
  ],
  name_prefix: 'lib',
  install: true,
  install_dir: plugin_module_dir,
)

install_data(
  'flags/us.svg', 'flags/us.png',
  'flags/gb.svg', 'flags/gb.png',
  'flags/ru.svg', 'flags/ru.png',
  'flags/ua.svg', 'flags/ua.png',
  'flags/de.svg', 'flags/de.png',
  'flags/fr.svg', 'flags/fr.png',
  'flags/es.svg', 'flags/es.png',
  'flags/it.svg', 'flags/it.png',
  'flags/pl.svg', 'flags/pl.png',
  'flags/cz.svg', 'flags/cz.png',
  'flags/tr.svg', 'flags/tr.png',
  'flags/nl.svg', 'flags/nl.png',
  install_dir: plugin_flags_dir,
)

install_data(
  'src/wayland-kbdstate.css',
  install_dir: plugin_data_dir,
)

configure_file(
  input: 'wayland-kbdstate.desktop.in',
  output: 'wayland-kbdstate.desktop',
  configuration: {
    'PLUGIN_NAME': plugin_name,
    'PLUGIN_MODULE_DIR': plugin_module_dir,
  },
  install: true,
  install_dir: plugin_desktop_dir,
)

# Universal, meson-driven install. `meson install` also clears stale /usr/local
# copies via the post-install hook below; `meson compile -C build restart-panel`
# (or `ninja -C build restart-panel`) relaunches the panel as a Wayland client.
meson.add_install_script('scripts/meson-postinstall.sh')

run_target(
  'restart-panel',
  command: [find_program('sh'), meson.current_source_dir() / 'scripts' / 'restart-panel.sh'],
)
