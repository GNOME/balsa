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

#include <gnome.h>
#include <balsa-app.h>
#include <iconv.h>

#include "print.h"
#include "misc.h"

#ifndef HAVE_GNOME_PRINT

/*
 * PUBLIC: message_print_cb
 *
 * creates print dialog box.  this should be the only routine global to
 * the world.
 */
void
message_print_cb(GtkWidget * widget, gpointer cbdata)
{
    balsa_information(LIBBALSA_INFORMATION_ERROR, NULL, _(
	"This version of balsa is compiled without gnome-print support.\n"
	"Printing is not possible."));
}
void
message_print(LibBalsaMessage *msg)
{
    balsa_information(LIBBALSA_INFORMATION_ERROR, NULL, _(
	"This version of balsa is compiled without gnome-print support.\n"
	"Printing is not possible."));
}
#else

#include <ctype.h>
#include <libgnomeprint/gnome-print.h>
#include <libgnomeprint/gnome-print-dialog.h>
#include <libgnomeprint/gnome-print-master.h>
#include <libgnomeprint/gnome-print-master-preview.h>
#include <libbalsa.h>
#ifdef HAVE_PCRE
#  include <pcreposix.h>
#else
#  include <sys/types.h>
#  include <regex.h>
#endif
#include "balsa-index.h"
#include "quote-color.h"

#define BALSA_PRINT_TYPE_HEADER     1
#define BALSA_PRINT_TYPE_SEPARATOR  2
#define BALSA_PRINT_TYPE_PLAINTEXT  3
#define BALSA_PRINT_TYPE_IMAGE      4
#define BALSA_PRINT_TYPE_DEFAULT    5


typedef struct _PrintInfo {
    /* gnome print info */
    const GnomePaper *paper;
    GnomePrintMaster *master;
    GnomePrintContext *pc;

    /* page info */
    gint pages, current_page;
    float ypos;
    float page_width, page_height;
    float margin_top, margin_bottom, margin_left, margin_right;
    float printable_width, printable_height;
    float pgnum_from_top;

    /* wrapping */
    gint tab_width;

    /* balsa data */
    LibBalsaMessage *message;
    gchar *footer;
    GList *print_parts;

    /* character conversion info */
    iconv_t conv_data;

} PrintInfo;

typedef void (*prepare_func_t)(PrintInfo * pi, LibBalsaMessageBody * body);

typedef struct _mime_action_t {
    gchar *mime_type;
    prepare_func_t prepare_func;
}mime_action_t;

/*
 * helper function: try to print with the correct charset...
 */
static void
gnome_print_show_with_charset(PrintInfo * pi, char const * text, iconv_t conv)
{
    /* if we can not convert to utf8, try to print "raw" (which might fail) */
    if (conv == (iconv_t)(-1))
	gnome_print_show(pi->pc, text);
    else {
	gchar *conv_ibuf, *conv_ibufp, *conv_obuf, *conv_obufp;
	size_t ibuflen, obuflen;
	
	/* as iconv() changes all supplied pointers, we have to remember
	 * them... */
	conv_ibuf = conv_ibufp = g_strdup (text);
	ibuflen = strlen(conv_ibuf) + 1;
	obuflen = ibuflen * 4 + 1; /* should be sufficient? */
	conv_obuf = conv_obufp = g_malloc(obuflen);
	obuflen--;
	/* the prototype of iconv() changed with glibc 2.2 */
#if defined __GLIBC__ && __GLIBC__ && __GLIBC_MINOR__ <= 1 || (defined sun)
	iconv(conv, (const char **)&conv_ibuf, &ibuflen, &conv_obuf, &obuflen);
#else
	iconv(conv, &conv_ibuf, &ibuflen, &conv_obuf, &obuflen);
#endif
	*conv_obuf = '\0';
	gnome_print_show(pi->pc, conv_obufp);
	g_free (conv_ibufp);
	g_free (conv_obufp);
    }
}

static int
print_wrap_string(gchar ** str, GnomeFont * font, gint width, gint tab_width)
{
    gchar *ptr, *line = *str;
    gchar *eol;
    gint lines = 1;
    GString *wrapped;
    gdouble space_width = gnome_font_get_width_string(font, " ");
 
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
			    gnome_font_get_width_string_n(font, ptr, 1);
		    }
		    pos++;
		}
		ptr++;
	    }
	    if (*ptr && last_space) {
		wrapped->str[last_space] = '\n';
		lines++;
		line_width = 
		    gnome_font_get_width_string(font, 
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
	width = gnome_font_get_width_string(font, ptr);
	gnome_print_moveto(pi->pc, 
			   pi->margin_left + (pi->printable_width - width) / 2.0,
			   y);
	gnome_print_show_with_charset(pi, ptr, pi->conv_data);
	ptr = eol;
	if (eol) {
	    *eol = '\n';
	    ptr++;
	}
	y -= line_height;
    }
}

