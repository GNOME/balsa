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

#include <gnome.h>
#include <config.h>
#include <balsa-app.h>

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


typedef struct _PrintInfo {
    /* gnome print info */
    const GnomePaper *paper;
    GnomePrintMaster *master;
    GnomePrintContext *pc;

    /* font info */
    gchar *font_name;
    gint font_size;
    float font_char_width;
    float font_char_height;

    /* page info */
    gint pages;
    float page_width, page_height;
    float margin_top, margin_bottom, margin_left, margin_right;
    float printable_width, printable_height;
    float header_height, header_label_width;
    gint total_lines;
    gint lines_per_page, chars_per_line;

    /* wrapping */
    gint tab_width;

    /* balsa data */
    LibBalsaMessage *message;
    gchar *buff, *pointer, *line_buf;
    gchar **headers;		/* can be released with g_strfreev() */

} PrintInfo;


static guint
linecount(const gchar * str)
{
    gint cnt = 1;
    if (!str)
	return 0;
    while (*str) {
	if (*str == '\n' && str[1])
	    cnt++;
	str++;
    }
    return cnt;
}

static int
print_wrap_string(gchar * str, GnomeFont * font, gint width)
{
    gchar *ptr = str, *last_space = NULL;
    int lines = 1, line_width;
    g_return_val_if_fail(str, 0);

    line_width = 0;
    g_strchomp(str);
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
    return lines;
}

/* prepare_page_header:
   helper function to fill in PrintInfo structure. Finds out which headers
   are available, adds them to the list, wraps, and computes the total
   header height. The wrapping is as good as one can get, for variable width
   fonts, too.
*/
static void
prepare_page_header(PrintInfo * pi)
{
    const int MAX_HDRS = 4;	/* max number of printed headers */
    int hdr = 0, i, width, lines;
    GnomeFont *font;

    pi->headers = g_new0(gchar *, (MAX_HDRS + 1) * 2);

    if (pi->message->from) {
	pi->headers[hdr++] = g_strdup(_("From:"));
	pi->headers[hdr++] = libbalsa_address_to_gchar(pi->message->from, 0);
    }

    if (pi->message->to_list) {
	pi->headers[hdr++] = g_strdup(_("To:"));
	pi->headers[hdr++] =
	    libbalsa_make_string_from_list(pi->message->to_list);
    }
    if (pi->message->subject) {
	pi->headers[hdr++] = g_strdup(_("Subject:"));
	pi->headers[hdr++] = g_strdup(pi->message->subject);
    }
    pi->headers[hdr++] = g_strdup(_("Date:"));
    pi->headers[hdr++] = libbalsa_message_date_to_gchar(pi->message,
							balsa_app.date_string);

    pi->header_label_width = 0;
    font = gnome_font_new(BALSA_PRINT_HEAD_FONT, BALSA_PRINT_HEAD_SIZE);
    for (i = 0; i < hdr; i += 2) {
	width = gnome_font_get_width_string(font, pi->headers[i]);
	if (width > pi->header_label_width)
	    pi->header_label_width = width;
    }
    pi->header_label_width += 6;	/* pts */

    lines = 0;
    for (i = 1; i < hdr; i += 2)
	lines += print_wrap_string(pi->headers[i], font,
				   pi->printable_width -
				   pi->header_label_width);

    pi->header_height = lines * BALSA_PRINT_HEAD_SIZE;
    gtk_object_unref(GTK_OBJECT(font));
}

static PrintInfo *
print_info_new(const gchar * paper, LibBalsaMessage * msg,
	       GnomePrintDialog * dlg)
{
    PrintInfo *pi = g_new0(PrintInfo, 1);
    pi->paper = gnome_paper_with_name(paper);
    pi->master = gnome_print_master_new_from_dialog(dlg);
    pi->pc = gnome_print_master_get_context(pi->master);

    pi->font_name = g_strdup(BALSA_PRINT_BODY_FONT);
    pi->font_size = 10;
    pi->font_char_width = 0.0808 * 72;
    pi->font_char_height = 0.1400 * 72;

    pi->page_width = gnome_paper_pswidth(pi->paper);
    pi->page_height = gnome_paper_psheight(pi->paper);

    pi->margin_top = 0.75 * 72;	/* printer's margins, not page margin */
    pi->margin_bottom = 0.75 * 72;	/* get it from gnome-print            */
    pi->margin_left = 0.75 * 72;
    pi->margin_right = 0.75 * 72;
    pi->printable_width =
	pi->page_width - pi->margin_left - pi->margin_right;
    pi->printable_height =
	pi->page_height - pi->margin_top - pi->margin_bottom;

    pi->chars_per_line =
	(gint) (pi->printable_width / pi->font_char_width);
    pi->tab_width = 8;

    pi->message = msg;
    prepare_page_header(pi);
    pi->buff = libbalsa_message_get_text_content(msg, pi->chars_per_line);
    pi->line_buf = g_malloc(pi->chars_per_line + 1);

    pi->total_lines = linecount(pi->buff);
    pi->lines_per_page =
	(gint) ((pi->printable_height - pi->header_height) /
		pi->font_char_height);
    pi->pages = ((pi->total_lines - 1) / pi->lines_per_page) + 1;

    return pi;
}


static void
print_info_destroy(PrintInfo * pi)
{
    /* ... */
    g_strfreev(pi->headers);
    pi->headers = NULL;
    g_free(pi->line_buf);
    pi->line_buf = NULL;
    g_free(pi->buff);
    pi->buff = NULL;
    g_free(pi->font_name);
    pi->font_name = NULL;
    if (pi->pc) {
	gtk_object_unref(GTK_OBJECT(pi->pc));
	pi->pc = NULL;
    }
    gtk_object_unref(GTK_OBJECT(pi->master));
    pi->master = NULL;
    g_free(pi);
}

