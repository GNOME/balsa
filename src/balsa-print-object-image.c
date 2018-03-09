/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2016 Stuart Parmenter and others
 * Written by (C) Albrecht Dre� <albrecht.dress@arcor.de> 2007
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
#   include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "balsa-print-object-image.h"
#include "balsa-print-object-default.h"

#include <gtk/gtk.h>

/* object related functions */
static void balsa_print_object_image_class_init(BalsaPrintObjectImageClass *klass);
static void balsa_print_object_image_init(GTypeInstance *instance,
                                          gpointer       g_class);
static void balsa_print_object_image_dispose(GObject *self);

static void balsa_print_object_image_draw(BalsaPrintObject *self,
                                          GtkPrintContext  *context,
                                          cairo_t          *cairo_ctx);


static BalsaPrintObjectClass *parent_class = NULL;


GType
balsa_print_object_image_get_type()
{
    static GType balsa_print_object_image_type = 0;

    if (!balsa_print_object_image_type) {
        static const GTypeInfo balsa_print_object_image_info = {
            sizeof(BalsaPrintObjectImageClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
            (GClassInitFunc) balsa_print_object_image_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
            sizeof(BalsaPrintObjectImage),
            0,                  /* n_preallocs */
            (GInstanceInitFunc) balsa_print_object_image_init
        };

        balsa_print_object_image_type =
            g_type_register_static(BALSA_TYPE_PRINT_OBJECT,
                                   "BalsaPrintObjectImage",
                                   &balsa_print_object_image_info, 0);
    }

    return balsa_print_object_image_type;
}


static void
balsa_print_object_image_class_init(BalsaPrintObjectImageClass *klass)
{
    parent_class = g_type_class_ref(BALSA_TYPE_PRINT_OBJECT);
    BALSA_PRINT_OBJECT_CLASS(klass)->draw =
        balsa_print_object_image_draw;
    G_OBJECT_CLASS(klass)->dispose = balsa_print_object_image_dispose;
}


static void
balsa_print_object_image_init(GTypeInstance *instance,
                              gpointer       g_class)
{
    BalsaPrintObjectImage *po = BALSA_PRINT_OBJECT_IMAGE(instance);

    po->pixbuf = NULL;
    po->scale = 1.0;
}


static void
balsa_print_object_image_dispose(GObject *self)
{
    BalsaPrintObjectImage *po = BALSA_PRINT_OBJECT_IMAGE(self);

    g_clear_object(&po->pixbuf);

    G_OBJECT_CLASS(parent_class)->dispose(self);
}


GList *
balsa_print_object_image(GList               *list,
                         GtkPrintContext     *context,
                         LibBalsaMessageBody *body,
                         BalsaPrintSetup     *psetup)
{
    BalsaPrintObjectImage *poi;
    BalsaPrintObject *po;
    GdkPixbuf *pixbuf;
    GError *err = NULL;
    gdouble c_use_width;
    gdouble c_img_width;
    gdouble c_img_height;

    /* check if we can handle the image */
    pixbuf = libbalsa_message_body_get_pixbuf(body, &err);
    if (err) {
        g_message("Error loading image from file: %s", err->message);
        g_error_free(err);
    }

    /* fall back to default if the pixbuf could no be loaded */
    if (!pixbuf) {
        return balsa_print_object_default(list, context, body, psetup);
    }

    /* create the part */
    poi = g_object_new(BALSA_TYPE_PRINT_OBJECT_IMAGE, NULL);
    g_assert(poi != NULL);
    po = BALSA_PRINT_OBJECT(poi);
    po->depth = psetup->curr_depth;
    poi->pixbuf = pixbuf;
    c_use_width = psetup->c_width - 2 * psetup->curr_depth * C_LABEL_SEP;
    c_img_width = gdk_pixbuf_get_width(pixbuf);
    c_img_height = gdk_pixbuf_get_height(pixbuf);
    poi->scale = 1.0;

    /* check if we should scale the width */
    if (c_img_width > c_use_width) {
        poi->scale = c_use_width / c_img_width;
        c_img_height *= poi->scale;
        c_img_width = c_use_width;
    }

    /* check if the image is too high for one full page */
    if (c_img_height > psetup->c_height) {
        gdouble hscale = psetup->c_height / c_img_height;
        poi->scale *= hscale;
        c_img_width *= hscale;
        c_img_height = psetup->c_height;
    }

    /* check if we should move to the next page */
    if (psetup->c_y_pos + c_img_height > psetup->c_height) {
        psetup->c_y_pos = 0;
        psetup->page_count++;
    }

    /* remember the extent */
    po->on_page = psetup->page_count - 1;
    po->c_at_x = psetup->c_x0 + psetup->curr_depth * C_LABEL_SEP;
    po->c_at_y = psetup->c_y0 + psetup->c_y_pos;
    po->c_width = c_img_width;
    poi->c_img_offs = 0.5 * (c_use_width - c_img_width);
    po->c_height = c_img_height;

    /* adjust the y position */
    psetup->c_y_pos += c_img_height;

    return g_list_append(list, po);
}


static void
balsa_print_object_image_draw(BalsaPrintObject *self,
                              GtkPrintContext  *context,
                              cairo_t          *cairo_ctx)
{
    BalsaPrintObjectImage *poi;

    /* prepare */
    poi = BALSA_PRINT_OBJECT_IMAGE(self);
    g_assert(poi != NULL);

    /* print the image */
    cairo_print_pixbuf(cairo_ctx, poi->pixbuf, self->c_at_x + poi->c_img_offs,
                       self->c_at_y, poi->scale);
}
