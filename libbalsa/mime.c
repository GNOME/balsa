/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
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

#include <string.h>
#include <ctype.h>

#include "libbalsa.h"
#include "misc.h"
#include "html.h"
#include <glib/gi18n.h>

/* FIXME: The content of this file could go to message.c */

static GString *process_mime_multipart(LibBalsaMessage * message,
                                       LibBalsaMessageBody * body,
				       gchar * reply_prefix_str,
				       gint llen, gboolean ignore_html,
                                       gboolean flow);

/* process_mime_part:
   returns string representation of given message part.
   NOTE: may return NULL(!).
*/

GString *
process_mime_part(LibBalsaMessage * message, LibBalsaMessageBody * body,
		  gchar * reply_prefix_str, gint llen, gboolean ignore_html,
                  gboolean flow)
{
    gchar *res = NULL;
    size_t allocated;
    GString *reply = NULL;
    gchar *mime_type;
    LibBalsaHTMLType html_type;

    switch (libbalsa_message_body_type(body)) {
    case LIBBALSA_MESSAGE_BODY_TYPE_OTHER:
    case LIBBALSA_MESSAGE_BODY_TYPE_AUDIO:
    case LIBBALSA_MESSAGE_BODY_TYPE_APPLICATION:
    case LIBBALSA_MESSAGE_BODY_TYPE_IMAGE:
    case LIBBALSA_MESSAGE_BODY_TYPE_MODEL:
    case LIBBALSA_MESSAGE_BODY_TYPE_MESSAGE:
    case LIBBALSA_MESSAGE_BODY_TYPE_VIDEO:
	break;
    case LIBBALSA_MESSAGE_BODY_TYPE_MULTIPART:
        reply = process_mime_multipart(message, body, reply_prefix_str,
                                       llen, ignore_html, flow);
	break;
    case LIBBALSA_MESSAGE_BODY_TYPE_TEXT:
	/* don't return text/html stuff... */
	mime_type = libbalsa_message_body_get_mime_type(body);
	html_type = libbalsa_html_type(mime_type);
	g_free(mime_type);

	if (ignore_html && html_type)
	    break;

	allocated = libbalsa_message_body_get_content(body, &res, NULL);
	if (!res)
	    return NULL;

#ifdef HAVE_GTKHTML
	if (html_type) {
	    allocated = libbalsa_html_filter(html_type, &res, allocated);
	    libbalsa_html_to_string(&res, allocated);
	}
#endif /* HAVE_GTKHTML */

	if (llen > 0) {
            if (flow && libbalsa_message_body_is_flowed(body)) {
                /* we're making a `format=flowed' message, and the
                 * message we're quoting was flowed
                 *
                 * we'll assume it's going to the screen */
		gboolean delsp = libbalsa_message_body_is_delsp(body);

		reply =
		    libbalsa_process_text_rfc2646(res, G_MAXINT, FALSE,
						  TRUE,
						  reply_prefix_str != NULL,
						  delsp);
                g_free(res);
                break;
            }
	    if (reply_prefix_str)
		llen -= strlen(reply_prefix_str);
	    libbalsa_wrap_string(res, llen);
	}
        if (reply_prefix_str || flow) {
	    gchar *str, *ptr;
	    /* prepend the prefix to all the lines */

	    reply = g_string_new("");
	    str = res;
	    do {
		ptr = strchr(str, '\n');
		if (ptr)
		    *ptr = '\0';
                if (reply_prefix_str)
		reply = g_string_append(reply, reply_prefix_str);
                if (flow) {
                    gchar *p;
                    /* we're making a `format=flowed' message, but the
                     * message we're quoting was `format=fixed', so we
                     * must make sure all lines are `fixed'--that is,
                     * trim any trailing ' ' characters */
                    for (p = str; *p; ++p);
                    while (*--p == ' ');
                    *++p = '\0';
                }
		reply = g_string_append(reply, str);
		reply = g_string_append_c(reply, '\n');
		str = ptr;
	    } while (str++);
	} else
	    reply = g_string_new(res);
	g_free(res);
	break;
    }
    return reply;
}

static GString *
process_mime_multipart(LibBalsaMessage * message,
                       LibBalsaMessageBody * body,
		       gchar * reply_prefix_str, gint llen,
		       gboolean ignore_html, gboolean flow)
{
    LibBalsaMessageBody *part;
    GString *res = NULL, *s;

    for (part = body->parts; part; part = part->next) {
        s = process_mime_part(message, part, reply_prefix_str, llen,
                              ignore_html, flow);
	if (!s)
	    continue;
	if (res) {
	    if (res->str[res->len - 1] != '\n')
		g_string_append_c(res, '\n');
	    g_string_append_c(res, '\n');
	    g_string_append(res, s->str);
	    g_string_free(s, TRUE);
	} else
	    res = s;
    }
    return res;
}

GString *
content2reply(LibBalsaMessageBody * root, gchar * reply_prefix_str,
	      gint llen, gboolean ignore_html, gboolean flow)
{
    LibBalsaMessage *message = root->message;
    LibBalsaMessageBody *body;
    GString *reply = NULL, *res;

    libbalsa_message_body_ref(message, FALSE, FALSE);
    for (body = root; body; body = body->next) {
	res = process_mime_part(message, body, reply_prefix_str, llen,
                                ignore_html, flow);
	if (!res)
	    continue;
	if (reply) {
	    reply = g_string_append(reply, res->str);
	    g_string_free(res, TRUE);
	} else
	    reply = res;
    }
    libbalsa_message_body_unref(message);

    return reply;
}

