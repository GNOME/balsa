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

#ifndef __TOOLBAR_FACTORY_H__
#define __TOOLBAR_FACTORY_H__


typedef enum { 
    TOOLBAR_INVALID = -1,
    TOOLBAR_MAIN = 0, /* main    window toolbar, main-window.c */
    TOOLBAR_COMPOSE,  /* compose window toolbar, sendmsg-window.c */
    TOOLBAR_MESSAGE,  /* message window toolbar, message-window.c */
    STOCK_TOOLBAR_COUNT
} BalsaToolbarType;

typedef struct t_button_data {
    char *pixmap_id;   /* not translatable */
    char *button_text; /* translatable */
    char *help_text;   /* translatable */
    int type;
} button_data;

extern button_data toolbar_buttons[];
extern const int toolbar_button_count;

int create_stock_toolbar(BalsaToolbarType id);
int get_toolbar_index(BalsaToolbarType id);
void set_toolbar_button_callback(BalsaToolbarType toolbar, const char *id, 
				 void (*callback)(GtkWidget *, gpointer), 
				 gpointer);
void set_toolbar_button_sensitive(GtkWidget *window, BalsaToolbarType toolbar,
				  char *id, int sensitive);
GtkToolbar *get_toolbar(GtkWidget *window, BalsaToolbarType toolbar);
void release_toolbars(GtkWidget *window);
GtkWidget *get_tool_widget(GtkWidget *window, BalsaToolbarType toolbar, 
			   char *id);
void update_all_toolbars(void);
char **get_legal_toolbar_buttons(int toolbar);

#endif