static void
start_new_page(PrintInfo * pi)
{
    GnomeFont *font;
    gchar *page_no;
    int width, ypos;
    gchar buf[20];

    if (pi->current_page)
	gnome_print_showpage(pi->pc);
    pi->current_page++;
    snprintf(buf, sizeof(buf ) - 1, "%d", pi->current_page);
    if (balsa_app.debug)
	g_print("Processing page %s\n", buf);

    /* print the page number */
    if (balsa_app.print_highlight_cited)
	gnome_print_setrgbcolor (pi->pc, 0.0, 0.0, 0.0);
    page_no = g_strdup_printf(_("Page: %i/%i"), pi->current_page, pi->pages);
    gnome_print_beginpage(pi->pc, buf);
    ypos = pi->page_height - pi->pgnum_from_top;
    font = gnome_font_new(balsa_app.print_header_font,
			  balsa_app.print_header_size);
    gnome_print_setfont(pi->pc, font);
    width = gnome_font_get_width_string(font, page_no);
    gnome_print_moveto(pi->pc, pi->page_width - pi->margin_left - width,
		       ypos);
    gnome_print_show_with_charset(pi, page_no, pi->conv_data);
    g_free(page_no);
    gtk_object_unref(GTK_OBJECT(font));
    
    /* print the footer */
    font = gnome_font_new(balsa_app.print_header_font,
			  balsa_app.print_footer_size);
    gnome_print_setfont(pi->pc, font);
    print_foot_lines(pi, font, 
		     pi->margin_bottom - 2 * balsa_app.print_footer_size,
		     balsa_app.print_footer_size, pi->footer);
    gtk_object_unref(GTK_OBJECT(font));
    pi->ypos = pi->margin_bottom + pi->printable_height;
}

/*
 * ~~~ stuff for the message header ~~~
 */
typedef struct _HeaderInfo {
    guint id_tag;
    float header_label_width;
    GList *headers;
} HeaderInfo;