/*
 * implement RFC2646 `text=flowed' 
 * first version by Emmanuel <e allaud wanadoo fr>
 * this version by Peter <PeterBloomfield mindspring com>
 *
 * Note : the buffer pointed by par is not modified, the returned string
 * is newly allocated (so we can directly wrap from message body)
 *
 * Note about quoted material:
 * >>>text with no space separating the `>>>' and the `text...' is ugly,
 * so we'll space-stuff all quoted lines both for the screen and for the
 * wire (except the usenet signature separator `-- ', which wouldn't be
 * recognized if stuffed!). That means we must destuff *quoted* lines
 * coming off the screen, but we'll assume that leading spaces on
 * *unquoted* lines were put there intentionally by the user.
 * The stuffing space isn't logically part of the text of the paragraph,
 * so this doesn't change the content.
 * */
/* Updated 2003 to implement DelSp=Yes as in
 * http://www.ietf.org/internet-drafts/draft-gellens-format-bis-01.txt
 */
/* Now documented in RFC 3676:
 * http://www.ietf.org/rfc/rfc3676.txt
 * or
 * http://www.faqs.org/rfcs/rfc3676.html
 */

#define MAX_WIDTH	997	/* enshrined somewhere */
#define QUOTE_CHAR	'>'

/*
 * we'll use one routine to parse text into paragraphs
 *
 * if the text is coming off the wire, use the RFC specs
 * if it's coming off the screen, don't destuff unquoted lines
 * */

typedef struct {
    gint quote_depth;
    gchar *str;
} rfc2646text;

static GList *
unwrap_rfc2646(gchar * str, gboolean from_screen, gboolean delsp)
{
    GList *list = NULL;

    while (*str) {
        /* make a line of output */
        rfc2646text *text = g_new(rfc2646text, 1);
        GString *string = g_string_new(NULL);
        gboolean flowed;

        text->quote_depth = -1;

        for (flowed = TRUE; flowed && *str; ) {
            /* process a line of input */
            gboolean sig_sep;
            gchar *p;
            gint len;

            for (p = str; *p == QUOTE_CHAR; p++)
                /* nothing */;
            len = p - str;
            sig_sep = (p[0] == '-' && p[1] == '-' && p[2] == ' '
                       && (p[3] == '\n'
                           || (p[3] == '\r' && p[4] == '\n')));
            if (text->quote_depth < 0)
                text->quote_depth = len;
            else if (len != text->quote_depth || sig_sep)
                /* broken! a flowed line was followed by either a line
                 * with different quote depth, or a sig separator
                 *
                 * we'll break before updating str, so we pick up this
                 * line again on the next pass through this loop */
                break;

            if (*p == ' ' && (!from_screen || len > 0))
                /* space stuffed */
                p++;

            /* move str to the start of the next line, if any */
            for (str = p; *str && *str != '\n'; str++)
                /* nothing */;
            len = str - p;
            if (*str)
                str++;

            if(len>0 && p[len-1] == '\r')
                len--;  /* take care of '\r\n' line endings */
	    flowed = (len > 0 && p[len - 1] == ' ' && !sig_sep);
	    if (flowed && delsp)
		/* Don't include the last space. */
		--len;

            g_string_append_len(string, p, len);
        }
        text->str = g_string_free(string, FALSE);
        if (flowed) {
	    /* Broken: remove trailing spaces. */
            gchar *p = text->str;
            while (*p)
                p++;
            while (--p >= text->str && *p == ' ')
                /* nothing */ ;
            *++p = '\0';
        }
        list = g_list_append(list, text);
    }

    return list;
}

/*
 * we'll use one routine to wrap the paragraphs
 *
 * if the text is going to the wire, use the RFC specs
 * if it's going to the screen, don't space-stuff unquoted lines
 * */
static void
dowrap_rfc2646(GList * list, gint width, gboolean to_screen,
               gboolean quote, GString * result)
{
    const gint max_width = to_screen ? G_MAXINT : MAX_WIDTH - 4;

    /* outer loop over paragraphs */
    while (list) {
        rfc2646text *text = list->data;
        gchar *str;
        gint qd;

        str = text->str;
        qd = text->quote_depth;
        if (quote)
            qd++;
        /* one output line per middle loop */
        do {                    /* ... while (*str); */
            gboolean first_word = TRUE;
            gchar *start = str;
            gchar *line_break = start;
            gint len = qd;
            gint i;

            /* start of line: emit quote string */
            for (i = 0; i < qd; i++)
                g_string_append_c(result, QUOTE_CHAR);
            /* space-stuffing:
             * - for the wire, stuffing is required for lines beginning
             *   with ` ', `>', or `From '
             * - for the screen and for the wire, we'll use optional
             *   stuffing of quoted lines to provide a visual separation
             *   of quoting string and text
             * - ...but we mustn't stuff `-- ' */
            if (((!to_screen
                  && (*str == ' ' || *str == QUOTE_CHAR
                      || !strncmp(str, "From ", 5))) || len > 0)
                && strcmp(str, "-- ")) {
                g_string_append_c(result, ' ');
                ++len;
            }
            /* 
             * wrapping strategy:
             * break line into words, each with its trailing whitespace;
             * emit words while they don't break the width;
             *
             * first word (with its trailing whitespace) is allowed to
             * break the width
             *
             * one word per inner loop
             * */
            while (*str) {
                while (*str && !isspace((int)*str)
                       && (str - start) < max_width) {
                    len++;
                    str = g_utf8_next_char(str);
                }
                while ((str - start) < max_width && isspace((int)*str)) {
                    if (*str == '\t')
                        len += 8 - len % 8;
                    else
                        len++;
                    str = g_utf8_next_char(str);;
                }
                /*
                 * to avoid some unnecessary space-stuffing,
                 * we won't wrap at '>', ' ', or "From "
                 * (we already passed any spaces, so just check for '>'
                 * and "From ")
                 * */
                if ((str - start) < max_width && *str
                    && (*str == QUOTE_CHAR
                        || !strncmp(str, "From ", 5)))
                    continue;

                if (!*str || len > width || (str - start) >= max_width) {
                    /* allow an overlong first word, otherwise back up
                     * str */
                    if (len > width && !first_word)
                        str = line_break;
                    g_string_append_len(result, start, str - start);
                    break;
                }
                first_word = FALSE;
                line_break = str;
            }                   /* end of loop over words */
            /* 
             * make sure that a line ending in whitespace ends in an
             * actual ' ' 
             * */
            if (str > start && isspace((int)str[-1]) && str[-1] != ' ')
                g_string_append_c(result, ' ');
            if (*str) {         /* line separator */
                if (to_screen || str == start)
		    g_string_append_c(result, '\n');
		else		/* DelSP = Yes */
		    g_string_append(result, " \n");
	    }
        } while (*str);         /* end of loop over output lines */

        g_free(text->str);
        g_free(text);
        list = g_list_next(list);
        if (list)               /* paragraph separator */
            g_string_append_c(result, '\n');
    }                           /* end of paragraph */
}

