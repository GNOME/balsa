/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
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

#include "config.h"

#define _XOPEN_SOURCE 500

#include <gnome.h>
#include "balsa-app.h"

#include "print.h"
#include "misc.h"
#include "balsa-message.h"

#include <ctype.h>
#include <libgnomeprint/gnome-print.h>
#include <libgnomeprint/gnome-font.h>

#include <libgnomeprintui/gnome-font-dialog.h>
#ifdef HAVE_LGPRINT22
#  include <libgnomeprint/gnome-print-job.h>
#  include <libgnomeprintui/gnome-print-job-preview.h>
#  define BALSA_GNOME_PRINT_UI GnomePrintJob
#  define BALSA_GNOME_PRINT_UI_GET_CONFIG gnome_print_job_get_config
#  define BALSA_GNOME_PRINT_UI_GET_PAGE_SIZE_FROM_CONFIG gnome_print_job_get_page_size_from_config
#  define BALSA_GNOME_PRINT_UI_GET_CONTEXT gnome_print_job_get_context
#  define BALSA_GNOME_PRINT_DIALOG_NEW gnome_print_dialog_new
#  define BALSA_GNOME_PRINT_UI_NEW gnome_print_job_new(NULL)
#  define BALSA_GNOME_PRINT_UI_CLOSE gnome_print_job_close
#  define BALSA_GNOME_PRINT_UI_PREVIEW_NEW gnome_print_job_preview_new
#  define BALSA_GNOME_PRINT_UI_PRINT gnome_print_job_print
#else
#  include <libgnomeprint/gnome-print-master.h>
#  include <libgnomeprintui/gnome-print-master-preview.h>
#  define BALSA_GNOME_PRINT_UI GnomePrintMaster
#  define BALSA_GNOME_PRINT_UI_GET_CONFIG gnome_print_master_get_config
#  define BALSA_GNOME_PRINT_UI_GET_PAGE_SIZE_FROM_CONFIG gnome_print_master_get_page_size_from_config
#  define BALSA_GNOME_PRINT_UI_GET_CONTEXT gnome_print_master_get_context
#  define BALSA_GNOME_PRINT_DIALOG_NEW gnome_print_dialog_new_from_master
#  define BALSA_GNOME_PRINT_UI_NEW gnome_print_master_new()
#  define BALSA_GNOME_PRINT_UI_CLOSE gnome_print_master_close
#  define BALSA_GNOME_PRINT_UI_PREVIEW_NEW gnome_print_master_preview_new
#  define BALSA_GNOME_PRINT_UI_PRINT gnome_print_master_print
#endif

#include <libgnomeprint/gnome-print-paper.h>
#include <libgnomeprintui/gnome-print-dialog.h>

#include <libbalsa.h>
#include "html.h"
#ifdef HAVE_PCRE
#  include <pcreposix.h>
#else
#  include <sys/types.h>
#  include <regex.h>
#endif
#include "quote-color.h"

#include <string.h>

#define BALSA_PRINT_TYPE_HEADER     1
#define BALSA_PRINT_TYPE_SEPARATOR  2
#define BALSA_PRINT_TYPE_PLAINTEXT  3
#define BALSA_PRINT_TYPE_IMAGE      4
#define BALSA_PRINT_TYPE_DEFAULT    5
#ifdef HAVE_GPGME
#define BALSA_PRINT_TYPE_CRYPT_SIGN   6
#endif
#ifdef HAVE_GTKHTML
# define BALSA_PRINT_TYPE_HTML      7
#endif /* HAVE_GTKHTML */


typedef struct _PrintInfo {
    /* gnome print info */
    GnomePrintContext *pc;

    /* page info */
    gint pages, current_page;
    float ypos;
    gdouble page_width, page_height;
    float margin_top, margin_bottom, margin_left, margin_right;
    float printable_width, printable_height;
    float pgnum_from_top;

    /* wrapping */
    gint tab_width;

    /* balsa data */
    LibBalsaMessage *message;
    gchar *footer;
    GList *print_parts;

    /* fonts */
    GnomeFont *header_font;
    GnomeFont *body_font;
    GnomeFont *footer_font;
} PrintInfo;

typedef struct _FontInfo FontInfo;
typedef struct _CommonInfo CommonInfo;

struct _FontInfo {
    gchar **font_name;
    GnomeFont *font;
    GtkWidget* font_status, *name_label;
    CommonInfo *common_info;
};

struct _CommonInfo {
    FontInfo header_font_info;
    FontInfo body_font_info;
    FontInfo footer_font_info;
    /* Some other per-dialog data: */
    GtkWidget *dialog;
    BALSA_GNOME_PRINT_UI *master;
    LibBalsaMessage *message;
    gboolean have_ref;
    GObject *parent_object;
};

typedef void (*prepare_func_t)(PrintInfo * pi, LibBalsaMessageBody * body);

typedef struct _mime_action_t {
    gchar *mime_type;
    prepare_func_t prepare_func;
}mime_action_t;

static int
print_wrap_string(gchar ** str, GnomeFont * font, gint width, gint tab_width)
{
    gchar *ptr, *line = *str;
    gchar *eol;
    gint lines = 1;
    GString *wrapped;
    gdouble space_width = gnome_font_get_width_utf8(font, " ");
 
    g_return_val_if_fail(*str, 0);

    g_strchomp(*str);
    wrapped = g_string_new("");
    while (line) {
	gdouble line_width = 0.0;

	eol = strchr(line, '\n');
	if (eol)
	    *eol = '\0';
	ptr = line;
	while (*ptr) {
	    gint pos = 0;
	    gint last_space = 0;

	    while (*ptr && (line_width <= width || !last_space)) {
		if (*ptr == '\t') {
		    gint i, spc = ((pos / tab_width) + 1) * tab_width - pos;

		    for (i = 0; line_width <= width && i < spc; i++, pos++) {
			wrapped = g_string_append_c(wrapped, ' ');
			last_space = wrapped->len - 1;
			line_width += space_width;
		    }
		} else {
		    if (isspace((int)*ptr)) {
			wrapped = g_string_append_c(wrapped, ' ');
			last_space = wrapped->len - 1;
			line_width += space_width;
		    } else {
			wrapped = g_string_append_c(wrapped, *ptr);
			line_width += 
			    gnome_font_get_width_utf8_sized(font, ptr, 1);
		    }
		    pos++;
		}
		ptr++;
	    }
	    if (*ptr && last_space) {
		wrapped->str[last_space] = '\n';
		lines++;
		line_width = 
		    gnome_font_get_width_utf8(font, 
                                              &wrapped->str[last_space + 1]);
	    }
	}
	line = eol;
	if (eol) {
	    wrapped = g_string_append_c(wrapped, '\n');
	    lines++;
	    line++;
	}
    }
    g_free(*str);
    *str = wrapped->str;
    g_string_free(wrapped, FALSE);
    return lines;
}

static void
print_foot_lines(PrintInfo * pi, GnomeFont * font, float y,
		 gint line_height, gchar * val)
{
    gchar *ptr, *eol;
    gint width;

    ptr = val;
    while (ptr) {
	eol = strchr(ptr, '\n');
	if (eol)
	    *eol = '\0';
	width = gnome_font_get_width_utf8(font, ptr);
	gnome_print_moveto(pi->pc, 
			   pi->margin_left + (pi->printable_width - width) / 2.0,
			   y);
	gnome_print_show(pi->pc, ptr);
	ptr = eol;
	if (eol) {
	    *eol = '\n';
	    ptr++;
	}
	y -= line_height;
    }
}

static void
start_new_page_real(PrintInfo * pi)
{
    gdouble font_size;
    gchar *page_no;
    int width, ypos;
    gchar buf[20];

    pi->current_page++;
    snprintf(buf, sizeof(buf ) - 1, "%d", pi->current_page);
    if (balsa_app.debug)
	g_print("Processing page %s\n", buf);

    gnome_print_beginpage(pi->pc, buf);
    /* print the page number */
    if (balsa_app.print_highlight_cited)
	gnome_print_setrgbcolor (pi->pc, 0.0, 0.0, 0.0);
    page_no = g_strdup_printf(_("Page: %i/%i"), pi->current_page, pi->pages);
    ypos = pi->page_height - pi->pgnum_from_top;
    gnome_print_setfont(pi->pc, pi->header_font);
    width = gnome_font_get_width_utf8(pi->header_font, page_no);
    gnome_print_moveto(pi->pc, pi->page_width - pi->margin_left - width,
		       ypos);
    gnome_print_show(pi->pc, page_no);
    g_free(page_no);
    
    /* print the footer */
    gnome_print_setfont(pi->pc, pi->footer_font);
    font_size = gnome_font_get_size(pi->footer_font);
    print_foot_lines(pi, pi->footer_font, 
		     pi->margin_bottom - 2 * font_size,
		     font_size, pi->footer);
    pi->ypos = pi->margin_bottom + pi->printable_height;
}

