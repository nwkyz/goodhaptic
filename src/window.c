/*
 * Good Haptic — control Goodix pressure-sensing touchpad vibration
 * Copyright (C) 2025–2026  nwkyz
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "window.h"
#include "haptic.h"
#include "config.h"

#include <adwaita.h>
#include <glib/gi18n.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCK_PATH "/run/goodhaptic/sock"

static int       slider_value      = 50;
static int       threshold_value   = 2;
static int       config_persist    = 1;
static int       config_stepless   = 0;
static char     *sel_device        = NULL;

static GtkWidget *slider         = NULL;
static GtkWidget *toggle_group   = NULL;
static GtkWidget *preset_row     = NULL;
static GtkWidget *slider_row     = NULL;
static GtkWidget *threshold_row  = NULL;
static GtkWidget *threshold_tg   = NULL;
static GtkWidget *main_win       = NULL;

/* debounce save */
static guint debounce_id = 0;


/* ---- daemon communication ----------------------------------------- */

static int daemon_launched = 0;

static void ensure_daemon(void)
{
    if (daemon_launched)
        return;
    daemon_launched = 1;

    const char *argv[] = {
        "pkexec",
        "/usr/local/libexec/goodhapticd",
        NULL
    };

    GError *err = NULL;
    gboolean ok = g_spawn_async(
        NULL, (gchar **)argv, NULL,
        G_SPAWN_SEARCH_PATH
            | G_SPAWN_DO_NOT_REAP_CHILD,
        NULL, NULL, NULL, &err);

    if (!ok) {
        g_warning("Failed to launch daemon: %s", err->message);
        g_error_free(err);
        daemon_launched = 0;
    }
}

static int
daemon_send(const char *cmd)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        g_warning("daemon socket: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (errno == ENOENT || errno == ECONNREFUSED) {
            close(fd);
            ensure_daemon();
            return -1;
        }
        g_warning("daemon connect: %s", strerror(errno));
        close(fd);
        return -1;
    }

    if (send(fd, cmd, strlen(cmd), 0) < 0) {
        g_warning("daemon send: %s", strerror(errno));
        close(fd);
        return -1;
    }

    char buf[64];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        buf[n] = '\0';
        if (strncmp(buf, "OK", 2) != 0)
            g_warning("daemon: %s", buf);
    }

    close(fd);
    return 0;
}

static void
send_strength(int value)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "STRENGTH %d\n", value);
    daemon_send(cmd);
}

/* debounced save */
static gboolean
debounce_save(gpointer data)
{
    send_strength(slider_value);
    debounce_id = 0;
    return G_SOURCE_REMOVE;
}

static void
schedule_save(void)
{
    if (debounce_id)
        g_source_remove(debounce_id);
    debounce_id = g_timeout_add(200, debounce_save, NULL);
}


/* ---- threshold --------------------------------------------------- */

static void
send_threshold(int value)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "THRESHOLD %d\n", value);
    daemon_send(cmd);
}

static void
on_threshold_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
    AdwToggleGroup *group = ADW_TOGGLE_GROUP(obj);
    guint idx = adw_toggle_group_get_active(group);
    if (idx < 3) {
        threshold_value = idx + 1;  /* 0→1, 1→2, 2→3 */
        send_threshold(threshold_value);
    }
}

static GtkWidget *
build_threshold_row(void)
{
    static const char *labels[] = { N_("轻"), N_("中"), N_("重") };

    GtkWidget *group = adw_toggle_group_new();
    adw_toggle_group_set_homogeneous(ADW_TOGGLE_GROUP(group), TRUE);
    threshold_tg = group;

    for (int i = 0; i < 3; i++) {
        AdwToggle *toggle = adw_toggle_new();
        adw_toggle_set_label(toggle, _(labels[i]));
        adw_toggle_group_add(ADW_TOGGLE_GROUP(group), toggle);
    }

    /* initial selection (signal not yet connected) */
    adw_toggle_group_set_active(ADW_TOGGLE_GROUP(group),
                                (guint)(threshold_value - 1));

    /* connect after initial selection */
    g_signal_connect(group, "notify::active",
                     G_CALLBACK(on_threshold_changed), NULL);

    /* wrap so toggle group doesn't stretch vertically */
    GtkWidget *wrapper = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_valign(wrapper, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(wrapper, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(wrapper), group);

    GtkWidget *row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), _("点击灵敏度"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row),
        _("触发点击所需的按压力度"));
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), wrapper);

    return row;
}


static GtkWidget *
build_click_section(void)
{
    GtkWidget *group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group), _("点击"));

    GtkWidget *tr = build_threshold_row();
    threshold_row = tr;
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), tr);

    return group;
}


/* ---- device scan & dropdown --------------------------------------- */

typedef struct {
    HapticDevice *devs;
    int           n;
} DeviceCtx;

static void
on_device_changed(GObject *o, GParamSpec *pspec, gpointer data)
{
    DeviceCtx *ctx = data;
    AdwComboRow *cr = ADW_COMBO_ROW(o);
    guint idx = adw_combo_row_get_selected(cr);

    if ((int)idx < ctx->n) {
        g_free(sel_device);
        sel_device = g_strdup(ctx->devs[idx].path);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(cr), ctx->devs[idx].name);

        char cmd[320];
        snprintf(cmd, sizeof(cmd), "DEVICE=%s\n", sel_device);
        daemon_send(cmd);
    }
}

static void
device_ctx_free(gpointer data, GClosure *closure)
{
    DeviceCtx *cx = data;
    haptic_scan_free(cx->devs, cx->n);
    g_free(cx);
}

