/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "information.h"
#include "libbalsa.h"
#include <string.h>

static GNotification *notification;

static void
lbi_notification_parent_weak_notify(gpointer data, GObject * parent)
{
    if (notification == NULL)
        return;

    g_object_set_data(G_OBJECT(notification), "send", GINT_TO_POINTER(FALSE));
    g_signal_emit_by_name(notification, "notify", NULL);
}

void
libbalsa_information_varg(GtkWindow *parent, LibBalsaInformationType type,
                          const char *fmt, va_list ap)
{
    gchar *msg, *p, *q;
    GString *escaped;
    const gchar *icon_str;
    GIcon *icon;
    gboolean send;

    if (notification == NULL)
        return;

    g_return_if_fail(fmt != NULL);

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

    icon = g_themed_icon_new(icon_str);
    g_notification_set_icon(notification, icon);
    g_object_unref(icon);

    msg = g_strdup_vprintf(fmt, ap);
    /* GNotification uses HTML markup (???), so we must replace '<' and
     * '&' with the corresponding entity in the message string. */
    escaped = g_string_new(NULL);
    for (p = msg; (q = strpbrk(p, "<>&\"")) != NULL; p = ++q) {
        g_string_append_len(escaped, p, q - p);
        switch (*q) {
        case '<':
            g_string_append(escaped, "&lt;");
            break;
        case '>':
            g_string_append(escaped, "&gt;");
            break;
        case '&':
            g_string_append(escaped, "&amp;");
            break;
        case '"':
            g_string_append(escaped, "&quot;");
            break;
        default:
            break;
        }
    }
    g_string_append(escaped, p);
    g_free(msg);

    g_notification_set_body(notification, escaped->str);
    send = *escaped->str != '\0';
    g_string_free(escaped, TRUE);

    g_object_set_data(G_OBJECT(notification), "send", GINT_TO_POINTER(send));
    g_signal_emit_by_name(notification, "notify", NULL);

    if (parent != NULL) {
        /* Close with parent if earlier. */
        g_object_weak_ref(G_OBJECT(parent),
                          lbi_notification_parent_weak_notify, NULL);
    }
}

void
libbalsa_information(LibBalsaInformationType type,
                     const char *fmt, ...)
{
    va_list va_args;

#ifndef DEBUG
    if (type == LIBBALSA_INFORMATION_DEBUG) return;
#endif
    va_start(va_args, fmt);
    libbalsa_information_varg(NULL, type, fmt, va_args);
    va_end(va_args);
}

void
libbalsa_information_parented(GtkWindow *parent, LibBalsaInformationType type,
                              const char *fmt, ...)
{
    va_list va_args;

    va_start(va_args, fmt);
    libbalsa_information_varg(parent, type, fmt, va_args);
    va_end(va_args);
}

GNotification *
libbalsa_notification_new(const gchar *title)
{
    notification = g_notification_new(title);

    g_object_add_weak_pointer(G_OBJECT(notification), (gpointer *) &notification);

    return notification;
}