static void
start_new_page(PrintInfo * pi)
{
    gnome_print_showpage(pi->pc);
    start_new_page_real(pi);
}

/*
 * ~~~ generic stuff for print tasks ~~~
 */
typedef struct _TaskInfo {
    guint id_tag;
} TaskInfo;

/*
 * ~~~ stuff for the message and embedded headers ~~~
 */

typedef struct _HeaderInfo {
    guint id_tag;
    float header_label_width;
    GList *headers;
#ifdef HAVE_GPGME
    gchar *sig_status;
#endif
} HeaderInfo;

static void
print_header_string(GList **header_list, const gchar *field_id,
		    const gchar *label, const gchar *value)
{
    gchar **hdr_pair;

    if (!value || balsa_app.shown_headers == HEADERS_NONE ||
	 !(balsa_app.show_all_headers ||
	   balsa_app.shown_headers == HEADERS_ALL ||
	   libbalsa_find_word(field_id, balsa_app.selected_headers)))
	return;

    hdr_pair = g_new0(gchar *, 3);
    hdr_pair[0] = g_strdup(label);
    hdr_pair[1] = g_strdup(value);
    *header_list = g_list_append(*header_list, hdr_pair);
}

static void
print_header_list(GList **header_list, const gchar *field_id,
		  const gchar *label, GList *values)
{
    gchar **hdr_pair;

    if (!values || balsa_app.shown_headers == HEADERS_NONE ||
	 !(balsa_app.show_all_headers ||
	   balsa_app.shown_headers == HEADERS_ALL ||
	   libbalsa_find_word(field_id, balsa_app.selected_headers)))
	return;

    hdr_pair = g_new0(gchar *, 3);
    hdr_pair[0] = g_strdup(label);
    hdr_pair[1] = libbalsa_make_string_from_list(values);
    *header_list = g_list_append(*header_list, hdr_pair);
}

static HeaderInfo *
prepare_header_real(PrintInfo * pi, LibBalsaMessageBody * sig_body,
		    LibBalsaMessageHeaders *headers, gchar *the_subject)
{
    gint lines;
    gdouble font_size;
    HeaderInfo *pdata;
    gchar *subject;
    gchar *date;
    GList *p;

    pdata = g_malloc(sizeof(HeaderInfo));
    pdata->id_tag = BALSA_PRINT_TYPE_HEADER;
    pdata->headers = NULL;

    subject = g_strdup(the_subject);
    libbalsa_utf8_sanitize(&subject, balsa_app.convert_unknown_8bit,
			   NULL);
    if (subject) {
	print_header_string (&pdata->headers, "subject", _("Subject:"),
			     subject);
    }
    g_free(subject);

    date = libbalsa_message_headers_date_to_gchar(headers, balsa_app.date_string);
    print_header_string (&pdata->headers, "date", _("Date:"), date);
    g_free(date);

    if (headers->from) {
	gchar *from = libbalsa_address_to_gchar(headers->from, 0);
	print_header_string (&pdata->headers, "from", _("From:"), from);
	g_free(from);
    }

    print_header_list(&pdata->headers, "to", _("To:"), headers->to_list);
    print_header_list(&pdata->headers, "cc", _("Cc:"), headers->cc_list);
    print_header_list(&pdata->headers, "bcc", _("Bcc:"), headers->bcc_list);
    print_header_string (&pdata->headers, "fcc", _("Fcc:"), headers->fcc_url);

    if (headers->dispnotify_to) {
	gchar *mdn_to = libbalsa_address_to_gchar(headers->dispnotify_to, 0);
	print_header_string (&pdata->headers, "disposition-notification-to", 
			     _("Disposition-Notification-To:"), mdn_to);
	g_free(mdn_to);
    }

    /* and now for the remaining headers... */
    p = g_list_first(headers->user_hdrs);
    while (p) {
	gchar **pair, *curr_hdr;
	pair = p->data;
	curr_hdr = g_strconcat(pair[0], ":", NULL);
	print_header_string (&pdata->headers, pair[0], curr_hdr, pair[1]);
	g_free(curr_hdr);
	p = g_list_next(p);
    }

    /* calculate the label width */
    pdata->header_label_width = 0;
    p = g_list_first(pdata->headers);
    while (p) {
	gchar **strgs = p->data;
	gint width;

	width = gnome_font_get_width_utf8(pi->header_font, strgs[0]);
	if (width > pdata->header_label_width)
	    pdata->header_label_width = width;
	p = g_list_next(p);
    }
    pdata->header_label_width += 6;	/* pts */

    /* wrap headers if necessary */
    lines = 0;
    p = g_list_first(pdata->headers);
    while (p) {
	gchar **strgs = p->data;
	lines += 
	    print_wrap_string(&strgs[1], pi->header_font,
			      pi->printable_width - pdata->header_label_width, 
			      pi->tab_width);
	p = g_list_next(p);
    }

#ifdef HAVE_GPGME
    if (balsa_app.shown_headers != HEADERS_NONE && sig_body &&
        sig_body->parts && sig_body->parts->next &&
        sig_body->parts->next->sig_info) {
        gint prot = libbalsa_message_body_protection(sig_body);

        if ((prot & LIBBALSA_PROTECT_SIGN) &&
            (prot & (LIBBALSA_PROTECT_RFC3156 | LIBBALSA_PROTECT_SMIMEV3))) {
            GMimeGpgmeSigstat *siginfo = sig_body->parts->next->sig_info;

            pdata->sig_status =
                g_strconcat
                (libbalsa_gpgme_sig_protocol_name(siginfo->protocol),
                 libbalsa_gpgme_sig_stat_to_gchar(siginfo->status),
                 NULL);
            lines += print_wrap_string(&pdata->sig_status, pi->header_font,
                                       pi->printable_width, pi->tab_width);
        }
    } else {
        pdata->sig_status = NULL;
    }
#endif

    font_size = gnome_font_get_size(pi->header_font);
    if (pi->ypos - lines * font_size < pi->margin_bottom) {
	lines -= (pi->ypos - pi->margin_bottom) / gnome_font_get_size(pi->header_font);
	pi->pages++;
	while (lines * font_size > pi->printable_height) {
	    lines -= pi->printable_height / font_size;
	    pi->pages++;
	}
	pi->ypos = pi->margin_bottom + pi->printable_height -
	    lines * font_size;
    } else
	pi->ypos -= lines * font_size;

    return pdata;
}

static void
prepare_message_header(PrintInfo * pi, LibBalsaMessageBody * body)
{
    HeaderInfo *pdata;
    GString *footer_string = NULL;
    gchar *subject;
    gchar *date;

    g_return_if_fail(pi->message->headers);

    /* create the headers */
    subject = g_strdup(LIBBALSA_MESSAGE_GET_SUBJECT(pi->message));
    pdata = prepare_header_real(pi, body, pi->message->headers, subject);
    pi->print_parts = g_list_append (pi->print_parts, pdata);

    /* create the footer */
    libbalsa_utf8_sanitize(&subject, balsa_app.convert_unknown_8bit,
			   NULL);
    if (subject)
	footer_string = g_string_new(subject);
    g_free(subject);

    date = libbalsa_message_date_to_gchar(pi->message, balsa_app.date_string);
    if (footer_string) {
	footer_string = g_string_append(footer_string, " - ");
	footer_string = g_string_append(footer_string, date);
    } else {
	footer_string = g_string_new(date);
    }
    g_free(date);

    if (pi->message->headers->from) {
	gchar *from = libbalsa_address_to_gchar(pi->message->headers->from, 0);
	if (footer_string) {
	    footer_string = g_string_prepend(footer_string, " - ");
	    footer_string = g_string_prepend(footer_string, from);
	} else {
	    footer_string = g_string_new(from);
	}
	g_free(from);
    }

    /* wrap the footer if necessary */
    pi->footer = footer_string->str;
    g_string_free(footer_string, FALSE);

    print_wrap_string(&pi->footer, pi->footer_font, pi->printable_width, pi->tab_width);
}