/* GString *libbalsa_process_text_rfc2646:
   re-wrap given flowed string to required width. 
   Parameters:
   gchar * par:          string to be wrapped
   gint width:           maximum length of wrapped line
   gboolean from_screen: is par from the text input area of
                         a sendmsg-window?
   gboolean to_screen:   is the wrapped text going to be
                         displayed in the text area of a sendmsg-window 
                         or of a received message window?
   gboolean quote:       should the wrapped lines be prefixed 
                         with the RFC 2646 quote character '>'?
   gboolean delsp:	 was the message formatted with DelSp=Yes?
*/
GString *
libbalsa_process_text_rfc2646(gchar * par, gint width,
                              gboolean from_screen,
                              gboolean to_screen, gboolean quote,
			      gboolean delsp)
{
    gint len = strlen(par);
    GString *result = g_string_sized_new(len);
    GList *list;

    list = unwrap_rfc2646(par, from_screen, delsp);
    dowrap_rfc2646(list, width, to_screen, quote, result);
    g_list_free(list);

    return result;
}

/* libbalsa_wrap_rfc2646:
   wraps given string using soft breaks according to rfc2646
   convenience function, uses libbalsa_process_text_rfc2646 to do all
   the work, but returns a gchar * and g_free's the string passed in
*/
gchar *
libbalsa_wrap_rfc2646(gchar * par, gint width, gboolean from_screen,
                      gboolean to_screen, gboolean delsp)
{
    GString *result;

    result = libbalsa_process_text_rfc2646(par, width, from_screen,
                                           to_screen, FALSE, delsp);
    g_free(par);

    return g_string_free(result, FALSE);
}

/*
 * libbalsa_wrap_view(GtkTextView * view, gint length)
 *
 * Wrap the text in a GtkTextView to the given line length.
 */

/* Forward references: */
static GtkTextTag *get_quote_tag(GtkTextIter * iter);
static gint get_quote_depth(GtkTextIter * iter, gchar ** quote_string);
static gchar *get_line(GtkTextBuffer * buffer, GtkTextIter * iter);
static gboolean is_in_url(GtkTextIter * iter, gint offset,
                          GtkTextTag * url_tag);

void
libbalsa_wrap_view(GtkTextView * view, gint length)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(view);
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *url_tag = gtk_text_tag_table_lookup(table, "url");
    GtkTextTag *soft_tag = gtk_text_tag_table_lookup(table, "soft");
    GtkTextIter iter;
    PangoContext *context = gtk_widget_get_pango_context(GTK_WIDGET(view));
    PangoLanguage *language = pango_context_get_language(context);
    PangoLogAttr *log_attrs = NULL;

    gtk_text_buffer_get_start_iter(buffer, &iter);

    /* Loop over original lines. */
    while (!gtk_text_iter_is_end(&iter)) {
	GtkTextTag *quote_tag;
	gchar *quote_string;
	gint quote_len;

	quote_tag = get_quote_tag(&iter);
	quote_len = get_quote_depth(&iter, &quote_string);
	if (quote_string) {
	    if (quote_string[quote_len])
		quote_len++;
	    else {
		gchar *tmp = g_strconcat(quote_string, " ", NULL);
		g_free(quote_string);
		quote_string = tmp;
	    }
	}

	/* Loop over breaks within the original line. */
	while (!gtk_text_iter_ends_line(&iter)) {
	    gchar *line;
	    gint num_chars;
	    gint attrs_len;
	    gint offset;
	    gint len;
	    gint brk_offset = 0;
	    gunichar c = 0;
	    gboolean in_space = FALSE;

	    line = get_line(buffer, &iter);
	    num_chars = g_utf8_strlen(line, -1);
	    attrs_len = num_chars + 1;
	    log_attrs = g_renew(PangoLogAttr, log_attrs, attrs_len);
	    pango_get_log_attrs(line, -1, -1, language, log_attrs, attrs_len);
	    g_free(line);

	    for (len = offset = quote_len;
		 len < length && offset < num_chars; offset++) {
		gtk_text_iter_set_line_offset(&iter, offset);
		c = gtk_text_iter_get_char(&iter);
		if (c == '\t')
		    len = ((len + 8) / 8) * 8;
		else if (g_unichar_isprint(c))
		    len++;

		if (log_attrs[offset].is_line_break
		    && !is_in_url(&iter, offset, url_tag))
		    brk_offset = offset;
	    }

	    if (len < length)
		break;

	    in_space = g_unichar_isspace(c);
	    if (brk_offset > quote_len && !in_space)
		/* Break at the last break we saw. */
		gtk_text_iter_set_line_offset(&iter, brk_offset);
	    else {
                GtkTextIter start = iter;
		/* Break at the next line break. */
		if (offset <= quote_len)
		    offset = quote_len + 1;
		while (offset < num_chars
		       && (is_in_url(&iter, offset, url_tag)
			   || !log_attrs[offset].is_line_break))
		    offset++;

		if (offset >= num_chars)
		    /* No next line break. */
		    break;

                /* Trim extra trailing whitespace */
                gtk_text_iter_forward_char(&start);
                gtk_text_buffer_delete(buffer, &start, &iter);
	    }

	    gtk_text_buffer_insert_with_tags(buffer, &iter, "\n", 1,
					     soft_tag, NULL);
	    if (quote_string)
		gtk_text_buffer_insert_with_tags(buffer, &iter,
						 quote_string, -1,
						 quote_tag, NULL);
	}
	g_free(quote_string);
	gtk_text_iter_forward_line(&iter);
    }
    g_free(log_attrs);
}