static GtkWidget *
build_device_row(const char *pref_device)
{
    DeviceCtx *ctx = g_new0(DeviceCtx, 1);
    ctx->n = haptic_scan(&ctx->devs);

    GtkWidget *row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), _("设备"));

    GtkStringList *model = gtk_string_list_new(NULL);

    int selected = 0;

    if (ctx->n > 0) {
        for (int i = 0; i < ctx->n; i++) {
            if (pref_device && strcmp(ctx->devs[i].path, pref_device) == 0)
                selected = i;
        }
        sel_device = g_strdup(ctx->devs[selected].path);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row),
                                    ctx->devs[selected].name);
    } else {
        sel_device = g_strdup("/dev/hidraw0");
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), _("未发现设备"));
    }

    for (int i = 0; i < ctx->n; i++) {
        gchar *label = g_strdup_printf("%s (%s)",
                                       ctx->devs[i].name, ctx->devs[i].path);
        gtk_string_list_append(model, label);
        g_free(label);
    }

    adw_combo_row_set_model(ADW_COMBO_ROW(row), G_LIST_MODEL(model));
    adw_combo_row_set_selected(ADW_COMBO_ROW(row), selected);

    g_signal_connect_data(row, "notify::selected",
                          G_CALLBACK(on_device_changed), ctx,
                          device_ctx_free, 0);

    return row;
}


/* ---- slider ------------------------------------------------------- */

static void
on_slider_changed(GtkRange *range, gpointer data)
{
    slider_value = (int) gtk_range_get_value(range);
    schedule_save();
}


/* ---- presets (4 levels) ------------------------------------------- */

static const int   preset_vals[]   = { 25, 50, 75, 100 };
static const char *preset_labels[] = { N_("轻"), N_("中"), N_("重"), N_("非常重") };

static int
find_preset_index(int value)
{
    int nearest = 0, best = abs(value - preset_vals[0]);
    for (int i = 1; i < 4; i++) {
        int d = abs(value - preset_vals[i]);
        if (d < best) { best = d; nearest = i; }
    }
    return nearest;
}

static void
highlight_preset(void)
{
    if (!toggle_group)
        return;
    int idx = find_preset_index(slider_value);
    adw_toggle_group_set_active(ADW_TOGGLE_GROUP(toggle_group), idx);
}

static void
on_preset_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
    AdwToggleGroup *group = ADW_TOGGLE_GROUP(obj);
    guint idx = adw_toggle_group_get_active(group);
    if (idx < 4) {
        slider_value = preset_vals[idx];
        send_strength(slider_value);
    }
}

static GtkWidget *
build_presets(void)
{
    GtkWidget *group = adw_toggle_group_new();
    adw_toggle_group_set_homogeneous(ADW_TOGGLE_GROUP(group), TRUE);
    toggle_group = group;

    for (int i = 0; i < 4; i++) {
        AdwToggle *toggle = adw_toggle_new();
        adw_toggle_set_label(toggle, _(preset_labels[i]));
        adw_toggle_group_add(ADW_TOGGLE_GROUP(group), toggle);
    }

    /* initial selection (signal not yet connected) */
    adw_toggle_group_set_active(ADW_TOGGLE_GROUP(group),
                                find_preset_index(slider_value));

    return group;
}


/* ---- vibration section -------------------------------------------- */

static void
rebuild_vibration_content(void)
{
    if (slider_row)
        gtk_widget_set_visible(slider_row, config_stepless);
    if (preset_row)
        gtk_widget_set_visible(preset_row, !config_stepless);

    if (config_stepless)
        gtk_range_set_value(GTK_RANGE(slider), slider_value);
    else
        highlight_preset();
}

static void
on_vib_enabled(GObject *obj, GParamSpec *pspec, gpointer data)
{
    AdwExpanderRow *row = ADW_EXPANDER_ROW(obj);
    gboolean on = adw_expander_row_get_enable_expansion(row);

    if (on)
        send_strength(slider_value);
    else
        send_strength(0);
}

static GtkWidget *
build_vibration_section(void)
{
    GtkWidget *group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group), _("振动"));

    GtkWidget *expander = adw_expander_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(expander), _("振动"));
    adw_expander_row_set_show_enable_switch(ADW_EXPANDER_ROW(expander), TRUE);
    adw_expander_row_set_expanded(ADW_EXPANDER_ROW(expander), TRUE);
    adw_expander_row_set_enable_expansion(ADW_EXPANDER_ROW(expander), TRUE);

    g_signal_connect(expander, "notify::enable-expansion",
                     G_CALLBACK(on_vib_enabled), NULL);

    /* slider row */
    slider = gtk_scale_new_with_range(
        GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_range_set_value(GTK_RANGE(slider), slider_value);
    gtk_scale_set_draw_value(GTK_SCALE(slider), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(slider), GTK_POS_RIGHT);
    gtk_widget_set_size_request(slider, 200, -1);
    gtk_widget_set_margin_start(slider, 12);
    gtk_widget_set_margin_end(slider, 12);

    GtkWidget *sr = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(sr), _("力度"));
    adw_action_row_add_suffix(ADW_ACTION_ROW(sr), slider);
    adw_action_row_set_activatable_widget(ADW_ACTION_ROW(sr), slider);

    g_signal_connect(slider, "value-changed",
                     G_CALLBACK(on_slider_changed), NULL);

    /* preset buttons */
    GtkWidget *presets = build_presets();

    /* add both, show one */
    adw_expander_row_add_row(ADW_EXPANDER_ROW(expander), sr);
    slider_row = sr;

    /* wrap toggle group so it doesn't stretch vertically in the action row */
    GtkWidget *wrapper = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_valign(wrapper, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(wrapper, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(wrapper), presets);

    GtkWidget *pr = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(pr), _("力度"));
    adw_action_row_add_suffix(ADW_ACTION_ROW(pr), wrapper);
    adw_expander_row_add_row(ADW_EXPANDER_ROW(expander), pr);
    preset_row = pr;

    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), expander);

    rebuild_vibration_content();

    /* connect toggle group signal after initial selection is set */
    if (toggle_group)
        g_signal_connect(toggle_group, "notify::active",
                         G_CALLBACK(on_preset_changed), NULL);

    return group;
}


