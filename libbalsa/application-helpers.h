/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 2013-2020 Peter Bloomfield
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
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

/*
 * Helpers for GtkApplicationWindow
 *
 * Currently only one helper
 */

#ifndef   __LIBBALSA_APPLICATION_HELPERS_H__
#define   __LIBBALSA_APPLICATION_HELPERS_H__

#ifndef BALSA_VERSION
#error "Include config.h before this file."
#endif

#include <gtk/gtk.h>

GtkWidget *libbalsa_window_get_menu_bar(GtkApplicationWindow * window,
                                        const GActionEntry   * entries,
                                        gint                   n_entries,
                                        const gchar          * resource_path,
                                        GError              ** error,
                                        gpointer               cb_data);

void libbalsa_window_set_accels        (GtkApplicationWindow * window,
                                        GMenuModel           * menu_model);
void libbalsa_window_block_accels      (GtkApplicationWindow * window,
                                        gboolean               block);

void libbalsa_window_add_accelerator   (GtkApplicationWindow * window,
                                        const gchar          * accel,
                                        const gchar          * action_name);

#endif				/* __LIBBALSA_APPLICATION_HELPERS_H__ */