/* Find the quote tag, if any, at iter; doesn't move iter; returns tag
 * or NULL. */
static GtkTextTag *
get_quote_tag(GtkTextIter * iter)
{
    GtkTextTag *quote_tag = NULL;
    GSList *list;
    GSList *tag_list = gtk_text_iter_get_tags(iter);

    for (list = tag_list; list; list = list->next) {
        GtkTextTag *tag = list->data;
        gchar *name;
        g_object_get(G_OBJECT(tag), "name", &name, NULL);
        if (name) {
            if (!strncmp(name, "quote-", 6))
                quote_tag = tag_list->data;
            g_free(name);
        }
    }
    g_slist_free(tag_list);

    return quote_tag;
}

/* Move the iter over a string of consecutive '>' characters, and an
 * optional ' ' character; returns the number of '>'s, and, if
 * quote_string is not NULL, stores an allocated copy of the string
 * (including the trailing ' ') at quote_string (or NULL, if there were
 * no '>'s). */
static gint
get_quote_depth(GtkTextIter * iter, gchar ** quote_string)
{
    gint quote_depth = 0;
    GtkTextIter start = *iter;

    while (gtk_text_iter_get_char(iter) == QUOTE_CHAR) {
        quote_depth++;
        gtk_text_iter_forward_char(iter);
    }
    if (quote_depth > 0 && gtk_text_iter_get_char(iter) == ' ')
        gtk_text_iter_forward_char(iter);

    if (quote_string) {
        if (quote_depth > 0)
            *quote_string = gtk_text_iter_get_text(&start, iter);
        else
            *quote_string = NULL;
    }

    return quote_depth;
}

/* Move the iter to the start of the line it's on; returns an allocated
 * copy of the line (utf-8, including invisible characters and image
 * markers). */
static gchar *
get_line(GtkTextBuffer * buffer, GtkTextIter * iter)
{
    GtkTextIter end;

    gtk_text_iter_set_line_offset(iter, 0);
    end = *iter;
    if (!gtk_text_iter_ends_line(&end))
        gtk_text_iter_forward_to_line_end(&end);

    return gtk_text_buffer_get_slice(buffer, iter, &end, TRUE);
}

/* Move the iter to position offset in the current line, and test
 * whether it's in an URL. */
static gboolean
is_in_url(GtkTextIter * iter, gint offset, GtkTextTag * url_tag)
{
    gtk_text_iter_set_line_offset(iter, offset);
    return url_tag ? (gtk_text_iter_has_tag(iter, url_tag)
                      && !gtk_text_iter_begins_tag(iter, url_tag)) : FALSE;
}

/* Remove soft newlines and associated quote strings from num_paras
 * paragraphs in the buffer, starting at the line before iter; if
 * num_paras < 0, process the whole buffer. */

/* Forward references: */
static gboolean prescanner(const gchar * p);
static void mark_urls(GtkTextBuffer * buffer, GtkTextIter * iter,
                      GtkTextTag * tag, const gchar * p);
#if GLIB_CHECK_VERSION(2, 14, 0)
static GRegex *get_url_reg(void);
#else                           /* GLIB_CHECK_VERSION(2, 14, 0) */
static regex_t *get_url_reg(void);
#endif                          /* GLIB_CHECK_VERSION(2, 14, 0) */

