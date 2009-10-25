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

#include <glib-object.h>
#include "toolbar-prefs.h"

GType balsa_toolbar_model_get_type(void);

#define BALSA_TYPE_TOOLBAR_MODEL \
    (balsa_toolbar_model_get_type ())
#define BALSA_TOOLBAR_MODEL(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST (obj, BALSA_TYPE_TOOLBAR_MODEL, \
                                 BalsaToolbarModel))
#define BALSA_TOOLBAR_MODEL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST (klass, BALSA_TYPE_TOOLBAR_MODEL, \
                              BalsaToolbarModelClass))
#define BALSA_IS_TOOLBAR_MODEL(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE (obj, BALSA_TYPE_TOOLBAR_MODEL))
#define BALSA_IS_TOOLBAR_MODEL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE (klass, BALSA_TYPE_TOOLBAR_MODEL))

typedef struct BalsaToolbarModel_ BalsaToolbarModel;
typedef struct BalsaToolbarModelClass_ BalsaToolbarModelClass;

struct BalsaToolbarModelClass_ {
    GObjectClass parent_class;
};
typedef void (*BalsaToolbarFunc) (GtkWidget *, gpointer);
#define BALSA_TOOLBAR_FUNC(f) ((BalsaToolbarFunc) (f))

typedef struct t_button_data {
    char *pixmap_id;            /* not translatable */
    char *button_text;          /* translatable */
    gboolean is_important;      /* whether to show beside icon */
} button_data;

extern button_data toolbar_buttons[];
extern const int toolbar_button_count;

typedef enum {
    BALSA_TOOLBAR_TYPE_MAIN_WINDOW,
    BALSA_TOOLBAR_TYPE_COMPOSE_WINDOW,
    BALSA_TOOLBAR_TYPE_MESSAGE_WINDOW
} BalsaToolbarType;

void update_all_toolbars(void);
void balsa_toolbar_remove_all(GtkWidget * toolbar);

/* toolbar code for gtk+-2 */
const gchar *balsa_toolbar_button_text(gint button);
const gchar *balsa_toolbar_sanitize_id(const gchar * id);

/* BalsaToolbarModel */
BalsaToolbarModel *balsa_toolbar_model_new(BalsaToolbarType type,
                                           GSList * standard);
void balsa_toolbar_model_add_actions(BalsaToolbarModel * model,
                                     const GtkActionEntry * entries,
                                     guint n_entries);
void balsa_toolbar_model_add_toggle_actions(BalsaToolbarModel * model,
                                            const GtkToggleActionEntry *
                                            entries, guint n_entries);
GHashTable *balsa_toolbar_model_get_legal(BalsaToolbarModel * model);
GSList *balsa_toolbar_model_get_current(BalsaToolbarModel * model);
gboolean balsa_toolbar_model_is_standard(BalsaToolbarModel * model);
void balsa_toolbar_model_insert_icon(BalsaToolbarModel * model,
                                     gchar * icon, gint position);
void balsa_toolbar_model_delete_icon(BalsaToolbarModel * model,
                                     gchar * icon);
void balsa_toolbar_model_clear(BalsaToolbarModel * model);
void balsa_toolbar_model_changed(BalsaToolbarModel * model);

/* BalsaToolbar */
GtkWidget *balsa_toolbar_new(BalsaToolbarModel * model,
                             GtkUIManager * ui_manager);

#endif
