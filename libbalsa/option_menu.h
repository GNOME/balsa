#ifndef __option_menu_h__
#define __option_menu_h__ 1
/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2004 Stuart Parmenter and others,
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
GObject *libbalsa_option_menu_new(void);
void libbalsa_option_menu_append(GObject *w, const char *name, void *data);
GtkWidget* libbalsa_option_menu_get_widget(GObject *w,
                                           GCallback cb, void *data);
void libbalsa_option_menu_set_active(GtkWidget *w, int idx);
void* libbalsa_option_menu_get_active_data(GtkWidget *widget);

#endif