void
libbalsa_unwrap_buffer(GtkTextBuffer * buffer, GtkTextIter * iter,
		       gint num_paras)
{
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *soft_tag = gtk_text_tag_table_lookup(table, "soft");
    GtkTextTag *url_tag = gtk_text_tag_table_lookup(table, "url");

    /* Check whether the previous line flowed into this one. */
    gtk_text_iter_set_line_offset(iter, 0);
    if (gtk_text_iter_ends_tag(iter, soft_tag))
	gtk_text_iter_backward_line(iter);

    for (; num_paras; num_paras--) {
	gint quote_depth;
	gint qd;
	GtkTextIter start;
	gchar *line;

	gtk_text_iter_set_line_offset(iter, 0);
	quote_depth = get_quote_depth(iter, NULL);

	for (;;) {
	    gchar *quote_string;
	    gboolean stuffed;

	    /* Move to the end of the line, if not there already. */
	    if (!gtk_text_iter_ends_line(iter)
		&& !gtk_text_iter_forward_to_line_end(iter))
		return;
	    /* Save this iter as the start of a possible deletion. */
	    start = *iter;
	    /* Move to the start of the next line. */
	    if (!gtk_text_iter_forward_line(iter))
		return;

	    qd = get_quote_depth(iter, &quote_string);
	    stuffed = quote_string && quote_string[qd];
	    g_free(quote_string);

	    if (gtk_text_iter_has_tag(&start, soft_tag)) {
		if (qd != quote_depth) {
		    /* Broken: use quote-depth-wins. */
		    GtkTextIter end = start;
		    gtk_text_iter_forward_to_tag_toggle(&end, soft_tag);
		    gtk_text_buffer_remove_tag(buffer, soft_tag,
					       &start, &end);
		    break;
		}
	    } else
		/* Hard newline. */
		break;

	    if (qd > 0 && !stuffed) {
		/* User deleted the space following the '>' chars; we'll
		 * delete the space at the end of the previous line, if
		 * there was one. */
		gtk_text_iter_backward_char(&start);
		if (gtk_text_iter_get_char(&start) != ' ')
		    gtk_text_iter_forward_char(&start);
	    }
	    gtk_text_buffer_delete(buffer, &start, iter);
	}

	if (num_paras < 0) {
	    /* This is a wrap_body call, not a continuous wrap, so we'll
	     * remove spaces before a hard newline. */
	    GtkTextIter tmp_iter;

	    /* Make sure it's not a usenet sig separator: */
	    tmp_iter = start;
	    if (!(gtk_text_iter_backward_char(&tmp_iter)
		  && gtk_text_iter_get_char(&tmp_iter) == ' '
		  && gtk_text_iter_backward_char(&tmp_iter)
		  && gtk_text_iter_get_char(&tmp_iter) == '-'
		  && gtk_text_iter_backward_char(&tmp_iter)
		  && gtk_text_iter_get_char(&tmp_iter) == '-'
		  && gtk_text_iter_get_line_offset(&tmp_iter) == qd)) {
		*iter = start;
		while (gtk_text_iter_get_line_offset(&start) >
		       (qd ? qd + 1 : 0)) {
		    gtk_text_iter_backward_char(&start);
		    if (gtk_text_iter_get_char(&start) != ' ') {
			gtk_text_iter_forward_char(&start);
			break;
		    }
		}
		gtk_text_buffer_delete(buffer, &start, iter);
	    }
	    if (!gtk_text_iter_forward_line(iter))
		return;
	}

	line = get_line(buffer, &start);
	if (prescanner(line))
	    mark_urls(buffer, &start, url_tag, line);
	g_free(line);
    }
}

/* Mark URLs in one line of the buffer */
static void
mark_urls(GtkTextBuffer * buffer, GtkTextIter * iter, GtkTextTag * tag,
          const gchar * line)
{
    const gchar *p = line;
#if GLIB_CHECK_VERSION(2, 14, 0)
    GRegex *url_reg = get_url_reg();
    GMatchInfo *url_match;
    GtkTextIter start = *iter;
    GtkTextIter end = *iter;

    while (g_regex_match(url_reg, p, 0, &url_match)) {
        gint start_pos, end_pos;

        if (g_match_info_fetch_pos(url_match, 0, &start_pos, &end_pos)) {
            glong offset = g_utf8_pointer_to_offset(line, p + start_pos);
            gtk_text_iter_set_line_offset(&start, offset);
            offset = g_utf8_pointer_to_offset(line, p + end_pos);
            gtk_text_iter_set_line_offset(&end, offset);
            gtk_text_buffer_apply_tag(buffer, tag, &start, &end);

            p += end_pos;
        }
        g_match_info_free(url_match);
        if (!prescanner(p))
            break;
    }
#else                           /* GLIB_CHECK_VERSION(2, 14, 0) */
    regex_t *url_reg = get_url_reg();
    regmatch_t url_match;
    GtkTextIter start = *iter;
    GtkTextIter end = *iter;

    while (!regexec(url_reg, p, 1, &url_match, 0)) {
        glong offset = g_utf8_pointer_to_offset(line, p + url_match.rm_so);
        gtk_text_iter_set_line_offset(&start, offset);
        offset = g_utf8_pointer_to_offset(line, p + url_match.rm_eo);
        gtk_text_iter_set_line_offset(&end, offset);
        gtk_text_buffer_apply_tag(buffer, tag, &start, &end);

        p += url_match.rm_eo;
        if (!prescanner(p))
            break;
    }
#endif                          /* GLIB_CHECK_VERSION(2, 14, 0) */
}

/* Prepare the buffer for sending with DelSp=Yes. */
void
libbalsa_prepare_delsp(GtkTextBuffer * buffer)
{
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *soft_tag = gtk_text_tag_table_lookup(table, "soft");
    GtkTextIter iter;

    gtk_text_buffer_get_start_iter(buffer, &iter);
    while (gtk_text_iter_forward_to_line_end(&iter))
	if (gtk_text_iter_has_tag(&iter, soft_tag))
	    gtk_text_buffer_insert(buffer, &iter, " ", 1);
}

/*
 * End of wrap/unwrap view.
 */


/* libbalsa_insert_with_url:
 * do a gtk_text_buffer_insert, but mark URL's with balsa_app.url_color
 *
 * prescanner: 
 * used to find candidates for lines containing URL's.
 * Empirially, this approach is faster (by factor of 8) than scanning
 * entire message with regexec. YMMV.
 * s - is the line to scan. 
 * returns TRUE if the line may contain an URL.
 */
static gboolean
prescanner(const gchar * s)
{
    gint left = strlen(s) - 6;

    if (left <= 0)
        return FALSE;

    while (left--) {
        switch (tolower(*s++)) {
        case 'f':              /* ftp:/, ftps: */
            if (tolower(*s) == 't' &&
                tolower(*(s + 1)) == 'p' &&
                (*(s + 2) == ':' || tolower(*(s + 2)) == 's') &&
                (*(s + 3) == ':' || *(s + 3) == '/'))
                return TRUE;
            break;
        case 'h':              /* http:, https */
            if (tolower(*s) == 't' &&
                tolower(*(s + 1)) == 't' &&
                tolower(*(s + 2)) == 'p' &&
                (*(s + 3) == ':' || tolower(*(s + 3)) == 's'))
                return TRUE;
            break;
        case 'm':              /* mailt */
            if (tolower(*s) == 'a' &&
                tolower(*(s + 1)) == 'i' &&
                tolower(*(s + 2)) == 'l' && tolower(*(s + 3)) == 't')
                return TRUE;
            break;
        case 'n':              /* news:, nntp: */
            if ((tolower(*s) == 'e' || tolower(*s) == 'n') &&
                (tolower(*(s + 1)) == 'w' || tolower(*(s + 1)) == 't') &&
                (tolower(*(s + 2)) == 's' || tolower(*(s + 2)) == 'p') &&
                *(s + 3) == ':')
                return TRUE;
            break;
        }
    }

    return FALSE;
}