static void
prepare_embedded_header(PrintInfo * pi, LibBalsaMessageBody * body)
{
    HeaderInfo *pdata;

    g_return_if_fail(body->embhdrs);

    pdata = prepare_header_real(pi, body->parts, body->embhdrs, body->embhdrs->subject);
    pi->print_parts = g_list_append (pi->print_parts, pdata);
}

static void
print_header_val(PrintInfo * pi, gint x, float * y,
		 gint line_height, gchar * val, GnomeFont *font)
{
    gchar *ptr, *eol;

    ptr = val;
    while (ptr) {
	eol = strchr(ptr, '\n');
	if (eol)
	    *eol = '\0';
	gnome_print_moveto(pi->pc, x, *y);
	gnome_print_show(pi->pc, ptr);
	ptr = eol;
	if (eol)
	    ptr++;
	if (ptr) {
	    *y -= line_height;
	    if (*y < pi->margin_bottom) {
		start_new_page(pi);
		gnome_print_setfont(pi->pc, font);
	    }
	}
    }
}

static void
print_header(PrintInfo * pi, gpointer * data)
{
    HeaderInfo *pdata = (HeaderInfo *)data;
    GList *p;
    gdouble font_size = gnome_font_get_size(pi->header_font);

    g_return_if_fail(pdata->id_tag == BALSA_PRINT_TYPE_HEADER);

    if (balsa_app.print_highlight_cited)
	gnome_print_setrgbcolor (pi->pc, 0.0, 0.0, 0.0);
    gnome_print_setfont(pi->pc, pi->header_font);
    p = g_list_first(pdata->headers);
    while (p) {
	gchar **pair = p->data;

	pi->ypos -= font_size;
	if (pi->ypos < pi->margin_bottom)
	    start_new_page(pi);
	gnome_print_moveto(pi->pc, pi->margin_left, pi->ypos);
	gnome_print_show(pi->pc, pair[0]);
	print_header_val(pi, pi->margin_left + pdata->header_label_width,
			 &pi->ypos, font_size, pair[1], pi->header_font);
	g_strfreev(pair);
	p = g_list_next(p);
    }
#ifdef HAVE_GPGME
    if (pdata->sig_status) {
	pi->ypos -= font_size;
	if (pi->ypos < pi->margin_bottom)
	    start_new_page(pi);
	print_header_val(pi, pi->margin_left, &pi->ypos, font_size,
			 pdata->sig_status, pi->header_font);
	g_free(pdata->sig_status);
    }
#endif
}

/*
 * ~~~ stuff to print a separator line ~~~
 */
typedef struct _SeparatorInfo {
    guint id_tag;
} SeparatorInfo;

static void
prepare_separator(PrintInfo * pi, LibBalsaMessageBody * body)
{
    SeparatorInfo *pdata;
    gdouble font_size = gnome_font_get_size(pi->header_font);

    pdata = g_malloc(sizeof(SeparatorInfo));
    pdata->id_tag = BALSA_PRINT_TYPE_SEPARATOR;
    pi->ypos -= (font_size / 2.0);
    if (pi->ypos < pi->margin_bottom) {
	pi->pages++;
	pi->ypos = pi->margin_bottom + pi->printable_height - 
	    (font_size / 2.0);
    } else
	pi->ypos -= (font_size / 2.0);

    pi->print_parts = g_list_append (pi->print_parts, pdata);
}

static void
print_separator(PrintInfo * pi, gpointer * data)
{
    SeparatorInfo *pdata = (SeparatorInfo *)data;
    gdouble font_size = gnome_font_get_size(pi->header_font);

    g_return_if_fail(pdata->id_tag == BALSA_PRINT_TYPE_SEPARATOR);

    if (balsa_app.print_highlight_cited)
	gnome_print_setrgbcolor (pi->pc, 0.0, 0.0, 0.0);
    pi->ypos -= (font_size / 2.0);
    if (pi->ypos < pi->margin_bottom)
	start_new_page(pi);
    gnome_print_setlinewidth(pi->pc, 0.5);
    gnome_print_newpath(pi->pc);
    gnome_print_moveto(pi->pc, pi->margin_left, pi->ypos);
    gnome_print_lineto(pi->pc, pi->printable_width + pi->margin_left, pi->ypos);
    gnome_print_stroke (pi->pc);
    pi->ypos -= (font_size / 2.0);
}

#ifdef HAVE_GTKHTML
/*
 * ~~~ stuff to print an html part ~~~
 */
typedef struct _HtmlInfo {
    guint id_tag;
    GtkWidget *html;
} HtmlInfo;

static void prepare_default(PrintInfo * pi, LibBalsaMessageBody * body);

static void
prepare_html(PrintInfo * pi, LibBalsaMessageBody * body)
{
    GtkWidget *dialog;
    gint response;
    HtmlInfo *pdata;
    FILE *fp;
    size_t len;
    gchar *html_text;
    gchar *conttype;
    LibBalsaHTMLType html_type;

    conttype = libbalsa_message_body_get_mime_type(body);
    html_type = libbalsa_html_type(conttype);
    g_free(conttype);

    if (!libbalsa_html_can_print() || !html_type) {
	prepare_default(pi, body);
	return;
    }

    dialog =
	gtk_message_dialog_new(NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
			       GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
			       _("Preparing an HTML part, "
				 "which must start on a new page.\n"
				 "Print this part?"));
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response != GTK_RESPONSE_YES) {
	prepare_default(pi, body);
	return;
    }

    if (!libbalsa_message_body_save_temporary(body)) {
	balsa_information
	    (LIBBALSA_INFORMATION_ERROR,
	     _("Error writing to temporary file %s.\n"
	       "Check the directory permissions."), body->temp_filename);
	return;
    }

    if ((fp = fopen(body->temp_filename, "r")) == NULL) {
	balsa_information(LIBBALSA_INFORMATION_ERROR,
			  _("Cannot open temporary file %s."),
			  body->temp_filename);
	return;
    }

    len = libbalsa_readfile(fp, &html_text);
    fclose(fp);
    if (!html_text)
	return;

    len = libbalsa_html_filter(html_type, &html_text, len);

    pdata = g_new(HtmlInfo, 1);
    pdata->id_tag = BALSA_PRINT_TYPE_HTML;
    pdata->html =
	libbalsa_html_new(html_text, len, pi->message, NULL);
    g_free(html_text);

    if (libbalsa_html_can_zoom(pdata->html)) {
	gint zoom = GPOINTER_TO_INT(g_object_get_data
				    (G_OBJECT(pi->message),
				     BALSA_MESSAGE_ZOOM_KEY));

	if (zoom > 0)
	    do
		libbalsa_html_zoom(pdata->html, 1);
	    while (--zoom);
	else if (zoom < 0)
	    do
		libbalsa_html_zoom(pdata->html, -1);
	    while (++zoom);
    }

    pi->pages +=
	libbalsa_html_print_get_pages_num(pdata->html, pi->pc,
					  pi->margin_top,
					  pi->margin_bottom);
    pi->ypos = 0;		/* Must start a new page for the next part. */
    pi->print_parts = g_list_append(pi->print_parts, pdata);
}

static void
print_html_header(GtkWidget * html, GnomePrintContext * print_context,
		  gdouble x, gdouble y, gdouble width, gdouble height,
		  PrintInfo * pi)
{
    gchar *page_no;
    int page_no_width, ypos;

    if (balsa_app.print_highlight_cited)
	gnome_print_setrgbcolor(pi->pc, 0.0, 0.0, 0.0);

    pi->current_page++;
    page_no =
	g_strdup_printf(_("Page: %i/%i"), pi->current_page, pi->pages);
    ypos = pi->page_height - pi->pgnum_from_top;
    gnome_print_setfont(pi->pc, pi->header_font);
    page_no_width = gnome_font_get_width_utf8(pi->header_font, page_no);
    gnome_print_moveto(pi->pc,
		       pi->page_width - pi->margin_left - page_no_width,
		       ypos);
    gnome_print_show(pi->pc, page_no);
    g_free(page_no);
}

