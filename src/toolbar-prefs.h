/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2001 Stuart Parmenter and others,
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

#ifndef __TOOLBAR_PREFS_H__
#define __TOOLBAR_PREFS_H__

#define MAXTOOLBARS 10
#define MAXTOOLBARITEMS 50

#define TOOLBAR_BUTTON_TYPE_BUTTON 0
#define TOOLBAR_BUTTON_TYPE_TOGGLE 1
#define TOOLBAR_BUTTON_TYPE_RADIO 2

typedef struct t_button_data {
    char *pixmap_id;   /* not translatable */
    char *button_text; /* translatable */
    char *help_text;   /* translatable */
    int type;
} button_data;

extern button_data toolbar_buttons[];
int get_toolbar_button_index(const char *id);

void customize_dialog_cb(GtkWidget *, gpointer);

#endif
