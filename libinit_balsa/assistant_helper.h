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

#ifndef LIBINIT_BALSA_HELPER_H
#define LIBINIT_BALSA_HELPER_H

#include <gtk/gtk.h>

typedef struct EntryData_s EntryData;
typedef struct EntryController_s EntryController;

struct EntryData_s {
    GtkAssistant *druid;
    GtkWidget    *page;
    guint num;
    EntryController *controller;
};

#define ENTRY_DATA_INIT { NULL, 0 }

struct EntryController_s {
    guint32 setbits;
    guint32 numentries;
    guint32 donemask;
};

#define ENTRY_CONTROLLER_INIT { 0, 0, 0 }
#define ENTRY_CONTROLLER_DONE( e ) ( ((e)->setbits & (e)->donemask) == (e)->donemask )

GtkWidget *balsa_init_add_grid_entry(GtkGrid * grid, gint num,
                                     const gchar * ltext,
                                     const gchar * etext, EntryData * ed,
                                     GtkAssistant * druid,
                                     GtkWidget * page, GtkWidget ** dest);
void balsa_init_add_grid_option(GtkGrid *grid, gint num,
                                const gchar *ltext, const gchar **optns,
                                GtkAssistant *druid, GtkWidget **dest);
gint balsa_option_get_active(GtkWidget *option_widget);

gboolean balsa_init_create_to_directory(const gchar * dir,
                                        gchar ** complaint);

#endif