static void
print_html_footer(GtkWidget * html, GnomePrintContext * print_context,
		  gdouble x, gdouble y, gdouble width, gdouble height,
		  PrintInfo * pi)
{
    gdouble font_size;

    gnome_print_setfont(pi->pc, pi->footer_font);
    font_size = gnome_font_get_size(pi->footer_font);
    print_foot_lines(pi, pi->footer_font, 
		     pi->margin_bottom - 2 * font_size,
		     font_size, pi->footer);
}

static void
print_html(PrintInfo * pi, HtmlInfo * pdata)
{
    g_return_if_fail(pdata->id_tag == BALSA_PRINT_TYPE_HTML);

    libbalsa_html_print(pdata->html, pi->pc,
				      pi->margin_top,
				      pi->margin_bottom,
				      (LibBalsaHTMLPrintCallback)
				      print_html_header,
				      (LibBalsaHTMLPrintCallback)
				      print_html_footer, pi);
    gtk_widget_destroy(pdata->html);
}

#endif /* HAVE_GTKHTML */
/*
 * ~~~ stuff to print a plain text part ~~~
 */
typedef struct _lineInfo {
    gchar *lineData;
    gint quoteLevel;
} lineInfo_T;

typedef struct _PlainTextInfo {
    guint id_tag;
    GList *textlines;
} PlainTextInfo;

static GList *
print_wrap_body(gchar * str, GnomeFont * font, gint width, gint tab_width)
{
    gchar *ptr, *line = str;
    gchar *eol;
    regex_t rex;
    gboolean checkQuote = balsa_app.print_highlight_cited;
    GList *wrappedLines = NULL;
    gdouble space_width = gnome_font_get_width_utf8(font, " ");
 
    if (checkQuote)
	if (regcomp(&rex, balsa_app.quote_regex, REG_EXTENDED) != 0) {
	    g_warning("quote regex compilation failed.");
	    checkQuote = FALSE;
	}
    
    g_strchomp(str);
    while (line) {
	gdouble line_width = 0.0;
	GString *wrLine = g_string_new("");
	lineInfo_T *lineInfo = g_malloc(sizeof(lineInfo_T));

	eol = strchr(line, '\n');
	if (eol)
	    *eol = '\0';
	ptr = line;
	lineInfo->quoteLevel = checkQuote ? is_a_quote(ptr, &rex) : 0;
	while (*ptr) {
	    gint pos = 0;
	    gint last_space = 0;

	    while (*ptr && (line_width <= width || !last_space)) {
		if (*ptr == '\t') {
		    gint i, spc = ((pos / tab_width) + 1) * tab_width - pos;

		    for (i = 0; line_width <= width && i < spc; i++, pos++) {
			wrLine = g_string_append_c(wrLine, ' ');
			last_space = wrLine->len - 1;
			line_width += space_width;
		    }
		} else {
		    if (isspace((int)*ptr)) {
			wrLine = g_string_append_c(wrLine, ' ');
			last_space = wrLine->len - 1;
			line_width += space_width;
		    } else {
			wrLine = g_string_append_c(wrLine, *ptr);
			line_width += 
			    gnome_font_get_width_utf8_sized(font, ptr, 1);
		    }
		    pos++;
		}
		ptr++;
	    }
	    if (*ptr && last_space) {
		gint lastQLevel = lineInfo->quoteLevel;
		lineInfo->lineData = g_strndup(wrLine->str, last_space);
		wrappedLines = g_list_prepend(wrappedLines, lineInfo);
		lineInfo = g_malloc(sizeof(lineInfo_T));
		lineInfo->quoteLevel = lastQLevel;
		wrLine = g_string_erase(wrLine, 0, last_space + 1);
		line_width = 
		    gnome_font_get_width_utf8(font, wrLine->str);
		last_space = 0;
	    }
	}
	lineInfo->lineData = wrLine->str;
	wrappedLines = g_list_prepend(wrappedLines, lineInfo);
	g_string_free(wrLine, FALSE);
	line = eol;
	if (eol)
	    line++;
    }

    if (checkQuote)
	regfree(&rex);

    return g_list_reverse(wrappedLines);
}

static void
prepare_plaintext(PrintInfo * pi, LibBalsaMessageBody * body)
{
    PlainTextInfo *pdata;
    gdouble font_size;
    gchar *textbuf;
    guint lines;

    pdata = g_malloc(sizeof(PlainTextInfo));
    pdata->id_tag = BALSA_PRINT_TYPE_PLAINTEXT;

    /* copy the text body to a buffer */
    if (body->buffer)
	textbuf = g_strdup(body->buffer);
    else {
	FILE *part;

	textbuf = NULL;
	libbalsa_message_body_save_temporary(body);
	part = fopen(body->temp_filename, "r");
	if (part) {
	    libbalsa_readfile(part, &textbuf);
	    fclose(part);
	    }
    }

    /* fake an empty buffer if textbuf is NULL */
    if (!textbuf)
	textbuf = g_strdup("");

    /* be sure the we have correct utf-8 stuff here... */
    libbalsa_utf8_sanitize(&textbuf, balsa_app.convert_unknown_8bit,
			   NULL);
    
    /* wrap lines (if necessary) */
    pdata->textlines = 
	print_wrap_body(textbuf, pi->body_font, pi->printable_width, pi->tab_width);
    g_free(textbuf);
    lines = g_list_length(pdata->textlines);

    /* calculate the y end position */
    font_size = gnome_font_get_size(pi->body_font);
    if (pi->ypos - lines * font_size < pi->margin_bottom) {
	int lines_left = lines;

	lines_left -= (pi->ypos - pi->margin_bottom) / font_size;
	pi->pages++;
	while (lines_left * font_size > pi->printable_height) {
	    lines_left -= pi->printable_height / font_size;
	    pi->pages++;
	}
	pi->ypos = pi->margin_bottom + pi->printable_height -
	    lines_left * font_size;
    } else
	pi->ypos -= lines * font_size;

    pi->print_parts = g_list_append (pi->print_parts, pdata);
}

static void
print_plaintext(PrintInfo * pi, gpointer * data)
{
    PlainTextInfo *pdata = (PlainTextInfo *)data;
    GList *l;

    g_return_if_fail(pdata->id_tag == BALSA_PRINT_TYPE_PLAINTEXT);

    gnome_print_setfont(pi->pc, pi->body_font);
    l = pdata->textlines;
    while (l) {
 	lineInfo_T *lineInfo = (lineInfo_T *)l->data;
 	
 	pi->ypos -= gnome_font_get_size(pi->body_font);
	if (pi->ypos < pi->margin_bottom) {
	    start_new_page(pi);
	    gnome_print_setfont(pi->pc, pi->body_font);
	}
 	if (balsa_app.print_highlight_cited) {
 	    if (lineInfo->quoteLevel != 0) {
 		GdkColor *col;
 		
 		col = &balsa_app.quoted_color[(lineInfo->quoteLevel - 1) %
 					     MAX_QUOTED_COLOR];
 		gnome_print_setrgbcolor (pi->pc,
 					 col->red / 65535.0,
 					 col->green / 65535.0,
 					 col->blue / 65535.0);
 	    } else
 		gnome_print_setrgbcolor (pi->pc, 0.0, 0.0, 0.0);
 	}
 	gnome_print_moveto(pi->pc, pi->margin_left, pi->ypos);
 	gnome_print_show(pi->pc, lineInfo->lineData);
 	g_free(lineInfo->lineData);
 	g_free(l->data);
 	l = l->next;
    }
    g_list_free(pdata->textlines);
}

/*
 * ~~~ default print method: print an icon plus a description ~~~
 */
typedef struct _DefaultInfo {
    guint id_tag;
    float label_width, image_width, image_height, text_height, part_height;
    gchar **labels;
    GdkPixbuf *pixbuf;
} DefaultInfo;