/* ---- system section ----------------------------------------------- */

static void
on_persist_toggled(GObject *obj, GParamSpec *pspec, gpointer data)
{
    GtkSwitch *sw = GTK_SWITCH(obj);
    int on = gtk_switch_get_active(sw) ? 1 : 0;
    config_persist = on;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "PERSIST=%d\n", on);
    daemon_send(cmd);
}

static GtkWidget *
build_system_group(void)
{
    GtkWidget *group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group), _("系统"));

    GtkWidget *sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sw), config_persist != 0);
    gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
    g_signal_connect(sw, "notify::active",
                     G_CALLBACK(on_persist_toggled), NULL);

    GtkWidget *row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), _("开机恢复设置"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row),
        _("启动时自动将设置写入硬件，防止重启后恢复默认值"));
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), sw);
    adw_action_row_set_activatable_widget(ADW_ACTION_ROW(row), sw);

    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
    return group;
}


/* ---- preferences dialog ------------------------------------------- */

static void
on_stepless_toggled(GObject *obj, GParamSpec *pspec, gpointer data)
{
    GtkSwitch *sw = GTK_SWITCH(obj);
    config_stepless = gtk_switch_get_active(sw) ? 1 : 0;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "STEPLESS=%d\n", config_stepless);
    daemon_send(cmd);

    rebuild_vibration_content();
}

static void
on_preferences(GSimpleAction *action, GVariant *param, gpointer data)
{
    AdwDialog *dialog = adw_preferences_dialog_new();

    AdwPreferencesPage *page =
        ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(page, _("首选项"));

    AdwPreferencesGroup *group =
        ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, _("调节"));

    GtkWidget *sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sw), config_stepless != 0);
    gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
    g_signal_connect(sw, "notify::active",
                     G_CALLBACK(on_stepless_toggled), NULL);

    GtkWidget *row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), _("无级调节"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row),
        _("无级调节对于某些设备不起作用"));
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), sw);
    adw_action_row_set_activatable_widget(ADW_ACTION_ROW(row), sw);
    adw_preferences_group_add(group, row);

    adw_preferences_page_add(page, group);
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(dialog), page);

    adw_dialog_present(dialog, GTK_WIDGET(data));
}


/* ---- about dialog ------------------------------------------------- */

static void
on_about(GSimpleAction *action, GVariant *param, gpointer data)
{
    const char *gplv3 =
        "This program is free software: you can redistribute it and/or "
        "modify it under the terms of the GNU General Public License as "
        "published by the Free Software Foundation, either version 3 of "
        "the License, or (at your option) any later version.\n"
        "\n"
        "This program is distributed in the hope that it will be useful, "
        "but WITHOUT ANY WARRANTY; without even the implied warranty of "
        "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the "
        "GNU General Public License for more details.\n"
        "\n"
        "You should have received a copy of the GNU General Public License "
        "along with this program.  If not, see "
        "https://www.gnu.org/licenses/.";

    AdwAboutDialog *about = ADW_ABOUT_DIALOG(adw_about_dialog_new());
    adw_about_dialog_set_application_name(about, _("Good Haptic"));
    adw_about_dialog_set_application_icon(about, "io.github.nwkyz.goodhaptic");
    adw_about_dialog_set_version(about, "1.0");
    adw_about_dialog_set_developer_name(about, "nwkyz");
    adw_about_dialog_set_copyright(about, "© 2025–2026 nwkyz");
    adw_about_dialog_set_license(about, gplv3);
    adw_about_dialog_set_website(about, "https://github.com/nwkyz/goodhaptic");
    adw_about_dialog_set_issue_url(about,
        "https://github.com/nwkyz/goodhaptic/issues");

    adw_dialog_present(ADW_DIALOG(about), GTK_WIDGET(data));
}


/* ---- data section ------------------------------------------------- */

static GtkWidget *
build_data_section(void)
{
    GtkWidget *group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group), _("数据"));

    int contact_max = 0, pad_type = 0;
    gboolean ok = FALSE;

    /* query daemon for device capability (daemon runs as root) */
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd >= 0) {
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);

        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            const char *cmd = "CAPABILITY\n";
            send(fd, cmd, strlen(cmd), 0);

            char resp[64];
            ssize_t n = recv(fd, resp, sizeof(resp) - 1, 0);
            if (n > 0) {
                resp[n] = '\0';
                if (sscanf(resp, "OK %d %d", &contact_max, &pad_type) == 2)
                    ok = TRUE;
            }
        }
        close(fd);
    }

    if (ok) {
        const char *type_str;
        switch (pad_type) {
        case 0:  type_str = _("Touchpad"); break;
        case 1:  type_str = _("Clickpad"); break;
        case 2:  type_str = _("Precision Touchpad"); break;
        default: type_str = _("未知"); break;
        }

        gchar *contact_str = g_strdup_printf("%d", contact_max);
        GtkWidget *cr = adw_action_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(cr),
                                      _("最大触摸点数"));
        adw_action_row_set_subtitle(ADW_ACTION_ROW(cr), contact_str);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), cr);
        g_free(contact_str);

        GtkWidget *tr = adw_action_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(tr),
                                      _("触控板类型"));
        adw_action_row_set_subtitle(ADW_ACTION_ROW(tr), type_str);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), tr);
    } else {
        GtkWidget *nr = adw_action_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(nr),
                                      _("设备信息"));
        adw_action_row_set_subtitle(ADW_ACTION_ROW(nr),
                                    _("无法读取"));
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), nr);
    }

    return group;
}


