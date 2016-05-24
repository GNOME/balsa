/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
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

#ifndef __BALSA_DRUID_PAGE_USER_H__
#define __BALSA_DRUID_PAGE_USER_H__

#include <gtk/gtk.h>
#include "assistant_helper.h"
#include "assistant_init.h"

G_BEGIN_DECLS

/*
 * Main object structure
 */
#ifndef __TYPEDEF_BALSA_DRUID_PAGE_USER__
#define __TYPEDEF_BALSA_DRUID_PAGE_USER__
    typedef struct _BalsaDruidPageUser BalsaDruidPageUser;
#endif
#define BALSA_DRUID_PAGE_USER(obj) ((BalsaDruidPageUser *) obj)
    struct _BalsaDruidPageUser {
        GtkWidget *page;
        GtkWidget *incoming_srv;
        GtkWidget *incoming_type;
        GtkWidget *using_ssl;
        GtkWidget *login;
        GtkWidget *passwd;
        GtkWidget *remember_passwd;
#if ENABLE_ESMTP
        GtkWidget *smtp;
#endif
        GtkWidget *name;
        GtkWidget *email;
#if !defined(ENABLE_TOUCH_UI)
        GtkWidget *localmaildir;
#endif /* ENABLE_TOUCH_UI */
        EntryMaster emaster;
        EntryData ed0;
        EntryData ed1;
        EntryData ed2;
        EntryData ed3;
        EntryData ed4;
        EntryData ed5;
        EntryData ed6;
        EntryData ed7;
        gboolean need_set;
    };

/*
 * Public methods
 */
    void balsa_druid_page_user(GtkAssistant * druid);

G_END_DECLS

#endif
