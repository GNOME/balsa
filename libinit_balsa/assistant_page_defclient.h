/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gtk/gtk.h>

#ifndef __BALSA_DRUID_PAGE_DEFCLIENT_H__
#define __BALSA_DRUID_PAGE_DEFCLIENT_H__

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#include "assistant_helper.h"
#include "assistant_init.h"

G_BEGIN_DECLS

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
    void balsa_druid_page_defclient(GtkAssistant * druid);
    void balsa_druid_page_defclient_save(BalsaDruidPageDefclient * defclient);

G_END_DECLS

#endif