/* ---- window ------------------------------------------------------- */

/* ==================================================================
 *  Monitor dialog — real-time touch / pressure visualisation
 * ================================================================== */

#define TOUCH_MAX_X  4149
#define TOUCH_MAX_Y  2147
#define MONITOR_PAD  20    /* padding inside the drawing area */

/* distinct colours for up to 5 fingers */
static const double FINGER_COLORS[TOUCH_MAX_FINGERS][3] = {
    {0.88, 0.11, 0.14},   /* red     */
    {0.20, 0.82, 0.48},   /* green   */
    {0.21, 0.52, 0.89},   /* blue    */
    {0.96, 0.76, 0.07},   /* yellow  */
    {0.57, 0.25, 0.67},   /* purple  */
};

typedef struct {
    AdwDialog  *dialog;
    GIOChannel *chan;
    guint       io_watch;

    TouchReport report;
    gboolean    connected;

    int         max_x;    /* from HID descriptor (Logical Max X) */
    int         max_y;    /* from HID descriptor (Logical Max Y) */

    /* widgets updated from the socket callback */
    GtkWidget  *count_label;
    GtkWidget  *button_label;
    GtkWidget  *scan_label;
    GtkWidget  *draw_area;
    GtkWidget  *bar_area;         /* 5-column pressure bar chart */
} MonitorCtx;


static MonitorCtx *monitor_active = NULL;  /* only one monitor at a time */

static void
monitor_cleanup(gpointer data, GObject *where_the_object_was)
{
    MonitorCtx *ctx = data;

    if (ctx->io_watch) {
        g_source_remove(ctx->io_watch);
        ctx->io_watch = 0;
    }
    if (ctx->chan) {
        g_io_channel_shutdown(ctx->chan, FALSE, NULL);
        g_io_channel_unref(ctx->chan);
        ctx->chan = NULL;
    }
    ctx->connected = FALSE;

    /* called from weak-ref = dialog is being destroyed → free context */
    if (where_the_object_was != NULL) {
        monitor_active = NULL;
        g_free(ctx);
    }
}


/*
 * Helper: draw centred text at (x, y) using the widget's system font.
 */
static void
draw_centered_text(cairo_t *cr, GtkWidget *widget,
                   const char *text, double x, double y,
                   const char *weight, double size_px)
{
    PangoContext *ctx = gtk_widget_get_pango_context(widget);
    PangoLayout  *lay = pango_layout_new(ctx);

    PangoFontDescription *desc = pango_font_description_new();
    if (g_strcmp0(weight, "bold") == 0)
        pango_font_description_set_weight(desc, PANGO_WEIGHT_BOLD);
    else
        pango_font_description_set_weight(desc, PANGO_WEIGHT_NORMAL);
    pango_font_description_set_absolute_size(desc,
                                             size_px * PANGO_SCALE);
    pango_layout_set_font_description(lay, desc);
    pango_font_description_free(desc);

    /* enable tabular numbers so digits don't wobble */
    PangoAttrList *al = pango_attr_list_new();
    pango_attr_list_insert(al, pango_attr_font_features_new("tnum 1"));
    pango_layout_set_attributes(lay, al);
    pango_attr_list_unref(al);

    pango_layout_set_text(lay, text, -1);

    int tw, th;
    pango_layout_get_pixel_size(lay, &tw, &th);
    cairo_move_to(cr, x - tw / 2.0, y);
    pango_cairo_show_layout(cr, lay);
    g_object_unref(lay);
}

/* Same as draw_centered_text but with a vertical pixel offset. */
static void
draw_centered_text_off(cairo_t *cr, GtkWidget *widget,
                       const char *text, double x, double y,
                       double y_off, const char *weight, double size_px)
{
    PangoContext *ctx = gtk_widget_get_pango_context(widget);
    PangoLayout  *lay = pango_layout_new(ctx);

    PangoFontDescription *desc = pango_font_description_new();
    if (g_strcmp0(weight, "bold") == 0)
        pango_font_description_set_weight(desc, PANGO_WEIGHT_BOLD);
    else
        pango_font_description_set_weight(desc, PANGO_WEIGHT_NORMAL);
    pango_font_description_set_absolute_size(desc,
                                             size_px * PANGO_SCALE);
    pango_layout_set_font_description(lay, desc);
    pango_font_description_free(desc);

    PangoAttrList *al = pango_attr_list_new();
    pango_attr_list_insert(al, pango_attr_font_features_new("tnum 1"));
    pango_layout_set_attributes(lay, al);
    pango_attr_list_unref(al);

    pango_layout_set_text(lay, text, -1);

    int tw, th;
    pango_layout_get_pixel_size(lay, &tw, &th);
    cairo_move_to(cr, x - tw / 2.0, y + y_off);
    pango_cairo_show_layout(cr, lay);
    g_object_unref(lay);
}

/*
 * Helper: draw left-aligned text at (x, y) using the widget's system font.
 */