#if GLIB_CHECK_VERSION(2, 14, 0)
struct url_regex_info {
    GRegex *url_reg;
    const gchar *str;
    const gchar *func;
    const gchar *msg;
};

static GRegex *
get_url_helper(struct url_regex_info *info)
{
    if (!info->url_reg) {
        GError *err = NULL;

        info->url_reg = g_regex_new(info->str, G_REGEX_CASELESS, 0, &err);
        if (err) {
            g_warning("%s %s: %s", info->func, info->msg, err->message);
            g_error_free(err);
        }
    }

    return info->url_reg;
}

static GRegex *
get_url_reg(void)
{
    static struct url_regex_info info = {
        NULL,
        "(((https?|ftps?|nntp)://)|(mailto:|news:))"
            "(%[0-9A-F]{2}|[-_.!~*';/?:@&=+$,#[:alnum:]])+",
        __func__,
        "url regex compilation failed"
    };

    return get_url_helper(&info);
}

static GRegex *
get_ml_url_reg(void)
{
    static struct url_regex_info info = {
        NULL,
        "(%[0-9A-F]{2}|[-_.!~*';/?:@&=+$,#[:alnum:]]|[ \t]*[\r\n]+[ \t]*)+>",
        __func__,
        "multiline url regex compilation failed"
    };

    return get_url_helper(&info);
}

static GRegex *
get_ml_flowed_url_reg(void)
{
    static struct url_regex_info info = {
        NULL,
        "(%[0-9A-F]{2}|[-_.!~*';/?:@&=+$,#[:alnum:]]|[ \t]+)+>",
        __func__,
        "multiline url regex compilation failed"
    };

    return get_url_helper(&info);
}
#else                           /* GLIB_CHECK_VERSION(2, 14, 0) */
static regex_t *
get_url_reg(void)
{
    static regex_t *url_reg = NULL;

    if (!url_reg) {
        /* one-time compilation of a constant url_str expression */
        static const char url_str[] =
            "(((https?|ftps?|nntp)://)|(mailto:|news:))"
            "(%[0-9A-F]{2}|[-_.!~*';/?:@&=+$,#[:alnum:]])+";

        url_reg = g_new(regex_t, 1);
        if (regcomp(url_reg, url_str, REG_EXTENDED | REG_ICASE) != 0)
            g_warning("libbalsa_insert_with_url: "
                      "url regex compilation failed.");
    }

    return url_reg;
}

static regex_t *
get_ml_url_reg(void)
{
    static regex_t *url_reg = NULL;
    
    if (!url_reg) {
        /* one-time compilation of a constant url_str expression */
        static const char url_str[] =
	    "(%[0-9A-F]{2}|[-_.!~*';/?:@&=+$,#[:alnum:]]|[ \t]*[\r\n]+[ \t]*)+>";

	url_reg = g_new(regex_t, 1);
        if (regcomp(url_reg, url_str, REG_EXTENDED | REG_ICASE) != 0)
            g_warning("libbalsa_insert_with_url: "
                      "multiline url regex compilation failed.");
    }
    
    return url_reg;
}

static regex_t *
get_ml_flowed_url_reg(void)
{
    static regex_t *url_reg = NULL;
    
    if (!url_reg) {
        /* one-time compilation of a constant url_str expression */
        static const char url_str[] =
	    "(%[0-9A-F]{2}|[-_.!~*';/?:@&=+$,#[:alnum:]]|[ \t]+)+>";

	url_reg = g_new(regex_t, 1);
        if (regcomp(url_reg, url_str, REG_EXTENDED | REG_ICASE) != 0)
            g_warning("libbalsa_insert_with_url: "
                      "multiline url regex compilation failed.");
    }
    
    return url_reg;
}
#endif                          /* GLIB_CHECK_VERSION(2, 14, 0) */

