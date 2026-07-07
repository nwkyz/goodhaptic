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

static int       slider_value    = 50;
static int       config_persist  = 1;
static int       config_stepless = 0;
static char     *sel_device      = NULL;

static GtkWidget *slider       = NULL;
static GtkWidget *toggle_group = NULL;
static GtkWidget *preset_row   = NULL;
static GtkWidget *slider_row   = NULL;
static GtkWidget *main_win     = NULL;

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
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), _("开机恢复力度"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row),
        _("启动时自动将力度写入硬件，防止重启后恢复默认值"));
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


/* ---- window ------------------------------------------------------- */

GtkWidget *
create_main_window(GtkApplication *app)
{
    /* read persisted config */
    GoodhapticConfig cfg;
    config_load(&cfg);
    slider_value    = cfg.strength;
    config_persist  = cfg.persist;
    config_stepless = cfg.stepless;

    /* --- device scan ------------------------------------------- */
    GtkWidget *device_row = build_device_row(
        cfg.device[0] ? cfg.device : NULL);
    slider_value = cfg.strength;

    /* --- window ------------------------------------------------ */
    GtkWidget *win = adw_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(win), _("Good Haptic"));
    gtk_window_set_default_size(GTK_WINDOW(win), 600, 480);
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
    GtkWidget *clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 500);
    gtk_widget_set_margin_top(clamp, 24);
    gtk_widget_set_margin_bottom(clamp, 24);
    gtk_widget_set_margin_start(clamp, 12);
    gtk_widget_set_margin_end(clamp, 12);

    /* === 设备 === */
    GtkWidget *dev_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(dev_group), _("设备"));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(dev_group), device_row);

    /* === 振动 === */
    GtkWidget *vib_section = build_vibration_section();

    /* === 系统 === */
    GtkWidget *sys_group = build_system_group();

    /* pack groups */
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_box_append(GTK_BOX(box), dev_group);
    gtk_box_append(GTK_BOX(box), vib_section);
    gtk_box_append(GTK_BOX(box), sys_group);
    adw_clamp_set_child(ADW_CLAMP(clamp), box);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), clamp);

    return win;
}
