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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
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
#include "libbalsa-gpgme.h"


struct _BalsaPrintObjectHeader {
    BalsaPrintObject parent;

    gint p_label_width;
    gint p_layout_width;
    gchar *headers;
    GdkPixbuf *face;
};


G_DEFINE_TYPE(BalsaPrintObjectHeader, balsa_print_object_header, BALSA_TYPE_PRINT_OBJECT)


/* object related functions */
static void balsa_print_object_header_destroy(GObject * self);

static void balsa_print_object_header_draw(BalsaPrintObject * self,
					    GtkPrintContext * context,
					    cairo_t * cairo_ctx);

static void header_add_string(PangoLayout * layout, GString * header_buf,
			      const gchar * field_id, const gchar * label,
			      const gchar * value, gint * p_label_width,
			      gboolean print_all_headers);
static void header_add_list(PangoLayout * layout, GString * header_buf,
			    const gchar * field_id, const gchar * label,
			    InternetAddressList * values,
			    gint * p_label_width,
			    gboolean print_all_headers);


static void
balsa_print_object_header_class_init(BalsaPrintObjectHeaderClass * klass)
{
    BALSA_PRINT_OBJECT_CLASS(klass)->draw = balsa_print_object_header_draw;
    G_OBJECT_CLASS(klass)->finalize = balsa_print_object_header_destroy;
}


static void
balsa_print_object_header_init(BalsaPrintObjectHeader *self)
{
    self->headers = NULL;
}


static void
balsa_print_object_header_destroy(GObject * self)
{
    BalsaPrintObjectHeader *po = BALSA_PRINT_OBJECT_HEADER(self);

    g_free(po->headers);
    if (po->face) {
    	g_object_unref(po->face);
    }
    G_OBJECT_CLASS(balsa_print_object_header_parent_class)->finalize(self);
}

