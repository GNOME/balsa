/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
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

#ifndef __BALSA_DRUID_PAGE_SERVER_H__
#define __BALSA_DRUID_PAGE_SERVER_H__

#include <gtk/gtk.h>
#include "assistant_helper.h"
#include "assistant_init.h"

G_BEGIN_DECLS

/*
 * Main object structure
 */
#ifndef __TYPEDEF_BALSA_DRUID_PAGE_SERVER__
#define __TYPEDEF_BALSA_DRUID_PAGE_SERVER__
typedef struct _BalsaDruidPageServer BalsaDruidPageServer;
#endif
#define BALSA_DRUID_PAGE_SERVER(obj) ((BalsaDruidPageServer *) obj)
struct _BalsaDruidPageServer {
	GtkWidget *page;
	GtkWidget *incoming_type;
	GtkWidget *incoming_srv;
	GtkWidget *login;
	GtkWidget *passwd;
	GtkWidget *smtp;
	GtkWidget *remember_passwd;
	gboolean need_set;
};

/*
 * Public methods
 */
void balsa_druid_page_server(GtkAssistant * druid);

G_END_DECLS

#endif
