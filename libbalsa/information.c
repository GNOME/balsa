/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2021 Stuart Parmenter and others,
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
#include "information.h"
#include "libbalsa-conf.h"
#include <string.h>
#include <glib/gi18n.h>

static GApplication *notify_app = NULL;
static const gchar *notify_title = NULL;
static const gchar *id_base = NULL;
static GHashTable *hide_ids = NULL;
static gint serial = 0;


static void libbalsa_information_varg(LibBalsaInformationType  type,
									  const gchar             *hide_id,
									  const gchar             *fmt,
									  va_list                  ap);
static void add_hide_id(GSimpleAction *simple,
						GVariant      *parameter,
						gpointer       user_data);


void
libbalsa_information_init(GApplication *application, const gchar *title, const gchar *notification_id)
{
	GActionEntry actions[] = {
		{ "hide-notify", add_hide_id, "s", NULL, NULL },
	};
	gint hide_cnt;
	gchar **hide_list;

	g_return_if_fail(G_IS_APPLICATION(application) && (title != NULL) && (notification_id != NULL));

	g_action_map_add_action_entries(G_ACTION_MAP(application), actions, 1, NULL);
	notify_app = application;
	notify_title = title;
	id_base = notification_id;
	hide_ids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	libbalsa_conf_push_group("Notifications");
	libbalsa_conf_get_vector_with_default("hide", &hide_cnt, &hide_list, NULL);
	if (hide_list != NULL) {
		gint n;

		for (n = 0; hide_list[n] != NULL; n++) {
			g_hash_table_add(hide_ids, hide_list[n]);
		}
		g_free(hide_list);	/* note: strings consumed by the hash table */
	}
	libbalsa_conf_pop_group();
}

void
libbalsa_information_save_cfg(void)
{
	if (g_hash_table_size(hide_ids) > 0U) {
		gchar **ids;
		guint count;

		libbalsa_conf_push_group("Notifications");
		ids = (gchar **) g_hash_table_get_keys_as_array(hide_ids, &count);
		libbalsa_conf_set_vector("hide", (int) count, (const gchar **) ids);
		g_free(ids);
		libbalsa_conf_pop_group();
	}
}

void
libbalsa_information(LibBalsaInformationType type,
					 const gchar *fmt, ...)
{
	va_list va_args;

	va_start(va_args, fmt);
	libbalsa_information_varg(type, NULL, fmt, va_args);
	va_end(va_args);
}

void
libbalsa_information_may_hide(LibBalsaInformationType type,
							  const gchar *hide_id, const gchar *fmt, ...)
{
	va_list va_args;

	va_start(va_args, fmt);
	libbalsa_information_varg(type, hide_id, fmt, va_args);
	va_end(va_args);
}

void
libbalsa_information_shutdown(void)
{
	gint last_serial;

	if (hide_ids != NULL) {
		g_hash_table_destroy(hide_ids);
		hide_ids = NULL;
	}

	/* withdraw the last notification sent... */
	last_serial = g_atomic_int_get(&serial);
	if (last_serial > 0) {
		gchar *id;

		id = g_strdup_printf("%s%d", id_base, last_serial - 1);
		g_application_withdraw_notification(notify_app, id);
		g_free(id);
	}
	notify_app = NULL;		/* ...so calling libbalsa_information() later will fail */
}

static void
libbalsa_information_varg(LibBalsaInformationType type,
						  const gchar *hide_id,
						  const gchar *fmt, va_list ap)
{
	GNotification *notification;
	gchar *msg;
	gchar *id;
	const gchar *icon_str;

	g_return_if_fail(G_IS_APPLICATION(notify_app) && (notify_title != NULL) && (id_base != NULL) && (fmt != NULL));

	msg = g_strdup_vprintf(fmt, ap);
	if ((hide_id != NULL) && g_hash_table_contains(hide_ids, hide_id)) {
		g_debug("hide notification with id '%s': %s", hide_id, msg);
		g_free(msg);
		return;
	}

    switch (type) {
    case LIBBALSA_INFORMATION_MESSAGE:
        icon_str = "dialog-information";
        break;
    case LIBBALSA_INFORMATION_WARNING:
        icon_str = "dialog-warning";
        break;
    case LIBBALSA_INFORMATION_ERROR:
        icon_str = "dialog-error";
        break;
    default:
        icon_str = NULL;
        break;
    }

	notification = g_notification_new(notify_title);
	if (icon_str != NULL) {
		GIcon *icon;

		icon = g_themed_icon_new(icon_str);
		g_notification_set_icon(notification, icon);
		g_object_unref(icon);
	}

	g_notification_set_body(notification, msg);
	if (hide_id != NULL) {
		g_notification_add_button_with_target(notification,
			_("do not show again"), "app.hide-notify", "s", hide_id);
	}
	g_free(msg);

	id = g_strdup_printf("%s%d", id_base, g_atomic_int_add(&serial, 1));
	g_application_send_notification(notify_app, id, notification);
	g_free(id);
	g_object_unref(notification);
}

static void
add_hide_id(GSimpleAction *simple, GVariant *parameter, gpointer G_GNUC_UNUSED user_data)
{
	g_hash_table_add(hide_ids, g_variant_dup_string(parameter, NULL));
}