static void
print_header_string(GList **header_list, const gchar *field_id,
		    const gchar *label, const gchar *value)
{
    gchar **hdr_pair;

    if (!value ||
	balsa_app.shown_headers == HEADERS_NONE ||
	!(balsa_app.shown_headers == HEADERS_ALL || 
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

    if (!values ||
	balsa_app.shown_headers == HEADERS_NONE ||
	!(balsa_app.shown_headers == HEADERS_ALL || 
	  libbalsa_find_word(field_id, balsa_app.selected_headers)))
	return;

    hdr_pair = g_new0(gchar *, 3);
    hdr_pair[0] = g_strdup(label);
    hdr_pair[1] = libbalsa_make_string_from_list(values);
    *header_list = g_list_append(*header_list, hdr_pair);
}

static void
prepare_header(PrintInfo * pi, LibBalsaMessageBody * body)
{
    gint lines;
    GnomeFont *font;
    HeaderInfo *pdata;
    GString *footer_string = NULL;
    const gchar *subject;
    gchar *date;
    GList *other_hdrs, *p;

    pdata = g_malloc(sizeof(HeaderInfo));
    pdata->id_tag = BALSA_PRINT_TYPE_HEADER;
    pdata->headers = NULL;

    subject = LIBBALSA_MESSAGE_GET_SUBJECT(pi->message);
    if (subject) {
	print_header_string (&pdata->headers, "subject", _("Subject:"),
			     subject);
	footer_string = g_string_new(subject);
    }

    date = libbalsa_message_date_to_gchar(pi->message, balsa_app.date_string);
    print_header_string (&pdata->headers, "date", _("Date:"), date);
    if (footer_string) {
	footer_string = g_string_append(footer_string, " - ");
	footer_string = g_string_append(footer_string, date);
    } else {
	footer_string = g_string_new(date);
    }
    g_free(date);

    if (pi->message->from) {
	gchar *from = libbalsa_address_to_gchar(pi->message->from, 0);
	print_header_string (&pdata->headers, "from", _("From:"), from);
	if (footer_string) {
	    footer_string = g_string_prepend(footer_string, " - ");
	    footer_string = g_string_prepend(footer_string, from);
	} else {
	    footer_string = g_string_new(from);
	}
	g_free(from);
    }

    print_header_list(&pdata->headers, "to", _("To:"), pi->message->to_list);
    print_header_list(&pdata->headers, "cc", _("Cc:"), pi->message->cc_list);
    print_header_list(&pdata->headers, "bcc", _("Bcc:"), pi->message->bcc_list);
    print_header_string (&pdata->headers, "fcc", _("Fcc:"),
			 pi->message->fcc_url);

    if (pi->message->dispnotify_to) {
	gchar *mdn_to = libbalsa_address_to_gchar(pi->message->dispnotify_to, 0);
	print_header_string (&pdata->headers, "disposition-notification-to", 
			     _("Disposition-Notification-To:"), mdn_to);
	g_free(mdn_to);
    }

    /* and now for the remaining headers... */
    other_hdrs = libbalsa_message_user_hdrs(pi->message);
    p = g_list_first(other_hdrs);
    while (p) {
	gchar **pair, *curr_hdr;
	pair = p->data;
	curr_hdr = g_strconcat(pair[0], ":", NULL);
	print_header_string (&pdata->headers, pair[0], curr_hdr, pair[1]);
	g_free(curr_hdr);
	g_strfreev(pair);
	p = g_list_next(p);
    }
    g_list_free(other_hdrs);

    /* wrap the footer if necessary */
    pi->footer = footer_string->str;
    g_string_free(footer_string, FALSE);

    font = gnome_font_new(balsa_app.print_header_font,
			  balsa_app.print_footer_size);
    print_wrap_string(&pi->footer, font, pi->printable_width, pi->tab_width);
    gtk_object_unref(GTK_OBJECT(font));    
    
    /* calculate the label width */
    pdata->header_label_width = 0;
    font = gnome_font_new(balsa_app.print_header_font,
			  balsa_app.print_header_size);
    p = g_list_first(pdata->headers);
    while (p) {
	gchar **strgs = p->data;
	gint width;

	width = gnome_font_get_width_string(font, strgs[0]);
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
	    print_wrap_string(&strgs[1], font,
			      pi->printable_width - pdata->header_label_width, 
			      pi->tab_width);
	p = g_list_next(p);
    }

    if (pi->ypos - lines * balsa_app.print_header_size < pi->margin_bottom) {
	lines -= (pi->ypos - pi->margin_bottom) / balsa_app.print_header_size;
	pi->pages++;
	while (lines * balsa_app.print_header_size > pi->printable_height) {
	    lines -= pi->printable_height / balsa_app.print_header_size;
	    pi->pages++;
	}
	pi->ypos = pi->margin_bottom + pi->printable_height -
	    lines * balsa_app.print_header_size;
    } else
	pi->ypos -= lines * balsa_app.print_header_size;
    gtk_object_unref(GTK_OBJECT(font));

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
	gnome_print_show_with_charset(pi, ptr, pi->conv_data);
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
    GnomeFont *font;
    GList *p;

    g_return_if_fail(pdata->id_tag == BALSA_PRINT_TYPE_HEADER);

    if (balsa_app.print_highlight_cited)
	gnome_print_setrgbcolor (pi->pc, 0.0, 0.0, 0.0);
    font = gnome_font_new(balsa_app.print_header_font, 
			  balsa_app.print_header_size);
    gnome_print_setfont(pi->pc, font);
    p = g_list_first(pdata->headers);
    while (p) {
	gchar **pair = p->data;

	pi->ypos -= balsa_app.print_header_size;
	if (pi->ypos < pi->margin_bottom)
	    start_new_page(pi);
	gnome_print_moveto(pi->pc, pi->margin_left, pi->ypos);
	gnome_print_show_with_charset(pi, pair[0], pi->conv_data);
	print_header_val(pi, pi->margin_left + pdata->header_label_width,
			 &pi->ypos, balsa_app.print_header_size, pair[1], font);
	g_strfreev(pair);
	p = g_list_next(p);
    }
    gtk_object_unref(GTK_OBJECT(font));
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

    pdata = g_malloc(sizeof(SeparatorInfo));
    pdata->id_tag = BALSA_PRINT_TYPE_SEPARATOR;
    pi->ypos -= (balsa_app.print_header_size / 2.0);
    if (pi->ypos < pi->margin_bottom) {
	pi->pages++;
	pi->ypos = pi->margin_bottom + pi->printable_height - 
	    (balsa_app.print_header_size / 2.0);
    } else
	pi->ypos -= (balsa_app.print_header_size / 2.0);

    pi->print_parts = g_list_append (pi->print_parts, pdata);
}

static void
print_separator(PrintInfo * pi, gpointer * data)
{
    SeparatorInfo *pdata = (SeparatorInfo *)data;

    g_return_if_fail(pdata->id_tag == BALSA_PRINT_TYPE_SEPARATOR);

    if (balsa_app.print_highlight_cited)
	gnome_print_setrgbcolor (pi->pc, 0.0, 0.0, 0.0);
    pi->ypos -= (balsa_app.print_header_size / 2.0);
    if (pi->ypos < pi->margin_bottom)
	start_new_page(pi);
    gnome_print_setlinewidth(pi->pc, 0.5);
    gnome_print_newpath(pi->pc);
    gnome_print_moveto(pi->pc, pi->margin_left, pi->ypos);
    gnome_print_lineto(pi->pc, pi->printable_width + pi->margin_left, pi->ypos);
    gnome_print_stroke (pi->pc);
    pi->ypos -= (balsa_app.print_header_size / 2.0);
}

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
    iconv_t conv;
} PlainTextInfo;

static GList *
print_wrap_body(gchar * str, GnomeFont * font, gint width, gint tab_width)
{
    gchar *ptr, *line = str;
    gchar *eol;
    regex_t rex;
    gboolean checkQuote = balsa_app.print_highlight_cited;
    GList *wrappedLines = NULL;
    gdouble space_width = gnome_font_get_width_string(font, " ");
 
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
			    gnome_font_get_width_string_n(font, ptr, 1);
		    }
		    pos++;
		}
		ptr++;
	    }
	    if (*ptr && last_space) {
		gint lastQLevel = lineInfo->quoteLevel;
		lineInfo->lineData = g_strndup(wrLine->str, last_space);
		wrappedLines = g_list_append(wrappedLines, lineInfo);
		lineInfo = g_malloc(sizeof(lineInfo_T));
		lineInfo->quoteLevel = lastQLevel;
		wrLine = g_string_erase(wrLine, 0, last_space + 1);
		line_width = 
		    gnome_font_get_width_string(font, wrLine->str);
		last_space = 0;
	    }
	}
	lineInfo->lineData = wrLine->str;
	wrappedLines = g_list_append(wrappedLines, lineInfo);
	g_string_free(wrLine, FALSE);
	line = eol;
	if (eol)
	    line++;
    }

    if (checkQuote)
	regfree(&rex);

    return wrappedLines;
}