static void
prepare_default(PrintInfo * pi, LibBalsaMessageBody * body)
{
    DefaultInfo *pdata;
    gchar *icon_name, *conttype;
    gint hdr = 0, lines;
    GError* err = NULL;

    pdata = g_malloc(sizeof(DefaultInfo));
    pdata->id_tag = BALSA_PRINT_TYPE_DEFAULT;

    conttype = libbalsa_message_body_get_mime_type(body);

    /* get a pixbuf according to the mime type */
    icon_name = libbalsa_icon_finder(conttype, NULL, NULL);
    pdata->pixbuf = gdk_pixbuf_new_from_file(icon_name, &err);
    if(err) { g_warning("error loading pixbuf."); g_error_free(err); }
    pdata->image_width = gdk_pixbuf_get_width (pdata->pixbuf);
    pdata->image_height = gdk_pixbuf_get_height (pdata->pixbuf);
    g_free(icon_name);

    /* gather some info about this part */
    pdata->labels = g_new0(gchar *, 5); /* four fields, one terminator */
    pdata->labels[hdr++] = g_strdup(_("Type:"));
    pdata->labels[hdr++] = g_strdup(conttype);
    if (body->filename) {
	pdata->labels[hdr++] = g_strdup(_("File name:"));
	pdata->labels[hdr++] = g_strdup(body->filename);
    }
    pdata->label_width = gnome_font_get_width_utf8(pi->header_font, pdata->labels[0]);
    if (pdata->labels[2] && 
	gnome_font_get_width_utf8(pi->header_font, pdata->labels[2]) > pdata->label_width)
	pdata->label_width = gnome_font_get_width_utf8(pi->header_font, pdata->labels[2]);
    pdata->label_width += 6;

    lines = print_wrap_string(&pdata->labels[1], pi->header_font,
			      pi->printable_width - pdata->label_width - 
			      pdata->image_width - 10, pi->tab_width);
    if (!lines)
	lines = 1;
    if (pdata->labels[3])
	lines += print_wrap_string(&pdata->labels[3], pi->header_font,
				   pi->printable_width - pdata->label_width - 
				   pdata->image_width - 10, pi->tab_width);
    pdata->text_height = lines * gnome_font_get_size(pi->header_font);

    pdata->part_height = (pdata->text_height > pdata->image_height) ?
	pdata->text_height : pdata->image_height;
    if (pi->ypos - pdata->part_height < pi->margin_bottom) {
	pi->pages++;
	pi->ypos = pi->margin_bottom + pi->printable_height - pdata->part_height;
    } else
	pi->ypos -= pdata->part_height;
    
    g_free(conttype);

    pi->print_parts = g_list_append (pi->print_parts, pdata);
}

/* print_image_from_pixbuf:
 *
 * replacement for gnome_print_pixbuf, straight out of
 * libgnomeprintui/examples/example_02.c
 */
static void
print_image_from_pixbuf(GnomePrintContext * gpc, GdkPixbuf * pixbuf)
{
    guchar *raw_image;
    gboolean has_alpha;
    gint rowstride, height, width;

    raw_image = gdk_pixbuf_get_pixels(pixbuf);
    has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    height    = gdk_pixbuf_get_height(pixbuf);
    width     = gdk_pixbuf_get_width(pixbuf);

    if (has_alpha)
        gnome_print_rgbaimage(gpc, (char *) raw_image, width, height,
                              rowstride);
    else
        gnome_print_rgbimage(gpc, (char *) raw_image, width, height,
                             rowstride);
}

