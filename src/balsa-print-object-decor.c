/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2013 Stuart Parmenter and others
 * Written by (C) Albrecht Dreﬂ <albrecht.dress@arcor.de> 2007
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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "balsa-print-object-decor.h"

#include <gtk/gtk.h>


/* object related functions */
static void
balsa_print_object_decor_class_init(BalsaPrintObjectDecorClass *klass);
static void balsa_print_object_decor_init (GTypeInstance *instance, gpointer g_class);
static void balsa_print_object_decor_destroy(GObject * self);

static void balsa_print_object_decor_draw(BalsaPrintObject * self,
					  GtkPrintContext * context,
					  cairo_t * cairo_ctx);


static BalsaPrintObjectClass *parent_class = NULL;


GType
balsa_print_object_decor_get_type()
{
    static GType balsa_print_object_decor_type = 0;

    if (!balsa_print_object_decor_type) {
	static const GTypeInfo balsa_print_object_decor_info = {
	    sizeof(BalsaPrintObjectDecorClass),
	    NULL,		/* base_init */
	    NULL,		/* base_finalize */
	    (GClassInitFunc) balsa_print_object_decor_class_init,
	    NULL,		/* class_finalize */
	    NULL,		/* class_data */
	    sizeof(BalsaPrintObjectDecor),
	    0,			/* n_preallocs */
	    (GInstanceInitFunc) balsa_print_object_decor_init
	};

	balsa_print_object_decor_type =
	    g_type_register_static(BALSA_TYPE_PRINT_OBJECT,
				   "BalsaPrintObjectDecor",
				   &balsa_print_object_decor_info, 0);
    }

    return balsa_print_object_decor_type;
}


static void
balsa_print_object_decor_class_init(BalsaPrintObjectDecorClass *klass)
{
    parent_class = g_type_class_ref(BALSA_TYPE_PRINT_OBJECT);
    BALSA_PRINT_OBJECT_CLASS(klass)->draw =
	balsa_print_object_decor_draw;
    G_OBJECT_CLASS(klass)->finalize = balsa_print_object_decor_destroy;
}


static void
balsa_print_object_decor_init(GTypeInstance * instance,
			      gpointer g_class)
{
    BalsaPrintObjectDecor *po = BALSA_PRINT_OBJECT_DECOR(instance);

    po->label = NULL;
}


static void
balsa_print_object_decor_destroy(GObject * self)
{
    BalsaPrintObjectDecor *po = BALSA_PRINT_OBJECT_DECOR(self);

    g_free(po->label);

    G_OBJECT_CLASS(parent_class)->finalize(self);
}


static BalsaPrintObject *
decor_new_real(BalsaPrintSetup * psetup, BalsaPrintDecorType mode)
{
    BalsaPrintObjectDecor *pod;
    BalsaPrintObject *po;

    pod = g_object_new(BALSA_TYPE_PRINT_OBJECT_DECOR, NULL);
    g_assert(pod != NULL);
    po = BALSA_PRINT_OBJECT(pod);

    /* create the part */
    po->depth = psetup->curr_depth;
    pod->mode = mode;

    /* check if it should start on a new page */
    if (psetup->c_y_pos + C_SEPARATOR > psetup->c_height) {
	psetup->page_count++;
	psetup->c_y_pos = 0;
    }

    /* remember the extent */
    po->on_page = psetup->page_count - 1;
    po->c_at_x = psetup->c_x0 + po->depth * C_LABEL_SEP;
    po->c_at_y = psetup->c_y0 + psetup->c_y_pos;
    po->c_width = psetup->c_width - 2 * po->depth * C_LABEL_SEP;
    po->c_height = C_SEPARATOR;

    /* adjust the y position */
    psetup->c_y_pos += C_SEPARATOR;

    return po;
}


GList *
balsa_print_object_separator(GList * list, BalsaPrintSetup * psetup)
{
    return g_list_append(list, decor_new_real(psetup, BALSA_PRINT_DECOR_SEPARATOR));
}


GList *
balsa_print_object_frame_begin(GList * list, const gchar * label, BalsaPrintSetup * psetup)
{
    BalsaPrintObjectDecor *pod;
    BalsaPrintObject *po;

    pod = g_object_new(BALSA_TYPE_PRINT_OBJECT_DECOR, NULL);
    g_assert(pod != NULL);
    po = BALSA_PRINT_OBJECT(pod);

    /* create the part */
    po->depth = psetup->curr_depth++;
    pod->mode = BALSA_PRINT_DECOR_FRAME_BEGIN;
    if (label) {
	pod->label = g_strdup(label);
	po->c_height = MAX(P_TO_C(psetup->p_hdr_font_height), C_SEPARATOR) +
	    C_SEPARATOR * 0.5;
    } else
	po->c_height = C_SEPARATOR;

    /* check if it should start on a new page */
    if (psetup->c_y_pos + po->c_height +
	3 * P_TO_C(psetup->p_body_font_height) > psetup->c_height) {
	psetup->page_count++;
	psetup->c_y_pos = 0;
    }

    /* remember the extent */
    po->on_page = psetup->page_count - 1;
    po->c_at_x = psetup->c_x0 + po->depth * C_LABEL_SEP;
    po->c_at_y = psetup->c_y0 + psetup->c_y_pos;
    po->c_width = psetup->c_width - 2 * po->depth * C_LABEL_SEP;

    /* adjust the y position */
    psetup->c_y_pos += po->c_height;

    return g_list_append(list, po);
}