static void
monitor_draw_cb(GtkDrawingArea *area, cairo_t *cr,
                int width, int height, gpointer data)
{
    MonitorCtx *ctx = data;

    /* background */
    cairo_set_source_rgba(cr, 0, 0, 0, 0.12);
    cairo_paint(cr);

    /* touchpad area – maintain ~2:1 aspect ratio */
    int pad_w = width  - MONITOR_PAD * 2;
    int pad_h = height - MONITOR_PAD * 2;
    if (pad_w < 10 || pad_h < 10) return;

    double ratio = (double)ctx->max_x / (double)ctx->max_y;
    if ((double)pad_w / (double)pad_h > ratio)
        pad_w = (int)(pad_h * ratio);
    else
        pad_h = (int)(pad_w / ratio);

    int pad_x = (width  - pad_w) / 2;
    int pad_y = (height - pad_h) / 2;

    /* pad background */
    cairo_rectangle(cr, pad_x, pad_y, pad_w, pad_h);
    cairo_set_source_rgba(cr, 0.18, 0.18, 0.20, 0.90);
    cairo_fill(cr);

    /* pad border */
    cairo_rectangle(cr, pad_x + 0.5, pad_y + 0.5, pad_w - 1, pad_h - 1);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.48, 0.80);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    /* resolution labels inside frame edges, using border colour */
    {
        cairo_set_source_rgba(cr, 0.45, 0.45, 0.48, 0.65);
        char res_label[16];
        snprintf(res_label, sizeof(res_label), "%d", ctx->max_x);
        draw_centered_text(cr, GTK_WIDGET(area), res_label,
                           pad_x + pad_w / 2.0,
                           pad_y + pad_h - 20, "normal", 9);
        snprintf(res_label, sizeof(res_label), "%d", ctx->max_y);
        draw_centered_text(cr, GTK_WIDGET(area), res_label,
                           pad_x + pad_w - 26,
                           pad_y + pad_h / 2.0, "normal", 9);
    }

    if (!ctx->connected)
        return;

    /* draw each active finger */
    for (int i = 0; i < TOUCH_MAX_FINGERS; i++) {
        TouchFinger *f = &ctx->report.fingers[i];
        if (!f->tip && !f->confidence)
            continue;

        double fx = pad_x + (double)f->x / (double)ctx->max_x * pad_w;
        double fy = pad_y + (double)f->y / (double)ctx->max_y * pad_h;

        double r_max = 12.0;  /* outer ring = max pressure (BAR_PRESSURE_MAX) */
        double r     = 4.0 + (double)f->pressure / 500.0 * 8.0;
        if (r > r_max) r = r_max;
        if (r < 3)     r = 3;

        /* glow behind the outer ring */
        cairo_arc(cr, fx, fy, r_max + 3, 0, 2 * G_PI);
        cairo_set_source_rgba(cr,
                              FINGER_COLORS[i][0],
                              FINGER_COLORS[i][1],
                              FINGER_COLORS[i][2],
                              0.20);
        cairo_fill(cr);

        /* outer ring — represents max pressure ceiling */
        cairo_arc(cr, fx, fy, r_max, 0, 2 * G_PI);
        cairo_set_source_rgba(cr,
                              FINGER_COLORS[i][0],
                              FINGER_COLORS[i][1],
                              FINGER_COLORS[i][2],
                              0.55);
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);

        /* solid inner circle — actual pressure */
        cairo_arc(cr, fx, fy, r, 0, 2 * G_PI);
        cairo_set_source_rgb(cr,
                             FINGER_COLORS[i][0],
                             FINGER_COLORS[i][1],
                             FINGER_COLORS[i][2]);
        cairo_fill(cr);

        /* contact id label — fixed size, centred on outer ring */
        char idstr[4];
        snprintf(idstr, sizeof(idstr), "%d", f->contact_id);
        cairo_set_source_rgb(cr, 1, 1, 1);
        draw_centered_text_off(cr, GTK_WIDGET(area), idstr,
                               fx, fy, -7, "bold", 10);
    }
}


/* Practical max for bar scaling – HID logical max is 2000 but typical
 * finger pressure rarely exceeds 400–500 in normal use.  Clamped to
 * 500 so the bar is legible; values above 500 fill the bar completely. */
#define BAR_PRESSURE_MAX 500

/*
 * monitor_bar_draw_cb  —  render 5 pressure-bar columns for fingers 0–4.
 *
 * Each column shows:
 *   [ vertical bar (height ∝ pressure) with Contact ID inside ]
 *   [ pressure value                                          ]
 *   [ (X, Y) coordinates                                      ]
 */
