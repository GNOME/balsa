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

#include <gnome.h>

#include "information.h"

static gboolean libbalsa_message_idle_handler(gchar * msg);
static gboolean libbalsa_warning_idle_handler(gchar * msg);
static gboolean libbalsa_error_idle_handler(gchar * msg);
static gboolean libbalsa_debug_idle_handler(gchar * msg);

LibBalsaInformationFunc libbalsa_real_information_func;

/*
 * We are adding an idle handler - we do not need to hold the gdk lock for this.
 *
 * We can't just grab the GDK lock and call the real_error function since this
 * runs a dialog, which has a nested g_main loop - glib doesn't like haveing main 
 * loops active in two threads at one time. When the idle handler gets run it is 
 * from the main thread.
 *
 */
void
libbalsa_information_varg(LibBalsaInformationType type, const char *fmt,
			  va_list ap)
{
    gchar *msg;

    g_assert(libbalsa_real_information_func != NULL);

    /* We format the string here. It must be free()d in the idle
     * handler We parse the args here because by the time the idle
     * function runs we will no longer be in this stack frame. 
     */
    msg = g_strdup_vprintf(fmt, ap);

    switch (type) {
    case LIBBALSA_INFORMATION_MESSAGE:
	g_idle_add((GSourceFunc) libbalsa_message_idle_handler, msg);
	break;
    case LIBBALSA_INFORMATION_WARNING:
	g_idle_add((GSourceFunc) libbalsa_warning_idle_handler, msg);
	break;
    case LIBBALSA_INFORMATION_ERROR:
	g_idle_add((GSourceFunc) libbalsa_error_idle_handler, msg);
	break;
    case LIBBALSA_INFORMATION_DEBUG:
	g_idle_add((GSourceFunc) libbalsa_debug_idle_handler, msg);
	break;
    default:
	g_assert_not_reached();
    }
}

void
libbalsa_information(LibBalsaInformationType type, const char *fmt, ...)
{
    va_list va_args;

    va_start(va_args, fmt);
    libbalsa_information_varg(type, fmt, va_args);
    va_end(va_args);
}

/*
 * These are all idle handlers, so we need to grab the GDK lock 
 */
static gboolean
libbalsa_message_idle_handler(gchar * msg)
{
    gdk_threads_enter();
    libbalsa_real_information_func(LIBBALSA_INFORMATION_MESSAGE, msg);
    gdk_threads_leave();

    g_free(msg);
    return FALSE;
}

static gboolean
libbalsa_warning_idle_handler(gchar * msg)
{
    gdk_threads_enter();
    libbalsa_real_information_func(LIBBALSA_INFORMATION_WARNING, msg);
    gdk_threads_leave();

    g_free(msg);
    return FALSE;
}

static gboolean
libbalsa_error_idle_handler(gchar * msg)
{
    gdk_threads_enter();
    libbalsa_real_information_func(LIBBALSA_INFORMATION_ERROR, msg);
    gdk_threads_leave();

    g_free(msg);
    return FALSE;
}

static gboolean
libbalsa_debug_idle_handler(gchar * msg)
{
    gdk_threads_enter();
    libbalsa_real_information_func(LIBBALSA_INFORMATION_DEBUG, msg);
    gdk_threads_leave();

    g_free(msg);
    return FALSE;
}
