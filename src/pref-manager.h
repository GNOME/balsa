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

#ifndef __PREF_MANAGER_H__
#define __PREF_MANAGER_H__

#include <gtk/gtk.h>

/* open the preferences manager window */
void open_preferences_manager(GtkWidget * widget, gpointer data);

/* refresh any data displayed in the preferences manager
 * window in case it has changed */
void refresh_preferences_manager(void);

/* update the mail (POP3, IMAP) server clist */
void update_mail_servers(void);

#endif
/* __PREF_MANAGER_H__ */