static void
print_default(PrintInfo * pi, gpointer data)
{
    double matrix[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    DefaultInfo *pdata = (DefaultInfo *)data;
    gdouble font_size;
    gint i, offset;

    g_return_if_fail(pdata->id_tag == BALSA_PRINT_TYPE_DEFAULT);

    if (pi->ypos - pdata->part_height < pi->margin_bottom)
	start_new_page(pi);

    if (balsa_app.print_highlight_cited)
 	gnome_print_setrgbcolor (pi->pc, 0.0, 0.0, 0.0);
    /* print the icon */
    gnome_print_gsave(pi->pc);
    matrix[0] = pdata->image_width;
    matrix[3] = pdata->image_height;
    matrix[4] = pi->margin_left;
    matrix[5] = pi->ypos - (pdata->part_height + pdata->image_height) / 2.0;
    gnome_print_concat(pi->pc, matrix);
    print_image_from_pixbuf(pi->pc, pdata->pixbuf);
    gnome_print_grestore (pi->pc);
    g_object_unref(pdata->pixbuf);
    
    /* print the description */
    gnome_print_setfont(pi->pc, pi->header_font);
    font_size = gnome_font_get_size(pi->header_font);
    pi->ypos -= (pdata->part_height - pdata->text_height) / 2.0 + 
	font_size;
    offset = pi->margin_left + pdata->image_width + 10;
    for (i = 0; pdata->labels[i]; i += 2) {
	gnome_print_moveto(pi->pc, offset, pi->ypos);
	gnome_print_show(pi->pc, pdata->labels[i]);
	print_header_val(pi, offset + pdata->label_width, &pi->ypos,
 			 font_size, pdata->labels[i + 1], pi->header_font);
 	pi->ypos -= font_size;
    }
    pi->ypos -= (pdata->part_height - pdata->text_height) / 2.0 -
	font_size;
    g_strfreev(pdata->labels);
}

/*
 * ~~~ stuff to print an image ~~~
 */
typedef struct _ImageInfo {
    guint id_tag;
    GdkPixbuf *pixbuf;
    float print_width, print_height;
} ImageInfo;

static void
prepare_image(PrintInfo * pi, LibBalsaMessageBody * body)
{
    ImageInfo * pdata;
    GError *err = NULL;
    pdata = g_malloc(sizeof(ImageInfo));
    pdata->id_tag = BALSA_PRINT_TYPE_IMAGE;

    libbalsa_message_body_save_temporary(body);
    pdata->pixbuf = gdk_pixbuf_new_from_file(body->temp_filename, &err);
    if(err) {
        g_warning("Error loading image from file.");
        g_error_free(err);
    }
    /* fall back to default if the pixbuf could no be loaded */
    if (!pdata->pixbuf) {
	g_free(pdata);
	prepare_default(pi, body);
	return;
    }

    /* print with 72 dpi, or scale the image */
    pdata->print_width = gdk_pixbuf_get_width (pdata->pixbuf);
    pdata->print_height = gdk_pixbuf_get_height (pdata->pixbuf);
    if (pdata->print_height > pi->printable_height) {
	pdata->print_width *= pi->printable_height / pdata->print_height;
	pdata->print_height = pi->printable_height;
    }
    if (pdata->print_width > pi->printable_width) {
	pdata->print_height *= pi->printable_width / pdata->print_width;
	pdata->print_width = pi->printable_width;
    }
    
    if (pi->ypos - pdata->print_height < pi->margin_bottom) {
	pi->pages++;
	pi->ypos = pi->margin_bottom + pi->printable_height - pdata->print_height;
    } else
	pi->ypos -= pdata->print_height;
	
    pi->print_parts = g_list_append (pi->print_parts, pdata);
}

static void
print_image(PrintInfo * pi, gpointer * data)
{
    ImageInfo *pdata = (ImageInfo *)data;
    double matrix[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

    g_return_if_fail(pdata->id_tag == BALSA_PRINT_TYPE_IMAGE);

    if (pi->ypos - pdata->print_height < pi->margin_bottom)
	start_new_page(pi);

    gnome_print_gsave(pi->pc);
    matrix[0] = pdata->print_width;
    matrix[3] = pdata->print_height;
    matrix[4] = pi->margin_left + 
	(pi->printable_width - pdata->print_width) / 2.0;
    matrix[5] = pi->ypos - pdata->print_height;
    gnome_print_concat(pi->pc, matrix);
    print_image_from_pixbuf(pi->pc, pdata->pixbuf);
    gnome_print_grestore (pi->pc);
    pi->ypos -= pdata->print_height;
    g_object_unref(pdata->pixbuf);
}

#ifdef HAVE_GPGME
/*
 * ~~~ print a gpg signature info (like plain text, but with header font) ~~~
 */
static void
prepare_crypto_signature(PrintInfo * pi, LibBalsaMessageBody * body)
{
    PlainTextInfo *pdata;
    gdouble font_size;
    gchar *textbuf;
    guint lines;

    /* check if there is a sig_info and prepare as unknown if not */
    if (!body->sig_info) {
	prepare_default(pi, body);
	return;
    }

    pdata = g_malloc(sizeof(PlainTextInfo));
    pdata->id_tag = BALSA_PRINT_TYPE_CRYPT_SIGN;

    /* create a buffer with the signature info */
    textbuf =
	libbalsa_signature_info_to_gchar(body->sig_info, balsa_app.date_string);

    /* wrap lines (if necessary) */
    pdata->textlines = 
	print_wrap_body(textbuf, pi->header_font, pi->printable_width,
			pi->tab_width);
    g_free(textbuf);
    lines = g_list_length(pdata->textlines);

    /* calculate the y end position */
    font_size = gnome_font_get_size(pi->body_font);
    if (pi->ypos - lines * font_size < pi->margin_bottom) {
	int lines_left = lines;

	lines_left -= (pi->ypos - pi->margin_bottom) / font_size;
	pi->pages++;
	while (lines_left * font_size > pi->printable_height) {
	    lines_left -= pi->printable_height / font_size;
	    pi->pages++;
	}
	pi->ypos = pi->margin_bottom + pi->printable_height -
	    lines_left * font_size;
    } else
	pi->ypos -= lines * font_size;

    pi->print_parts = g_list_append (pi->print_parts, pdata);
}

static void
print_crypto_signature(PrintInfo * pi, gpointer * data)
{
    PlainTextInfo *pdata = (PlainTextInfo *)data;
    GList *l;

    g_return_if_fail(pdata->id_tag == BALSA_PRINT_TYPE_CRYPT_SIGN);

    gnome_print_setfont(pi->pc, pi->header_font);
    l = pdata->textlines;
    gnome_print_setrgbcolor (pi->pc, 0.0, 0.0, 0.0);
    while (l) {
 	lineInfo_T *lineInfo = (lineInfo_T *)l->data;
 	
 	pi->ypos -= gnome_font_get_size(pi->header_font);
	if (pi->ypos < pi->margin_bottom) {
	    start_new_page(pi);
	    gnome_print_setfont(pi->pc, pi->header_font);
	}
 	gnome_print_moveto(pi->pc, pi->margin_left, pi->ypos);
 	gnome_print_show(pi->pc, lineInfo->lineData);
 	g_free(lineInfo->lineData);
 	g_free(l->data);
 	l = l->next;
    }
    g_list_free(pdata->textlines);
}
#endif

/*
 * scan the body list and prepare print data according to the content type
 */
static void 
scan_body(PrintInfo * pi, LibBalsaMessageBody * body)
{
    static mime_action_t mime_actions [] = {
	{"multipart", NULL},              /* ignore `multipart' entries */
#ifndef HAVE_GTKHTML
	{"text/html", prepare_default},   /* don't print html source */
#else /* HAVE_GTKHTML */
	{"text/html", prepare_html},
	{"text/enriched", prepare_html},
	{"text/richtext", prepare_html},
#endif /* HAVE_GTKHTML */
	{"text", prepare_plaintext},
	{"image", prepare_image},
	{"message/rfc822", prepare_embedded_header},
#ifdef HAVE_GPGME
        {"application/pgp-signature", prepare_crypto_signature},
#ifdef HAVE_SMIME
        {"application/pkcs7-signature", prepare_crypto_signature},
#endif
#endif
	{NULL, prepare_default}           /* anything else... */
    };
    mime_action_t *action;

    while (body) {
	gchar *conttype;

	conttype = libbalsa_message_body_get_mime_type(body);
	
	for (action = mime_actions; 
	     action->mime_type && 
		 strncmp(action->mime_type, conttype,
			 strlen(action->mime_type));
	     action++);
	g_free(conttype);

	if (action->prepare_func) {
	    prepare_separator(pi, body);
	    action->prepare_func(pi, body);
	}

	if (body->parts)
	    scan_body(pi, body->parts);

	body = body->next;
    }
}

/*
 * get the GnomeFont from a name returned by the font picker
 * libgnomeui-2.2, gtk-2.2, libgnomeprint-2.2
 * gnome_font_picker returns "URW Palladio L, Bold 12",
 * gnome_font_find_by_name expects "URW Palladio L Bold 12".
 */
static GnomeFont *
find_font(const gchar * name)
{
#ifdef GNOME_FONT_FIND_HANDLES_BAD_NAME_SANELY
    return gnome_font_find_from_full_name(name);
#else
    gchar *copy;
    gchar *space;
    GnomeFontFace *face;
    GnomeFont *font = NULL;

    copy = g_strdup(name);
    space = strrchr(copy, ' ');
    if (space)
        *space = 0;
    face = gnome_font_face_find(copy);
    g_free(copy);
    if (face) {
        gnome_font_face_unref(face);
        font = gnome_font_find_from_full_name(name);
    }
    return font;
#endif          /*  GNOME_FONT_FIND_HANDLES_BAD_NAME_SANELY */
}

static gdouble
get_length_from_config(GnomePrintConfig * config, const gchar * key)
{
    const GnomePrintUnit *unit;
    gdouble length = 0.0;

    if (gnome_print_config_get_length(config, key, &length, &unit))
        gnome_print_convert_distance(&length, unit, GNOME_PRINT_PS_UNIT);

    return length;
}

static PrintInfo *
print_info_new(CommonInfo * ci)
{
    GnomePrintConfig *config;
    PrintInfo *pi = g_new(PrintInfo, 1);

    config = BALSA_GNOME_PRINT_UI_GET_CONFIG(ci->master);
    BALSA_GNOME_PRINT_UI_GET_PAGE_SIZE_FROM_CONFIG(config,
                                                   &pi->page_width,
                                                   &pi->page_height);
    pi->margin_top =
        get_length_from_config(config, GNOME_PRINT_KEY_PAGE_MARGIN_TOP);
    pi->margin_bottom =
        get_length_from_config(config, GNOME_PRINT_KEY_PAGE_MARGIN_BOTTOM);
    pi->margin_left =
        get_length_from_config(config, GNOME_PRINT_KEY_PAGE_MARGIN_LEFT);
    pi->margin_right =
        get_length_from_config(config, GNOME_PRINT_KEY_PAGE_MARGIN_RIGHT);
    gnome_print_config_unref(config);

    pi->pc = BALSA_GNOME_PRINT_UI_GET_CONTEXT(ci->master);
    pi->current_page = 0;
    pi->pgnum_from_top = pi->margin_top - 0.25 * 72;
    pi->printable_width =
	pi->page_width - pi->margin_left - pi->margin_right;
    pi->printable_height =
	pi->page_height - pi->margin_top - pi->margin_bottom;

    pi->tab_width = 8;
    pi->pages = 1;
    pi->ypos = pi->margin_bottom + pi->printable_height;

    /* we don't hold refs to these: */
    pi->header_font = ci->header_font_info.font;
    pi->body_font =   ci->body_font_info.font;
    pi->footer_font = ci->footer_font_info.font;

    pi->message = ci->message;
    pi->print_parts = NULL;
    
    /* now get the message contents... */
    if (!pi->message->mailbox 
        || libbalsa_message_body_ref(pi->message, TRUE,
                                     balsa_app.shown_headers == HEADERS_ALL||
                                     balsa_app.show_all_headers)) {
	prepare_message_header(pi, pi->message->body_list);
        scan_body(pi, pi->message->body_list);
        libbalsa_message_body_unref(pi->message);
    } else
	prepare_message_header(pi, NULL);

    return pi;
}

static void
print_info_destroy(PrintInfo * pi)
{
    g_list_foreach(pi->print_parts, (GFunc) g_free, NULL);
    g_list_free(pi->print_parts);
    g_free(pi->footer);
    g_free(pi);
}

/* print_message:
   prints given message
*/
static void
print_message(PrintInfo * pi)
{
#ifdef HAVE_GTKHTML
    gboolean haspage;
#endif /* HAVE_GTKHTML */
    GList *print_task;

    if (balsa_app.debug)
	g_print("Printing.\n");

#ifndef HAVE_GTKHTML
    start_new_page_real(pi);

    print_task = pi->print_parts;
    while (print_task) {
	TaskInfo *pdata = print_task->data;

	switch (pdata->id_tag) {
	case BALSA_PRINT_TYPE_HEADER:
	    print_header(pi, print_task->data);
	    break;
	case BALSA_PRINT_TYPE_SEPARATOR:
	    print_separator(pi, print_task->data);
	    break;
	case BALSA_PRINT_TYPE_PLAINTEXT:
	    print_plaintext(pi, print_task->data);
	    break;
	case BALSA_PRINT_TYPE_DEFAULT:
	    print_default(pi, print_task->data);
	    break;
	case BALSA_PRINT_TYPE_IMAGE:
	    print_image(pi, print_task->data);
	    break;
#ifdef HAVE_GPGME
	case BALSA_PRINT_TYPE_CRYPT_SIGN:
	    print_crypto_signature(pi, print_task->data);
#endif
	default:
	    break;
	}

	print_task = g_list_next(print_task);
    }
    gnome_print_showpage(pi->pc);
#else /* HAVE_GTKHTML */
    haspage = FALSE;
    for (print_task = pi->print_parts; print_task;
	 print_task = g_list_next(print_task)) {
	TaskInfo *pdata = print_task->data;

	if (pdata->id_tag == BALSA_PRINT_TYPE_SEPARATOR) {
	    if (haspage)
		print_separator(pi, print_task->data);
	} else if (pdata->id_tag == BALSA_PRINT_TYPE_HTML) {
	    if (haspage)
		gnome_print_showpage(pi->pc);
	    print_html(pi, print_task->data);
	    haspage = FALSE;
	} else {
	    if (!haspage)
		start_new_page_real(pi);

	    switch (pdata->id_tag) {
	    case BALSA_PRINT_TYPE_HEADER:
		print_header(pi, print_task->data);
		break;
	    case BALSA_PRINT_TYPE_PLAINTEXT:
		print_plaintext(pi, print_task->data);
		break;
	    case BALSA_PRINT_TYPE_DEFAULT:
		print_default(pi, print_task->data);
		break;
	    case BALSA_PRINT_TYPE_IMAGE:
		print_image(pi, print_task->data);
		break;
#ifdef HAVE_GPGME
	    case BALSA_PRINT_TYPE_CRYPT_SIGN:
		print_crypto_signature(pi, print_task->data);
		break;
#endif
	    default:
		break;
	    }
	    haspage = TRUE;
	}
    }

    if (haspage)
	gnome_print_showpage(pi->pc);
#endif /* HAVE_GTKHTML */
}

/* callback to read a toggle button */
static void 
togglebut_changed (GtkToggleButton *tbut, gboolean *value)
{
    *value = gtk_toggle_button_get_active (tbut);
}

/*
 * enable/disable the Print and Preview buttons
 */
static void
set_dialog_buttons_sensitive(CommonInfo * ci)
{
    gboolean sensitive = (ci->header_font_info.font
                          && ci->body_font_info.font
                          && ci->footer_font_info.font);

    gtk_dialog_set_response_sensitive(GTK_DIALOG(ci->dialog),
                                      GNOME_PRINT_DIALOG_RESPONSE_PRINT,
                                      sensitive);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(ci->dialog),
                                      GNOME_PRINT_DIALOG_RESPONSE_PREVIEW,
                                      sensitive);
}

static void
set_font_status(FontInfo *fi)
{
    gtk_label_set_text(GTK_LABEL(fi->name_label), *fi->font_name);
    if(fi->font) 
	gtk_label_set_text(GTK_LABEL(fi->font_status),
			   _("Font available for printing"));
    else {
	GnomeFont* fncl = 
	    gnome_font_find_closest_from_full_name(*fi->font_name);
	gchar* fn = gnome_font_get_full_name(fncl);
	gchar *msg = 
	    g_strdup_printf(_("Font <b>not</b> available for printing. "
			      "Closest: %s"), fn);

	gtk_label_set_markup(GTK_LABEL(fi->font_status), msg);
	g_free(fn); g_free(msg);
    }
}
/*
 * callback for the button's font change signal.
 */
static void
font_change_cb(GtkWidget * widget, FontInfo *fi)
{
    GtkWidget* dialog = gnome_font_dialog_new(_("Select Font"));
    GtkWidget* fontsel = 
	gnome_font_dialog_get_fontsel(GNOME_FONT_DIALOG(dialog));
    if(fi->font) {
	gnome_font_selection_set_font(GNOME_FONT_SELECTION(fontsel), 
				      fi->font);
    } else {
	printf("font unknown\n");
    }
    switch(gtk_dialog_run(GTK_DIALOG(dialog))) {
    case GTK_RESPONSE_OK: 
	g_free(*fi->font_name);
	if(fi->font) gnome_font_unref(fi->font);
	fi->font = 
	    gnome_font_selection_get_font(GNOME_FONT_SELECTION(fontsel));
	*fi->font_name = gnome_font_get_full_name(fi->font);
	set_font_status(fi);
	set_dialog_buttons_sensitive(fi->common_info);
	break;
    default: break;
    }
    gtk_widget_destroy(dialog);
}

/*
 * create a frame with a font-picker widget
 */
static GtkWidget *
font_frame(gchar * title, FontInfo * fi)
{
    GtkWidget  *frame = gtk_frame_new(title);
    GtkWidget *vbox    = gtk_vbox_new(FALSE, 3);
    GtkWidget *hbox    = gtk_hbox_new(FALSE, 3);
    GtkWidget *button = gtk_button_new_with_label(_("Change..."));

    fi->name_label = gtk_label_new(*fi->font_name);
    gtk_box_pack_start(GTK_BOX(hbox), fi->name_label, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 5);
    g_signal_connect(G_OBJECT(button), "clicked", 
		     G_CALLBACK(font_change_cb), fi);

    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 5);
    gtk_container_add(GTK_CONTAINER(frame), vbox);
    fi->font_status = gtk_label_new("");
    set_font_status(fi);
    gtk_box_pack_start(GTK_BOX(vbox), fi->font_status, TRUE, TRUE, 3);
    gtk_container_set_border_width(GTK_CONTAINER(frame), 3);

    return frame;
}