static void print_message(PrintInfo * pi);
static void print_start_job(PrintInfo * pi);
static void print_line(PrintInfo * pi, int line_on_page);
static void print_header(PrintInfo * pi, guint page);

static gboolean
is_font_ok(const gchar * font_name)
{
    GnomeFont *test_font = gnome_font_new(font_name, 10);

    if (!test_font) {
	balsa_information(LIBBALSA_INFORMATION_WARNING,
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

    if (!is_font_ok(BALSA_PRINT_HEAD_FONT)
	|| !is_font_ok(BALSA_PRINT_BODY_FONT))
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
    pi = print_info_new("A4", msg, GNOME_PRINT_DIALOG(dialog));
    gnome_dialog_close(GNOME_DIALOG(dialog));

    /* do the Real Job */
    print_message(pi);

    if (preview) {
	GnomePrintMasterPreview *preview_widget =
	    gnome_print_master_preview_new(pi->master,
					   _("Balsa: message print preview"));
	gtk_widget_show(GTK_WIDGET(preview_widget));
    } else
	gnome_print_master_print(pi->master);

    print_info_destroy(pi);
}

/* print_message:
   prints given message
*/
static void
print_message(PrintInfo * pi)
{
    guint current_page, current_line;
    char buf[20];
    GnomeFont *font;
    if (balsa_app.debug)
	g_print("Printing.\n");

    g_return_if_fail(pi->buff);
    font = gnome_font_new(BALSA_PRINT_BODY_FONT, pi->font_size);
    print_start_job(pi);

    for (current_page = 1; current_page <= pi->pages; current_page++) {
	snprintf(buf, sizeof(buf), "%d", current_page);
	if (balsa_app.debug)
	    g_print("Processing page %s\n", buf);
	gnome_print_beginpage(pi->pc, buf);
	print_header(pi, current_page);
	gnome_print_setfont(pi->pc, font);

	for (current_line = 0; current_line < pi->lines_per_page;
	     current_line++) {
	    if (!pi->pointer)	/*EOF reached */
		break;
	    print_line(pi, current_line);
	}
	gnome_print_showpage(pi->pc);	/* print_end_page(pi); */
    }

    /* print_end_job(pi); */
    gnome_print_context_close(pi->pc);
    pi->pc = NULL;
    gnome_print_master_close(pi->master);
    gtk_object_unref(GTK_OBJECT(font));
}

static void
print_start_job(PrintInfo * pi)
{
    pi->pointer = pi->buff;
}

/* print_line:
   prepares the line, replaces tabs with spaces and prints.
   Trusts that libbalsa_wrap_strig did its job (which it might not for
   very long lines without spaces).
*/
static void
print_line(PrintInfo * pi, int line_on_page)
{
    int pos = 0, i;
    while (*pi->pointer && *pi->pointer != '\n') {
	if (pos < pi->chars_per_line) {
	    switch (*pi->pointer) {
	    case '\t':
		for (i = 0;
		     pos + i < pi->chars_per_line && i < pi->tab_width;
		     i++) pi->line_buf[pos++] = ' ';
		break;
	    default:
		pi->line_buf[pos++] = *pi->pointer;
	    }
	}
	pi->pointer++;
    }

    if (*pi->pointer)
	pi->pointer++;		/* skip EOL character     */
    pi->line_buf[pos] = '\0';	/* make sure line has EOS */

    gnome_print_moveto(pi->pc, pi->margin_left,
		       pi->page_height - pi->margin_top - pi->header_height
		       - (pi->font_char_height * line_on_page));
    gnome_print_show(pi->pc, pi->line_buf);
}

static void
print_header_val(GnomePrintContext * pc, gint x, gint * y,
		 gint line_height, gchar * val)
{
    gchar *ptr, *eol;

    ptr = val;
    while (ptr) {
	eol = strchr(ptr, '\n');
	if (eol)
	    *eol = '\0';
	gnome_print_moveto(pc, x, *y);
	gnome_print_show(pc, ptr);
	ptr = eol;
	if (eol) {
	    *eol = '\n';
	    ptr++;
	}
	*y -= line_height;
    }
}

static void
print_header(PrintInfo * pi, guint page)
{
    GnomeFont *font;
    gchar *page_no = g_strdup_printf(_("Page: %i/%i"), page, pi->pages);
    int width, ypos, i;

    ypos = pi->page_height - pi->margin_top;
    font = gnome_font_new(BALSA_PRINT_HEAD_FONT, BALSA_PRINT_HEAD_SIZE);
    gnome_print_setfont(pi->pc, font);
    width = gnome_font_get_width_string(font, page_no);
    gnome_print_moveto(pi->pc, pi->page_width - pi->margin_left - width,
		       ypos);
    gnome_print_show(pi->pc, page_no);
    g_free(page_no);

    for (i = 0; pi->headers[i]; i += 2) {
	gnome_print_moveto(pi->pc, pi->margin_left, ypos);
	gnome_print_show(pi->pc, pi->headers[i]);
	print_header_val(pi->pc, pi->margin_left + pi->header_label_width,
			 &ypos, BALSA_PRINT_HEAD_SIZE, pi->headers[i + 1]);
    }

    gtk_object_unref(GTK_OBJECT(font));
}

#endif




