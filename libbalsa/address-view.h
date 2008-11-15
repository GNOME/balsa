/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2007 Stuart Parmenter and others,
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


#ifndef __LIBBALSA_ADDRESS_VIEW_H__
#define __LIBBALSA_ADDRESS_VIEW_H__

#include <gtk/gtk.h>
#include <gmime/gmime.h>

G_BEGIN_DECLS
#define LIBBALSA_TYPE_ADDRESS_VIEW                              \
    (libbalsa_address_view_get_type())
#define LIBBALSA_ADDRESS_VIEW(obj)                              \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                          \
                                LIBBALSA_TYPE_ADDRESS_VIEW,     \
                                LibBalsaAddressView))
#define LIBBALSA_ADDRESS_VIEW_CLASS(klass)                      \
    (G_TYPE_CHECK_CLASS_CAST((klass),                           \
                             LIBBALSA_TYPE_ADDRESS_VIEW,        \
                             LibBalsaAddressViewClass))
#define LIBBALSA_IS_ADDRESS_VIEW(obj)                           \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj),                          \
                                LIBBALSA_TYPE_ADDRESS_VIEW))
#define LIBBALSA_IS_ADDRESS_VIEW_CLASS(klass)                   \
    (G_TYPE_CHECK_CLASS_TYPE((klass),                           \
                             LIBBALSA_TYPE_ADDRESS_VIEW))
#define LIBBALSA_ADDRESS_VIEW_GET_CLASS(obj)                    \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                           \
                               LIBBALSA_TYPE_ADDRESS_VIEW,      \
                               LibBalsaAddressViewClass))
typedef struct _LibBalsaAddressView LibBalsaAddressView;
typedef struct _LibBalsaAddressViewClass LibBalsaAddressViewClass;

struct _LibBalsaAddressView {
    GtkTreeView parent;

    /*
     * Permanent data
     */
    const gchar *const *types;
    guint n_types;
    gboolean fallback;
    GList *address_book_list;

    gchar *domain;

    GtkTreeViewColumn *type_column;
    GtkTreeViewColumn *focus_column;
    GtkCellRenderer   *focus_cell;

    /*
     * Ephemera
     */
    gboolean last_was_escape;   /* keystroke    */

    GtkTreeRowReference *focus_row;     /* set cursor   */
    guint focus_idle_id;        /* ditto        */

    GtkCellEditable *editable;  /* cell editing */
    gchar *path_string;         /* ditto        */
};

struct _LibBalsaAddressViewClass {
    GtkTreeViewClass parent_class;
};

GType libbalsa_address_view_get_type(void) G_GNUC_CONST;

LibBalsaAddressView *libbalsa_address_view_new(const gchar * const *types,
                                               guint n_types,
                                               GList * address_book_list,
                                               gboolean fallback);
void libbalsa_address_view_set_domain(LibBalsaAddressView * address_view,
                                      const gchar * domain);
void libbalsa_address_view_set_from_string(LibBalsaAddressView *
                                           address_view,
                                           const gchar * address_type,
                                           const gchar * addresses);
void libbalsa_address_view_add_from_string(LibBalsaAddressView *
                                           address_view,
                                           const gchar * address_type,
                                           const gchar * addresses);
void libbalsa_address_view_add_to_row(LibBalsaAddressView * address_view,
                                      GtkTreeRowReference * row_ref,
                                      const gchar * addresses);
void libbalsa_address_view_set_from_list(LibBalsaAddressView *
                                         address_view,
                                         const gchar * address_type,
                                         InternetAddressList * list);

gint libbalsa_address_view_n_addresses(LibBalsaAddressView * address_view);
InternetAddressList *libbalsa_address_view_get_list(LibBalsaAddressView *
                                                    address_view,
                                                    const gchar *
                                                    address_type);

void libbalsa_address_view_set_book_icon(GdkPixbuf * book_icon);
void libbalsa_address_view_set_close_icon(GdkPixbuf * close_icon);
void libbalsa_address_view_set_drop_down_icon(GdkPixbuf * drop_down_icon);

G_END_DECLS
#endif                          /* __LIBBALSA_ADDRESS_VIEW_H__ */
