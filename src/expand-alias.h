/* Balsa E-Mail Client
 * Copyright (C) 1998 Jay Painter and Stuart Parmenter
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

#ifndef __BALSA_EXPANDALIAS_H__
#define __BALSA_EXPANDALIAS_H__

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */

extern void next_entrybox (GtkWidget *widget, GtkWidget *entry);
extern gboolean key_pressed_cb(GtkWidget *widget, GdkEventKey *event,
	gpointer user_data);
extern gboolean button_pressed_cb(GtkWidget *widget, GdkEventButton *event,
	gpointer user_data);
extern gboolean lost_focus_cb(GtkWidget *widget, GdkEventFocus *event,
	gpointer user_data);
extern void destroy_cb (GtkWidget *widget, gpointer user_data);
extern void alias_load_addressbook (void);
extern void alias_free_addressbook (void);
 
#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __BALSA_EXPANDALIAS_H__ */