static void
prepare_plaintext(PrintInfo * pi, LibBalsaMessageBody * body)
{
    PlainTextInfo *pdata;
    GnomeFont *font;
    gchar *charset;
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
	libbalsa_message_body_save_temporary(body, NULL);
	part = fopen(body->temp_filename, "r");
	if (part) {
	    libbalsa_readfile(part, &textbuf);
	    fclose(part);
	    }
    }

    /* get the necessary iconv structure, or use iso-8859-1 */
    charset = g_strdup(libbalsa_message_body_charset(body));
    if (charset) {
	pdata->conv = iconv_open("UTF-8", charset);
	if (pdata->conv == (iconv_t)(-1)) {
	    balsa_information(LIBBALSA_INFORMATION_ERROR, NULL,
			      _("Can not convert %s, falling back to US-ASCII.\nSome characters may be printed incorrectly."),
			      charset);
	    pdata->conv = iconv_open("UTF-8", "US-ASCII");
	}
	g_free(charset);
    } else
	pdata->conv = iconv_open("UTF-8", "ISO-8859-1");

    /* fake an empty buffer if textbuf is NULL */
    if (!textbuf)
	textbuf = g_strdup("");
    
    /* wrap lines (if necessary) */
    font = gnome_font_new(balsa_app.print_body_font, balsa_app.print_body_size);
    pdata->textlines = 
	print_wrap_body(textbuf, font, pi->printable_width, pi->tab_width);
    g_free(textbuf);
    lines = g_list_length(pdata->textlines);
    gtk_object_unref(GTK_OBJECT(font));

    /* calculate the y end position */
    if (pi->ypos - lines * balsa_app.print_body_size < pi->margin_bottom) {
	int lines_left = lines;

	lines_left -= (pi->ypos - pi->margin_bottom) / balsa_app.print_body_size;
	pi->pages++;
	while (lines_left * balsa_app.print_body_size > pi->printable_height) {
	    lines_left -= pi->printable_height / balsa_app.print_body_size;
	    pi->pages++;
	}
	pi->ypos = pi->margin_bottom + pi->printable_height -
	    lines_left * balsa_app.print_body_size;
    } else
	pi->ypos -= lines * balsa_app.print_body_size;

    pi->print_parts = g_list_append (pi->print_parts, pdata);
}

