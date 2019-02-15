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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __BALSA_PRINT_OBJECT_HTML_H__
#define __BALSA_PRINT_OBJECT_HTML_H__

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#ifndef HAVE_HTML_WIDGET

#include "balsa-print-object-default.h"

/* fall back to the default print object if we don't have HTML support */
#define balsa_print_object_html		\
		balsa_print_object_default

#else

#include "balsa-print-object.h"

G_BEGIN_DECLS

#define BALSA_TYPE_PRINT_OBJECT_HTML			\
	(balsa_print_object_html_get_type())
#define BALSA_PRINT_OBJECT_HTML(obj)			\
	G_TYPE_CHECK_INSTANCE_CAST(obj, BALSA_TYPE_PRINT_OBJECT_HTML, BalsaPrintObjectHtml)
#define BALSA_PRINT_OBJECT_HTML_CLASS(klass)	\
	G_TYPE_CHECK_CLASS_CAST(klass, BALSA_TYPE_PRINT_OBJECT_HTML, BalsaPrintObjectHtmlClass)
#define BALSA_IS_PRINT_OBJECT_HTML(obj)			\
	G_TYPE_CHECK_INSTANCE_TYPE(obj, BALSA_TYPE_PRINT_OBJECT_HTML)


typedef struct _BalsaPrintObjectHtmlClass BalsaPrintObjectHtmlClass;
typedef struct _BalsaPrintObjectHtml BalsaPrintObjectHtml;


struct _BalsaPrintObjectHtml {
    BalsaPrintObject parent;

    cairo_surface_t *html_surface;
    gdouble c_y_offs;
    gdouble scale;
};


struct _BalsaPrintObjectHtmlClass {
    BalsaPrintObjectClass parent;
};


GType balsa_print_object_html_get_type(void);
GList *balsa_print_object_html(GList *list,
				GtkPrintContext *context,
				LibBalsaMessageBody *body,
				BalsaPrintSetup *psetup);


G_END_DECLS

#endif				/* HAVE_HTML_WIDGET */

#endif				/* __BALSA_PRINT_OBJECT_IMAGE_H__ */