GList *
balsa_print_object_frame_end(GList * list, BalsaPrintSetup * psetup)
{
    g_return_val_if_fail(psetup->curr_depth > 0, list);
    psetup->curr_depth--;
    return g_list_append(list, decor_new_real(psetup, BALSA_PRINT_DECOR_FRAME_END));
}


static void
balsa_print_object_decor_draw(BalsaPrintObject * self,
			      GtkPrintContext * context,
			      cairo_t * cairo_ctx)
{
    BalsaPrintObjectDecor *pod;

    pod = BALSA_PRINT_OBJECT_DECOR(self);
    g_assert(pod != NULL);

    /* draw the decor */
    cairo_save(cairo_ctx);
    cairo_new_path(cairo_ctx);

    switch (pod->mode) {
    case BALSA_PRINT_DECOR_SEPARATOR:
	cairo_set_line_width(cairo_ctx, 1.0);
	cairo_move_to(cairo_ctx, self->c_at_x, self->c_at_y + 0.5 * C_SEPARATOR);
	cairo_line_to(cairo_ctx, self->c_at_x + self->c_width,
		      self->c_at_y + 0.5 * C_SEPARATOR);
	break;

    case BALSA_PRINT_DECOR_FRAME_END:
	cairo_set_line_width(cairo_ctx, 0.25);
	cairo_move_to(cairo_ctx, self->c_at_x, self->c_at_y);
	cairo_line_to(cairo_ctx, self->c_at_x, self->c_at_y + 0.5 * C_SEPARATOR);
	cairo_line_to(cairo_ctx, self->c_at_x + self->c_width,
		      self->c_at_y + 0.5 * C_SEPARATOR);
	cairo_line_to(cairo_ctx, self->c_at_x + self->c_width, self->c_at_y);
	break;

    case BALSA_PRINT_DECOR_FRAME_BEGIN:
	cairo_set_line_width(cairo_ctx, 0.25);
	cairo_move_to(cairo_ctx, self->c_at_x, self->c_at_y + self->c_height);
	if (pod->label) {
	    PangoLayout *layout;
	    PangoFontDescription *font;
	    gint p_label_width;
	    gint p_label_height;
	    gdouble c_y_hor_line;

	    /* print the label */
	    font = pango_font_description_from_string(balsa_app.print_header_font);
	    layout = gtk_print_context_create_pango_layout(context);
	    pango_layout_set_font_description(layout, font);
	    pango_font_description_free(font);
	    pango_layout_set_text(layout, pod->label, -1);
	    pango_layout_get_size(layout, &p_label_width, &p_label_height);
	    c_y_hor_line = self->c_at_y + self->c_height -
		MAX(P_TO_C(p_label_height), C_SEPARATOR) * 0.5;
	    cairo_line_to(cairo_ctx, self->c_at_x, c_y_hor_line);
	    cairo_line_to(cairo_ctx, self->c_at_x + C_LABEL_SEP, c_y_hor_line);
	    cairo_move_to(cairo_ctx, self->c_at_x + 1.5 * C_LABEL_SEP,
			  c_y_hor_line - 0.5 * P_TO_C(p_label_height));
	    pango_cairo_show_layout(cairo_ctx, layout);
	    g_object_unref(G_OBJECT(layout));
	    cairo_move_to(cairo_ctx, self->c_at_x + 2.0 * C_LABEL_SEP + P_TO_C(p_label_width),
			  c_y_hor_line);
	    cairo_line_to(cairo_ctx, self->c_at_x + self->c_width, c_y_hor_line);
	} else {
	    cairo_line_to(cairo_ctx, self->c_at_x,
			  self->c_at_y + self->c_height - 0.5 * C_SEPARATOR);
	    cairo_line_to(cairo_ctx, self->c_at_x + self->c_width,
			  self->c_at_y + self->c_height - 0.5 * C_SEPARATOR);
	}
	cairo_line_to(cairo_ctx, self->c_at_x + self->c_width,
		      self->c_at_y + self->c_height);
	break;
    }

    cairo_stroke(cairo_ctx);
    cairo_restore(cairo_ctx);
}
