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
#include "balsa-print-object-header.h"

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include "balsa-print-object.h"
#include "balsa-print-object-decor.h"
#include "balsa-print-object-default.h"


/* object related functions */
static void balsa_print_object_header_class_init(BalsaPrintObjectHeaderClass * klass);
static void balsa_print_object_header_init(GTypeInstance * instance,
					    gpointer g_class);
static void balsa_print_object_header_destroy(GObject * self);

static void balsa_print_object_header_draw(BalsaPrintObject * self,
					    GtkPrintContext * context,
					    cairo_t * cairo_ctx);

static void header_add_string(PangoLayout * layout, GString * header_buf,
			      const gchar * field_id, const gchar * label,
			      const gchar * value, gint * p_label_width);
static void header_add_list(PangoLayout * layout, GString * header_buf,
			    const gchar * field_id, const gchar * label,
			    InternetAddressList * values,
			    gint * p_label_width);


static BalsaPrintObjectClass *parent_class = NULL;


GType
balsa_print_object_header_get_type()
{
    static GType balsa_print_object_header_type = 0;

    if (!balsa_print_object_header_type) {
	static const GTypeInfo balsa_print_object_header_info = {
	    sizeof(BalsaPrintObjectHeaderClass),
	    NULL,		/* base_init */
	    NULL,		/* base_finalize */
	    (GClassInitFunc) balsa_print_object_header_class_init,
	    NULL,		/* class_finalize */
	    NULL,		/* class_data */
	    sizeof(BalsaPrintObjectHeader),
	    0,			/* n_preallocs */
	    (GInstanceInitFunc) balsa_print_object_header_init
	};

	balsa_print_object_header_type =
	    g_type_register_static(BALSA_TYPE_PRINT_OBJECT,
				   "BalsaPrintObjectHeader",
				   &balsa_print_object_header_info, 0);
    }

    return balsa_print_object_header_type;
}


static void
balsa_print_object_header_class_init(BalsaPrintObjectHeaderClass * klass)
{
    parent_class = g_type_class_ref(BALSA_TYPE_PRINT_OBJECT);
    BALSA_PRINT_OBJECT_CLASS(klass)->draw =
	balsa_print_object_header_draw;
    G_OBJECT_CLASS(klass)->finalize = balsa_print_object_header_destroy;
}


static void
balsa_print_object_header_init(GTypeInstance * instance, gpointer g_class)
{
    BalsaPrintObjectHeader *po = BALSA_PRINT_OBJECT_HEADER(instance);

    po->headers = NULL;
}


static void
balsa_print_object_header_destroy(GObject * self)
{
    BalsaPrintObjectHeader *po = BALSA_PRINT_OBJECT_HEADER(self);

    g_free(po->headers);
    if (po->face)
	g_object_unref(po->face);

    G_OBJECT_CLASS(parent_class)->finalize(self);
}

