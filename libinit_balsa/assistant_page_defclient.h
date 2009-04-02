/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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

#include <gtk/gtk.h>

#ifndef __BALSA_DRUID_PAGE_DEFCLIENT_H__
#define __BALSA_DRUID_PAGE_DEFCLIENT_H__

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#ifdef __cplusplus
extern "C" {
#endif                          /* __cplusplus */

#if HAVE_GNOME
/* setting the default Gnome mail client doesn't make sense if we don't build
 for Gnome */

#include "assistant_helper.h"
#include "assistant_init.h"

/*
 * Main object structure
 */
#ifndef __TYPEDEF_BALSA_DRUID_PAGE_DEFCLIENT__
#define __TYPEDEF_BALSA_DRUID_PAGE_DEFCLIENT__
    typedef struct _BalsaDruidPageDefclient BalsaDruidPageDefclient;
#endif
#define BALSA_DRUID_PAGE_DEFCLIENT(obj) ((BalsaDruidPageDefclient *) obj)
    struct _BalsaDruidPageDefclient {
        int default_client;
    };

/*
 * Public methods
 */
    void balsa_druid_page_defclient(GtkAssistant * druid, 
                                    GdkPixbuf * default_logo);
    void balsa_druid_page_defclient_save(BalsaDruidPageDefclient * defclient);

#endif /* HAVE_GNOME */

#ifdef __cplusplus
}
#endif                          /* __cplusplus */
#endif