static void
print_plaintext(PrintInfo * pi, gpointer * data)
{
    PlainTextInfo *pdata = (PlainTextInfo *)data;
    GnomeFont *font;
    GList *l;

    g_return_if_fail(pdata->id_tag == BALSA_PRINT_TYPE_PLAINTEXT);

    font = gnome_font_new(balsa_app.print_body_font, balsa_app.print_body_size);
    gnome_print_setfont(pi->pc, font);
    l = pdata->textlines;
    while (l) {
 	lineInfo_T *lineInfo = (lineInfo_T *)l->data;
 	
 	pi->ypos -= balsa_app.print_body_size;
	if (pi->ypos < pi->margin_bottom) {
	    start_new_page(pi);
	    gnome_print_setfont(pi->pc, font);
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
 	gnome_print_show_with_charset(pi, lineInfo->lineData, pdata->conv);
 	g_free(lineInfo->lineData);
 	g_free(l->data);
 	l = l->next;
    }
    g_list_free(pdata->textlines);
    if (pdata->conv != (iconv_t)(-1))
	iconv_close(pdata->conv);
    gtk_object_unref(GTK_OBJECT(font));
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
    GnomeFont *font;

    pdata = g_malloc(sizeof(DefaultInfo));
    pdata->id_tag = BALSA_PRINT_TYPE_DEFAULT;

    if (body->mutt_body)
	conttype = libbalsa_message_body_get_content_type(body);
    else
	conttype = libbalsa_lookup_mime_type(body->filename);

    /* get a pixbuf according to the mime type */
    icon_name = libbalsa_icon_finder(conttype, NULL, NULL);
    pdata->pixbuf = gdk_pixbuf_new_from_file(icon_name);
    pdata->image_width = gdk_pixbuf_get_width (pdata->pixbuf);
    pdata->image_height = gdk_pixbuf_get_height (pdata->pixbuf);
    g_free(icon_name);

    /* gather some info about this part */
    pdata->labels = g_new0(gchar *, 5); /* four fields, one terminator */
    pdata->labels[hdr++] = g_strdup(_("Type:"));
    pdata->labels[hdr++] = g_strdup(conttype);
    if (body->filename) {
	pdata->labels[hdr++] = g_strdup(_("Filename:"));
	pdata->labels[hdr++] = g_strdup(body->filename);
    }
    font = gnome_font_new(balsa_app.print_header_font,
 			  balsa_app.print_header_size);
    pdata->label_width = gnome_font_get_width_string(font, pdata->labels[0]);
    if (pdata->labels[2] && 
	gnome_font_get_width_string(font, pdata->labels[2]) > pdata->label_width)
	pdata->label_width = gnome_font_get_width_string(font, pdata->labels[2]);
    pdata->label_width += 6;

    lines = print_wrap_string(&pdata->labels[1], font,
			      pi->printable_width - pdata->label_width - 
			      pdata->image_width - 10, pi->tab_width);
    if (!lines)
	lines = 1;
    if (pdata->labels[3])
	lines += print_wrap_string(&pdata->labels[3], font,
				   pi->printable_width - pdata->label_width - 
				   pdata->image_width - 10, pi->tab_width);
    pdata->text_height = lines * balsa_app.print_header_size;

    pdata->part_height = (pdata->text_height > pdata->image_height) ?
	pdata->text_height : pdata->image_height;
    if (pi->ypos - pdata->part_height < pi->margin_bottom) {
	pi->pages++;
	pi->ypos = pi->margin_bottom + pi->printable_height - pdata->part_height;
    } else
	pi->ypos -= pdata->part_height;
    
    gtk_object_unref(GTK_OBJECT(font));
    g_free(conttype);

    pi->print_parts = g_list_append (pi->print_parts, pdata);
}

static void
print_default(PrintInfo * pi, gpointer data)
{
    double matrix[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    DefaultInfo *pdata = (DefaultInfo *)data;
    GnomeFont *font;
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
    gnome_print_pixbuf (pi->pc, pdata->pixbuf);
    gnome_print_grestore (pi->pc);
    gdk_pixbuf_unref(pdata->pixbuf);
    
    /* print the description */
    font = gnome_font_new(balsa_app.print_header_font,
 			  balsa_app.print_header_size);
    gnome_print_setfont(pi->pc, font);
    pi->ypos -= (pdata->part_height - pdata->text_height) / 2.0 + 
	balsa_app.print_header_size;
    offset = pi->margin_left + pdata->image_width + 10;
    for (i = 0; pdata->labels[i]; i += 2) {
	gnome_print_moveto(pi->pc, offset, pi->ypos);
	gnome_print_show_with_charset(pi, pdata->labels[i], pi->conv_data);
	print_header_val(pi, offset + pdata->label_width, &pi->ypos,
 			 balsa_app.print_header_size, pdata->labels[i + 1], font);
 	pi->ypos -= balsa_app.print_header_size;
    }
    pi->ypos -= (pdata->part_height - pdata->text_height) / 2.0 -
	balsa_app.print_header_size;
    gtk_object_unref(GTK_OBJECT(font));
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
    
    pdata = g_malloc(sizeof(ImageInfo));
    pdata->id_tag = BALSA_PRINT_TYPE_IMAGE;

    libbalsa_message_body_save_temporary(body, NULL);
    pdata->pixbuf = gdk_pixbuf_new_from_file(body->temp_filename);
    
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
    gnome_print_pixbuf (pi->pc, pdata->pixbuf);
    gnome_print_grestore (pi->pc);
    pi->ypos -= pdata->print_height;
    gdk_pixbuf_unref(pdata->pixbuf);
}

/*
 * scan the body list and prepare print data according to the content type
 */
static void 
scan_body(PrintInfo * pi, LibBalsaMessageBody * body)
{
    static mime_action_t mime_actions [] = {
	{"multipart", NULL},              /* ignore `multipart' entries */
	{"text/html", prepare_default},   /* don't print html source */
	{"text", prepare_plaintext},
	{"image", prepare_image},
	{NULL, prepare_default}           /* anything else... */
    };
    mime_action_t *action;

    while (body) {
	gchar *conttype;

	if (body->buffer)
	    conttype = g_strdup("text");
	else
	    if (!body->mutt_body)
		conttype = g_strdup("default");
	    else
		conttype = libbalsa_message_body_get_content_type(body);
	
	for (action = mime_actions; 
	     action->mime_type && 
		 g_strncasecmp(action->mime_type, conttype, 
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

static PrintInfo *
print_info_new(const gchar * paper, LibBalsaMessage * msg,
	       GnomePrintDialog * dlg)
{
    gchar *the_charset;
    GnomeFont *font;
    GList *papers;
    PrintInfo *pi = g_new0(PrintInfo, 1);

    pi->paper = gnome_paper_with_name(paper);
    if (pi->paper == NULL) {
     	papers = gnome_paper_name_list();
	balsa_information(LIBBALSA_INFORMATION_WARNING, NULL,
			  _("Balsa could not find paper type \"%s\".\n"), paper);
	balsa_information(LIBBALSA_INFORMATION_WARNING, NULL,
			  _("Using paper type \"%s\" from /etc/paper.config instead\n"),
			  (char *)papers->data);
	pi->paper = gnome_paper_with_name((char *)papers->data);
    }
    pi->master = gnome_print_master_new_from_dialog(dlg);
    gnome_print_master_set_paper(pi->master, pi->paper);
    pi->pc = gnome_print_master_get_context(pi->master);

    pi->page_width = gnome_paper_pswidth(pi->paper);
    pi->page_height = gnome_paper_psheight(pi->paper);

    pi->margin_top = 0.75 * 72;
    pi->margin_bottom = 0.75 * 72;
    pi->margin_left = 0.75 * 72;
    pi->margin_right = 0.75 * 72;
    pi->pgnum_from_top = 0.5 * 72;
    pi->printable_width =
	pi->page_width - pi->margin_left - pi->margin_right;
    pi->printable_height =
	pi->page_height - pi->margin_top - pi->margin_bottom;

    pi->tab_width = 8;
    pi->pages = 1;
    pi->ypos = pi->margin_bottom + pi->printable_height;

    pi->message = msg;
    prepare_header(pi, NULL);
    
    the_charset = (gchar *)libbalsa_message_charset(msg);
    if (the_charset) {
	pi->conv_data = iconv_open("UTF-8", the_charset);
	if (pi->conv_data == (iconv_t)(-1)) {
	    balsa_information(LIBBALSA_INFORMATION_ERROR, NULL,
			      _("Can not convert %s, falling back to US-ASCII.\nSome characters may be printed incorrectly."),
			      the_charset);
	    pi->conv_data = iconv_open("UTF-8", "US-ASCII");
	}
    } else
	pi->conv_data = iconv_open("UTF-8", "ISO-8859-1");

    /* now get the message contents... */
    libbalsa_message_body_ref(msg);
    scan_body(pi, msg->body_list);
    libbalsa_message_body_unref(msg);

    return pi;
}

static void
print_info_destroy(PrintInfo * pi)
{
    GList *part;

    if (pi->conv_data != (iconv_t)(-1))
	iconv_close(pi->conv_data);
    part = pi->print_parts;
    while (part) {
	if (part->data)
	    g_free(part->data);
	part = g_list_next(part);
    }
    g_list_free(pi->print_parts);
    pi->print_parts = NULL;
    g_free(pi->footer);
    pi->footer = NULL;
    g_free(pi);
}

/* print_message:
   prints given message
*/
static void
print_message(PrintInfo * pi)
{
    GList *print_task;

    if (balsa_app.debug)
	g_print("Printing.\n");
    start_new_page(pi);

    print_task = pi->print_parts;
    while (print_task) {
	guint *id = (guint *)(print_task->data);

	switch (*id) {
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
	default:
	    break;
	}

	print_task = g_list_next(print_task);
    }
    gnome_print_showpage(pi->pc);
}

static gboolean
is_font_ok(const gchar * font_name)
{
    GnomeFont *test_font = gnome_font_new(font_name, 10);

    if (!test_font) {
	balsa_information(LIBBALSA_INFORMATION_ERROR, NULL,
			  _("Balsa could not find font %s\n"
			    "Printing is not possible"), font_name);
	return FALSE;
    }
    gtk_object_unref(GTK_OBJECT(test_font));
    return TRUE;
}

void
message_print_cb(GtkWidget * widget, gpointer cbdata)
{
    BalsaIndex *index;
    GList *list;
    LibBalsaMessage *msg;

    g_return_if_fail(cbdata);

    index = BALSA_INDEX(balsa_window_find_current_index(BALSA_WINDOW(cbdata)));
    if (!index || (list = GTK_CLIST(index->ctree)->selection) == NULL)
	return;

    msg =
	LIBBALSA_MESSAGE(gtk_ctree_node_get_row_data(
	    GTK_CTREE(index->ctree), list->data));
    /* print only first selected message */

    message_print(msg);
}

/* callback to read a comby entry */
static void 
entry_changed (GtkEntry *entry, gchar **value)
{
    g_free(*value);
    *value = g_strdup(gtk_entry_get_text(entry));
}

/* callback to read an adjustment */
static void 
adjust_changed (GtkAdjustment *adj, gfloat *value)
{
    *value = adj->value;
}

/* callback to read a toggle button */
static void 
togglebut_changed (GtkToggleButton *tbut, gboolean *value)
{
    *value = gtk_toggle_button_get_active (tbut);
}

/*
 * changes print dialog to a two-page notebook with a "settings" page
 */
static void
adjust_print_dialog(GtkWidget * dialog)
{
    GtkWidget  *frame;
    GtkWidget  *combo;
    GtkWidget  *dlgVbox;
    GtkWidget  *notebook;
    GtkWidget  *vbox;
    GtkWidget  *oldPrDlg[2];
    GtkWidget  *table;
    GtkWidget  *label;
    GtkWidget  *spinbut;
    GtkWidget  *chkbut;
    GtkObject  *adj;
    GtkEntry   *entry;
    const GnomePaper   *gpaper;
    GList      *childList;
    gint       n, movedElems = 0;

    dlgVbox = GNOME_DIALOG(dialog)->vbox;

    notebook = gtk_notebook_new();

    label = gtk_label_new(_("Print"));
    vbox = gtk_vbox_new(FALSE, GNOME_PAD);
    gtk_container_set_border_width (GTK_CONTAINER (vbox),
				    GNOME_PAD_SMALL);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox, label);

    /* move all elements from the gnome dialog vbox to the 1st notebook page */
    childList = g_list_first(GTK_BOX(dlgVbox)->children);
    while(childList) {
	struct _GtkBoxChild *elem = (struct _GtkBoxChild *)childList->data;
	if (!GTK_IS_HSEPARATOR(elem->widget) &&
	    !GTK_IS_HBUTTON_BOX(elem->widget))
	    oldPrDlg[movedElems++] = elem->widget;
	childList = childList->next;
    }
    
    for (n = 0; n < movedElems; n++) {
	gtk_object_ref(GTK_OBJECT(oldPrDlg[n]));
	gtk_container_remove(GTK_CONTAINER(dlgVbox), oldPrDlg[n]);
	gtk_box_pack_start(GTK_BOX(vbox), oldPrDlg[n], FALSE, FALSE, 3);
	gtk_object_unref(GTK_OBJECT(oldPrDlg[n]));
    }

    /* create a 2nd notebook page for the settings */
    label = gtk_label_new(_("Settings"));
    vbox = gtk_vbox_new(FALSE, 3);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox, label);

    /* paper selection */
    if ((gpaper = gnome_paper_with_name(balsa_app.paper_size)) == NULL) {
	balsa_information(LIBBALSA_INFORMATION_WARNING, NULL,
			  _("Balsa could not find paper type \"%s\", using"
			    " system default.\n"), balsa_app.paper_size);
	balsa_information(LIBBALSA_INFORMATION_WARNING, NULL,
			  _("Check your paper type configuration or balsa preferences\n"));
	balsa_app.paper_size = g_strdup(gnome_paper_name_default());
    }

    frame = gtk_frame_new(_("Paper"));
    gtk_container_set_border_width(GTK_CONTAINER(frame), 3);
    gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 3);
    combo = gtk_combo_new();
    gtk_container_set_border_width(GTK_CONTAINER(combo), 3);
    entry = GTK_ENTRY(GTK_COMBO(combo)->entry);
    gtk_combo_set_popdown_strings(GTK_COMBO(combo), 
				  g_list_copy(gnome_paper_name_list()));
    gtk_entry_set_text(entry, balsa_app.paper_size);
    gtk_signal_connect(GTK_OBJECT(entry), "changed",
		       GTK_SIGNAL_FUNC(entry_changed), 
		       &balsa_app.paper_size);
    gtk_container_add (GTK_CONTAINER(frame), combo);

    /* header & footer font */
    frame = gtk_frame_new(_("Font for header and footer"));
    gtk_container_set_border_width(GTK_CONTAINER(frame), 3);
    gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 3);
    table = gtk_table_new (3, 2, FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(table), 3);
    gtk_table_set_row_spacings(GTK_TABLE(table), 5);
    gtk_table_set_col_spacings(GTK_TABLE(table), 5);
    gtk_container_add (GTK_CONTAINER (frame), table);
    combo = gtk_combo_new();
    entry = GTK_ENTRY(GTK_COMBO(combo)->entry);
    gtk_combo_set_popdown_strings(GTK_COMBO(combo), 
				  g_list_copy(gnome_font_list()));
    gtk_entry_set_text(entry, balsa_app.print_header_font);
    gtk_signal_connect(GTK_OBJECT(entry), "changed",
		       GTK_SIGNAL_FUNC(entry_changed), 
		       &balsa_app.print_header_font);
    gtk_table_attach (GTK_TABLE (table), combo, 0, 2, 0, 1,
                    (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
    label = gtk_label_new (_("header font size"));
    gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		      (GtkAttachOptions) (GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
    adj = 
	gtk_adjustment_new (balsa_app.print_header_size, 0.1, 255.0, 0.1, 1, 10);
    gtk_signal_connect(GTK_OBJECT(adj), "value-changed",
		       GTK_SIGNAL_FUNC(adjust_changed), 
		       &balsa_app.print_header_size);    
    spinbut = gtk_spin_button_new (GTK_ADJUSTMENT (adj), 0.1, 1);
    gtk_spin_button_set_numeric (GTK_SPIN_BUTTON(spinbut), TRUE);
    gtk_table_attach (GTK_TABLE (table), spinbut, 1, 2, 1, 2,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    label = gtk_label_new (_("footer font size"));
    gtk_table_attach (GTK_TABLE (table), label, 0, 1, 2, 3,
		      (GtkAttachOptions) (GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
    adj =
	gtk_adjustment_new (balsa_app.print_footer_size, 0.1, 255.0, 0.1, 1, 10);
    gtk_signal_connect(GTK_OBJECT(adj), "value-changed",
		       GTK_SIGNAL_FUNC(adjust_changed), 
		       &balsa_app.print_footer_size);    
    spinbut = gtk_spin_button_new (GTK_ADJUSTMENT (adj), 0.1, 1);
    gtk_spin_button_set_numeric (GTK_SPIN_BUTTON(spinbut), TRUE);
    gtk_table_attach (GTK_TABLE (table), spinbut, 1, 2, 2, 3,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);

    /* body font */
    frame = gtk_frame_new(_("Font for message body"));
    gtk_container_set_border_width(GTK_CONTAINER(frame), 3);
    gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 3);
    table = gtk_table_new (3, 2, FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(table), 3);
    gtk_table_set_row_spacings(GTK_TABLE(table), 5);
    gtk_table_set_col_spacings(GTK_TABLE(table), 5);
    gtk_container_add (GTK_CONTAINER (frame), table);
    combo = gtk_combo_new();
    gtk_container_set_border_width(GTK_CONTAINER(combo), 3);
    entry = GTK_ENTRY(GTK_COMBO(combo)->entry);
    gtk_combo_set_popdown_strings(GTK_COMBO(combo), 
				  g_list_copy(gnome_font_list()));
    gtk_entry_set_text(entry, balsa_app.print_body_font);
    gtk_signal_connect(GTK_OBJECT(entry), "changed",
		       GTK_SIGNAL_FUNC(entry_changed), 
		       &balsa_app.print_body_font);
    gtk_table_attach (GTK_TABLE (table), combo, 0, 2, 0, 1,
                    (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
    label = gtk_label_new (_("body font size"));
    gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		      (GtkAttachOptions) (GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
    adj = gtk_adjustment_new (balsa_app.print_body_size, 0.1, 255.0, 0.1, 1, 10);
    gtk_signal_connect(GTK_OBJECT(adj), "value-changed",
		       GTK_SIGNAL_FUNC(adjust_changed), 
		       &balsa_app.print_body_size);    
    spinbut = gtk_spin_button_new (GTK_ADJUSTMENT (adj), 0.1, 1);
    gtk_spin_button_set_numeric (GTK_SPIN_BUTTON(spinbut), TRUE);
    gtk_table_attach (GTK_TABLE (table), spinbut, 1, 2, 1, 2,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);

    /* highlight cited stuff */
    frame = gtk_frame_new(_("Highlight cited text"));
    gtk_container_set_border_width(GTK_CONTAINER(frame), 3);
    gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 3);    
    chkbut = 
	gtk_check_button_new_with_label ("enable highlighting of cited text");
    gtk_signal_connect(GTK_OBJECT(chkbut), "toggled",
		       GTK_SIGNAL_FUNC(togglebut_changed), 
		       &balsa_app.print_highlight_cited);    
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(chkbut),
				  balsa_app.print_highlight_cited);
    gtk_container_add (GTK_CONTAINER (frame), chkbut);

    gtk_box_pack_start(GTK_BOX(dlgVbox), notebook, FALSE, FALSE, 3);
    gtk_widget_show_all(notebook);
}

void
message_print(LibBalsaMessage * msg)
{
    GtkWidget *dialog;
    PrintInfo *pi;
    gboolean preview = FALSE;

    g_return_if_fail(msg);
    if (!is_font_ok(balsa_app.print_body_font) || 
	!is_font_ok(balsa_app.print_header_font))
	return;

    dialog = gnome_print_dialog_new(_("Print message"),
				    GNOME_PRINT_DIALOG_COPIES);
    /*
     * add paper selection combo to print dialog
     */
    adjust_print_dialog(dialog);
    gnome_dialog_set_parent(GNOME_DIALOG(dialog),
			    GTK_WINDOW(balsa_app.main_window));
    gtk_window_set_wmclass(GTK_WINDOW(dialog), "print", "Balsa");

    switch (gnome_dialog_run(GNOME_DIALOG(dialog))) {
    case GNOME_PRINT_PRINT:
	break;
    case GNOME_PRINT_PREVIEW:
	preview = TRUE;
	break;
    case GNOME_PRINT_CANCEL:
	gnome_dialog_close(GNOME_DIALOG(dialog));
    default:
	return;
    }
    pi = print_info_new(balsa_app.paper_size, msg, GNOME_PRINT_DIALOG(dialog));
    gnome_dialog_close(GNOME_DIALOG(dialog));

    /* do the Real Job */
    print_message(pi);
    gnome_print_master_close(pi->master);
    if (preview) {
	GnomePrintMasterPreview *preview_widget =
	    gnome_print_master_preview_new(pi->master,
		 			   _("Balsa: message print preview"));
	gtk_widget_show(GTK_WIDGET(preview_widget));
    } else {
	gnome_print_master_print(pi->master);
	gtk_object_unref(GTK_OBJECT(pi->master));
    }

    print_info_destroy(pi);
}
#endif