/*
 * creates the print dialog, and adds a page for fonts
 */
static GtkWidget *
print_dialog(CommonInfo * ci)
{
    GtkWidget  *dialog;
    GtkWidget  *frame;
    GtkWidget  *dlgVbox;
    GtkWidget  *notebook;
    GtkWidget  *vbox;
    GtkWidget  *label;
    GtkWidget  *chkbut;
    GList      *childList;

    dialog = BALSA_GNOME_PRINT_DIALOG_NEW(ci->master, _("Print message"),
				    GNOME_PRINT_DIALOG_COPIES);
    gtk_window_set_wmclass(GTK_WINDOW(dialog), "print", "Balsa");
    dlgVbox = GTK_DIALOG(dialog)->vbox;
    childList = gtk_container_get_children(GTK_CONTAINER(dlgVbox));
    notebook = childList->data;
    g_list_free(childList);

    /* create a 2nd notebook page for the fonts */
    label = gtk_label_new_with_mnemonic(_("_Fonts"));
    vbox = gtk_vbox_new(FALSE, 3);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox, label);
    frame = font_frame(_("Header font"), &ci->header_font_info);
    gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 3);    
    frame = font_frame(_("Body font"), &ci->body_font_info);
    gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 3);    
    frame = font_frame(_("Footer font"), &ci->footer_font_info);
    gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 3);    

    /* highlight cited stuff */
    frame = gtk_frame_new(_("Highlight cited text"));
    gtk_container_set_border_width(GTK_CONTAINER(frame), 3);
    gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 3);    
    chkbut = gtk_check_button_new_with_mnemonic
	(_("_Enable highlighting of cited text"));
    g_signal_connect(G_OBJECT(chkbut), "toggled",
		     G_CALLBACK(togglebut_changed), 
		     &balsa_app.print_highlight_cited);    
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(chkbut),
				  balsa_app.print_highlight_cited);
    gtk_container_add (GTK_CONTAINER (frame), chkbut);

    gtk_widget_show_all(vbox);
    
    return dialog;
}

/*
 * the FontInfo structure contains info used in the "font-set" callback
 * that's specific to a font, and a pointer to the CommonInfo structure
 */
static void
font_info_setup(FontInfo * fi, gchar ** font_name, CommonInfo * ci)
{
    fi->font_name = font_name;
    fi->common_info = ci;
    fi->font = find_font(*font_name);
    if (!fi->font)
	balsa_information(LIBBALSA_INFORMATION_WARNING,
			  _("Balsa could not find font \"%s\".\n"
			    "Use the \"Fonts\" page on the "
                            "\"Print message\" dialog to change it."),
                          *fi->font_name);
}

