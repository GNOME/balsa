/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include <string.h>
#include <ctype.h>
#include <fribidi.h>

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
#ifdef HAVE_HTML_WIDGET
    size_t allocated;
#endif /* HAVE_HTML_WIDGET */
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

	if (ignore_html && (html_type != LIBBALSA_HTML_TYPE_NONE))
	    break;

#ifdef HAVE_HTML_WIDGET
	allocated = libbalsa_message_body_get_content(body, &res, NULL);
	if (!res)
	    return NULL;

	if (html_type) {
	    allocated = libbalsa_html_filter(html_type, &res, allocated);
	    libbalsa_html_to_string(&res, allocated);
	}
#else  /* HAVE_HTML_WIDGET */
	libbalsa_message_body_get_content(body, &res, NULL);
	if (!res)
	    return NULL;
#endif /* HAVE_HTML_WIDGET */

        if (flow && libbalsa_message_body_is_flowed(body)) {
            /* we're making a `format=flowed' message, and the
             * message we're quoting was flowed
             *
             * we'll assume it's going to the screen */
            gboolean delsp = libbalsa_message_body_is_delsp(body);

            reply =
                libbalsa_process_text_rfc2646(res, G_MAXINT, FALSE, TRUE,
                                              reply_prefix_str != NULL,
                                              delsp);
            g_free(res);
            break;
        }

        if (llen > 0) {
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

    libbalsa_message_body_ref(message, FALSE);
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
 * https://www.ietf.org/internet-drafts/draft-gellens-format-bis-01.txt
 */
/* Now documented in RFC 3676:
 * https://www.ietf.org/rfc/rfc3676.txt
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

/* Remove soft newlines and associated quote strings from num_paras
 * paragraphs in the buffer, starting at the line before iter; if
 * num_paras < 0, process the whole buffer. */

/* Forward references: */
static gboolean prescanner(const gchar * p, guint len);
static void mark_urls(GtkTextBuffer * buffer, GtkTextIter * iter,
                      GtkTextTag * tag, const gchar * p);
static GRegex *get_url_reg(void);

void
libbalsa_unwrap_buffer(GtkTextBuffer * buffer, GtkTextIter * iter,
		       gint num_paras)
{
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *url_tag = gtk_text_tag_table_lookup(table, "url");

    /* Check whether the previous line flowed into this one. */
    gtk_text_iter_set_line_offset(iter, 0);

    for (; num_paras; num_paras--) {
	gint quote_depth;
	GtkTextIter start;
	gchar *line;

	gtk_text_iter_set_line_offset(iter, 0);
	quote_depth = get_quote_depth(iter, NULL);

	/* Move to the end of the line, if not there already. */
	if (!gtk_text_iter_ends_line(iter)
	    && !gtk_text_iter_forward_to_line_end(iter))
	    return;
	/* Save this iter as the start of a possible deletion. */
	start = *iter;
	/* Move to the start of the next line. */
	if (!gtk_text_iter_forward_line(iter))
	    return;

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
		  && gtk_text_iter_get_line_offset(&tmp_iter) ==
		  quote_depth)) {
		*iter = start;
		while (gtk_text_iter_get_line_offset(&start) >
		       (quote_depth ? quote_depth + 1 : 0)) {
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
	if (prescanner(line, strlen(line)))
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
    const gchar * const line_end = line + strlen(line);
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
        if (!prescanner(p, line_end - p))
            break;
        g_match_info_free(url_match);
    }
    g_match_info_free(url_match);
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
prescanner(const gchar * s, guint len)
{
    gint left = len - 5;

    while (--left > 0) {
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
            "(%[0-9A-F]{2}|[-_.!~*';/?:@&=+$,#[:alnum:]])+"
            /* do not include a trailing period or comma as part of the match;
             * it is more likely to be punctuation than part of a URL */
            "(%[0-9A-F]{2}|[-_!~*';/?:@&=+$#[:alnum:]])",
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
        "("
        "%[0-9A-F]{2}|[-_.!~*';/?:@&=+$,#[:alnum:]]|[ \t]*[\r\n]+[ \t>]*"
        ")+"
        "(%[0-9A-F]{2}|[-_.!~*';/?:@&=+$,#[:alnum:]])>",
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

gboolean
libbalsa_insert_with_url(GtkTextBuffer * buffer,
                         const char *chars,
                         guint len,
                         GtkTextTag * tag,
                         LibBalsaUrlInsertInfo *url_info)
{
    GtkTextIter iter;
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *url_tag = gtk_text_tag_table_lookup(table, "url");
    gboolean match;
    gint start_pos, end_pos;
    GRegex *url_reg;
    GMatchInfo *url_match;
    const gchar * const line_end = chars + len;

    gtk_text_buffer_get_iter_at_mark(buffer, &iter,
                                     gtk_text_buffer_get_insert(buffer));

    if (url_info->ml_url_buffer) {
        const gchar *url_end;
        gchar *url, *q, *r;

        if (!(url_end = strchr(chars, '>')) || url_end >= line_end) {
            g_string_append_len(url_info->ml_url_buffer, chars,
                                line_end - chars);
            g_string_append_c(url_info->ml_url_buffer, '\n');
            return TRUE;
        }

        g_string_append_len(url_info->ml_url_buffer, chars,
                            url_end - chars);
        gtk_text_buffer_insert_with_tags(buffer, &iter,
                                         url_info->ml_url_buffer->str,
                                         url_info->ml_url_buffer->len,
                                         url_tag, tag, NULL);
        q = url = g_new(gchar, url_info->ml_url_buffer->len);
        for (r = url_info->ml_url_buffer->str; *r; r++)
            if (*r > ' ')
                *q++ = *r;
        url_info->callback(buffer, &iter, url, q - url,
                           url_info->callback_data);
        g_free(url);
        g_string_free(url_info->ml_url_buffer, TRUE);
        url_info->ml_url_buffer = NULL;
        chars = url_end;
    }

    if (!prescanner(chars, line_end - chars)) {
        gtk_text_buffer_insert_with_tags(buffer, &iter, chars,
                                         line_end - chars, tag, NULL);
        return FALSE;
    }

    url_reg = get_url_reg();
    match = g_regex_match(url_reg, chars, 0, &url_match)
        && g_match_info_fetch_pos(url_match, 0, &start_pos, &end_pos)
        && chars + start_pos < line_end;
    g_match_info_free(url_match);

    while (match) {
        gchar *spc;

        gtk_text_buffer_insert_with_tags(buffer, &iter, chars,
                                         start_pos, tag, NULL);

        /* check if we hit a multi-line URL... (see RFC 1738) */
        if ((start_pos > 0 && (chars[start_pos - 1] == '<')) ||
            (start_pos > 4 &&
             !g_ascii_strncasecmp(chars + start_pos - 5, "<URL:", 5))) {
            GMatchInfo *ml_url_match;
            gint ml_start_pos, ml_end_pos;

            /* if the input is flowed, we may see a space at
             * url_match.rm_eo - in this case the complete remainder
             * of the ml uri should be in the passed buffer... */
            if (url_info->buffer_is_flowed && chars[end_pos] == ' ') {
                if (g_regex_match(get_ml_flowed_url_reg(), chars + end_pos,
                                  0, &ml_url_match)
                    && g_match_info_fetch_pos(ml_url_match, 0,
                                              &ml_start_pos, &ml_end_pos)
                    && ml_start_pos == 0)
                    end_pos += ml_end_pos - 1;
                g_match_info_free(ml_url_match);
            } else if (chars[end_pos] != '>') {
                if (g_regex_match(get_ml_url_reg(), chars + end_pos,
                                  0, &ml_url_match)
                    && g_match_info_fetch_pos(ml_url_match, 0,
                                              &ml_start_pos, NULL)
                    && ml_start_pos == 0) {
                    chars += start_pos;
                    url_info->ml_url_buffer =
                        g_string_new_len(chars, line_end - chars);
                    g_string_append_c(url_info->ml_url_buffer, '\n');
                }
                g_match_info_free(ml_url_match);
                if (url_info->ml_url_buffer)
                    return TRUE;
            }
        }

        /* add the url - it /may/ contain spaces if the text is flowed */
        if ((spc = strchr(chars + start_pos, ' ')) && spc < chars + end_pos) {
            GString *uri_real = g_string_new("");
            gchar *q, *buf;

            q = buf = g_strndup(chars + start_pos, end_pos - start_pos);
            spc = buf + (spc - (chars + start_pos));
            do {
                *spc = '\n';
                g_string_append_len(uri_real, q, spc - q);
                q = spc + 1;
            } while ((spc = strchr(q, ' ')));
            g_string_append(uri_real, q);
            gtk_text_buffer_insert_with_tags(buffer, &iter, buf, -1,
                                             url_tag, tag, NULL);
            g_free(buf);
            url_info->callback(buffer, &iter,
                               uri_real->str, uri_real->len,
                               url_info->callback_data);
            g_string_free(uri_real, TRUE);
        } else {
            gtk_text_buffer_insert_with_tags(buffer, &iter,
                                             chars + start_pos,
                                             end_pos - start_pos,
                                             url_tag, tag, NULL);

            /* remember the URL and its position within the text */
            url_info->callback(buffer, &iter, chars + start_pos,
                               end_pos - start_pos,
                               url_info->callback_data);
        }

        chars += end_pos;
        if (prescanner(chars, line_end - chars)) {
            match = g_regex_match(url_reg, chars, 0, &url_match)
                && g_match_info_fetch_pos(url_match, 0, &start_pos,
                                          &end_pos)
                && chars + start_pos < line_end;
            g_match_info_free(url_match);
        } else {
            match = FALSE;
        }
    }

    gtk_text_buffer_insert_with_tags(buffer, &iter, chars,
                                     line_end - chars, tag, NULL);

    return FALSE;
}

void
libbalsa_unwrap_selection(GtkTextBuffer * buffer, GRegex * rex)
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


GString *
libbalsa_html_encode_hyperlinks(GString * paragraph)
{
    GString * retval;
    gchar * p;
    GRegex *url_reg = get_url_reg();
    GMatchInfo *url_match;
    gboolean match;
    gchar * markup;

    /* check for any url */
    if (!prescanner(paragraph->str, paragraph->len)) {
        markup = g_markup_escape_text(paragraph->str, -1);
        g_string_assign(paragraph, markup);
        g_free(markup);
        return paragraph;
    }

    /* got some url's... */
    retval = g_string_new("");
    p = paragraph->str;

    match = g_regex_match(url_reg, p, 0, &url_match);

    while (match) {
        gint start_pos, end_pos;

        if (!g_match_info_fetch_pos(url_match, 0, &start_pos, &end_pos))
            break;

        /* add the url to the result */
        if (start_pos > 0) {
            markup = g_markup_escape_text(p, start_pos);
            retval = g_string_append(retval, markup);
            g_free(markup);
        }
        retval = g_string_append(retval, "<a href=\"");
        retval = g_string_append_len(retval, p + start_pos, end_pos - start_pos);
        retval = g_string_append(retval, "\">");
        retval = g_string_append_len(retval, p + start_pos, end_pos - start_pos);
        retval = g_string_append(retval, "</a>");

        /* find next (if any) */
        p += end_pos;
        if (prescanner(p, paragraph->len - (p - paragraph->str))) {
            g_match_info_free(url_match);
            match = g_regex_match(url_reg, p, 0, &url_match);
        } else
            match = FALSE;
    }
    g_match_info_free(url_match);

    /* copy remainder */
    if (*p != '\0') {
        markup = g_markup_escape_text(p, -1);
        retval = g_string_append(retval, markup);
        g_free(markup);
    }

    /* done - free original, return new */
    g_string_free(paragraph, TRUE);
    return retval;
}


gchar *
libbalsa_text_to_html(const gchar * title, const gchar * body, const gchar * lang)
{
    GString * html_body =
        g_string_new("<!DOCTYPE HTML>\n");
    gchar * html_subject;
    const gchar * start = body;
    gchar * html_lang;

    /* set the html header, including the primary language and the title */
    if (lang) {
        gchar * p;

        html_lang = g_strdup(lang);
        if ((p = strchr(html_lang, '_')))
            *p = '-';
    } else
        html_lang = g_strdup("x-unknown");
    html_subject = g_markup_escape_text(title, -1);
    g_string_append_printf(html_body, 
                           "<html lang=\"%s\">\n<head>\n"
                           "<title>%s</title>\n"
                           "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">\n"
                           "<style>\n"
                           "  p { margin-top: 0px; margin-bottom: 0px; }\n"
                           "</style>\n</head>\n"
                           "<body>\n", html_lang, html_subject);
    g_free(html_subject);
    g_free(html_lang);

    /* add the lines of the message body */
    while (*start) {
        const gchar * eol = strchr(start, '\n');
        const gchar * p = start;
        gboolean is_rtl = FALSE;
        GString * html;
        gsize idx;

        if (!eol)
            eol = start + strlen(start);

        /* find the first real char to determine the paragraph direction */
        /* Use the same logic as fribidi_get_par_direction(), but
         * without allocating memory for all the gunichars and
         * FriBidiCharTypes: */
        while (p < eol) {
            FriBidiCharType char_type;

            char_type = fribidi_get_bidi_type(g_utf8_get_char(p));

            if (FRIBIDI_IS_LETTER(char_type)) {
                is_rtl = FRIBIDI_IS_RTL(char_type);
                break;
            }

            p = g_utf8_next_char(p);
        }

        /* html escape the line */
        html = g_string_new_len(start, eol - start);

        /* encode hyperlinks */
        html = libbalsa_html_encode_hyperlinks(html);

        /* replace a series of n spaces by (n - 1) &nbsp; and one space */
        idx = 0;
        while (idx < html->len) {
            if (html->str[idx] == ' ' && (idx == 0 || html->str[idx + 1] == ' ')) {
                html->str[idx++] = '&';
                html = g_string_insert(html, idx, "nbsp;");
                idx += 5;
            } else
                idx = g_utf8_next_char(html->str + idx) - html->str;
        }

        /* append the paragraph, always stating the proper direction */
        g_string_append_printf(html_body, "<p dir=\"%s\">%s</p>\n",
                               is_rtl ? "rtl" : "ltr",
                               *html->str ? html->str : "&nbsp;");
        g_string_free(html, TRUE);

        /* next line */
        start = eol;
        if (*start)
            start++;
    }

    /* close the html context */
    html_body = g_string_append(html_body, "</body>\n</html>\n");

    /* return the utf-8 encoded text/html */
    return g_string_free(html_body, FALSE);
}

/*
 * libbalsa_wrap_quoted string
 * Wraps the string, prefixing wrapped lines with any quote string
 * Uses the same wrapping strategy as libbalsa_wrap_string()
 * Returns a newly allocated string--deallocate with g_free() when done
*/
char *
libbalsa_wrap_quoted_string(const char *str,
                            unsigned    width,
                            GRegex     *quote_regex)
{
    char **lines;
    char **line;
    GString *wrapped;
    PangoLogAttr *log_attrs = NULL;

    g_return_val_if_fail(str != NULL, NULL);
    g_return_val_if_fail(quote_regex != NULL, NULL);

    lines = g_strsplit(str, "\n", -1);
    wrapped = g_string_new(NULL);

    for (line = lines; *line != NULL; line++) {
        unsigned quote_len, quote_len_utf8;
        const char *start_ptr, *break_ptr, *ptr;
        const unsigned minl = width / 2;
        unsigned ptr_offset, start_offset, break_offset;
        int num_chars;
        int attrs_len;
        unsigned cursor;

        num_chars = g_utf8_strlen(*line, -1);
        attrs_len = num_chars + 1;
        log_attrs = g_renew(PangoLogAttr, log_attrs, attrs_len);
        pango_get_log_attrs(*line, -1, -1, pango_language_get_default(), log_attrs, attrs_len);

        libbalsa_match_regex(*line, quote_regex, NULL, &quote_len);

        g_string_append_len(wrapped, *line, quote_len);
        ptr = *line + quote_len;

        ptr_offset = g_utf8_pointer_to_offset(*line, ptr);
        cursor = quote_len_utf8 = ptr_offset;

        start_ptr = break_ptr = ptr;
        start_offset = break_offset = ptr_offset;

        while (*ptr != '\0') {
            gunichar c = g_utf8_get_char(ptr);

            if (c == '\t')
                cursor += 8 - cursor % 8;
            else
                cursor++;

            if (log_attrs[ptr_offset].is_line_break) {
                break_ptr = ptr;
                break_offset = ptr_offset;
            }

            if (cursor >= width && break_offset >= start_offset + minl && !g_unichar_isspace(c)) {
                const char *end_ptr, *test_ptr;
                gunichar test_char;

                /* Back up over whitespace */
                test_ptr = break_ptr;
                do {
                    end_ptr = test_ptr;
                    test_ptr = g_utf8_prev_char(test_ptr);
                    test_char = g_utf8_get_char(test_ptr);
                } while (test_ptr > start_ptr && g_unichar_isspace(test_char));

                g_string_append_len(wrapped, start_ptr, end_ptr - start_ptr);
                g_string_append_c(wrapped, '\n');
                g_string_append_len(wrapped, *line, quote_len);

                start_ptr = break_ptr;
                start_offset = break_offset;
                cursor = quote_len_utf8 + ptr_offset - start_offset;
            }
            ptr = g_utf8_next_char(ptr);
            ptr_offset++;
        }

        g_string_append(wrapped, start_ptr);
        g_string_append_c(wrapped, '\n');
    }

    g_free(log_attrs);
    g_strfreev(lines);

    return (char *) g_string_free(wrapped, FALSE);
}
