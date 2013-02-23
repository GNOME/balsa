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

#ifndef LIBINIT_BALSA_HELPER_H
#define LIBINIT_BALSA_HELPER_H

#include <gtk/gtk.h>

typedef struct EntryData_s EntryData;
typedef struct EntryMaster_s EntryMaster;

struct EntryData_s {
    GtkAssistant *druid;
    GtkWidget    *page;
    guint num;
    EntryMaster *master;
};

#define ENTRY_DATA_INIT { NULL, 0 }

struct EntryMaster_s {
    guint32 setbits;
    guint32 numentries;
    guint32 donemask;
};

#define ENTRY_MASTER_INIT { 0, 0, 0 }
#define ENTRY_MASTER_P_DONE( ep ) ( ((ep)->setbits & (ep)->donemask) == (ep)->donemask )
#define ENTRY_MASTER_DONE( e ) ( ((e).setbits & (e).donemask) == (e).donemask )

GdkPixbuf *balsa_init_get_png(const gchar * fname);

void balsa_init_add_grid_entry(GtkGrid * grid, guint num, const gchar * ltext,
                               const gchar * etext, EntryData * ed,
                               GtkAssistant * druid, GtkWidget *page,
                               GtkWidget ** dest);
void balsa_init_add_grid_option(GtkGrid *grid, guint num,
                                const gchar *ltext, const gchar **optns,
                                GtkAssistant *druid, GtkWidget **dest);
void balsa_init_add_grid_checkbox(GtkGrid *grid, guint num,
                                  const gchar *ltext, gboolean defval,
                                  GtkAssistant *druid, GtkWidget **dest);
gint balsa_option_get_active(GtkWidget *option_widget);

gboolean balsa_init_create_to_directory(const gchar * dir,
                                        gchar ** complaint);

#endif