gboolean
libbalsa_insert_with_url(GtkTextBuffer * buffer,
                         const char *chars,
			 const char *all_chars,
                         GtkTextTag * tag,
                         LibBalsaUrlInsertInfo *url_info)
{
    gchar *p, *buf;
    const gchar *all_p;
    GtkTextIter iter;

    buf = p = g_strdup(chars);
    all_p = all_chars;
    gtk_text_buffer_get_iter_at_mark(buffer, &iter,
                                     gtk_text_buffer_get_insert(buffer));

    /* if there shouldn't be a callback for URL's we don't need to detect
       them... */
    if (url_info) {
	GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
	GtkTextTag *url_tag = gtk_text_tag_table_lookup(table, "url");

	if (url_info->ml_url) {
	    gchar *url_end = strchr(p, '>');

	    if (url_end) {
		url_info->ml_url_buffer =
		    g_string_append_len(url_info->ml_url_buffer, p, url_end - p);
		gtk_text_buffer_insert_with_tags(buffer, &iter,
						 url_info->ml_url_buffer->str, -1,
						 url_tag, tag, NULL);
		if (url_info->callback)
		    url_info->callback(buffer, &iter, url_info->ml_url, url_info->callback_data);
		g_string_free(url_info->ml_url_buffer, TRUE);
		g_free(url_info->ml_url);
		url_info->ml_url_buffer = NULL;
		url_info->ml_url = NULL;
		p = url_end;
	    } else {
		url_info->ml_url_buffer =
		    g_string_append(url_info->ml_url_buffer, p);
		url_info->ml_url_buffer =
		    g_string_append_c(url_info->ml_url_buffer, '\n');
		return TRUE;
	    }
	}

	if (prescanner(p)) {
	    gint offset = 0;
#if GLIB_CHECK_VERSION(2, 14, 0)
            GRegex *url_reg = get_url_reg();
            GMatchInfo *url_match;
            gboolean match = g_regex_match(url_reg, p, 0, &url_match);
#else                           /* GLIB_CHECK_VERSION(2, 14, 0) */
	    regex_t *url_reg = get_url_reg();
	    regmatch_t url_match;
	    gboolean match = regexec(url_reg, p, 1, &url_match, 0) == 0;
#endif                          /* GLIB_CHECK_VERSION(2, 14, 0) */

	    while (match) {
		gchar *buf;
		gchar *spc;
                gint start_pos, end_pos;

#if GLIB_CHECK_VERSION(2, 14, 0)
                if (!g_match_info_fetch_pos(url_match, 0, &start_pos, &end_pos))
                    break;
#else                           /* GLIB_CHECK_VERSION(2, 14, 0) */
                start_pos = url_match.rm_so;
                end_pos   = url_match.rm_eo;
#endif                          /* GLIB_CHECK_VERSION(2, 14, 0) */

		if (start_pos > 0) {
		    /* check if we hit a multi-line URL... (see RFC 1738) */
		    if (all_p && (p[start_pos - 1] == '<' ||
				  (start_pos > 4 &&
				   !g_ascii_strncasecmp(p + start_pos - 5, "<URL:", 5)))) {
			/* if the input is flowed, we will see a space at
			 * url_match.rm_eo - in this case the complete remainder
			 * of the ml uri should be in the passed buffer... */
			if (url_info && url_info->buffer_is_flowed &&
			    *(p + end_pos) == ' ') {
#if GLIB_CHECK_VERSION(2, 14, 0)
                            GRegex *ml_flowed_url_reg = get_ml_flowed_url_reg();
                            GMatchInfo *ml_url_match;
                            gint ml_start_pos, ml_end_pos;

                            if (g_regex_match(ml_flowed_url_reg,
                                              all_chars + offset + end_pos,
                                              0, &ml_url_match)
                                && g_match_info_fetch_pos(ml_url_match, 0,
                                                          &ml_start_pos,
                                                          &ml_end_pos)
                                && ml_start_pos == 0)
                                end_pos += ml_end_pos - 1;
#else                           /* GLIB_CHECK_VERSION(2, 14, 0) */
			    regex_t *ml_flowed_url_reg = get_ml_flowed_url_reg();
			    regmatch_t ml_url_match;

			    if (!regexec(ml_flowed_url_reg,
					 all_chars + offset + end_pos, 1,
					 &ml_url_match, 0)
                                && ml_url_match.rm_so == 0)
				end_pos += ml_url_match.rm_eo - 1;
#endif                          /* GLIB_CHECK_VERSION(2, 14, 0) */
			} else if (!strchr(p + end_pos, '>')) {
                            gint ml_end_pos;
#if GLIB_CHECK_VERSION(2, 14, 0)
                            GRegex *ml_url_reg = get_ml_url_reg();
                            GMatchInfo *ml_url_match;

                            if (g_regex_match(ml_url_reg,
                                              all_chars + offset + end_pos,
                                              0, &ml_url_match)
                                && g_match_info_fetch_pos(ml_url_match, 0,
                                                          NULL,
                                                          &ml_end_pos)) {
				GString *ml_url = g_string_new("");
				const gchar *ml_p =
                                    all_chars + offset + start_pos;
				gint ml_cnt;

#else                           /* GLIB_CHECK_VERSION(2, 14, 0) */
			    regex_t *ml_url_reg = get_ml_url_reg();
			    regmatch_t ml_url_match;
		    
			    if (!regexec(ml_url_reg,
					 all_chars + offset + end_pos, 1,
					 &ml_url_match, 0)
                                && ml_url_match.rm_so == 0) {
				GString *ml_url = g_string_new("");
				const gchar *ml_p =
                                    all_chars + offset + start_pos;
				gint ml_cnt;

                                ml_end_pos = ml_url_match.rm_eo;
#endif                          /* GLIB_CHECK_VERSION(2, 14, 0) */
				ml_cnt = end_pos - start_pos + ml_end_pos - 1;
				for (; ml_cnt; (ml_p++, ml_cnt--))
				    if (*ml_p > ' ')
					ml_url = g_string_append_c(ml_url, *ml_p);
				url_info->ml_url = ml_url->str;
				g_string_free(ml_url, FALSE);
			    }
			}
		    }

		    buf = g_strndup(p, start_pos);
		    gtk_text_buffer_insert_with_tags(buffer, &iter,
						     buf, -1, tag, NULL);
		    g_free(buf);
		}

		if (url_info->ml_url) {
		    url_info->ml_url_buffer = g_string_new(p + start_pos);
		    url_info->ml_url_buffer =
			g_string_append_c(url_info->ml_url_buffer, '\n');
		    return TRUE;
		}

		/* add the url - it /may/ contain spaces if the text is flowed */
		buf = g_strndup(p + start_pos, end_pos - start_pos);
		if ((spc = strchr(buf, ' '))) {
		    GString *uri_real = g_string_new("");
		    gchar * p = buf;

		    while (spc) {
			*spc = '\n';
			g_string_append_len(uri_real, p, spc - p);
			p = spc + 1;
			spc = strchr(p, ' ');
		    }
		    g_string_append(uri_real, p);
		    gtk_text_buffer_insert_with_tags(buffer, &iter, buf, -1,
						     url_tag, tag, NULL);
		    if (url_info->callback)
			url_info->callback(buffer, &iter, uri_real->str, url_info->callback_data);
		    g_string_free(uri_real, TRUE);
		} else {
		    gtk_text_buffer_insert_with_tags(buffer, &iter, buf, -1,
						     url_tag, tag, NULL);

		    /* remember the URL and its position within the text */
		    if (url_info->callback)
			url_info->callback(buffer, &iter, buf, url_info->callback_data);
		}
		g_free(buf);

		p += end_pos;
		offset += end_pos;
		if (prescanner(p))
#if GLIB_CHECK_VERSION(2, 14, 0)
                    match = g_regex_match(url_reg, p, 0, &url_match);
#else                           /* GLIB_CHECK_VERSION(2, 14, 0) */
		    match = regexec(url_reg, p, 1, &url_match, 0) == 0;
#endif                          /* GLIB_CHECK_VERSION(2, 14, 0) */
		else
		    match = FALSE;
	    }
	}
    }

    if (*p)
        gtk_text_buffer_insert_with_tags(buffer, &iter, p, -1, tag, NULL);
    g_free(buf);

    return FALSE;
}

void
#if GLIB_CHECK_VERSION(2, 14, 0)
libbalsa_unwrap_selection(GtkTextBuffer * buffer, GRegex * rex)
#else                           /* GLIB_CHECK_VERSION(2, 14, 0) */
libbalsa_unwrap_selection(GtkTextBuffer * buffer, regex_t * rex)
#endif                          /* GLIB_CHECK_VERSION(2, 14, 0) */
{
    GtkTextIter start, end;
    gchar *line;
    guint quote_depth;
    guint index;
    GtkTextMark *selection_end;
    gboolean ins_quote;

    gtk_text_buffer_get_selection_bounds(buffer, &start, &end);
    gtk_text_iter_order(&start, &end);
    selection_end = gtk_text_buffer_create_mark(buffer, NULL, &end, FALSE);

    /* Find quote depth and index of first non-quoted character. */
    line = get_line(buffer, &start);
    if (libbalsa_match_regex(line, rex, &quote_depth, &index)) {
	/* skip one regular space following the quote characters */
	if (line[index] == ' ')
	    index++;
	/* Replace quote string with standard form. */
	end = start;
	gtk_text_iter_set_line_index(&end, index);
	gtk_text_buffer_delete(buffer, &start, &end);
	do
	    gtk_text_buffer_insert(buffer, &start, ">", 1);
	while (--quote_depth);
	gtk_text_buffer_insert(buffer, &start, " ", 1);
    }
    g_free(line);

    /* Unwrap remaining lines. */
    ins_quote = FALSE;
    while (gtk_text_iter_ends_line(&start)
	   || gtk_text_iter_forward_to_line_end(&start)) {
	gtk_text_buffer_get_iter_at_mark(buffer, &end, selection_end);
	if (gtk_text_iter_compare(&start, &end) >= 0)
	    break;
	end = start;
	if (!gtk_text_iter_forward_line(&end))
	    break;
	line = get_line(buffer, &end);
	if (libbalsa_match_regex(line, rex, &quote_depth, &index) &&
	    line[index] == ' ')
	    index++;
	gtk_text_iter_set_line_index(&end, index);
	gtk_text_buffer_delete(buffer, &start, &end);
	/* empty lines separate paragraphs */
	if (line[index] == '\0') {
	    gtk_text_buffer_insert(buffer, &start, "\n", 1);
	    while (quote_depth--)
		gtk_text_buffer_insert(buffer, &start, ">", 1);
	    gtk_text_buffer_insert(buffer, &start, "\n", 1);
	    ins_quote = TRUE;
	} else if (ins_quote) {
	    while (quote_depth--)
		gtk_text_buffer_insert(buffer, &start, ">", 1);
	    gtk_text_buffer_insert(buffer, &start, " ", 1);
	    ins_quote = FALSE;
	}
	g_free(line);
	/* Insert a space, if the line didn't end with one. */
	if (!gtk_text_iter_starts_line(&start)) {
	    gtk_text_iter_backward_char(&start);
	    if (gtk_text_iter_get_char(&start) != ' ') {
		start = end;
		gtk_text_buffer_insert(buffer, &start, " ", 1);
	    }
	}
    }
}

#if GLIB_CHECK_VERSION(2, 14, 0)
gboolean
libbalsa_match_regex(const gchar * line, GRegex * rex, guint * count,
                     guint * index)
{
    GMatchInfo *rm;
    gint c;
    const gchar *p;
    gint end_pos;

    c = 0;
    for (p = line;
         g_regex_match(rex, p, 0, &rm)
         && g_match_info_fetch_pos(rm, 0, NULL, &end_pos)
         && end_pos > 0;
         p += end_pos) {
        c++;
        g_match_info_free(rm);
    }
    g_match_info_free(rm);

    if (count)
        *count = c;
    if (index)
        *index = p - line;
    return c > 0;
}
#else                           /* GLIB_CHECK_VERSION(2, 14, 0) */
gboolean
libbalsa_match_regex(const gchar * line, regex_t * rex, guint * count,
		     guint * index)
{
    regmatch_t rm;
    gint c;
    const gchar *p;

    c = 0;
    for (p = line; regexec(rex, p, 1, &rm, 0) == 0; p += rm.rm_eo)
	c++;
    if (count)
	*count = c;
    if (index)
	*index = p - line;
    return c > 0;
}
#endif                          /* GLIB_CHECK_VERSION(2, 14, 0) */