static GList *
balsa_print_object_header_new_real(GList * list,
				   GtkPrintContext * context,
				   LibBalsaMessageBody * sig_body,
				   LibBalsaMessageHeaders * headers,
				   LibBalsaDkim *dkim,
				   const gchar * the_subject,
				   BalsaPrintSetup * psetup,
				   gboolean print_all_headers)
{
    gchar *subject;
    gchar *date;
    GList *p;
    PangoFontDescription *header_font;
    PangoTabArray *tabs;
    GString *header_buf;
    PangoLayout *test_layout;
    BalsaPrintRect rect;
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
		      subject, &p_label_width, print_all_headers);
    g_free(subject);

    /* date */
    date =
	libbalsa_message_headers_date_to_utf8(headers,
					      balsa_app.date_string);
    header_add_string(test_layout, header_buf, "date", _("Date:"), date,
		      &p_label_width, print_all_headers);
    g_free(date);

    /* addresses */
    header_add_list(test_layout, header_buf, "from", _("From:"),
		    headers->from, &p_label_width, print_all_headers);
    header_add_list(test_layout, header_buf, "to", _("To:"),
		    headers->to_list, &p_label_width, print_all_headers);
    header_add_list(test_layout, header_buf, "cc", _("CC:"),
		    headers->cc_list, &p_label_width, print_all_headers);
    header_add_list(test_layout, header_buf, "bcc", _("BCC:"),
		    headers->bcc_list, &p_label_width, print_all_headers);
    header_add_list(test_layout, header_buf, "sender", _("Sender:"),
		    headers->sender, &p_label_width, print_all_headers);
    header_add_string(test_layout, header_buf, "fcc", _("FCC:"),
		      headers->fcc_url, &p_label_width, print_all_headers);
    header_add_list(test_layout, header_buf, "disposition-notification-to",
		    _("Disposition-Notification-To:"),
		    headers->dispnotify_to, &p_label_width, print_all_headers);

    /* user headers */
    p = g_list_first(headers->user_hdrs);
    face = NULL;
    while (p) {
	gchar **pair, *curr_hdr;

	pair = p->data;
	curr_hdr = g_strconcat(pair[0], ":", NULL);
	header_add_string(test_layout, header_buf, pair[0], curr_hdr,
			  pair[1], &p_label_width, print_all_headers);
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
		face = g_object_ref(gtk_image_get_pixbuf(GTK_IMAGE(f_widget)));
		gtk_widget_destroy(f_widget);
	    }
	}

	/* next */
	p = g_list_next(p);
    }

    /* add a small space between label and value */
    p_label_width += C_TO_P(C_LABEL_SEP);

    /* add a signature status to the string */
    if (balsa_app.shown_headers != HEADERS_NONE) {
	    gchar *info_str = NULL;

    	if (libbalsa_message_body_multipart_signed(sig_body)) {
    		info_str = g_mime_gpgme_sigstat_info(sig_body->parts->next->sig_info, TRUE);
    	} else if (libbalsa_message_body_inline_signed(sig_body)) {
    		info_str = g_mime_gpgme_sigstat_info(sig_body->sig_info, TRUE);
    	} else {
    		/* no signature available */
    	}

    	if (info_str != NULL) {
    	    g_string_append_printf(header_buf, "%s\n", info_str);
    	    g_free(info_str);
    	}

        if ((balsa_app.enable_dkim_checks != 0) && (libbalsa_dkim_status_code(dkim) >= DKIM_SUCCESS)) {
        	g_string_append_printf(header_buf, "%s\n", libbalsa_dkim_status_str_short(dkim));
        }
    }

    /* strip the trailing '\n' */
    header_buf = g_string_truncate(header_buf, header_buf->len - 1);

    /* check if we have a face pixbuf */
    rect.c_width = psetup->c_width - 2 * psetup->curr_depth * C_LABEL_SEP;
    if (face != NULL) {
    	p_layout_width = C_TO_P(rect.c_width - gdk_pixbuf_get_width(face) - C_LABEL_SEP);
    	c_face_height = gdk_pixbuf_get_height(face);
    } else {
    	p_layout_width = C_TO_P(rect.c_width);
    	c_face_height = 0.0;
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
    tabs = pango_tab_array_new_with_positions(1, FALSE, PANGO_TAB_LEFT, p_label_width);
    pango_layout_set_tabs(test_layout, tabs);
    pango_tab_array_free(tabs);
    pango_layout_set_width(test_layout, p_layout_width);
    pango_layout_set_wrap(test_layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_alignment(test_layout, PANGO_ALIGN_LEFT);

    /* split the headers into a list fitting on one or more pages */
    chunks = split_for_layout(test_layout, header_buf->str, NULL, psetup, TRUE, NULL);
    g_string_free(header_buf, TRUE);

    /* create a list of objects */
    rect.c_at_x = psetup->c_x0 + psetup->curr_depth * C_LABEL_SEP;	/* fixed for all chunks */
    rect.c_height = -1.0;	/* note: height is calculated when the object is drawn */
    for (this_chunk = chunks; this_chunk != NULL; this_chunk = this_chunk->next) {
    	BalsaPrintObjectHeader *po;

    	po = g_object_new(BALSA_TYPE_PRINT_OBJECT_HEADER, NULL);
    	g_assert(po != NULL);
    	balsa_print_object_set_page_depth(BALSA_PRINT_OBJECT(po), first_page++, psetup->curr_depth);
    	rect.c_at_y = psetup->c_y0 + c_at_y;
    	balsa_print_object_set_rect(BALSA_PRINT_OBJECT(po), &rect);
    	c_at_y = 0.0;

    	/* note: height is calculated when the object is drawn */
    	po->headers = (gchar *) this_chunk->data;
    	po->p_label_width = p_label_width;
    	po->p_layout_width = p_layout_width;
    	if (face != NULL) {
    		po->face = face;
    		if (this_chunk->next == NULL) {
    			gint p_height;

    			/* verify that the image is not higher than the headers
    			 * if there is a next part, we checked before that the
    			 * image fits */
    			pango_layout_set_text(test_layout, po->headers, -1);
    			pango_layout_get_size(test_layout, NULL, &p_height);
    			if (c_face_height > P_TO_C(p_height)) {
    				psetup->c_y_pos += c_face_height - P_TO_C(p_height);
    			}
    		}
    		face = NULL;
    	}
    	list = g_list_append(list, po);
    }
    g_list_free(chunks);
    g_object_unref(test_layout);

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
					      libbalsa_message_get_body_list(message),
					      libbalsa_message_get_headers(message),
						  libbalsa_message_get_body_list(message)->dkim,
                                              subject, psetup, FALSE);
}


GList *
balsa_print_object_header_from_body(GList *list,
				    GtkPrintContext * context,
				    LibBalsaMessageBody * body,
				    BalsaPrintSetup * psetup)
{
    return balsa_print_object_header_new_real(list, context, body->parts,
					      body->embhdrs, body->dkim,
					      body->embhdrs->subject,
					      psetup,
					      body->body_type == LIBBALSA_MESSAGE_BODY_TYPE_TEXT);
}


