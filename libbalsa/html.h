/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2003 Stuart Parmenter and others,
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

#ifndef   __LIBBALSA_HTML_H__
# define  __LIBBALSA_HTML_H__

# ifdef HAVE_GTKHTML

#  include <gtk/gtk.h>
#  include <libgnomeprint/gnome-print.h>

typedef void (*LibBalsaHTMLPrintCallback) (GtkWidget * widget,
					   GnomePrintContext *
					   print_context, gdouble x,
					   gdouble y, gdouble width,
					   gdouble height,
					   gpointer user_data);

GtkWidget *libbalsa_html_new(const gchar * text, size_t len,
			     gpointer message,
			     GCallback link_clicked_cb);
gchar *libbalsa_html_to_string(const gchar * text, size_t len);
gboolean libbalsa_html_can_zoom(GtkWidget * widget);
void libbalsa_html_zoom(GtkWidget * widget, gint in_out);
gboolean libbalsa_html_can_select(GtkWidget * widget);
void libbalsa_html_select_all(GtkWidget * widget);
void libbalsa_html_copy(GtkWidget * widget);
gboolean libbalsa_html_can_print(void);
void libbalsa_html_print(GtkWidget * widget,
			 GnomePrintContext * print_context,
			 gdouble header_height, gdouble footer_height,
			 LibBalsaHTMLPrintCallback header_print,
			 LibBalsaHTMLPrintCallback footer_print,
			 gpointer user_data);
gint libbalsa_html_print_get_pages_num(GtkWidget * widget,
				       GnomePrintContext * print_context,
				       gdouble header_height,
				       gdouble footer_height);

gchar *libbalsa_html_from_rich(gchar * text, gint len,
			       gboolean is_richtext);

# endif				/* HAVE_GTKHTML */

#endif				/* __LIBBALSA_HTML_H__ */
