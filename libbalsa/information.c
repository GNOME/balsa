/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2013 Stuart Parmenter and others,
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "information.h"
#include "libbalsa.h"

#ifdef HAVE_NOTIFY
#include <libnotify/notify.h>
#include <gtk/gtk.h>
#endif
#include <string.h>

struct information_data {
    GtkWindow *parent;
    LibBalsaInformationType message_type;
    gchar *msg;
};

static gboolean libbalsa_information_idle_handler(struct information_data*);

LibBalsaInformationFunc libbalsa_real_information_func;

#ifdef HAVE_NOTIFY
static void lbi_notification_closed_cb(NotifyNotification * note,
                                       gpointer data);

static void
lbi_notification_parent_weak_notify(gpointer data, GObject * parent)
{
    NotifyNotification *note = NOTIFY_NOTIFICATION(data);
    g_signal_handlers_disconnect_by_func(note, lbi_notification_closed_cb,
                                         parent);
    notify_notification_close(note, NULL);
    g_object_unref(note);
}

static void
lbi_notification_closed_cb(NotifyNotification * note, gpointer data)
{
    GObject *parent = G_OBJECT(data);
    g_object_weak_unref(parent, lbi_notification_parent_weak_notify, note);
    g_object_unref(note);
}
#endif

/*
 * We are adding an idle handler - we do not need to hold the gdk lock
 * for this.
 *
 * We can't just grab the GDK lock and call the real_error function
 * since this runs a dialog, which has a nested g_main loop - glib
 * doesn't like haveing main loops active in two threads at one
 * time. When the idle handler gets run it is from the main thread.
 *
 */
void
libbalsa_information_varg(GtkWindow *parent, LibBalsaInformationType type,
                          const char *fmt, va_list ap)
{
    struct information_data *data;

    g_return_if_fail(fmt != NULL);
    g_assert(libbalsa_real_information_func != NULL);

#ifdef HAVE_NOTIFY
    if (notify_is_initted()) {
        gchar *msg, *p, *q;
        GString *escaped;
        NotifyNotification *note;
        char *icon_str;

        switch (type) {
        case LIBBALSA_INFORMATION_MESSAGE:
            icon_str = GTK_STOCK_DIALOG_INFO;
            break;
        case LIBBALSA_INFORMATION_WARNING:
            icon_str = GTK_STOCK_DIALOG_WARNING;
            break;
        case LIBBALSA_INFORMATION_ERROR:
            icon_str = GTK_STOCK_DIALOG_ERROR;
            break;
        default:
            icon_str = NULL;
            break;
        }
        msg = g_strdup_vprintf(fmt, ap);
        /* libnotify/DBUS uses HTML markup, so we must replace '<' and
         * '&' with the corresponding entity in the message string. */
        escaped = g_string_new(NULL);
        for (p = msg; (q = strpbrk(p, "<>&\"")) != NULL; p = ++q) {
            g_string_append_len(escaped, p, q - p);
            switch (*q) {
                case '<': g_string_append(escaped, "&lt;");   break;
                case '>': g_string_append(escaped, "&gt;");   break;
                case '&': g_string_append(escaped, "&amp;");  break;
                case '"': g_string_append(escaped, "&quot;"); break;
                default: break;
            }
        }
        g_string_append(escaped, p);
        g_free(msg);

#if HAVE_NOTIFY >= 7
        note = notify_notification_new("Balsa", escaped->str, icon_str);
#else
        /* prior to 0.7.0 */
        note = notify_notification_new("Balsa", escaped->str, icon_str, NULL);
#endif

        g_string_free(escaped, TRUE);

        notify_notification_set_timeout(note, 7000);    /* 7 seconds */
        notify_notification_show(note, NULL);
        if (parent) {
            /* Close with parent if earlier. */
            g_object_weak_ref(G_OBJECT(parent),
                              lbi_notification_parent_weak_notify, note);
            g_signal_connect(note, "closed",
                             G_CALLBACK(lbi_notification_closed_cb),
                             parent);
        } else
            g_object_unref(note);
        return;
    }
    /* Fall through to the ordinary notification scheme */
#endif
    data = g_new(struct information_data, 1);
    data->parent = parent;
    data->message_type = type;

    /* We format the string here. It must be free()d in the idle
     * handler We parse the args here because by the time the idle
     * function runs we will no longer be in this stack frame. 
     */
    data->msg = g_strdup_vprintf(fmt, ap);
    if (parent)
        g_object_add_weak_pointer(G_OBJECT(parent),
                                  (gpointer) & data->parent);
    g_idle_add((GSourceFunc) libbalsa_information_idle_handler, data);
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

/*
 * This is an idle handler, so we need to grab the GDK lock 
 */
static gboolean
libbalsa_information_idle_handler(struct information_data *data)
{
    gdk_threads_enter();
    libbalsa_real_information_func(data->parent,
                                   data->message_type,
                                   data->msg);
    gdk_threads_leave();

    if(data->parent)
        g_object_remove_weak_pointer(G_OBJECT(data->parent), 
                                     (gpointer) &data->parent);
    g_free(data->msg);
    g_free(data);
    return FALSE;
}
