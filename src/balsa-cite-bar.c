/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2001 Stuart Parmenter and others
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

#include <string.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include "balsa-cite-bar.h"


static void balsa_cite_bar_class_init(BalsaCiteBarClass *class);
static void balsa_cite_bar_init(BalsaCiteBar *cite_bar);
static void balsa_cite_bar_destroy(GtkObject *object);
static void balsa_cite_bar_realise(GtkWidget *widget);
static void balsa_cite_bar_size_request(GtkWidget *widget, GtkRequisition *requisition);
static void balsa_cite_bar_size_allocate(GtkWidget *widget, GtkAllocation *allocation);
static gboolean balsa_cite_bar_expose(GtkWidget *widget, GdkEventExpose *event);


static GtkWidgetClass *parent_class = NULL;


GType
balsa_cite_bar_get_type(void)
{
    static GType cite_bar_type = 0;
    
    if (!cite_bar_type) {
	static const GTypeInfo cite_bar_info = {
	    sizeof(BalsaCiteBarClass),
	    NULL,           /* base_init */
	    NULL,
	    (GClassInitFunc) balsa_cite_bar_class_init,
	    NULL,           /* class_finalize */
	    NULL,           /* class_init */
	    sizeof (BalsaCiteBar),
	    0,              /* n_preallocs */
	    (GInstanceInitFunc) balsa_cite_bar_init,
	    NULL,           /* value_table */
	};
	cite_bar_type =
	    g_type_register_static(GTK_TYPE_WIDGET, "BalsaCiteBar",
				   &cite_bar_info, 0);
    }

    return cite_bar_type;
}

static void
balsa_cite_bar_class_init(BalsaCiteBarClass *class)
{
    GtkObjectClass *object_class;
    GtkWidgetClass *widget_class;

    object_class = (GtkObjectClass*) class;
    widget_class = (GtkWidgetClass*) class;

    parent_class = gtk_type_class(gtk_widget_get_type());

    object_class->destroy = balsa_cite_bar_destroy;

    widget_class->realize = balsa_cite_bar_realise;
    widget_class->expose_event = balsa_cite_bar_expose;
    widget_class->size_request = balsa_cite_bar_size_request;
    widget_class->size_allocate = balsa_cite_bar_size_allocate;
}

static void
balsa_cite_bar_init(BalsaCiteBar *cite_bar)
{
    cite_bar->width = 0;
    cite_bar->height = 0;
}

GtkWidget*
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
balsa_cite_bar_resize(BalsaCiteBar *cite_bar, gint height)
{
    g_return_if_fail(cite_bar != NULL);
    g_return_if_fail(BALSA_IS_CITE_BAR(cite_bar));

    cite_bar->height = height;
    gtk_widget_queue_resize(GTK_WIDGET(cite_bar));
}

static void
balsa_cite_bar_destroy(GtkObject *object)
{
    g_return_if_fail(object != NULL);
    g_return_if_fail(BALSA_IS_CITE_BAR(object));

    if (GTK_OBJECT_CLASS (parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy)(object);
}

static void
balsa_cite_bar_realise(GtkWidget *widget)
{
    BalsaCiteBar *cite_bar;
    GdkWindowAttr attributes;
    gint attributes_mask;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(BALSA_IS_CITE_BAR(widget));

    GTK_WIDGET_SET_FLAGS(widget, GTK_REALIZED);
    cite_bar = BALSA_CITE_BAR(widget);

    attributes.x = widget->allocation.x;
    attributes.y = widget->allocation.y;
    attributes.width = widget->allocation.width;
    attributes.height = widget->allocation.height;
    attributes.wclass = GDK_INPUT_OUTPUT;
    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.event_mask = gtk_widget_get_events(widget) | 
	GDK_EXPOSURE_MASK;
    attributes.visual = gtk_widget_get_visual(widget);
    attributes.colormap = gtk_widget_get_colormap(widget);

    attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
    widget->window = gdk_window_new(widget->parent->window, &attributes, attributes_mask);

    widget->style = gtk_style_attach(widget->style, widget->window);

    gdk_window_set_user_data(widget->window, widget);

    gtk_style_set_background(widget->style, widget->window, GTK_STATE_NORMAL);
}

static void 
balsa_cite_bar_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
    BalsaCiteBar *cite_bar;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(BALSA_IS_CITE_BAR(widget));
    cite_bar = BALSA_CITE_BAR(widget);

    requisition->width = (cite_bar->bars - 1) * (cite_bar->width + cite_bar->space) +
	cite_bar->width;
    requisition->height = cite_bar->height;
}

static void
balsa_cite_bar_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
    g_return_if_fail(widget != NULL);
    g_return_if_fail(BALSA_IS_CITE_BAR(widget));

    widget->allocation = *allocation;
    if (GTK_WIDGET_REALIZED(widget))
	gdk_window_move_resize(widget->window,
			       allocation->x, allocation->y,
			       allocation->width, allocation->height);
}

static gboolean
balsa_cite_bar_expose(GtkWidget *widget, GdkEventExpose *event)
{
    BalsaCiteBar *cite_bar;
    int n;

    g_return_val_if_fail(widget != NULL, FALSE);
    g_return_val_if_fail(BALSA_IS_CITE_BAR(widget), FALSE);
    g_return_val_if_fail(event != NULL, FALSE);

    if (event->count > 0)
	return FALSE;
  
    cite_bar = BALSA_CITE_BAR(widget);
    gdk_window_clear(widget->window);
    for (n = 0; n < cite_bar->bars; n++)
	gdk_draw_rectangle(widget->window, widget->style->fg_gc[GTK_STATE_NORMAL], TRUE,
			   n * (cite_bar->width + cite_bar->space), 0,
			   cite_bar->width, cite_bar->height);
    return FALSE;
}
