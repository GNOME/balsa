/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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

#include "config.h"

#include "information.h"
#ifdef HAVE_NOTIFY
#include <libnotify/notify.h>
#include <gtk/gtkstock.h>
#endif

struct information_data {
    GtkWindow *parent;
    LibBalsaInformationType message_type;
    gchar *msg;
};

static gboolean libbalsa_information_idle_handler(struct information_data*);

LibBalsaInformationFunc libbalsa_real_information_func;

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
#ifdef HAVE_NOTIFY
    NotifyNotification *note;
    char *icon_str;
#endif

    g_return_if_fail(fmt != NULL);
    g_assert(libbalsa_real_information_func != NULL);

    /* We format the string here. It must be free()d in the idle
     * handler We parse the args here because by the time the idle
     * function runs we will no longer be in this stack frame. 
     */

#ifdef HAVE_NOTIFY
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
    if(notify_is_initted()) {
        gchar *msg = g_strdup_vprintf(fmt,ap);
        note = notify_notification_new("Balsa", msg, icon_str, NULL);
        g_free(msg);
        notify_notification_set_timeout (note, 7000); /* 7 seconds */
        notify_notification_show (note, NULL);
        g_object_unref(G_OBJECT(note));
        return;
    }
    /* Fall through to the ordinary notification scheme */
#endif
    data = g_new(struct information_data, 1);
    data->parent = parent;
    data->message_type = type;
    data->msg = g_strdup_vprintf(fmt, ap);
    if(parent)
        g_object_add_weak_pointer(G_OBJECT(parent), (gpointer) &data->parent);
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
