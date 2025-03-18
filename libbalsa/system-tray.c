/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2022 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */


#ifdef ENABLE_SYSTRAY

#include <glib/gi18n.h>
#include <libxapp/xapp-status-icon.h>
#include "files.h"
#include "system-tray.h"


static XAppStatusIcon *status_icon;
static gchar *icon_path[2] = {NULL, NULL};
static gboolean icon_attention;
static GtkMenu *icon_menu;
static libbalsa_systray_cb_t icon_activate_cb;
static gpointer icon_activate_cb_data;


static void systray_cb_internal(XAppStatusIcon *icon,
								guint           button,
								guint           time,
								gpointer        user_data);


void
libbalsa_systray_icon_init(GtkMenu *menu, libbalsa_systray_cb_t activate_cb, gpointer activate_cb_data)
{
	g_return_if_fail(icon_path[0] == NULL);

	icon_path[0] = libbalsa_pixmap_finder("balsa_icon.png");
	icon_path[1] = libbalsa_pixmap_finder("balsa_attention.png");
	icon_activate_cb = activate_cb;
	icon_activate_cb_data = activate_cb_data;
	if (menu != NULL) {
		icon_menu = g_object_ref(menu);
	}
}


void
libbalsa_systray_icon_enable(gboolean enable)
{
	g_return_if_fail(icon_path[0] != NULL);

	if (enable) {
		if (status_icon == NULL) {
			status_icon = xapp_status_icon_new();
			if (icon_menu != NULL) {
				xapp_status_icon_set_secondary_menu(status_icon, icon_menu);
			}
			if (icon_activate_cb != NULL) {
				g_signal_connect(status_icon, "activate", G_CALLBACK(systray_cb_internal), icon_activate_cb_data);
			}
			xapp_status_icon_set_visible(status_icon, TRUE);
			icon_attention = TRUE;
			libbalsa_systray_icon_attention(FALSE);
		}
	} else {
		if (status_icon != NULL) {
			g_object_unref(status_icon);
			status_icon = NULL;
		}
	}
}


void
libbalsa_systray_icon_attention(gboolean attention)
{
	g_return_if_fail(icon_path[0] != NULL);

	if ((status_icon != NULL) && (attention != icon_attention)) {
		icon_attention = attention;
		if (attention) {
			xapp_status_icon_set_icon_name(status_icon, icon_path[1]);
			xapp_status_icon_set_tooltip_text(status_icon, _("Balsa: you have new mail"));
		} else {
			xapp_status_icon_set_icon_name(status_icon, icon_path[0]);
			xapp_status_icon_set_tooltip_text(status_icon, _("Balsa"));
		}
	}
}


void
libbalsa_systray_icon_destroy(void)
{
	size_t n;

	if (icon_path[0] != NULL) {
		icon_activate_cb = NULL;
		if (icon_menu != NULL) {
			g_object_unref(icon_menu);
			icon_menu = NULL;
		}
		for (n = 0; n < G_N_ELEMENTS(icon_path); n++) {
			g_free(icon_path[n]);
			icon_path[n] = NULL;
		}
		if (status_icon != NULL) {
			g_object_unref(status_icon);
			status_icon = NULL;
		}
	}
}


static void
systray_cb_internal(XAppStatusIcon G_GNUC_UNUSED *icon, guint button, guint G_GNUC_UNUSED time, gpointer user_data)
{
	g_return_if_fail(status_icon != NULL);

	if ((button == 1) && (icon_activate_cb != NULL)) {
		icon_activate_cb(user_data);
	}
}


#endif /* ENABLE_SYSTRAY */
