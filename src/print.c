/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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
    balsa_information(LIBBALSA_INFORMATION_ERROR, _(
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
#include "balsa-index.h"

#define BALSA_PRINT_BODY_FONT "Courier"
#define BALSA_PRINT_BODY_SIZE 10
#define BALSA_PRINT_HEAD_FONT "Helvetica"
#define BALSA_PRINT_HEAD_SIZE 11
#define BALSA_PRINT_FOOT_SIZE 7


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
    gint chars_per_line;

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
gnome_print_show_with_charset(PrintInfo * pi, char const * text)
{
    /* if we can not convert to utf8, try to print "raw" (which might fail) */
    if (pi->conv_data == (iconv_t)(-1))
	gnome_print_show(pi->pc, text);
    else {
	gchar *conv_ibuf, *conv_ibufp, *conv_obuf, *conv_obufp;
	size_t ibuflen, obuflen;
	
	/* as iconv() changes all supplied pointers, we have to remember
	 * them... */
	conv_ibuf = conv_ibufp = g_strdup (text);
	ibuflen = strlen(conv_ibuf) + 1;
	obuflen = ibuflen << 1; /* should be sufficient? */
	conv_obuf = conv_obufp = g_malloc(obuflen);
	/* the prototype of iconv() changed with glibc 2.2 */
#if defined __GLIBC__ && __GLIBC__ && __GLIBC_MINOR__ <= 1
	iconv(pi->conv_data, (const char **)&conv_ibuf, &ibuflen, &conv_obuf, &obuflen);
#else
	iconv(pi->conv_data, &conv_ibuf, &ibuflen, &conv_obuf, &obuflen);
#endif
	gnome_print_show(pi->pc, conv_obufp);
	g_free (conv_ibufp);
	g_free (conv_obufp);
    }
}

static int
print_wrap_string(gchar * str, GnomeFont * font, gint width)
{
    gchar *ptr, *line = str, *last_space = NULL;
    gchar *eol;
    int lines = 1;
    double line_width;
    g_return_val_if_fail(str, 0);

    line_width = 0;
    g_strchomp(str);
    while (line) {
	eol = strchr(line, '\n');
	if (eol)
	    *eol = '\0';
	ptr = line;
	while (*ptr) {
	    line_width = 0;
	    last_space = NULL;
	    while (*ptr && (line_width <= width || !last_space)) {
		if (isspace((int)*ptr)) {
		    *ptr = ' ';
		    last_space = ptr;
		}
		line_width += gnome_font_get_width_string_n(font, ptr++, 1);
	    }
	    if (*ptr) {
		*last_space = '\n';
		ptr = last_space + 1;
		lines++;
	    }
	}
	line = eol;
	if (eol) {
	    *eol = '\n';
	    lines++;
	    line++;
	}
    }
    return lines;
}

/* print_line:
   prepares the line, replaces tabs with spaces and prints.
   Trusts that libbalsa_wrap_strig did its job (which it might not for
   very long lines without spaces).
*/
static gchar *
print_line(PrintInfo * pi, gchar * pointer)
{
    int pos = 0, i;
    gchar *linebuffer;

    linebuffer = g_malloc(pi->chars_per_line + 1);
    while (*pointer && *pointer != '\n') {
	if (pos < pi->chars_per_line) {
	    switch (*pointer) {
	    case '\t':
		for (i = 0;
		     pos + i < pi->chars_per_line && i < pi->tab_width;
		     i++) linebuffer[pos++] = ' ';
		break;
	    default:
		linebuffer[pos++] = *pointer;
	    }
	}
	pointer++;
    }

    if (*pointer)
	pointer++;		/* skip EOL character     */
    linebuffer[pos] = '\0';	/* make sure line has EOS */

    gnome_print_moveto(pi->pc, pi->margin_left, pi->ypos);
    gnome_print_show_with_charset(pi, linebuffer);
    g_free(linebuffer);
    return pointer;
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
	gnome_print_show_with_charset(pi, ptr);
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
    page_no = g_strdup_printf(_("Page: %i/%i"), pi->current_page, pi->pages);
    gnome_print_beginpage(pi->pc, buf);
    ypos = pi->page_height - pi->pgnum_from_top;
    font = gnome_font_new(BALSA_PRINT_HEAD_FONT, BALSA_PRINT_HEAD_SIZE);
    gnome_print_setfont(pi->pc, font);
    width = gnome_font_get_width_string(font, page_no);
    gnome_print_moveto(pi->pc, pi->page_width - pi->margin_left - width,
		       ypos);
    gnome_print_show_with_charset(pi, page_no);
    g_free(page_no);
    gtk_object_unref(GTK_OBJECT(font));
    
    /* print the footer */
    font = gnome_font_new(BALSA_PRINT_HEAD_FONT, BALSA_PRINT_FOOT_SIZE);
    gnome_print_setfont(pi->pc, font);
    print_foot_lines(pi, font, pi->margin_bottom - 2 * BALSA_PRINT_FOOT_SIZE,
		     BALSA_PRINT_FOOT_SIZE, pi->footer);
    gtk_object_unref(GTK_OBJECT(font));
    pi->ypos = pi->margin_bottom + pi->printable_height;
}

/*
 * ~~~ stuff for the message header ~~~
 */
typedef struct _HeaderInfo {
    guint id_tag;
    float header_label_width;
    gchar **headers;		/* can be released with g_strfreev() */    
} HeaderInfo;

static void
prepare_header(PrintInfo * pi, LibBalsaMessageBody * body)
{
    const int MAX_HDRS = 4;	/* max number of printed headers */
    int hdr = 0, i, width, lines;
    GnomeFont *font;
    HeaderInfo *pdata;
    GString *footer_string = NULL;

    pdata = g_malloc(sizeof(HeaderInfo));
    pdata->id_tag = BALSA_PRINT_TYPE_HEADER;
    pdata->headers = g_new0(gchar *, (MAX_HDRS + 1) * 2);

    if (pi->message->from) {
	pdata->headers[hdr++] = g_strdup(_("From:"));
	pdata->headers[hdr++] = 
	    libbalsa_address_to_gchar(pi->message->from, 0);
	footer_string = g_string_new(pdata->headers[hdr - 1]);
    }
    if (pi->message->to_list) {
	pdata->headers[hdr++] = g_strdup(_("To:"));
	pdata->headers[hdr++] =
	    libbalsa_make_string_from_list(pi->message->to_list);
    }
    if (pi->message->subject) {
	pdata->headers[hdr++] = g_strdup(_("Subject:"));
	pdata->headers[hdr++] = g_strdup(pi->message->subject);
	if (footer_string) {
	    footer_string = g_string_append(footer_string, " - ");
	    footer_string = g_string_append(footer_string, 
					    pdata->headers[hdr - 1]);
	} else
	    footer_string = g_string_new(pdata->headers[hdr - 1]);
    }
    pdata->headers[hdr++] = g_strdup(_("Date:"));
    pdata->headers[hdr++] = 
	libbalsa_message_date_to_gchar(pi->message, balsa_app.date_string);

    if (footer_string) {
	footer_string = g_string_append(footer_string, " - ");
	footer_string = g_string_append(footer_string, 
					pdata->headers[hdr - 1]);
    } else
	footer_string = g_string_new(pdata->headers[hdr - 1]);
    pi->footer = footer_string->str;
    g_string_free(footer_string, FALSE);

    font = gnome_font_new(BALSA_PRINT_HEAD_FONT, BALSA_PRINT_FOOT_SIZE);
    print_wrap_string(pi->footer, font, pi->printable_width);
    gtk_object_unref(GTK_OBJECT(font));    
    
    pdata->header_label_width = 0;
    font = gnome_font_new(BALSA_PRINT_HEAD_FONT, BALSA_PRINT_HEAD_SIZE);
    for (i = 0; i < hdr; i += 2) {
	width = gnome_font_get_width_string(font, pdata->headers[i]);
	if (width > pdata->header_label_width)
	    pdata->header_label_width = width;
    }
    pdata->header_label_width += 6;	/* pts */

    lines = 0;
    for (i = 1; i < hdr; i += 2)
	lines += print_wrap_string(pdata->headers[i], font,
				   pi->printable_width -
				   pdata->header_label_width);

    if (pi->ypos - lines * BALSA_PRINT_HEAD_SIZE < pi->margin_bottom) {
	lines -= (pi->ypos - pi->margin_bottom) / BALSA_PRINT_HEAD_SIZE;
	pi->pages++;
	while (lines * BALSA_PRINT_HEAD_SIZE > pi->printable_height) {
	    lines -= pi->printable_height / BALSA_PRINT_HEAD_SIZE;
	    pi->pages++;
	}
	pi->ypos = pi->margin_bottom + pi->printable_height -
	    lines * BALSA_PRINT_HEAD_SIZE;
    } else
	pi->ypos -= lines * BALSA_PRINT_HEAD_SIZE;
    gtk_object_unref(GTK_OBJECT(font));

    pi->print_parts = g_list_append (pi->print_parts, pdata);
}

static void
print_header_val(PrintInfo * pi, gint x, float * y,
		 gint line_height, gchar * val)
{
    gchar *ptr, *eol;

    ptr = val;
    while (ptr) {
	eol = strchr(ptr, '\n');
	if (eol)
	    *eol = '\0';
	gnome_print_moveto(pi->pc, x, *y);
	gnome_print_show_with_charset(pi, ptr);
	ptr = eol;
	if (eol)
	    ptr++;
      	*y -= line_height;
    }
}

static void
print_header(PrintInfo * pi, gpointer * data)
{
    HeaderInfo *pdata = (HeaderInfo *)data;
    GnomeFont *font;
    gint i;

    g_return_if_fail(pdata->id_tag == BALSA_PRINT_TYPE_HEADER);

    font = gnome_font_new(BALSA_PRINT_HEAD_FONT, BALSA_PRINT_HEAD_SIZE);
    gnome_print_setfont(pi->pc, font);
    pi->ypos -= BALSA_PRINT_HEAD_SIZE;
    for (i = 0; pdata->headers[i]; i += 2) {
	gnome_print_moveto(pi->pc, pi->margin_left, pi->ypos);
	gnome_print_show_with_charset(pi, pdata->headers[i]);
	print_header_val(pi, pi->margin_left + pdata->header_label_width,
			 &pi->ypos, BALSA_PRINT_HEAD_SIZE, 
			 pdata->headers[i + 1]);
    }
    pi->ypos += BALSA_PRINT_HEAD_SIZE;
    gtk_object_unref(GTK_OBJECT(font));
    g_strfreev(pdata->headers);
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
    pi->ypos -= (BALSA_PRINT_HEAD_SIZE >> 1);
    if (pi->ypos < pi->margin_bottom) {
	pi->pages++;
	pi->ypos = pi->margin_bottom + pi->printable_height - 
	    (BALSA_PRINT_HEAD_SIZE >> 1);
    } else
	pi->ypos -= (BALSA_PRINT_HEAD_SIZE >> 1);

    pi->print_parts = g_list_append (pi->print_parts, pdata);
}

static void
print_separator(PrintInfo * pi, gpointer * data)
{
    SeparatorInfo *pdata = (SeparatorInfo *)data;

    g_return_if_fail(pdata->id_tag == BALSA_PRINT_TYPE_SEPARATOR);

    pi->ypos -= (BALSA_PRINT_HEAD_SIZE >> 1);
    if (pi->ypos < pi->margin_bottom)
	start_new_page(pi);
    gnome_print_setlinewidth(pi->pc, 0.5);
    gnome_print_newpath(pi->pc);
    gnome_print_moveto(pi->pc, pi->margin_left, pi->ypos);
    gnome_print_lineto(pi->pc, pi->printable_width + pi->margin_left, pi->ypos);
    gnome_print_stroke (pi->pc);
    pi->ypos -= (BALSA_PRINT_HEAD_SIZE >> 1);
}

/*
 * ~~~ stuff to print a plain text part ~~~
 */
typedef struct _PlainTextInfo {
    guint id_tag;
    gchar *textbuf;
    gint lines;
    gint maxlength;
} PlainTextInfo;

static void
prepare_plaintext(PrintInfo * pi, LibBalsaMessageBody * body)
{
    PlainTextInfo *pdata;
    GnomeFont *font;

    pdata = g_malloc(sizeof(PlainTextInfo));
    pdata->id_tag = BALSA_PRINT_TYPE_PLAINTEXT;

    /* copy the text body to a buffer */
    if (body->buffer)
	pdata->textbuf = g_strdup(body->buffer);
    else {
	FILE *part;

	pdata->textbuf = NULL;
	libbalsa_message_body_save_temporary(body, NULL);
	part = fopen(body->temp_filename, "r");
	if (part) {
	    libbalsa_readfile(part, &pdata->textbuf);
	    fclose(part);
	    }
    }
    
    /* wrap lines (if necessary) */
    font = gnome_font_new(BALSA_PRINT_BODY_FONT, BALSA_PRINT_BODY_SIZE);
    pdata->lines = print_wrap_string(pdata->textbuf, font, pi->printable_width);
    gtk_object_unref(GTK_OBJECT(font));

    /* calculate the y end position */
    if (pi->ypos - pdata->lines * BALSA_PRINT_BODY_SIZE < pi->margin_bottom) {
	int lines_left = pdata->lines;

	lines_left -= (pi->ypos - pi->margin_bottom) / BALSA_PRINT_BODY_SIZE;
	pi->pages++;
	while (lines_left * BALSA_PRINT_BODY_SIZE > pi->printable_height) {
	    lines_left -= pi->printable_height / BALSA_PRINT_BODY_SIZE;
	    pi->pages++;
	}
	pi->ypos = pi->margin_bottom + pi->printable_height -
	    lines_left * BALSA_PRINT_BODY_SIZE;
    } else
	pi->ypos -= pdata->lines * BALSA_PRINT_BODY_SIZE;

    pi->print_parts = g_list_append (pi->print_parts, pdata);
}

static void
print_plaintext(PrintInfo * pi, gpointer * data)
{
    PlainTextInfo *pdata = (PlainTextInfo *)data;
    gint i;
    GnomeFont *font;
    gchar *ptr;

    g_return_if_fail(pdata->id_tag == BALSA_PRINT_TYPE_PLAINTEXT);
    font = gnome_font_new(BALSA_PRINT_BODY_FONT, BALSA_PRINT_BODY_SIZE);
    gnome_print_setfont(pi->pc, font);
    ptr = pdata->textbuf;
    for(i = 1; i <= pdata->lines; i++) {
	pi->ypos -= BALSA_PRINT_BODY_SIZE;
	if (pi->ypos < pi->margin_bottom) {
	    start_new_page(pi);
	    gnome_print_setfont(pi->pc, font);
	}
	ptr = print_line(pi, ptr);
    }
    gtk_object_unref(GTK_OBJECT(font));
    g_free (pdata->textbuf);
}

/*
 * ~~~ default print method: print an icon plus a description ~~~
 */
typedef struct _DefaultInfo {
    guint id_tag;
    float label_width, image_width, image_height, text_height, part_height;
    gchar **labels;
#ifdef USE_PIXBUF
    GdkPixbuf *pixbuf;
#endif
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
	conttype = 
	    g_strdup(gnome_mime_type_or_default_of_file(body->filename, 
							"application/octet-stream"));

#ifdef USE_PIXBUF
    /* get a pixbuf according to the mime type */
    icon_name = libbalsa_icon_finder(conttype, NULL);
    pdata->pixbuf = gdk_pixbuf_new_from_file(icon_name);
    pdata->image_width = gdk_pixbuf_get_width (pdata->pixbuf);
    pdata->image_height = gdk_pixbuf_get_height (pdata->pixbuf);
    g_free(icon_name);
#else
    pdata->image_width = -10;  /* pts */
    pdata->image_height = 0;
#endif

    /* gather some info about this part */
    pdata->labels = g_new0(gchar *, 5); /* four fields, one terminator */
    pdata->labels[hdr++] = g_strdup(_("Type:"));
    pdata->labels[hdr++] = g_strdup(conttype);
    if (body->filename) {
	pdata->labels[hdr++] = g_strdup(_("Filename:"));
	pdata->labels[hdr++] = g_strdup(body->filename);
    }
    font = gnome_font_new(BALSA_PRINT_HEAD_FONT, BALSA_PRINT_HEAD_SIZE);
    pdata->label_width = gnome_font_get_width_string(font, pdata->labels[0]);
    if (pdata->labels[2] && 
	gnome_font_get_width_string(font, pdata->labels[2]) > pdata->label_width)
	pdata->label_width = gnome_font_get_width_string(font, pdata->labels[2]);
    pdata->label_width += 6;

    lines = print_wrap_string(pdata->labels[1], font,
			      pi->printable_width - pdata->label_width - 
			      pdata->image_width - 10);
    if (!lines)
	lines = 1;
    if (pdata->labels[3])
	lines += print_wrap_string(pdata->labels[3], font,
				   pi->printable_width - pdata->label_width - 
				   pdata->image_width - 10);
    pdata->text_height = lines * BALSA_PRINT_HEAD_SIZE;

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
#ifdef USE_PIXBUF
    double matrix[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
#endif
    DefaultInfo *pdata = (DefaultInfo *)data;
    GnomeFont *font;
    gint i, offset;

    g_return_if_fail(pdata->id_tag == BALSA_PRINT_TYPE_DEFAULT);

    if (pi->ypos - pdata->part_height < pi->margin_bottom)
	start_new_page(pi);

#ifdef USE_PIXBUF
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
#endif
    
    /* print the description */
    font = gnome_font_new(BALSA_PRINT_HEAD_FONT, BALSA_PRINT_HEAD_SIZE);
    gnome_print_setfont(pi->pc, font);
    pi->ypos -= (pdata->part_height - pdata->text_height) / 2.0 + 
	BALSA_PRINT_HEAD_SIZE;
    offset = pi->margin_left + pdata->image_width + 10;
    for (i = 0; pdata->labels[i]; i += 2) {
	gnome_print_moveto(pi->pc, offset, pi->ypos);
	gnome_print_show_with_charset(pi, pdata->labels[i]);
	print_header_val(pi, offset + pdata->label_width, &pi->ypos,
			 BALSA_PRINT_HEAD_SIZE, pdata->labels[i + 1]);
    }
    pi->ypos -= (pdata->part_height - pdata->text_height) / 2.0 -
	BALSA_PRINT_HEAD_SIZE;
    gtk_object_unref(GTK_OBJECT(font));
    g_strfreev(pdata->labels);
}

#ifdef USE_PIXBUF
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
#endif

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
#ifdef USE_PIXBUF
	{"image", prepare_image},
#endif
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
		 g_strncasecmp(action->mime_type, conttype, strlen(action->mime_type));
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
    PrintInfo *pi = g_new0(PrintInfo, 1);

    pi->paper = gnome_paper_with_name(paper);
    pi->master = gnome_print_master_new_from_dialog(dlg);
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

    /* this works because Courier is a fixed font... */
    font = gnome_font_new(BALSA_PRINT_BODY_FONT, BALSA_PRINT_BODY_SIZE);
    pi->chars_per_line =
	(gint) (pi->printable_width / gnome_font_get_width_string(font, "X"));
    gtk_object_unref(GTK_OBJECT(font));

    pi->tab_width = 8;
    pi->pages = 1;
    pi->ypos = pi->margin_bottom + pi->printable_height;

    pi->message = msg;
    prepare_header(pi, NULL);
    
    the_charset = (gchar *)libbalsa_message_charset(msg);
    if (the_charset)
	pi->conv_data = iconv_open("utf8", the_charset);
    else
	pi->conv_data = iconv_open("utf8", "iso-8859-1");

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
	balsa_information(LIBBALSA_INFORMATION_ERROR,
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

void
message_print(LibBalsaMessage * msg)
{
    GtkWidget *dialog;
    PrintInfo *pi;
    gboolean preview = FALSE;

    g_return_if_fail(msg);
    if (!is_font_ok(BALSA_PRINT_BODY_FONT) || 
	!is_font_ok(BALSA_PRINT_HEAD_FONT))
	return;

    dialog = gnome_print_dialog_new(_("Print message"),
				    GNOME_PRINT_DIALOG_COPIES);
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



