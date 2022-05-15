/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2019 Stuart Parmenter and others
 * Written by (C) Albrecht Dreß <albrecht.dress@arcor.de> 2019
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

#include "balsa-print-object-html.h"
#include "balsa-print-object-default.h"

#ifdef HAVE_HTML_WIDGET


#include <gtk/gtk.h>
#include "html.h"


struct _BalsaPrintObjectHtml {
    BalsaPrintObject parent;

    cairo_surface_t *html_surface;
    gdouble c_y_offs;
    gdouble scale;
};


G_DEFINE_TYPE(BalsaPrintObjectHtml, balsa_print_object_html, BALSA_TYPE_PRINT_OBJECT)


/* object related functions */
static void balsa_print_object_html_destroy(GObject * self);
static void balsa_print_object_html_draw(BalsaPrintObject *self,
					  	  	  	  	  	 GtkPrintContext  *context,
										 cairo_t          *cairo_ctx);


static void
balsa_print_object_html_class_init(BalsaPrintObjectHtmlClass *klass)
{
    BALSA_PRINT_OBJECT_CLASS(klass)->draw = balsa_print_object_html_draw;
    G_OBJECT_CLASS(klass)->finalize = balsa_print_object_html_destroy;
}


static void
balsa_print_object_html_init(BalsaPrintObjectHtml *po)
{
    po->html_surface = NULL;
    po->scale = 1.0;
}


static void
balsa_print_object_html_destroy(GObject *self)
{
    BalsaPrintObjectHtml *po = BALSA_PRINT_OBJECT_HTML(self);

    if (po->html_surface) {
    	cairo_surface_destroy(po->html_surface);
    }
    G_OBJECT_CLASS(balsa_print_object_html_parent_class)->finalize(self);
}


GList *
balsa_print_object_html(GList 				*list,
						GtkPrintContext     *context,
						LibBalsaMessageBody *body,
						BalsaPrintSetup     *psetup)
{
	cairo_surface_t *html_surface;
	BalsaPrintObjectHtml *poh;
    BalsaPrintObject *po;
    gdouble surface_width;
    gdouble height_left;
    gdouble chunk_y_offs;
    gdouble c_use_width;
    gdouble scale;

    c_use_width = psetup->c_width - 2.0 * psetup->curr_depth * C_LABEL_SEP;

    /* render the HTML part into a surface, fall back to default if this fails */
    html_surface = libbalsa_html_print_bitmap(body, c_use_width);
    if (html_surface == NULL) {
    	return balsa_print_object_default(list, context, body, psetup);
    }

    /* calculate the scale for the surface */
    surface_width = (gdouble) cairo_image_surface_get_width(html_surface);
    if (surface_width > c_use_width) {
    	scale = c_use_width / surface_width;
    	surface_width = c_use_width;
    } else {
    	scale = 1.0;
    }
    height_left = (gdouble) cairo_image_surface_get_height(html_surface) * scale;

    /* split the surface into parts fitting on pages */
    chunk_y_offs = 0.0;
    do {
    	BalsaPrintRect rect;

        /* create the part */
        poh = g_object_new(BALSA_TYPE_PRINT_OBJECT_HTML, NULL);
        g_assert(poh != NULL);
        po = BALSA_PRINT_OBJECT(poh);
        poh->html_surface = cairo_surface_reference(html_surface);
        poh->scale = scale;
        poh->c_y_offs = chunk_y_offs;

        /* extent */
        balsa_print_object_set_page_depth(po, psetup->page_count - 1, psetup->curr_depth);
        rect.c_at_x = psetup->c_x0 + psetup->curr_depth * C_LABEL_SEP;
        rect.c_at_y = psetup->c_y0 + psetup->c_y_pos;
        rect.c_width = surface_width;
        if (psetup->c_y_pos + height_left > psetup->c_height) {
        	rect.c_height = psetup->c_height - psetup->c_y_pos;
        	chunk_y_offs += rect.c_height;
        	height_left -= rect.c_height;
        	psetup->c_y_pos = 0.0;
        	psetup->page_count++;
        } else {
        	rect.c_height = height_left;
        	psetup->c_y_pos += height_left;
        	height_left = 0.0;
        }
        balsa_print_object_set_rect(po, &rect);
        list = g_list_append(list, po);
    } while (height_left > 0.0);

    /* references held by the parts */
    cairo_surface_destroy(html_surface);

    return list;
}


static void
balsa_print_object_html_draw(BalsaPrintObject *self,
			      	  	  	 GtkPrintContext  *context,
							 cairo_t 		  *cairo_ctx)
{
	BalsaPrintObjectHtml *poh;
	const BalsaPrintRect *rect;
    cairo_pattern_t *pattern;
    cairo_matrix_t matrix;

    /* prepare */
    poh = BALSA_PRINT_OBJECT_HTML(self);
    rect = balsa_print_object_get_rect(self);
    g_assert(poh != NULL);

    /* save current state */
    cairo_save(cairo_ctx);

    /* set surface */
    cairo_set_source_surface(cairo_ctx, poh->html_surface, rect->c_at_x, rect->c_at_y);

    /* scale */
    pattern = cairo_get_source(cairo_ctx);
    cairo_pattern_get_matrix(pattern, &matrix);
    matrix.xx /= poh->scale;
    matrix.yy /= poh->scale;
    matrix.x0 /= poh->scale;
    matrix.y0  = (matrix.y0 + poh->c_y_offs) / poh->scale;
    cairo_pattern_set_matrix(pattern, &matrix);

    /* clip around the chunk */
    cairo_new_path(cairo_ctx);
    cairo_move_to(cairo_ctx, rect->c_at_x, rect->c_at_y);
    cairo_line_to(cairo_ctx, rect->c_at_x + rect->c_width, rect->c_at_y);
    cairo_line_to(cairo_ctx, rect->c_at_x + rect->c_width, rect->c_at_y + rect->c_height);
    cairo_line_to(cairo_ctx, rect->c_at_x, rect->c_at_y + rect->c_height);
    cairo_close_path(cairo_ctx);
    cairo_clip(cairo_ctx);

    /* paint */
    cairo_paint(cairo_ctx);
    cairo_restore(cairo_ctx);
}

#endif	/* HAVE_HTML_WIDGET */
