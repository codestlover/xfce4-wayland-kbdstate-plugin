// SPDX-FileCopyrightText: 2026 codestlover
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtk/gtk.h>
#include <libxfce4panel/libxfce4panel.h>
#include <gdk/gdkwayland.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef FLAGS_DIR
#define FLAGS_DIR "/usr/share/xfce4/panel/plugins/wayland-kbdstate/flags"
#endif

#ifndef STYLE_DIR
#define STYLE_DIR "/usr/share/xfce4/panel/plugins/wayland-kbdstate"
#endif

namespace {

constexpr int kFlagHeightPx = 15;
constexpr int kFlagMinWidthPx = 24;
constexpr int kGlowInsetPx = 3;

class KbdStatePlugin final {
public:
    explicit KbdStatePlugin(XfcePanelPlugin* plugin)
        : plugin_(plugin) {}

    ~KbdStatePlugin() {
        freeWayland();
        destroyXkb();

        if (xkb_context_ != nullptr) {
            xkb_context_unref(xkb_context_);
            xkb_context_ = nullptr;
        }

        if (css_provider_ != nullptr) {
            g_object_unref(css_provider_);
            css_provider_ = nullptr;
        }
    }

    void constructUi() {
        effective_layout_ = XKB_LAYOUT_INVALID;

        box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_show(box_);

        status_frame_ = gtk_event_box_new();
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(status_frame_), TRUE);
        gtk_widget_set_halign(status_frame_, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(status_frame_, GTK_ALIGN_CENTER);
        gtk_widget_set_margin_start(status_frame_, 2);
        gtk_widget_set_margin_end(status_frame_, 2);
        gtk_widget_show(status_frame_);
        gtk_style_context_add_class(gtk_widget_get_style_context(status_frame_), "kbdstate-status-frame");

        status_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_box_set_spacing(GTK_BOX(status_box_), 0);
        gtk_widget_set_halign(status_box_, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(status_box_, GTK_ALIGN_CENTER);
        gtk_widget_show(status_box_);

        flag_frame_ = gtk_event_box_new();
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(flag_frame_), TRUE);
        gtk_widget_set_halign(flag_frame_, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(flag_frame_, GTK_ALIGN_CENTER);
        gtk_widget_show(flag_frame_);
        gtk_style_context_add_class(gtk_widget_get_style_context(flag_frame_), "kbdstate-flag-frame");

        flag_image_ = gtk_image_new();
        gtk_widget_set_size_request(flag_image_, kFlagMinWidthPx, kFlagHeightPx);
        gtk_widget_set_margin_start(flag_image_, kGlowInsetPx);
        gtk_widget_set_margin_end(flag_image_, kGlowInsetPx);
        gtk_widget_set_margin_top(flag_image_, kGlowInsetPx);
        gtk_widget_set_margin_bottom(flag_image_, kGlowInsetPx);
        gtk_widget_set_halign(flag_image_, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(flag_image_, GTK_ALIGN_CENTER);
        gtk_widget_show(flag_image_);
        gtk_container_add(GTK_CONTAINER(flag_frame_), flag_image_);

        g_signal_connect_after(flag_frame_, "draw", G_CALLBACK(&KbdStatePlugin::flagFrameDrawCb), this);

        fallback_label_ = gtk_label_new("--");
        gtk_widget_set_halign(fallback_label_, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(fallback_label_, GTK_ALIGN_CENTER);
        gtk_widget_show(fallback_label_);
        gtk_style_context_add_class(gtk_widget_get_style_context(fallback_label_), "kbdstate-fallback");

        gtk_box_pack_start(GTK_BOX(status_box_), fallback_label_, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(status_box_), flag_frame_, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(status_frame_), status_box_);
        gtk_box_pack_start(GTK_BOX(box_), status_frame_, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(plugin_), box_);

        xfce_panel_plugin_add_action_widget(plugin_, box_);
        xfce_panel_plugin_set_small(plugin_, TRUE);

        g_signal_connect(plugin_, "destroy", G_CALLBACK(&KbdStatePlugin::pluginDestroyCb), this);

        applyCss();
        updateView();
        initWayland();
        gtk_widget_show_all(GTK_WIDGET(plugin_));
        updateView();
    }

    static void construct(XfcePanelPlugin* plugin) {
        auto* self = new KbdStatePlugin(plugin);
        self->constructUi();
    }

private:
    static void pluginDestroyCb(GtkWidget*, gpointer user_data) {
        delete static_cast<KbdStatePlugin*>(user_data);
    }

    static void keyboardKeymap(void* data,
                               wl_keyboard*,
                               uint32_t format,
                               int fd,
                               uint32_t size) {
        auto* self = static_cast<KbdStatePlugin*>(data);
        self->onKeyboardKeymap(format, fd, size);
    }

    static void keyboardEnter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}
    static void keyboardLeave(void*, wl_keyboard*, uint32_t, wl_surface*) {}
    static void keyboardKey(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t) {}
    static void keyboardRepeatInfo(void*, wl_keyboard*, int32_t, int32_t) {}

    static void keyboardModifiers(void* data,
                                  wl_keyboard*,
                                  uint32_t,
                                  uint32_t mods_depressed,
                                  uint32_t mods_latched,
                                  uint32_t mods_locked,
                                  uint32_t group) {
        auto* self = static_cast<KbdStatePlugin*>(data);
        self->onKeyboardModifiers(mods_depressed, mods_latched, mods_locked, group);
    }

    static void seatCapabilities(void* data, wl_seat* seat, uint32_t capabilities) {
        auto* self = static_cast<KbdStatePlugin*>(data);
        self->onSeatCapabilities(seat, capabilities);
    }

    static void seatName(void*, wl_seat*, const char*) {}

    static void registryGlobal(void* data,
                               wl_registry* registry,
                               uint32_t name,
                               const char* interface,
                               uint32_t version) {
        auto* self = static_cast<KbdStatePlugin*>(data);
        self->onRegistryGlobal(registry, name, interface, version);
    }

    static void registryGlobalRemove(void*, wl_registry*, uint32_t) {}

    void onKeyboardKeymap(uint32_t format, int fd, uint32_t size) {
        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
            close(fd);
            setError("The compositor sent an unsupported keymap format.");
            return;
        }

        g_debug("wayland-kbdstate: wl_keyboard.keymap event (size=%u)", size);

        char* map = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
        close(fd);

        if (map == MAP_FAILED) {
            setError("Failed to mmap() the keymap.");
            return;
        }

        xkb_keymap* keymap = xkb_keymap_new_from_string(
            xkb_context_, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
        munmap(map, size);

        if (keymap == nullptr) {
            setError("xkbcommon failed to parse the keymap.");
            return;
        }

        xkb_state* state = xkb_state_new(keymap);
        if (state == nullptr) {
            xkb_keymap_unref(keymap);
            setError("xkbcommon failed to create an xkb_state.");
            return;
        }

        destroyXkb();
        xkb_keymap_ = keymap;
        xkb_state_ = state;
        have_keyboard_ = true;
        refreshFromXkb();
    }

    void onKeyboardModifiers(uint32_t mods_depressed,
                             uint32_t mods_latched,
                             uint32_t mods_locked,
                             uint32_t group) {
        if (xkb_state_ == nullptr) {
            return;
        }

        g_debug("wayland-kbdstate: wl_keyboard.modifiers event (group=%u)", group);

        xkb_state_update_mask(xkb_state_,
                              mods_depressed,
                              mods_latched,
                              mods_locked,
                              0,
                              0,
                              group);
        refreshFromXkb();
    }

    void onSeatCapabilities(wl_seat* seat, uint32_t capabilities) {
        const bool has_keyboard = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;

        if (has_keyboard && wl_keyboard_ == nullptr) {
            wl_keyboard_ = wl_seat_get_keyboard(seat);
            wl_keyboard_add_listener(wl_keyboard_, &keyboard_listener_, this);
            have_keyboard_ = true;
            return;
        }

        if (!has_keyboard && wl_keyboard_ != nullptr) {
            wl_keyboard_destroy(wl_keyboard_);
            wl_keyboard_ = nullptr;
            have_keyboard_ = false;
            destroyXkb();
            updateView();
        }
    }

    void onRegistryGlobal(wl_registry* registry,
                          uint32_t name,
                          const char* interface,
                          uint32_t version) {
        if (wl_seat_ != nullptr) {
            return;
        }

        if (g_strcmp0(interface, wl_seat_interface.name) == 0) {
            const uint32_t bind_version = std::min(version, static_cast<uint32_t>(5));
            wl_seat_ = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, bind_version));
            wl_seat_add_listener(wl_seat_, &seat_listener_, this);
        }
    }

