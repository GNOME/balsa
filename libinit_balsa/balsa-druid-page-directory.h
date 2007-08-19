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

#ifndef __BALSA_DRUID_PAGE_DIRECTORY_H__
#define __BALSA_DRUID_PAGE_DIRECTORY_H__

#ifdef __cplusplus
extern "C" {
#endif                          /* __cplusplus */

#include "config.h"

#include <gtk/gtk.h>

#include "helper.h"
#include "balsa-initdruid.h"

/*
 * Main object structure
 */
#ifndef __TYPEDEF_BALSA_DRUID_PAGE_DIRECTORY__
#define __TYPEDEF_BALSA_DRUID_PAGE_DIRECTORY__
    typedef struct _BalsaDruidPageDirectory BalsaDruidPageDirectory;
#endif
#define BALSA_DRUID_PAGE_DIRECTORY(obj)	((BalsaDruidPageDirectory *) obj)
    
    enum __ed_types {
        INBOX,
        OUTBOX,
        SENTBOX,
        DRAFTBOX,
        TRASH,
        NUM_EDs
    };

    struct _BalsaDruidPageDirectory {
        GtkWidget *page;
        GtkWidget *inbox;
        GtkWidget *outbox;
        GtkWidget *sentbox;
        GtkWidget *draftbox;
        GtkWidget *trash;
        gboolean paths_locked, need_set;
        EntryMaster emaster;
        EntryData ed[NUM_EDs];
    };

/*
 * Public methods
 */
    void balsa_druid_page_directory(GtkAssistant * druid,
                                    GdkPixbuf * default_logo);
    void balsa_druid_page_directory_later(GtkWidget *druid);


#ifdef __cplusplus
}
#endif                          /* __cplusplus */
#endif