GList *
balsa_print_object_header_crypto(GList 				 *list,
								 GtkPrintContext     *context,
								 LibBalsaMessageBody *body,
								 BalsaPrintSetup     *psetup)
{
	gint first_page;
	BalsaPrintRect rect;
	gdouble c_at_y;
	PangoFontDescription *header_font;
	PangoLayout *test_layout;
	gchar *textbuf;
	GList *chunks;
	GList *this_chunk;

	/* only if the body has an attached signature info */
	if ((body->sig_info == NULL) || (g_mime_gpgme_sigstat_status(body->sig_info) == GPG_ERR_NOT_SIGNED)) {
		return balsa_print_object_default(list, context, body, psetup);
	}

	/* start on new page if less than 2 header lines can be printed */
	if (psetup->c_y_pos + 2 * P_TO_C(psetup->p_hdr_font_height) > psetup->c_height) {
		psetup->c_y_pos = 0;
		psetup->page_count++;
	}
	first_page = psetup->page_count - 1;
	c_at_y = psetup->c_y_pos;
	rect.c_width = psetup->c_width - 2 * psetup->curr_depth * C_LABEL_SEP;

	/* create a layout for wrapping */
	header_font = pango_font_description_from_string(balsa_app.print_header_font);
	test_layout = gtk_print_context_create_pango_layout(context);
	pango_layout_set_font_description(test_layout, header_font);
	pango_font_description_free(header_font);

	/* create a buffer with the signature info */
	textbuf = g_mime_gpgme_sigstat_to_gchar(body->sig_info, TRUE, balsa_app.date_string);

	/* configure the layout so we can use Pango to split the text into pages */
	pango_layout_set_width(test_layout, C_TO_P(rect.c_width));
	pango_layout_set_wrap(test_layout, PANGO_WRAP_WORD_CHAR);
	pango_layout_set_alignment(test_layout, PANGO_ALIGN_LEFT);

	/* split the headers into a list fitting on one or more pages */
	chunks = split_for_layout(test_layout, textbuf, NULL, psetup, FALSE, NULL);
	g_object_unref(test_layout);
	g_free(textbuf);

	/* create a list of objects */
	rect.c_at_x = psetup->c_x0 + psetup->curr_depth * C_LABEL_SEP;
    rect.c_height = -1.0;	/* note: height is calculated when the object is drawn */
	for (this_chunk = chunks; this_chunk != NULL; this_chunk = this_chunk->next) {
		BalsaPrintObjectHeader *po;

		po = g_object_new(BALSA_TYPE_PRINT_OBJECT_HEADER, NULL);
		g_assert(po != NULL);
		balsa_print_object_set_page_depth(BALSA_PRINT_OBJECT(po), first_page++, psetup->curr_depth);
		rect.c_at_y = psetup->c_y0 + c_at_y;
		balsa_print_object_set_rect(BALSA_PRINT_OBJECT(po), &rect);
		c_at_y = 0.0;

		/* note: height is calculated when the object is drawn */
		po->headers = (gchar *) this_chunk->data;
		po->p_label_width = 0;
		po->p_layout_width = C_TO_P(rect.c_width);
		list = g_list_append(list, po);
	}
	g_list_free(chunks);

	return list;
}


static void
balsa_print_object_header_draw(BalsaPrintObject * self,
				GtkPrintContext * context,
				cairo_t * cairo_ctx)
{
    BalsaPrintObjectHeader *po;
    const BalsaPrintRect *rect;
    PangoLayout *layout;
    PangoFontDescription *font;
    gint p_height;
    gdouble c_height;

    po = BALSA_PRINT_OBJECT_HEADER(self);
    rect = balsa_print_object_get_rect(self);
    g_assert(po != NULL);

    font = pango_font_description_from_string(balsa_app.print_header_font);
    layout = gtk_print_context_create_pango_layout(context);
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    if (po->p_label_width > 0) {
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
    c_height = P_TO_C(p_height);	/* needed to properly print borders */
    cairo_move_to(cairo_ctx, rect->c_at_x, rect->c_at_y);
    pango_cairo_show_layout(cairo_ctx, layout);
    g_object_unref(layout);

    /* print a face image */
    if (po->face != NULL) {
    	gdouble c_face_h;
    	gdouble c_face_w;

    	c_face_h = gdk_pixbuf_get_height(po->face);
    	c_face_w = gdk_pixbuf_get_width(po->face);

    	cairo_print_pixbuf(cairo_ctx, po->face,
    			rect->c_at_x + rect->c_width - c_face_w,
				rect->c_at_y, 1.0);
    	if (c_face_h > c_height) {
    		c_height = c_face_h;
    	}
    }

    /* update object height to the calculated value */
    balsa_print_object_set_height(self, c_height);
}


static void
header_add_string(PangoLayout * layout, GString * header_buf,
		  const gchar * field_id, const gchar * label,
		  const gchar * value, gint * p_label_width,
		  gboolean print_all_headers)
{
    gchar *_value;
    gint p_width;

    if (!value || balsa_app.shown_headers == HEADERS_NONE ||
	!(print_all_headers ||
	  balsa_app.show_all_headers ||
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
		InternetAddressList * values, gint * p_label_width,
		gboolean print_all_headers)
{
    gchar *_value;
    gint p_width;

    if (balsa_app.shown_headers == HEADERS_NONE ||
	!(print_all_headers ||
	  balsa_app.show_all_headers ||
	  balsa_app.shown_headers == HEADERS_ALL ||
	  libbalsa_find_word(field_id, balsa_app.selected_headers)) ||
        !values ||
        !(_value = internet_address_list_to_string(values, NULL, FALSE)))
	return;

    p_width = p_string_width_from_layout(layout, label);
    if (p_width > *p_label_width)
	*p_label_width = p_width;
    libbalsa_utf8_sanitize(&_value, balsa_app.convert_unknown_8bit, NULL);
    g_string_append_printf(header_buf, "%s\t%s\n", label, _value);
    g_free(_value);
}