static void
font_info_cleanup(FontInfo * fi)
{
    if (fi->font) {
        g_object_unref(fi->font);
        fi->font = NULL;
    }
}

/*
 * the CommonInfo structure contains info used in the "font-set" callback
 * that's common to all fonts
 */
static void
common_info_setup(CommonInfo * ci)
{
    font_info_setup(&ci->header_font_info, &balsa_app.print_header_font, ci);
    font_info_setup(&ci->body_font_info,   &balsa_app.print_body_font,   ci);
    font_info_setup(&ci->footer_font_info, &balsa_app.print_footer_font, ci);
}

#define BALSA_PRINT_COMMON_INFO_KEY "balsa-print-common-info"

/* GWeakNotify callback: destroy non-message-related stuff in
 * CommonInfo. */
static void
common_info_destroy(CommonInfo * ci)
{
    font_info_cleanup(&ci->header_font_info);
    font_info_cleanup(&ci->body_font_info);
    font_info_cleanup(&ci->footer_font_info);

    gtk_widget_destroy(GTK_WIDGET(ci->dialog));
    g_object_unref(ci->master);
    if (ci->have_ref)
        g_object_unref(ci->message);

    g_free(ci);
}

/* Clean up message-related stuff in CommonInfo, then destroy the rest. */
static void
common_info_cleanup(CommonInfo * ci)
{
    /* This triggers common_info_destroy(ci): */
    g_object_set_data(ci->parent_object, BALSA_PRINT_COMMON_INFO_KEY, NULL);
}

/* Callback for the "response" signal for the print dialog. */
static void
print_response_cb(GtkDialog * dialog, gint response, CommonInfo * ci)
{
    GnomePrintConfig *config;
    PrintInfo *pi;
    gboolean preview;

    switch (response) {
    case GNOME_PRINT_DIALOG_RESPONSE_PRINT:
        preview = FALSE;
	break;
    case GNOME_PRINT_DIALOG_RESPONSE_PREVIEW:
	preview = TRUE;
	break;
    default:
        common_info_cleanup(ci);
	return;
    }

    config = BALSA_GNOME_PRINT_UI_GET_CONFIG(ci->master);
    g_free(balsa_app.paper_size);
    balsa_app.paper_size =
        gnome_print_config_get(config, GNOME_PRINT_KEY_PAPER_SIZE); 
    balsa_app.print_unit =
        gnome_print_config_get(config, GNOME_PRINT_KEY_PREFERED_UNIT); 
    balsa_app.margin_left =
        gnome_print_config_get(config, GNOME_PRINT_KEY_PAGE_MARGIN_LEFT); 
    balsa_app.margin_top =
        gnome_print_config_get(config, GNOME_PRINT_KEY_PAGE_MARGIN_TOP); 
    balsa_app.margin_right =
        gnome_print_config_get(config, GNOME_PRINT_KEY_PAGE_MARGIN_RIGHT);
    balsa_app.margin_bottom =
        gnome_print_config_get(config, GNOME_PRINT_KEY_PAGE_MARGIN_BOTTOM); 
    balsa_app.print_layout =
        gnome_print_config_get(config, GNOME_PRINT_KEY_LAYOUT); 
    balsa_app.paper_orientation =
        gnome_print_config_get(config, GNOME_PRINT_KEY_PAPER_ORIENTATION); 
    balsa_app.page_orientation =
        gnome_print_config_get(config, GNOME_PRINT_KEY_PAGE_ORIENTATION); 

    gnome_print_config_unref(config);

    pi = print_info_new(ci);

    /* do the Real Job */
    print_message(pi);
    BALSA_GNOME_PRINT_UI_CLOSE(ci->master);
    if (preview) {
	GtkWidget *preview_widget =
	    BALSA_GNOME_PRINT_UI_PREVIEW_NEW(ci->master,
		 			   _("Balsa: message print preview"));
        gtk_window_set_wmclass(GTK_WINDOW(preview_widget), "print-preview",
                               "Balsa");
	gtk_widget_show(preview_widget);
    } else
	BALSA_GNOME_PRINT_UI_PRINT(ci->master);

    print_info_destroy(pi);
    common_info_cleanup(ci);
}

/*
 * message_print:
 *
 * the public method
 */
void
message_print(LibBalsaMessage * msg, GtkWindow * parent)
{
    GObject *parent_object;
    CommonInfo *ci;
    GnomePrintConfig *config;

    g_return_if_fail(msg != NULL);

    /* Show only one dialog: */
    parent_object = (parent == GTK_WINDOW(balsa_app.main_window))
        ? G_OBJECT(msg)         /* per message. */
        : G_OBJECT(parent);     /* per window. */
    ci = g_object_get_data(parent_object, BALSA_PRINT_COMMON_INFO_KEY);
    if (ci) {
        gdk_window_raise(ci->dialog->window);
        return;
    }

    ci = g_new(CommonInfo, 1);

    /* Close the dialog if the parent object is destroyed. We should also
     * close if the message is destroyed, but that's covered:
     * - if called from the main window, the message is the parent;
     * - if called from the message window, that window is the parent,
     *   but it's destroyed with the message, so we'll find out;
     * - if called from the compose window, we ref the message, so it
     *   can't be destroyed. */
    g_object_set_data_full(parent_object, BALSA_PRINT_COMMON_INFO_KEY, ci,
                           (GDestroyNotify) common_info_destroy);
    ci->parent_object = parent_object;

    common_info_setup(ci);
    ci->message = msg;
    if (!msg->mailbox) {
        /* temporary message from the compose window */
        g_object_ref(msg);
        ci->have_ref = TRUE;
    } else
        /* a message we're reading */
        ci->have_ref = FALSE;

    ci->master = BALSA_GNOME_PRINT_UI_NEW;

    /* FIXME: this sets the paper size in the GnomePrintConfig. We can
     * change it in the Paper page of the GnomePrintDialog, and retrieve
     * it from the GnomePrintConfig. However, it doesn't get set as the
     * initial value in the Paper page. Is there some Gnome-2-wide
     * repository for data like this? */
    config = BALSA_GNOME_PRINT_UI_GET_CONFIG(ci->master);
    gnome_print_config_set(config, GNOME_PRINT_KEY_PAPER_SIZE, 
                           balsa_app.paper_size);
    if(balsa_app.print_unit)
	gnome_print_config_set(config, GNOME_PRINT_KEY_PREFERED_UNIT,
			       balsa_app.print_unit); 
    if(balsa_app.margin_left)
	gnome_print_config_set(config, GNOME_PRINT_KEY_PAGE_MARGIN_LEFT,
			       balsa_app.margin_left); 
    if(balsa_app.margin_top)
	gnome_print_config_set(config, GNOME_PRINT_KEY_PAGE_MARGIN_TOP,
			       balsa_app.margin_top); 
    if(balsa_app.margin_right)
	gnome_print_config_set(config, GNOME_PRINT_KEY_PAGE_MARGIN_RIGHT,
			       balsa_app.margin_right);
    if(balsa_app.margin_bottom)
	gnome_print_config_set(config, GNOME_PRINT_KEY_PAGE_MARGIN_BOTTOM,
			       balsa_app.margin_bottom); 
    if(balsa_app.print_layout)
	gnome_print_config_set(config, GNOME_PRINT_KEY_LAYOUT,
			       balsa_app.print_layout); 
    if(balsa_app.paper_orientation)
	gnome_print_config_set(config, GNOME_PRINT_KEY_PAPER_ORIENTATION,
			       balsa_app.paper_orientation); 
    if(balsa_app.page_orientation)
	gnome_print_config_set(config, GNOME_PRINT_KEY_PAPER_ORIENTATION,
			       balsa_app.page_orientation); 
    gnome_print_config_unref(config);
    
    ci->dialog = print_dialog(ci);
    gtk_window_set_transient_for(GTK_WINDOW(ci->dialog), parent);

    set_dialog_buttons_sensitive(ci);
    g_signal_connect(G_OBJECT(ci->dialog), "response",
                     G_CALLBACK(print_response_cb), ci);

    gtk_widget_show_all(ci->dialog);
}
