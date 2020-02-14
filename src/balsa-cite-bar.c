/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2019 Stuart Parmenter and others
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "balsa-cite-bar.h"

struct _BalsaCiteBar {
    GtkWidget widget;

    /* Dimensions of each citation bar */
    gint width;
    gint height;

    /* bar count and spacing */
    gint bars;
    gint space;
};

static void balsa_cite_bar_get_preferred_width (GtkWidget * widget,
                                                gint      * minimum_width,
                                                gint      * natural_width);
static void balsa_cite_bar_get_preferred_height(GtkWidget * widget,
                                                gint      * minimum_height,
                                                gint      * natural_height);
static gboolean balsa_cite_bar_draw            (GtkWidget * widget,
                                                cairo_t   * cr);

G_DEFINE_TYPE(BalsaCiteBar, balsa_cite_bar, GTK_TYPE_WIDGET)

static void
balsa_cite_bar_class_init(BalsaCiteBarClass * class)
{
    GtkWidgetClass *widget_class;

    widget_class = (GtkWidgetClass *) class;

    widget_class->get_preferred_width  = balsa_cite_bar_get_preferred_width;
    widget_class->get_preferred_height = balsa_cite_bar_get_preferred_height;
    widget_class->draw                 = balsa_cite_bar_draw;
}

static void
balsa_cite_bar_init(BalsaCiteBar * cite_bar)
{
    gtk_widget_set_has_window(GTK_WIDGET(cite_bar), FALSE);
}

GtkWidget *
balsa_cite_bar_new(gint height, gint bars, gint dimension)
{
    BalsaCiteBar *cite_bar;

    g_return_val_if_fail(dimension > 1, NULL);

    cite_bar = g_object_new(BALSA_TYPE_CITE_BAR, NULL);

    cite_bar->height = height;
    cite_bar->bars = bars;

    /* the width is 1/4 of the dimension, the spaceing 3/4, but both
     * at least 1 pixel */
    cite_bar->width = dimension / 4;
    if (cite_bar->width == 0)
        cite_bar->width = 1;
    cite_bar->space = dimension - cite_bar->width;

    return GTK_WIDGET(cite_bar);
}

void
balsa_cite_bar_resize(BalsaCiteBar * cite_bar, gint height)
{
    g_return_if_fail(BALSA_IS_CITE_BAR(cite_bar));

    cite_bar->height = height;
    gtk_widget_queue_resize(GTK_WIDGET(cite_bar));
}

static void
balsa_cite_bar_get_preferred_width(GtkWidget * widget,
                                   gint      * minimum_width,
                                   gint      * natural_width)
{
    BalsaCiteBar *cite_bar;

    cite_bar = BALSA_CITE_BAR(widget);
    *minimum_width = *natural_width =
        cite_bar->bars * (cite_bar->width + cite_bar->space) -
        cite_bar->space;
}

static void
balsa_cite_bar_get_preferred_height(GtkWidget * widget,
                                    gint      * minimum_height,
                                    gint      * natural_height)
{
    BalsaCiteBar *cite_bar;

    cite_bar = BALSA_CITE_BAR(widget);
    *minimum_height = *natural_height = cite_bar->height;
}

static gboolean
balsa_cite_bar_draw(GtkWidget * widget, cairo_t * cr)
{
    GtkStyleContext *context;
    GdkRGBA rgba;
    BalsaCiteBar *cite_bar;
    int n, x;

    context = gtk_widget_get_style_context(widget);
    gtk_style_context_get_color(context, GTK_STATE_FLAG_NORMAL, &rgba);
    gdk_cairo_set_source_rgba(cr, &rgba);

    cite_bar = BALSA_CITE_BAR(widget);
    for (n = x = 0; n < cite_bar->bars; n++) {
        cairo_rectangle(cr, x, 0, cite_bar->width, cite_bar->height);
        cairo_fill(cr);
        x += cite_bar->width + cite_bar->space;
    }

    return FALSE;
}
