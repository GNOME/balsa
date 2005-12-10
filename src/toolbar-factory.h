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

typedef void (*BalsaToolbarFunc)(GtkWidget *, gpointer);
#define BALSA_TOOLBAR_FUNC(f) ((BalsaToolbarFunc) (f))

typedef enum { 
    TOOLBAR_INVALID = -1,
    TOOLBAR_MAIN = 0, /* main    window toolbar, main-window.c */
    TOOLBAR_COMPOSE,  /* compose window toolbar, sendmsg-window.c */
    TOOLBAR_MESSAGE,  /* message window toolbar, message-window.c */
    STOCK_TOOLBAR_COUNT
} BalsaToolbarType;

typedef enum {
    TOOLBAR_BUTTON_TYPE_BUTTON,
    TOOLBAR_BUTTON_TYPE_TOGGLE,
    TOOLBAR_BUTTON_TYPE_RADIO
} BalsaToolbarButtonType;

typedef struct t_button_data {
    char *pixmap_id;   /* not translatable */
    char *button_text; /* translatable */
    char *help_text;   /* translatable */
    BalsaToolbarButtonType type;
} button_data;

extern button_data toolbar_buttons[];
extern const int toolbar_button_count;

typedef struct BalsaToolbarModel_ BalsaToolbarModel;

void update_all_toolbars(void);
void balsa_toolbar_remove_all(GtkWidget *toolbar);

/* toolbar code for gtk+-2 */
const gchar * balsa_toolbar_sanitize_id(const gchar *id);

/* BalsaToolbarModel */
BalsaToolbarModel *balsa_toolbar_model_new(GSList * legal,
                                           GSList * standard,
                                           GSList ** current);
GSList *balsa_toolbar_model_get_legal(BalsaToolbarModel * model);
GSList *balsa_toolbar_model_get_current(BalsaToolbarModel * model);
void balsa_toolbar_model_insert_icon(BalsaToolbarModel * model,
                                     gchar * icon, gint position);
void balsa_toolbar_model_delete_icon(BalsaToolbarModel * model,
                                     gchar * icon);
void balsa_toolbar_model_clear(BalsaToolbarModel * model);

/* BalsaToolbar */
GtkWidget *balsa_toolbar_new(BalsaToolbarModel * model);
GtkWidget *balsa_toolbar_get_from_gnome_app(GnomeApp * app);
guint balsa_toolbar_set_callback(GtkWidget * toolbar, const gchar * icon,
                                 GCallback callback, gpointer user_data);
void balsa_toolbar_set_button_sensitive(GtkWidget * toolbar,
                                        const gchar * icon,
                                        gboolean sensitive);
gboolean balsa_toolbar_get_button_active(GtkWidget * toolbar,
                                         const gchar * icon);
void balsa_toolbar_set_button_active(GtkWidget * toolbar,
                                     const gchar * icon, gboolean active);
void balsa_toolbar_refresh(GtkWidget * toolbar);

#endif
