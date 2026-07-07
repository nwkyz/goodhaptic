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

#include "i18n-config.h"

#include <gtk/gtk.h>
#include <adwaita.h>
#include <glib/gi18n.h>
#include <locale.h>

#include "window.h"


static void
activate(GtkApplication *app, gpointer data)
{
    GtkWidget *window = create_main_window(app);
    gtk_window_present(GTK_WINDOW(window));
}


int
main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    adw_init();

    GtkApplication *app = gtk_application_new(
        "io.github.nwkyz.goodhaptic",
        G_APPLICATION_DEFAULT_FLAGS);

    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);

    int ret = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return ret;
}