static GList *
balsa_print_object_header_new_real(GList * list,
				   GtkPrintContext * context,
				   LibBalsaMessageBody * sig_body,
				   LibBalsaMessageHeaders * headers,
				   const gchar * the_subject,
				   BalsaPrintSetup * psetup)
{
    gchar *subject;
    gchar *date;
    GList *p;
    PangoFontDescription *header_font;
    PangoTabArray *tabs;
    GString *header_buf;
    PangoLayout *test_layout;
    gdouble c_use_width;
    gdouble c_at_x;
    gdouble c_at_y;
    GList *chunks;
    GList *this_chunk;
    guint first_page;
    gint p_label_width;
    gint p_layout_width;
    gdouble c_face_height;
    GdkPixbuf *face;

    g_return_val_if_fail(headers != NULL, NULL);

    /* create a layout for calculating the maximum label width */
    header_font =
	pango_font_description_from_string(balsa_app.print_header_font);
    test_layout = gtk_print_context_create_pango_layout(context);
    pango_layout_set_font_description(test_layout, header_font);
    pango_font_description_free(header_font);
    header_buf = g_string_new("");

    /* collect headers, starting with the subject */
    p_label_width = 0;
    subject = g_strdup(the_subject);
    libbalsa_utf8_sanitize(&subject, balsa_app.convert_unknown_8bit, NULL);
    header_add_string(test_layout, header_buf, "subject", _("Subject:"),
		      subject, &p_label_width);
    g_free(subject);

    /* date */
    date =
	libbalsa_message_headers_date_to_utf8(headers,
					      balsa_app.date_string);
    header_add_string(test_layout, header_buf, "date", _("Date:"), date,
		      &p_label_width);
    g_free(date);

    /* addresses */
    header_add_list(test_layout, header_buf, "from", _("From:"),
		    headers->from, &p_label_width);
    header_add_list(test_layout, header_buf, "to", _("To:"),
		    headers->to_list, &p_label_width);
    header_add_list(test_layout, header_buf, "cc", _("Cc:"),
		    headers->cc_list, &p_label_width);
    header_add_list(test_layout, header_buf, "bcc", _("Bcc:"),
		    headers->bcc_list, &p_label_width);
    header_add_string(test_layout, header_buf, "fcc", _("Fcc:"),
		      headers->fcc_url, &p_label_width);
    header_add_list(test_layout, header_buf, "disposition-notification-to",
		    _("Disposition-Notification-To:"),
		    headers->dispnotify_to, &p_label_width);

    /* user headers */
    p = g_list_first(headers->user_hdrs);
    face = NULL;
    while (p) {
	gchar **pair, *curr_hdr;

	pair = p->data;
	curr_hdr = g_strconcat(pair[0], ":", NULL);
	header_add_string(test_layout, header_buf, pair[0], curr_hdr,
			  pair[1], &p_label_width);
	g_free(curr_hdr);

	/* check for face and x-face */
	if (!face) {
	    GError *err = NULL;
	    GtkWidget * f_widget = NULL;

	    if (!g_ascii_strcasecmp("Face", pair[0]))
		f_widget = libbalsa_get_image_from_face_header(pair[1], &err);
#if HAVE_COMPFACE
	    else if (!g_ascii_strcasecmp("X-Face", pair[0]))
		f_widget = libbalsa_get_image_from_x_face_header(pair[1], &err);
#endif                          /* HAVE_COMPFACE */
	    if (err)
		g_error_free(err);

	    if (f_widget) {
		face = gtk_image_get_pixbuf(GTK_IMAGE(f_widget));
		g_object_ref(G_OBJECT(face));
		gtk_widget_destroy(f_widget);
	    }
	}

	/* next */
	p = g_list_next(p);
    }

    /* add a small space between label and value */
    p_label_width += C_TO_P(C_LABEL_SEP);

#ifdef HAVE_GPGME
    /* add a signature status to the string */
    if (balsa_app.shown_headers != HEADERS_NONE && sig_body &&
	sig_body->parts && sig_body->parts->next &&
	sig_body->parts->next->sig_info) {
	gint prot = libbalsa_message_body_protection(sig_body);

	if ((prot & LIBBALSA_PROTECT_SIGN) &&
	    (prot & (LIBBALSA_PROTECT_RFC3156 | LIBBALSA_PROTECT_SMIMEV3))) {
	    GMimeGpgmeSigstat *siginfo = sig_body->parts->next->sig_info;

	    g_string_append_printf(header_buf, "%s%s\n",
                                   libbalsa_gpgme_sig_protocol_name(siginfo->protocol),
                                   libbalsa_gpgme_sig_stat_to_gchar(siginfo->status));
	}
    }
#endif				/* HAVE_GPGME */

    /* strip the trailing '\n' */
    header_buf = g_string_truncate(header_buf, header_buf->len - 1);

    /* check if we have a face */
    c_use_width = psetup->c_width - 2 * psetup->curr_depth * C_LABEL_SEP;
    if (face) {
	p_layout_width = C_TO_P(c_use_width - gdk_pixbuf_get_width(face) - C_LABEL_SEP);
	c_face_height = gdk_pixbuf_get_height(face);
    } else {
	p_layout_width = C_TO_P(c_use_width);
	c_face_height = 0;
    }

    /* start on new page if less than 2 header lines can be printed or if
     * the face doesn't fit */
    if (psetup->c_y_pos + 2 * P_TO_C(psetup->p_hdr_font_height) > psetup->c_height ||
	psetup->c_y_pos + c_face_height > psetup->c_height) {
	psetup->c_y_pos = 0;
	psetup->page_count++;
    }
    first_page = psetup->page_count - 1;
    c_at_y = psetup->c_y_pos;

    /* configure the layout so we can use Pango to split the text into pages */
    pango_layout_set_indent(test_layout, -p_label_width);
    tabs =
	pango_tab_array_new_with_positions(1, FALSE, PANGO_TAB_LEFT,
					   p_label_width);
    pango_layout_set_tabs(test_layout, tabs);
    pango_tab_array_free(tabs);
    pango_layout_set_width(test_layout, p_layout_width);
    pango_layout_set_wrap(test_layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_alignment(test_layout, PANGO_ALIGN_LEFT);

    /* split the headers into a list fitting on one or more pages */
    chunks =
	split_for_layout(test_layout, header_buf->str, NULL, psetup, TRUE,
			 NULL);
    g_string_free(header_buf, TRUE);

    /* create a list of objects */
    this_chunk = chunks;
    c_at_x = psetup->c_x0 + psetup->curr_depth * C_LABEL_SEP;
    while (this_chunk) {
	BalsaPrintObjectHeader *po;

	po = g_object_new(BALSA_TYPE_PRINT_OBJECT_HEADER, NULL);
	g_assert(po != NULL);
	BALSA_PRINT_OBJECT(po)->on_page = first_page++;
	BALSA_PRINT_OBJECT(po)->c_at_x = c_at_x;
	BALSA_PRINT_OBJECT(po)->c_at_y = psetup->c_y0 + c_at_y;
	BALSA_PRINT_OBJECT(po)->depth = psetup->curr_depth;
	c_at_y = 0.0;
	BALSA_PRINT_OBJECT(po)->c_width = c_use_width;
	/* note: height is calculated when the object is drawn */
	po->headers = (gchar *) this_chunk->data;
	po->p_label_width = p_label_width;
	po->p_layout_width = p_layout_width;
	if (face) {
	    po->face = face;
	    if (!this_chunk->next) {
		gint p_height;

		/* verify that the image is not higher than the headers
		 * if there is a next part, we checked before that the
		 * image fits */
		pango_layout_set_text(test_layout, po->headers, -1);
		pango_layout_get_size(test_layout, NULL, &p_height);
		if (c_face_height > P_TO_C(p_height))
		    psetup->c_y_pos += c_face_height - P_TO_C(p_height);
	    }
	    face = NULL;
	}
	list = g_list_append(list, po);

	this_chunk = g_list_next(this_chunk);
    }
    g_list_free(chunks);
    g_object_unref(G_OBJECT(test_layout));

    return list;
}


GList *
balsa_print_object_header_from_message(GList *list,
				       GtkPrintContext * context,
				       LibBalsaMessage * message,
				       const gchar * subject,
				       BalsaPrintSetup * psetup)
{
    return balsa_print_object_header_new_real(list, context,
					      message->body_list,
					      message->headers, subject,
					      psetup);
}


GList *
balsa_print_object_header_from_body(GList *list,
				    GtkPrintContext * context,
				    LibBalsaMessageBody * body,
				    BalsaPrintSetup * psetup)
{
    return balsa_print_object_header_new_real(list, context, body->parts,
					      body->embhdrs,
					      body->embhdrs->subject,
					      psetup);
}


#ifdef HAVE_GPGME
GList *
balsa_print_object_header_crypto(GList *list, GtkPrintContext * context,
				 LibBalsaMessageBody * body, const gchar * label,
				 BalsaPrintSetup * psetup)
{
    gint first_page;
    gdouble c_at_x;
    gdouble c_at_y;
    gdouble c_use_width;
    PangoFontDescription *header_font;
    PangoLayout *test_layout;
    gchar *textbuf;
    GList *chunks;
    GList *this_chunk;

    /* only if the body has an attached signature info */
    if (!body->sig_info)
	return balsa_print_object_default(list, context, body, psetup);
    
    /* start on new page if less than 2 header lines can be printed */
    if (psetup->c_y_pos + 2 * P_TO_C(psetup->p_hdr_font_height) >
	psetup->c_height) {
	psetup->c_y_pos = 0;
	psetup->page_count++;
    }
    first_page = psetup->page_count - 1;
    c_at_y = psetup->c_y_pos;
    c_use_width = psetup->c_width - 2 * psetup->curr_depth * C_LABEL_SEP;

    /* create a layout for wrapping */
    header_font =
	pango_font_description_from_string(balsa_app.print_header_font);
    test_layout = gtk_print_context_create_pango_layout(context);
    pango_layout_set_font_description(test_layout, header_font);
    pango_font_description_free(header_font);

    /* create a buffer with the signature info */
    textbuf =
	libbalsa_signature_info_to_gchar(body->sig_info,
					 balsa_app.date_string);
    if (label) {
	gchar *newbuf = g_strconcat(label, "\n", textbuf, NULL);

	g_free(textbuf);
	textbuf = newbuf;
    }

    /* configure the layout so we can use Pango to split the text into pages */
    pango_layout_set_width(test_layout, C_TO_P(c_use_width));
    pango_layout_set_wrap(test_layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_alignment(test_layout, PANGO_ALIGN_LEFT);

    /* split the headers into a list fitting on one or more pages */
    chunks = split_for_layout(test_layout, textbuf, NULL, psetup, FALSE, NULL);
    g_object_unref(G_OBJECT(test_layout));
    g_free(textbuf);

    /* create a list of objects */
    this_chunk = chunks;
    c_at_x = psetup->c_x0 + psetup->curr_depth * C_LABEL_SEP;
    while (this_chunk) {
	BalsaPrintObjectHeader *po;

	po = g_object_new(BALSA_TYPE_PRINT_OBJECT_HEADER, NULL);
	g_assert(po != NULL);
	BALSA_PRINT_OBJECT(po)->on_page = first_page++;
	BALSA_PRINT_OBJECT(po)->c_at_x = c_at_x;
	BALSA_PRINT_OBJECT(po)->c_at_y = psetup->c_y0 + c_at_y;
	BALSA_PRINT_OBJECT(po)->depth = psetup->curr_depth;
	c_at_y = 0.0;
	BALSA_PRINT_OBJECT(po)->c_width = c_use_width;
	/* note: height is calculated when the object is drawn */
	po->headers = (gchar *) this_chunk->data;
	po->p_label_width = 0;
	po->p_layout_width = C_TO_P(c_use_width);
	list = g_list_append(list, po);

	this_chunk = g_list_next(this_chunk);
    }
    g_list_free(chunks);

    return list;
}
#endif


static void
balsa_print_object_header_draw(BalsaPrintObject * self,
				GtkPrintContext * context,
				cairo_t * cairo_ctx)
{
    BalsaPrintObjectHeader *po;
    PangoLayout *layout;
    PangoFontDescription *font;
    gint p_height;

    po = BALSA_PRINT_OBJECT_HEADER(self);
    g_assert(po != NULL);

    font = pango_font_description_from_string(balsa_app.print_header_font);
    layout = gtk_print_context_create_pango_layout(context);
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    if (po->p_label_width) {
	PangoTabArray *tabs;
	
	pango_layout_set_indent(layout, -po->p_label_width);
	tabs = pango_tab_array_new_with_positions(1, FALSE, PANGO_TAB_LEFT,
						  po->p_label_width);
	pango_layout_set_tabs(layout, tabs);
	pango_tab_array_free(tabs);
    }
    pango_layout_set_width(layout, po->p_layout_width);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
    pango_layout_set_text(layout, po->headers, -1);
    pango_layout_get_size(layout, NULL, &p_height);
    self->c_height = P_TO_C(p_height);	/* needed to properly print borders */
    cairo_move_to(cairo_ctx, self->c_at_x, self->c_at_y);
    pango_cairo_show_layout(cairo_ctx, layout);
    g_object_unref(G_OBJECT(layout));

    /* print a face image */
    if (po->face) {
	gdouble c_face_h;
	gdouble c_face_w;

	c_face_h = gdk_pixbuf_get_height(po->face);
	c_face_w = gdk_pixbuf_get_width(po->face);

	cairo_print_pixbuf(cairo_ctx, po->face,
			   self->c_at_x + self->c_width - c_face_w,
			   self->c_at_y, 1.0);
	if (c_face_h > self->c_height)
	    self->c_height = c_face_h;
    }
}


static void
header_add_string(PangoLayout * layout, GString * header_buf,
		  const gchar * field_id, const gchar * label,
		  const gchar * value, gint * p_label_width)
{
    gchar *_value;
    gint p_width;

    if (!value || balsa_app.shown_headers == HEADERS_NONE ||
	!(balsa_app.show_all_headers ||
	  balsa_app.shown_headers == HEADERS_ALL ||
	  libbalsa_find_word(field_id, balsa_app.selected_headers)))
	return;

    p_width = p_string_width_from_layout(layout, label);
    if (p_width > *p_label_width)
	*p_label_width = p_width;
    _value = g_strdup(value);
    libbalsa_utf8_sanitize(&_value, balsa_app.convert_unknown_8bit, NULL);
    g_string_append_printf(header_buf, "%s\t%s\n", label, _value);
    g_free(_value);
}


static void
header_add_list(PangoLayout * layout, GString * header_buf,
		const gchar * field_id, const gchar * label,
		InternetAddressList * values, gint * p_label_width)
{
    gchar *_value;
    gint p_width;

    if (balsa_app.shown_headers == HEADERS_NONE ||
	!(balsa_app.show_all_headers ||
	  balsa_app.shown_headers == HEADERS_ALL ||
	  libbalsa_find_word(field_id, balsa_app.selected_headers)) ||
        !values ||
        !(_value = internet_address_list_to_string(values, FALSE)))
	return;

    p_width = p_string_width_from_layout(layout, label);
    if (p_width > *p_label_width)
	*p_label_width = p_width;
    libbalsa_utf8_sanitize(&_value, balsa_app.convert_unknown_8bit, NULL);
    g_string_append_printf(header_buf, "%s\t%s\n", label, _value);
    g_free(_value);
}