static void
monitor_bar_draw_cb(GtkDrawingArea *area, cairo_t *cr,
                    int width, int height, gpointer data)
{
    MonitorCtx *ctx = data;

    /* background */
    cairo_set_source_rgba(cr, 0, 0, 0, 0.08);
    cairo_paint(cr);

    int ncols = TOUCH_MAX_FINGERS;
    int gap   = 6;
    int col_w = (width - gap * (ncols + 1)) / ncols;
    if (col_w < 20) col_w = 20;

    /* line height from system font at ~10 px */
    double line_h = 14;

    /* layout: top (bar + ID) → pressure → (X, Y) → blank */
    double bar_top    = 6;
    double text_gap   = 10;
    double bar_max_h  = height - bar_top - line_h * 3 - text_gap;
    if (bar_max_h < 24) bar_max_h = 24;

    double text_y0    = bar_top + bar_max_h + text_gap;  /* pressure */
    double text_y1    = text_y0 + line_h;                 /* (X, Y) */

    for (int i = 0; i < ncols; i++) {
        double cx = gap + i * (col_w + gap);
        TouchFinger *f = &ctx->report.fingers[i];
        int active = (f->tip || f->confidence) && ctx->connected;

        /* --- bar outline with rounded corners --- */
        {
            double r_out = (4 < col_w * 0.25) ? 4 : col_w * 0.25;
            cairo_new_sub_path(cr);
            cairo_arc(cr, cx + col_w - r_out, bar_top + r_out,
                      r_out, -G_PI / 2, 0);
            cairo_arc(cr, cx + col_w - r_out, bar_top + bar_max_h - r_out,
                      r_out, 0, G_PI / 2);
            cairo_arc(cr, cx + r_out, bar_top + bar_max_h - r_out,
                      r_out, G_PI / 2, G_PI);
            cairo_arc(cr, cx + r_out, bar_top + r_out,
                      r_out, G_PI, -G_PI / 2);
            cairo_close_path(cr);
            cairo_set_source_rgba(cr, 0.45, 0.45, 0.48, 0.50);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
        }

        if (active && f->pressure > 0) {
            double frac = (double)f->pressure / (double)BAR_PRESSURE_MAX;
            if (frac > 1.0) frac = 1.0;
            double fill_h = frac * bar_max_h;

            /* filled bar from bottom, with rounded corners */
            double fy = bar_top + bar_max_h - fill_h;  /* fill top Y */
            double r_in = (4 < col_w * 0.25) ? 4 : col_w * 0.25;
            r_in = (r_in < fill_h * 0.5) ? r_in : fill_h * 0.5;
            {
                cairo_new_sub_path(cr);
                cairo_arc(cr, cx + col_w - r_in, fy + r_in,
                          r_in, -G_PI / 2, 0);
                cairo_arc(cr, cx + col_w - r_in, bar_top + bar_max_h - r_in,
                          r_in, 0, G_PI / 2);
                cairo_arc(cr, cx + r_in, bar_top + bar_max_h - r_in,
                          r_in, G_PI / 2, G_PI);
                cairo_arc(cr, cx + r_in, fy + r_in,
                          r_in, G_PI, -G_PI / 2);
                cairo_close_path(cr);
                cairo_set_source_rgba(cr,
                                      FINGER_COLORS[i][0],
                                      FINGER_COLORS[i][1],
                                      FINGER_COLORS[i][2],
                                      0.75);
                cairo_fill(cr);
            }

            /* Contact ID — fixed in the bar outline centre */
            char idstr[4];
            snprintf(idstr, sizeof(idstr), "%d", f->contact_id);
            cairo_set_source_rgb(cr, 1, 1, 1);
            draw_centered_text_off(cr, GTK_WIDGET(area), idstr,
                                   cx + col_w / 2.0,
                                   bar_top + bar_max_h / 2.0,
                                   -3, "bold", col_w * 0.4);
        }

        /* --- pressure / X / Y text below bar --- */
        double tx = cx + col_w / 2.0;
        char buf[32];

        /* pressure */
        if (active) {
            snprintf(buf, sizeof(buf), "%d", f->pressure);
            cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);
        } else {
            snprintf(buf, sizeof(buf), "%s", "—");
            cairo_set_source_rgba(cr, 0.45, 0.45, 0.48, 0.40);
        }
        draw_centered_text(cr, GTK_WIDGET(area), buf,
                           tx, text_y0, "normal", 10);

        /* (X, Y) */
        if (active) {
            snprintf(buf, sizeof(buf), "(%d,%d)", f->x, f->y);
            cairo_set_source_rgb(cr,
                                 FINGER_COLORS[i][0],
                                 FINGER_COLORS[i][1],
                                 FINGER_COLORS[i][2]);
        } else {
            snprintf(buf, sizeof(buf), "%s", "—");
            cairo_set_source_rgba(cr, 0.45, 0.45, 0.48, 0.40);
        }
        draw_centered_text(cr, GTK_WIDGET(area), buf,
                           tx, text_y1, "normal", 10);
    }
}


/*
 * monitor_update_labels  —  refresh status labels and trigger redraws.
 */
static void
monitor_update_labels(MonitorCtx *ctx)
{
    char markup[64];
    snprintf(markup, sizeof(markup),
             _("<span font_features='tnum'>触点数: %d</span>"),
             ctx->report.contact_count);
    gtk_label_set_markup(GTK_LABEL(ctx->count_label), markup);

    snprintf(markup, sizeof(markup),
             _("<span font_features='tnum'>按键: %s</span>"),
             ctx->report.button ? _("按下") : _("松开"));
    gtk_label_set_markup(GTK_LABEL(ctx->button_label), markup);

    snprintf(markup, sizeof(markup),
             _("<span font_features='tnum'>扫描: %05d</span>"),
             ctx->report.scan_time);
    gtk_label_set_markup(GTK_LABEL(ctx->scan_label), markup);

    gtk_widget_queue_draw(ctx->draw_area);
    gtk_widget_queue_draw(ctx->bar_area);
}


/*
 * monitor_parse_line  —  parse a "T scan count btn f0..." line.
 */
static void
monitor_parse_line(MonitorCtx *ctx, const char *line)
{
    if (!line || line[0] != 'T')
        return;

    TouchReport *r = &ctx->report;
    const char *p = line + 2; /* skip "T " */

    if (sscanf(p, "%d %d %d",
               &r->scan_time, &r->contact_count, &r->button) != 3)
        return;

    /* advance past the three global fields */
    for (int field = 0; field < 3; field++) {
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
    }

    for (int i = 0; i < TOUCH_MAX_FINGERS; i++) {
        if (sscanf(p, "%d %d %d %d %d %d",
                   &r->fingers[i].tip,
                   &r->fingers[i].confidence,
                   &r->fingers[i].contact_id,
                   &r->fingers[i].x,
                   &r->fingers[i].y,
                   &r->fingers[i].pressure) != 6)
            break;
        /* advance past 6 fields */
        for (int field = 0; field < 6; field++) {
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;
        }
    }

    monitor_update_labels(ctx);
    gtk_widget_queue_draw(ctx->draw_area);
}


/*
 * monitor_io_cb  —  GLib IO watch: read a line from the daemon socket.
 */
