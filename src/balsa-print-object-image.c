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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "balsa-print-object-image.h"
#include "balsa-print-object-default.h"

#include <gtk/gtk.h>


struct _BalsaPrintObjectImage {
    BalsaPrintObject parent;

    GdkPixbuf *pixbuf;
    gdouble c_img_offs;
    gdouble scale;
};


G_DEFINE_TYPE(BalsaPrintObjectImage, balsa_print_object_image, BALSA_TYPE_PRINT_OBJECT)


/* object related functions */
static void balsa_print_object_image_destroy(GObject * self);

static void balsa_print_object_image_draw(BalsaPrintObject * self,
					  GtkPrintContext * context,
					  cairo_t * cairo_ctx);


static void
balsa_print_object_image_class_init(BalsaPrintObjectImageClass * klass)
{
    BALSA_PRINT_OBJECT_CLASS(klass)->draw =	balsa_print_object_image_draw;
    G_OBJECT_CLASS(klass)->finalize = balsa_print_object_image_destroy;
}


static void
balsa_print_object_image_init(BalsaPrintObjectImage *self)
{
    self->pixbuf = NULL;
    self->scale = 1.0;
}


static void
balsa_print_object_image_destroy(GObject * self)
{
    BalsaPrintObjectImage *po = BALSA_PRINT_OBJECT_IMAGE(self);

    if (po->pixbuf != NULL) {
    	g_object_unref(po->pixbuf);
    }
    G_OBJECT_CLASS(balsa_print_object_image_parent_class)->finalize(self);
}


GList *
balsa_print_object_image(GList * list, GtkPrintContext *context,
			 LibBalsaMessageBody *body, BalsaPrintSetup * psetup)
{
    BalsaPrintObjectImage *poi;
    BalsaPrintObject *po;
    BalsaPrintRect rect;
    GdkPixbuf *pixbuf;
    GError *err = NULL;
    gdouble c_use_width;

    /* check if we can handle the image */
    pixbuf = libbalsa_message_body_get_pixbuf(body, &err);
    if (err) {
	g_warning("Error loading image from file: %s", err->message);
	g_error_free(err);
    }

    /* fall back to default if the pixbuf could no be loaded */
    if (!pixbuf)
      return balsa_print_object_default(list, context, body, psetup);

    /* create the part */
    poi = g_object_new(BALSA_TYPE_PRINT_OBJECT_IMAGE, NULL);
    g_assert(poi != NULL);
    po = BALSA_PRINT_OBJECT(poi);
    poi->pixbuf = pixbuf;
    c_use_width = psetup->c_width - 2 * psetup->curr_depth * C_LABEL_SEP;
    rect.c_width = gdk_pixbuf_get_width(pixbuf);
    rect.c_height = gdk_pixbuf_get_height(pixbuf);
    poi->scale = 1.0;

    /* check if we should scale the width */
    if (rect.c_width > c_use_width) {
    	poi->scale = c_use_width / rect.c_width;
    	rect.c_height *= poi->scale;
    	rect.c_width = c_use_width;
    }

    /* check if the image is too high for one full page */
    if (rect.c_height > psetup->c_height) {
    	gdouble hscale = psetup->c_height / rect.c_height;

    	poi->scale *= hscale;
    	rect.c_width *= hscale;
    	rect.c_height = psetup->c_height;
    }

    /* check if we should move to the next page */
    if (psetup->c_y_pos + rect.c_height > psetup->c_height) {
    	psetup->c_y_pos = 0;
    	psetup->page_count++;
    }

    /* remember the extent */
    balsa_print_object_set_page_depth(po, psetup->page_count - 1, psetup->curr_depth);
    rect.c_at_x = psetup->c_x0 + psetup->curr_depth * C_LABEL_SEP;
    rect.c_at_y = psetup->c_y0 + psetup->c_y_pos;
    poi->c_img_offs = 0.5 * (c_use_width - rect.c_width);
    balsa_print_object_set_rect(po, &rect);

    /* adjust the y position */
    psetup->c_y_pos += rect.c_height;

    return g_list_append(list, po);
}


static void
balsa_print_object_image_draw(BalsaPrintObject * self,
			      GtkPrintContext * context,
			      cairo_t * cairo_ctx)
{
    BalsaPrintObjectImage *poi;
	const BalsaPrintRect *rect;

    /* prepare */
    poi = BALSA_PRINT_OBJECT_IMAGE(self);
    rect = balsa_print_object_get_rect(self);
    g_assert(poi != NULL);

    /* print the image */
    cairo_print_pixbuf(cairo_ctx, poi->pixbuf, rect->c_at_x + poi->c_img_offs,
    		rect->c_at_y, poi->scale);
}
