// SPDX-FileCopyrightText: 2026 codestlover
// SPDX-License-Identifier: GPL-2.0-or-later

#include <libxfce4panel/libxfce4panel.h>

extern void wayland_kbdstate_construct(XfcePanelPlugin* plugin);

XFCE_PANEL_PLUGIN_REGISTER(wayland_kbdstate_construct)