static gboolean
monitor_io_cb(GIOChannel *source, GIOCondition cond, gpointer data)
{
    MonitorCtx *ctx = data;

    if (cond & (G_IO_HUP | G_IO_ERR)) {
        monitor_cleanup(ctx, NULL);
        return FALSE;
    }

    gchar *line = NULL;
    gsize len = 0;
    GError *err = NULL;

    GIOStatus st = g_io_channel_read_line(source, &line, &len, NULL, &err);
    if (st == G_IO_STATUS_ERROR) {
        if (err) {
            g_warning("monitor read: %s", err->message);
            g_error_free(err);
        }
        monitor_cleanup(ctx, NULL);
        return FALSE;
    }

    if (st == G_IO_STATUS_EOF) {
        monitor_cleanup(ctx, NULL);
        return FALSE;
    }

    if (line && len > 0)
        monitor_parse_line(ctx, line);

    g_free(line);
    return TRUE;
}


/*
 * on_monitor_activate  —  open the monitor dialog.
 */
static void
on_monitor_activate(GSimpleAction *action, GVariant *param, gpointer data)
{
    GtkWidget *parent = GTK_WIDGET(data);

    /* prevent multiple monitor dialogs */
    if (monitor_active != NULL) {
        adw_dialog_present(monitor_active->dialog, parent);
        return;
    }

    /* ---------- query resolution from daemon (BEFORE monitor) ---------- */
    int max_x = TOUCH_MAX_X, max_y = TOUCH_MAX_Y;  /* fallback */
    {
        int qfd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (qfd >= 0) {
            struct sockaddr_un ra;
            memset(&ra, 0, sizeof(ra));
            ra.sun_family = AF_UNIX;
            strncpy(ra.sun_path, SOCK_PATH, sizeof(ra.sun_path) - 1);
            if (connect(qfd, (struct sockaddr *)&ra, sizeof(ra)) == 0) {
                const char *rc = "RESOLUTION\n";
                send(qfd, rc, strlen(rc), 0);
                char resp[64];
                ssize_t rn = recv(qfd, resp, sizeof(resp) - 1, 0);
                if (rn > 0) {
                    resp[rn] = '\0';
                    int mx, my;
                    if (sscanf(resp, "OK %d %d", &mx, &my) == 2) {
                        max_x = mx; max_y = my;
                    }
                }
            }
            close(qfd);
        }
    }

    /* ---------- connect to daemon ---------- */
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        g_warning("monitor socket: %s", strerror(errno));
        return;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (errno == ENOENT || errno == ECONNREFUSED) {
            close(fd);
            ensure_daemon();
            /* retry once after a short wait */
            g_usleep(400000);
            fd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (fd < 0 || connect(fd, (struct sockaddr *)&addr,
                                  sizeof(addr)) < 0) {
                g_warning(_("无法连接到守护进程"));
                if (fd >= 0) close(fd);
                return;
            }
        } else {
            g_warning("monitor connect: %s", strerror(errno));
            close(fd);
            return;
        }
    }

    /* send MONITOR command and read initial OK */
    if (send(fd, "MONITOR\n", 8, MSG_NOSIGNAL) < 0) {
        g_warning("monitor send: %s", strerror(errno));
        close(fd);
        return;
    }

    char okbuf[4] = {0};
    ssize_t n = recv(fd, okbuf, sizeof(okbuf) - 1, 0);
    if (n <= 0 || strncmp(okbuf, "OK", 2) != 0) {
        g_warning(_("守护进程不支持监控功能"));
        close(fd);
        return;
    }

    /* ---------- allocate context ---------- */
    MonitorCtx *ctx = g_new0(MonitorCtx, 1);
    monitor_active = ctx;
    ctx->max_x = max_x;
    ctx->max_y = max_y;
    ctx->report.contact_count = -1;

    GIOChannel *chan = g_io_channel_unix_new(fd);
    g_io_channel_set_buffered(chan, FALSE);
    ctx->chan     = chan;
    ctx->connected = TRUE;

    /* ---------- build dialog content ---------- */
    AdwDialog *dialog = adw_dialog_new();
    adw_dialog_set_title(ADW_DIALOG(dialog), _("触摸监控"));
    adw_dialog_set_content_width(ADW_DIALOG(dialog), 500);
    adw_dialog_set_content_height(ADW_DIALOG(dialog), 570);
    ctx->dialog = dialog;

    /* toolbar + header */
    GtkWidget *toolbar_v = adw_toolbar_view_new();

    GtkWidget *header = adw_header_bar_new();
    adw_header_bar_set_show_title(ADW_HEADER_BAR(header), TRUE);
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_v), header);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(content, 18);
    gtk_widget_set_margin_end(content, 18);
    gtk_widget_set_margin_top(content, 12);
    gtk_widget_set_margin_bottom(content, 12);

    /* --- status row --- */
    GtkWidget *status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    gtk_widget_set_halign(status_box, GTK_ALIGN_CENTER);

    GtkWidget *cl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(cl),
                         "<span font_features='tnum'>触点数: —</span>");
    ctx->count_label = cl;
    gtk_box_append(GTK_BOX(status_box), cl);

    GtkWidget *bl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(bl),
                         "<span font_features='tnum'>按键: —</span>");
    ctx->button_label = bl;
    gtk_box_append(GTK_BOX(status_box), bl);

    GtkWidget *sl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(sl),
                         "<span font_features='tnum'>扫描: —</span>");
    ctx->scan_label = sl;
    gtk_box_append(GTK_BOX(status_box), sl);

    gtk_box_append(GTK_BOX(content), status_box);

    /* --- drawing area --- */
    GtkWidget *draw = gtk_drawing_area_new();
    gtk_widget_set_size_request(draw, -1, 240);
    gtk_widget_set_halign(draw, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(draw, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(draw),
                                   monitor_draw_cb, ctx, NULL);
    ctx->draw_area = draw;

    /* wrap in a subtle frame */
    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_frame_set_child(GTK_FRAME(frame), draw);
    gtk_box_append(GTK_BOX(content), frame);

    /* --- pressure bar chart (5 fingers) --- */
    GtkWidget *bar = gtk_drawing_area_new();
    gtk_widget_set_size_request(bar, -1, 145);
    gtk_widget_set_halign(bar, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(bar, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(bar),
                                   monitor_bar_draw_cb, ctx, NULL);
    ctx->bar_area = bar;

    GtkWidget *bar_frame = gtk_frame_new(NULL);
    gtk_frame_set_child(GTK_FRAME(bar_frame), bar);
    gtk_box_append(GTK_BOX(content), bar_frame);

    /* --- wrap in scrolled window --- */
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), content);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar_v), scroll);
    adw_dialog_set_child(ADW_DIALOG(dialog), toolbar_v);

    /* ---------- lifecycle ---------- */

    /* clean up socket on dialog destroy */
    g_object_weak_ref(G_OBJECT(dialog),
                      monitor_cleanup, ctx);

    /* ---------- start streaming ---------- */
    ctx->io_watch = g_io_add_watch(chan,
                                   G_IO_IN | G_IO_HUP | G_IO_ERR,
                                   monitor_io_cb, ctx);

    /* ---------- present ---------- */
    adw_dialog_present(ADW_DIALOG(dialog), parent);
}


