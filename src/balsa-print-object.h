/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2019 Stuart Parmenter and others
 * Written by (C) Albrecht Dreß <albrecht.dress@arcor.de> 2007
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

#ifndef __BALSA_PRINT_OBJECT_H__
#define __BALSA_PRINT_OBJECT_H__

#include <gtk/gtk.h>
#include <libbalsa.h>
#include "balsa-app.h"
#include "balsa-message.h"

G_BEGIN_DECLS


/*  == print helper stuff ==  */
/* note:
 * stuff starting with "c_" is in cairo units (= points = 1/72")
 * stuff starting with "p_" is in pango units
 */
typedef struct {
    gdouble c_width;
    gdouble c_height;
    gdouble c_x0;
    gdouble c_y0;
    gdouble c_y_pos;

    gint p_hdr_font_height;
    gint p_body_font_height;

    gint page_count;
    guint curr_depth;
} BalsaPrintSetup;


typedef struct {
    gdouble c_at_x;
    gdouble c_at_y;
    gdouble c_width;
    gdouble c_height;
} BalsaPrintRect;


#define P_TO_C(x)               ((gdouble)(x) / (gdouble)PANGO_SCALE)
#define C_TO_P(x)               ((gdouble)(x) * (gdouble)PANGO_SCALE)

#define C_LABEL_SEP             6
#define C_SEPARATOR             12
#define C_HEADER_SEP            12


gint p_string_width_from_layout(PangoLayout * layout, const gchar * text);
gint p_string_height_from_layout(PangoLayout * layout, const gchar * text);
gboolean cairo_print_pixbuf(cairo_t * cairo_ctx, const GdkPixbuf * pixbuf,
			    gdouble c_at_x, gdouble c_at_y, gdouble scale);
GList *split_for_layout(PangoLayout * layout, const gchar * text,
			PangoAttrList * attributes,
			BalsaPrintSetup * psetup, gboolean is_header,
			GArray ** offsets);


/*  == print object base class stuff ==  */

#define BALSA_TYPE_PRINT_OBJECT		balsa_print_object_get_type()
G_DECLARE_DERIVABLE_TYPE(BalsaPrintObject, balsa_print_object, BALSA, PRINT_OBJECT, GObject)


struct _BalsaPrintObjectClass {
    GObjectClass parent;

    void (*draw) (BalsaPrintObject *self, GtkPrintContext *context, cairo_t *cairo_ctx);
};


GList *balsa_print_objects_append_from_body(GList               *list,
					    					GtkPrintContext     *context,
											LibBalsaMessageBody *mime_body,
											BalsaPrintSetup     *psetup)
	G_GNUC_WARN_UNUSED_RESULT;
void balsa_print_object_draw(BalsaPrintObject *self,
			     	 	 	 GtkPrintContext  *context,
							 cairo_t          *cairo_ctx);

gint balsa_print_object_get_page(BalsaPrintObject *self);
const BalsaPrintRect *balsa_print_object_get_rect(BalsaPrintObject *self);

void balsa_print_object_set_page_depth(BalsaPrintObject *self,
								 	   gint              page,
								       guint             depth);
void balsa_print_object_set_rect(BalsaPrintObject     *self,
								 const BalsaPrintRect *rect);
void balsa_print_object_set_height(BalsaPrintObject *self,
								   gdouble           height);

G_END_DECLS


#endif				/* __BALSA_PRINT_OBJECT_H__ */