    void applyCss() {
        static constexpr const char* kStyleFileName = "wayland-kbdstate.css";
        const std::array<const char*, 3> style_dirs = {
            STYLE_DIR,
            "/usr/share/xfce4/panel/plugins/wayland-kbdstate",
            "/usr/local/share/xfce4/panel/plugins/wayland-kbdstate",
        };

        css_provider_ = gtk_css_provider_new();

        bool loaded = false;
        for (const char* dir : style_dirs) {
            if (dir == nullptr || *dir == '\0') {
                continue;
            }

            const std::string path = std::string(dir) + "/" + kStyleFileName;
            if (!g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) {
                continue;
            }

            GError* error = nullptr;
            if (gtk_css_provider_load_from_path(css_provider_, path.c_str(), &error)) {
                loaded = true;
                break;
            }

            if (error != nullptr) {
                g_warning("wayland-kbdstate: failed to load CSS from %s: %s", path.c_str(), error->message);
                g_error_free(error);
            }
        }

        if (!loaded) {
            g_warning("wayland-kbdstate: no stylesheet found, using GTK theme defaults");
            g_object_unref(css_provider_);
            css_provider_ = nullptr;
            return;
        }

        gtk_style_context_add_provider_for_screen(
            gdk_screen_get_default(), GTK_STYLE_PROVIDER(css_provider_), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

    void setCapsVisual() {
        gtk_widget_queue_draw(flag_frame_);
    }

    static gboolean flagFrameDrawCb(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
        auto* self = static_cast<KbdStatePlugin*>(user_data);
        self->drawFlagGlow(widget, cr);
        return FALSE;
    }

    void drawFlagGlow(GtkWidget* widget, cairo_t* cr) {
        if (!caps_lock_ || current_flag_code_ == std::nullopt || !gtk_widget_get_visible(flag_image_)) {
            return;
        }

        GtkAllocation frame_alloc{};
        GtkAllocation image_alloc{};
        gtk_widget_get_allocation(widget, &frame_alloc);
        gtk_widget_get_allocation(flag_image_, &image_alloc);

        const double x = static_cast<double>(image_alloc.x) - 1.5;
        const double y = static_cast<double>(image_alloc.y) - 1.5;
        const double w = static_cast<double>(image_alloc.width) + 3.0;
        const double h = static_cast<double>(image_alloc.height) + 3.0;
        const double r = 3.5;

        cairo_save(cr);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

        auto rounded_rect = [&](double inset, double alpha, double width_px) {
            const double rx = x + inset;
            const double ry = y + inset;
            const double rw = w - inset * 2.0;
            const double rh = h - inset * 2.0;
            const double rr = std::max(1.5, r - inset * 0.4);

            if (rw <= 0.0 || rh <= 0.0) {
                return;
            }

            cairo_new_path(cr);
            cairo_arc(cr, rx + rw - rr, ry + rr, rr, -G_PI / 2.0, 0.0);
            cairo_arc(cr, rx + rw - rr, ry + rh - rr, rr, 0.0, G_PI / 2.0);
            cairo_arc(cr, rx + rr, ry + rh - rr, rr, G_PI / 2.0, G_PI);
            cairo_arc(cr, rx + rr, ry + rr, rr, G_PI, 3.0 * G_PI / 2.0);
            cairo_close_path(cr);
            cairo_set_source_rgba(cr, 1.0, 77.0 / 255.0, 85.0 / 255.0, alpha);
            cairo_set_line_width(cr, width_px);
            cairo_stroke(cr);
        };

        rounded_rect(-1.0, 0.16, 7.0);
        rounded_rect(0.0, 0.28, 4.2);
        rounded_rect(0.6, 0.52, 2.2);
        rounded_rect(1.0, 0.98, 1.05);

        cairo_restore(cr);
    }

    bool loadFlagFile(const char* path) {
        GError* raw_error = nullptr;
        GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file_at_scale(path, -1, kFlagHeightPx, TRUE, &raw_error);
        if (raw_error != nullptr) {
            g_error_free(raw_error);
        }
        if (pixbuf == nullptr) {
            return false;
        }

        int width = gdk_pixbuf_get_width(pixbuf);
        int height = gdk_pixbuf_get_height(pixbuf);
        if (width < kFlagMinWidthPx) {
            width = kFlagMinWidthPx;
        }
        if (height < kFlagHeightPx) {
            height = kFlagHeightPx;
        }

        gtk_widget_set_size_request(flag_image_, width, height);
        gtk_image_set_from_pixbuf(GTK_IMAGE(flag_image_), pixbuf);
        gtk_widget_queue_resize(flag_image_);
        g_object_unref(pixbuf);
        return true;
    }

    bool tryFlagDir(const char* dir, const char* flag_code) {
        if (dir == nullptr || *dir == '\0' || flag_code == nullptr || *flag_code == '\0') {
            return false;
        }

        const std::string base = std::string(dir) + "/" + flag_code;
        const std::string png_path = base + ".png";
        if (g_file_test(png_path.c_str(), G_FILE_TEST_EXISTS) && loadFlagFile(png_path.c_str())) {
            return true;
        }

        const std::string svg_path = base + ".svg";
        if (g_file_test(svg_path.c_str(), G_FILE_TEST_EXISTS) && loadFlagFile(svg_path.c_str())) {
            return true;
        }

        return false;
    }

    void showFlag(const char* flag_code, const char* fallback_text) {
        constexpr std::array<const char*, 4> dirs = {
            FLAGS_DIR,
            "/usr/share/xfce4/panel/plugins/wayland-kbdstate/flags",
            "/usr/local/share/xfce4/panel/plugins/wayland-kbdstate/flags",
            nullptr,
        };

        bool loaded = false;
        if (flag_code != nullptr) {
            for (const char* dir : dirs) {
                if (dir == nullptr || loaded) {
                    continue;
                }
                loaded = tryFlagDir(dir, flag_code);
            }

            if (loaded) {
                current_flag_code_ = flag_code;
                gtk_widget_show(flag_frame_);
                gtk_widget_show(flag_image_);
                gtk_widget_hide(fallback_label_);
                queueResizeAll();
                return;
            }
        }

        current_flag_code_.reset();
        gtk_widget_set_size_request(flag_image_, 1, 1);
        gtk_widget_hide(flag_frame_);
        gtk_widget_hide(flag_image_);
        gtk_label_set_text(GTK_LABEL(fallback_label_), fallback_text != nullptr ? fallback_text : "??");
        gtk_widget_show(fallback_label_);
        queueResizeAll();
    }

    void updateView() {
        if (!is_wayland_) {
            gtk_widget_hide(flag_frame_);
            gtk_widget_hide(flag_image_);
            gtk_label_set_text(GTK_LABEL(fallback_label_), "kbd?");
            gtk_widget_show(fallback_label_);
            setCapsVisual();
            gtk_widget_set_tooltip_text(GTK_WIDGET(plugin_),
                                        "xfce4-panel is not running as a native Wayland client.\n"
                                        "Restart the panel with GDK_BACKEND=wayland.");
            return;
        }

        if (error_message_.has_value()) {
            gtk_widget_hide(flag_frame_);
            gtk_widget_hide(flag_image_);
            gtk_label_set_text(GTK_LABEL(fallback_label_), "kbd!");
            gtk_widget_show(fallback_label_);
            setCapsVisual();
            gtk_widget_set_tooltip_text(GTK_WIDGET(plugin_), error_message_->c_str());
            return;
        }

        if (!have_keyboard_ || xkb_state_ == nullptr || xkb_keymap_ == nullptr) {
            gtk_widget_hide(flag_frame_);
            gtk_widget_hide(flag_image_);
            gtk_label_set_text(GTK_LABEL(fallback_label_), "--");
            gtk_widget_show(fallback_label_);
            setCapsVisual();
            gtk_widget_set_tooltip_text(GTK_WIDGET(plugin_), "Waiting for keymap / modifier state from the compositor.");
            return;
        }

        const std::string fallback_text = guessFallbackText(layout_name_);
        const char* flag_code = guessFlagCode(layout_name_).value_or(nullptr);
        showFlag(flag_code, fallback_text.c_str());
        setCapsVisual();

        std::string tooltip;
        if (!layout_name_.empty()) {
            tooltip = "Layout: " + layout_name_ + "\nCaps Lock: ";
            tooltip += caps_lock_ ? "on" : "off";
        } else {
            tooltip = "Caps Lock: ";
            tooltip += caps_lock_ ? "on" : "off";
        }

        gtk_widget_set_tooltip_text(GTK_WIDGET(plugin_), tooltip.c_str());
    }

    void setError(const char* message) {
        error_message_ = std::string(message != nullptr ? message : "Unknown error");
        updateView();
    }

    void clearError() {
        error_message_.reset();
    }

    void refreshFromXkb() {
        if (xkb_state_ == nullptr || xkb_keymap_ == nullptr) {
            return;
        }

        effective_layout_ = xkb_state_serialize_layout(xkb_state_, XKB_STATE_LAYOUT_EFFECTIVE);
        const char* layout = xkb_keymap_layout_get_name(xkb_keymap_, effective_layout_);
        layout_name_ = layout != nullptr ? std::string(layout) : std::string();
        caps_lock_ = xkb_state_mod_name_is_active(xkb_state_, XKB_MOD_NAME_CAPS, XKB_STATE_MODS_LOCKED) > 0;

        clearError();
        updateView();
    }

    void destroyXkb() {
        if (xkb_state_ != nullptr) {
            xkb_state_unref(xkb_state_);
            xkb_state_ = nullptr;
        }
        if (xkb_keymap_ != nullptr) {
            xkb_keymap_unref(xkb_keymap_);
            xkb_keymap_ = nullptr;
        }
        layout_name_.clear();
        current_flag_code_.reset();
        caps_lock_ = false;
        effective_layout_ = XKB_LAYOUT_INVALID;
    }

    void freeWayland() {
        if (wl_keyboard_ != nullptr) {
            wl_keyboard_destroy(wl_keyboard_);
            wl_keyboard_ = nullptr;
        }
        if (wl_seat_ != nullptr) {
            wl_seat_destroy(wl_seat_);
            wl_seat_ = nullptr;
        }
        if (wl_registry_ != nullptr) {
            wl_registry_destroy(wl_registry_);
            wl_registry_ = nullptr;
        }
    }

    bool initWayland() {
        GdkDisplay* display = gdk_display_get_default();
        if (!GDK_IS_WAYLAND_DISPLAY(display)) {
            is_wayland_ = false;
            return false;
        }

        is_wayland_ = true;
        wl_display_ = gdk_wayland_display_get_wl_display(display);
        if (wl_display_ == nullptr) {
            setError("GDK Wayland display returned no wl_display.");
            return false;
        }

        xkb_context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (xkb_context_ == nullptr) {
            setError("Failed to create an xkb_context.");
            return false;
        }

        wl_registry_ = wl_display_get_registry(wl_display_);
        if (wl_registry_ == nullptr) {
            setError("Failed to obtain the Wayland registry.");
            return false;
        }

        wl_registry_add_listener(wl_registry_, &registry_listener_, this);
        wl_display_roundtrip(wl_display_);
        wl_display_roundtrip(wl_display_);

        if (wl_seat_ == nullptr) {
            setError("No Wayland seat found.\nMake sure the panel runs inside a Wayland session.");
            return false;
        }

        updateView();
        return true;
    }

    static std::optional<const char*> guessFlagCode(const std::string& layout_name) {
        if (layout_name.empty()) {
            return std::nullopt;
        }

        gchar* lower_g = g_utf8_strdown(layout_name.c_str(), -1);
        if (lower_g == nullptr) {
            return std::nullopt;
        }

        const std::string lower(lower_g);
        g_free(lower_g);

        const auto contains = [&lower](const char* needle) {
            return lower.find(needle) != std::string::npos;
        };

        if (contains("british") || contains("united kingdom") || contains("english (uk)") ||
            contains(" english (gb)") || contains("uk english") || lower == "gb" || lower == "uk") {
            return "gb";
        }
        if (contains("russian") || contains("рус") || lower == "ru") {
            return "ru";
        }
        if (contains("ukrain") || contains("укра") || lower == "ua") {
            return "ua";
        }
        if (contains("german") || contains("deutsch") || lower == "de") {
            return "de";
        }
        if (contains("french") || contains("français") || contains("francais") || lower == "fr") {
            return "fr";
        }
        if (contains("italian") || contains("italiano") || lower == "it") {
            return "it";
        }
        if (contains("spanish") || contains("español") || contains("espanol") || lower == "es") {
            return "es";
        }
        if (contains("polish") || contains("polski") || lower == "pl") {
            return "pl";
        }
        if (contains("czech") || contains("čeština") || contains("cestina") || lower == "cz" || lower == "cs") {
            return "cz";
        }
        if (contains("turkish") || contains("türk") || contains("turk") || lower == "tr") {
            return "tr";
        }
        if (contains("dutch") || contains("neder") || lower == "nl") {
            return "nl";
        }
        if (contains("english") || contains("амер") || contains("англ") || lower == "us" || lower == "en") {
            return "us";
        }
        return std::nullopt;
    }

    static std::string guessFallbackText(const std::string& layout_name) {
        if (layout_name.empty()) {
            return "??";
        }

        bool compact = true;
        for (unsigned char ch : layout_name) {
            if (!(g_ascii_isalnum(ch) || ch == '_' || ch == '-')) {
                compact = false;
                break;
            }
        }

        if (compact && layout_name.size() <= 8) {
            gchar* upper_g = g_ascii_strup(layout_name.c_str(), -1);
            std::string upper = upper_g != nullptr ? std::string(upper_g) : std::string("??");
            g_free(upper_g);
            return upper;
        }

        return "??";
    }

    void queueResizeAll() {
        gtk_widget_queue_resize(status_box_);
        gtk_widget_queue_resize(status_frame_);
        gtk_widget_queue_resize(GTK_WIDGET(plugin_));
    }

private:
    XfcePanelPlugin* plugin_{};
    GtkWidget* box_{};
    GtkWidget* status_frame_{};
    GtkWidget* status_box_{};
    GtkWidget* flag_frame_{};
    GtkWidget* flag_image_{};
    GtkWidget* fallback_label_{};
    GtkCssProvider* css_provider_{};

    wl_display* wl_display_{};
    wl_registry* wl_registry_{};
    wl_seat* wl_seat_{};
    wl_keyboard* wl_keyboard_{};

    xkb_context* xkb_context_{};
    xkb_keymap* xkb_keymap_{};
    xkb_state* xkb_state_{};

    bool is_wayland_{};
    bool have_keyboard_{};
    bool caps_lock_{};
    xkb_layout_index_t effective_layout_{XKB_LAYOUT_INVALID};
    std::string layout_name_;
    std::optional<std::string> error_message_;
    std::optional<std::string> current_flag_code_;

    inline static const wl_keyboard_listener keyboard_listener_ = {
        .keymap = &KbdStatePlugin::keyboardKeymap,
        .enter = &KbdStatePlugin::keyboardEnter,
        .leave = &KbdStatePlugin::keyboardLeave,
        .key = &KbdStatePlugin::keyboardKey,
        .modifiers = &KbdStatePlugin::keyboardModifiers,
        .repeat_info = &KbdStatePlugin::keyboardRepeatInfo,
    };

    inline static const wl_seat_listener seat_listener_ = {
        .capabilities = &KbdStatePlugin::seatCapabilities,
        .name = &KbdStatePlugin::seatName,
    };

    inline static const wl_registry_listener registry_listener_ = {
        .global = &KbdStatePlugin::registryGlobal,
        .global_remove = &KbdStatePlugin::registryGlobalRemove,
    };
};

} // namespace

extern "C" void wayland_kbdstate_construct(XfcePanelPlugin* plugin) {
    KbdStatePlugin::construct(plugin);
}