/* ---- build monitor row for device section ------------------------- */

static void
on_monitor_row_clicked(GtkGestureClick *gesture, int n_press,
                       double x, double y, gpointer data)
{
    g_action_activate(G_ACTION(data), NULL);
}

static GtkWidget *
build_monitor_row(GtkWidget *parent)
{
    GtkWidget *row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row),
                                  _("状态监控"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row),
        _("实时触摸位置、压力与按键状态"));

    GtkWidget *arrow = gtk_image_new_from_icon_name("go-next-symbolic");
    gtk_widget_set_opacity(arrow, 0.55);
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), arrow);

    GSimpleAction *act = g_simple_action_new("monitor", NULL);
    g_signal_connect(act, "activate",
                     G_CALLBACK(on_monitor_activate), parent);
    g_action_map_add_action(G_ACTION_MAP(
        gtk_widget_get_root(parent)), G_ACTION(act));

    /* make the row clickable via a gesture controller */
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 1);
    gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(click));
    g_signal_connect(click, "pressed",
                     G_CALLBACK(on_monitor_row_clicked), act);

    return row;
}


/* ---- window ------------------------------------------------------- */

GtkWidget *
create_main_window(GtkApplication *app)
{
    /* read persisted config */
    GoodhapticConfig cfg;
    config_load(&cfg);
    slider_value    = cfg.strength;
    threshold_value = cfg.threshold;
    config_persist  = cfg.persist;
    config_stepless = cfg.stepless;

    /* --- device scan ------------------------------------------- */
    GtkWidget *device_row = build_device_row(
        cfg.device[0] ? cfg.device : NULL);
    slider_value = cfg.strength;

    /* --- window ------------------------------------------------ */
    GtkWidget *win = adw_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(win), _("Good Haptic"));
    gtk_window_set_default_size(GTK_WINDOW(win), 600, 560);
    gtk_window_set_icon_name(GTK_WINDOW(win), "io.github.nwkyz.goodhaptic");
    main_win = win;

    GtkWidget *toolbar = adw_toolbar_view_new();
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(win), toolbar);

    /* header bar with hamburger menu */
    GtkWidget *header = adw_header_bar_new();

    GMenu *menu = g_menu_new();
    g_menu_append(menu, _("首选项"), "app.preferences");
    g_menu_append(menu, _("关于"), "app.about");

    GtkWidget *menu_btn = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_btn),
                                  "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_btn),
                                   G_MENU_MODEL(menu));
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), menu_btn);

    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);

    /* actions */
    GSimpleAction *prefs_act = g_simple_action_new("preferences", NULL);
    g_signal_connect(prefs_act, "activate",
                     G_CALLBACK(on_preferences), win);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(prefs_act));

    GSimpleAction *about_act = g_simple_action_new("about", NULL);
    g_signal_connect(about_act, "activate",
                     G_CALLBACK(on_about), win);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(about_act));

    /* content */
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_set_margin_top(box, 24);
    gtk_widget_set_margin_bottom(box, 24);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);

    GtkWidget *clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 550);
    adw_clamp_set_child(ADW_CLAMP(clamp), box);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), clamp);

    /* === 设备 === */
    GtkWidget *dev_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(dev_group), _("设备"));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(dev_group), device_row);

    /* status monitor row */
    GtkWidget *monitor_row = build_monitor_row(win);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(dev_group), monitor_row);

    /* === 振动 === */
    GtkWidget *vib_section = build_vibration_section();

    /* === 点击 === */
    GtkWidget *click_section = build_click_section();

    /* === 系统 === */
    GtkWidget *sys_group = build_system_group();

    /* === 数据 === */
    GtkWidget *data_section = build_data_section();

    /* pack groups */
    gtk_box_append(GTK_BOX(box), dev_group);
    gtk_box_append(GTK_BOX(box), vib_section);
    gtk_box_append(GTK_BOX(box), click_section);
    gtk_box_append(GTK_BOX(box), sys_group);
    gtk_box_append(GTK_BOX(box), data_section);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), scroll);

    return win;
}
