/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2003 Stuart Parmenter and others,
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

#include <sys/utsname.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>

#include <libgnomevfs/gnome-vfs-file-info.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnome/gnome-i18n.h>

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "misc.h"

static const gchar *libbalsa_get_codeset_name(const gchar *txt, 
					      LibBalsaCodeset Codeset);


/* libbalsa_lookup_mime_type:
   find out mime type of a file. Must work for both relative and absolute
   paths.
*/
gchar*
libbalsa_lookup_mime_type(const gchar * path)
{
    GnomeVFSFileInfo* vi = gnome_vfs_file_info_new();
    gchar* uri, *mime_type;

    if(g_path_is_absolute(path))
        uri = g_strconcat("file://", path, NULL);
    else {
        gchar* curr_dir = g_get_current_dir();
        uri = g_strconcat("file://", curr_dir, "/", path, NULL);
        g_free(curr_dir);
    }
    gnome_vfs_get_file_info (uri, vi,
                             GNOME_VFS_FILE_INFO_GET_MIME_TYPE
                             | GNOME_VFS_FILE_INFO_FOLLOW_LINKS);
    g_free(uri);
    mime_type = g_strdup(gnome_vfs_file_info_get_mime_type(vi));
    gnome_vfs_file_info_unref(vi);
    return mime_type;
}

gchar *
libbalsa_get_hostname(void)
{
    struct utsname utsname;
    gchar *p;
    uname(&utsname);

    /* Some systems return Fqdn rather than just the hostname */
    if ((p = strchr (utsname.nodename, '.')))
	*p = 0;

    return g_strdup (utsname.nodename);
}

gchar *
libbalsa_get_domainname(void)
{
    char domainname[SYS_NMLN];
    struct utsname utsname;
    gchar *d;
 
    uname(&utsname);
    d = strchr( utsname.nodename, '.' );
    if(d) {
        return g_strdup( d+1 );
    }
 
    if ( getdnsdomainname(domainname, SYS_NMLN) == 0 ) {
	return g_strdup(domainname);
    }
    return NULL;
}

/* libbalsa_urlencode: 
 * Taken from PHP's urlencode()
 */
gchar*
libbalsa_urlencode(const gchar* str)
{
    static const unsigned char hexchars[] = "0123456789ABCDEF";
    gchar *retval = NULL;
    gchar *x = NULL;
    
    g_return_val_if_fail(str != NULL, NULL);
    
    retval = malloc(strlen(str) * 3 + 1);
    
    for (x = retval; *str != '\0'; str++, x++) {
       *x = *str;
       if (*x == ' ') {
           *x = '+';
       } else if (!isalnum(*x) && strchr("_-.", *x) == NULL) {
           /* Allow only alnum chars and '_', '-', '.'; escape the rest */
           *x++ = '%';
           *x++ = hexchars[*str >> 4];
           *x = hexchars[*str & 0x0F];
       }
    }
    
    *x = '\0';
    return retval;
}


/* FIXME: Move to address.c and change name to
 *   libbalsa_address_list_to_string or something */
gchar *
libbalsa_make_string_from_list(const GList * the_list)
{
    return libbalsa_make_string_from_list_p(the_list);
}
/* private to libbalsa: */
gchar *
libbalsa_make_string_from_list_p(const GList * the_list)
{
    const GList *list;
    GString *gs = g_string_new(NULL);

    for (list = the_list; list; list = list->next) {
        LibBalsaAddress *addy = list->data;
        gchar *str = libbalsa_address_to_gchar_p(addy, 0);

        if (str) {
            if (gs->len > 0)
                g_string_append(gs, ", ");
            g_string_append(gs, str);
            g_free(str);
        }
    }

    return g_string_free(gs, FALSE);
}


/* readfile allocates enough space for the ending '\0' characeter as well.
   returns the number of read characters.
*/
size_t libbalsa_readfile(FILE * fp, char **buf)
{
    size_t size;
    off_t offset;
    int r;
    int fd;
    struct stat statbuf;

    *buf = NULL;
    if (!fp)
	return 0;

    fd = fileno(fp);
    if (fstat(fd, &statbuf) == -1)
	return -1;

    size = statbuf.st_size;

    if (!size) {
	*buf = NULL;
	return size;
    }

    lseek(fd, 0, SEEK_SET);

    *buf = (char *) g_malloc(size + 1);
    if (*buf == NULL)
	return -1;

    offset = 0;
    while ((size_t)offset < size) {
	r = read(fd, *buf + offset, size - offset);
	if (r == 0) { /* proper EOF */
            (*buf)[offset] = '\0';
	    return offset;
        }
	if (r > 0) {
	    offset += r;
	} else if ((errno != EAGAIN) && (errno != EINTR)) {
	    perror("Error reading file:");
            (*buf)[offset] = '\0';
	    return -1;
	}
    }
    (*buf)[size] = '\0';

    return size;
}

/* readfile_nostat is identical to readfile except it reads to EOF.
   This enables the use of pipes to programs for such things as
   the signature file.
*/
size_t libbalsa_readfile_nostat(FILE * fp, char **buf)
{
    size_t size;
    GString *gstr;
    char rbuf[512];
    size_t rlen = 512;
    int r;
    int fd;

    *buf = NULL;
    fd = fileno(fp);
    if (!fp)
	return 0;

    r = read(fd, rbuf, rlen-1);
    if (r <= 0)
	return 0;

    rbuf[r] = '\0';
    gstr = g_string_new(rbuf);

    do {
	if ((r = read(fd, rbuf, rlen-1)) != 0) {
	    /* chbm: if your sig is larger than 512 you deserve this */
	    if ((r < 0) && (errno != EAGAIN) && (errno != EINTR)) {
		perror("Error reading file:");
		g_string_free(gstr, TRUE);
		return -1;
	    };
	    if (r > 0) {
		rbuf[r] = '\0';	
		g_string_append(gstr,rbuf);
	    }
	}
    } while( r != 0 );

    size = gstr->len;
    *buf = g_string_free(gstr, FALSE);

    return size;
}

/* libbalsa_find_word:
   searches given word delimited by blanks or string boundaries in given
   string. IS NOT case-sensitive.
   Returns TRUE if the word is found.
*/
gboolean libbalsa_find_word(const gchar * word, const gchar * str)
{
    const gchar *ptr = str;
    int len = strlen(word);

    while (*ptr) {
	if (g_ascii_strncasecmp(word, ptr, len) == 0)
	    return TRUE;
	/* skip one word */
	while (*ptr && !isspace((int)*ptr))
	    ptr++;
	while (*ptr && isspace((int) *ptr))
	    ptr++;
    }
    return FALSE;
}

/* libbalsa_wrap_string
   wraps given string replacing spaces with '\n'.  do changes in place.
   lnbeg - line beginning position, sppos - space position, 
   te - tab's extra space.
*/
void
libbalsa_wrap_string(gchar * str, int width)
{
    const int minl = width / 2;

    gchar *space_pos, *ptr;
    gint te = 0;

    gint ptr_offset, line_begin_offset, space_pos_offset;

    g_return_if_fail(str != NULL);


    line_begin_offset = ptr_offset = space_pos_offset = 0;
    space_pos = ptr = str;

    while (*ptr) {
	switch (*ptr) {
	case '\t':
	    te += 7;
	    break;
	case '\n':
	    line_begin_offset = ptr_offset + 1;
	    te = 0;
	    break;
	case ' ':
	    space_pos = ptr;
	    space_pos_offset = ptr_offset;
	    break;
	}
	if (ptr_offset - line_begin_offset >= width - te 
	     && space_pos_offset >= line_begin_offset + minl) {
	    *space_pos = '\n';
	    line_begin_offset = space_pos_offset + 1;
	    te = 0;
	}
	ptr=g_utf8_next_char(ptr);
	ptr_offset++;
    }
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
unwrap_rfc2646(gchar * str, gboolean from_screen)
{
    GList *list = NULL;

    while (*str) {
        /* make a line of output */
        rfc2646text *text = g_new(rfc2646text, 1);
        GString *string = g_string_new(NULL);
        gboolean chomp = TRUE;

        text->quote_depth = -1;

        while (*str) {
            /* process a line of input */
            gboolean sig_sep;
            gchar *p;
            gint len;

            for (p = str; *p == QUOTE_CHAR; p++)
                /* nothing */;
            len = p - str;
            sig_sep = (strncmp(p, "-- \n", 4) == 0);
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

            g_string_append_len(string, p, len);
            if (len == 0 || p[--len] != ' ' || sig_sep) {
                chomp = FALSE;
                break;
            }
        }
        text->str = g_string_free(string, FALSE);
        if (chomp) {
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
            if (*str)           /* line separator */
                g_string_append_c(result, '\n');
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
*/
GString *
libbalsa_process_text_rfc2646(gchar * par, gint width,
                              gboolean from_screen,
                              gboolean to_screen, gboolean quote)
{
    gint len = strlen(par);
    GString *result = g_string_sized_new(len);
    GList *list;

    list = unwrap_rfc2646(par, from_screen);
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
                      gboolean to_screen)
{
    GString *result;

    result = libbalsa_process_text_rfc2646(par, width, from_screen,
                                           to_screen, FALSE);
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
static void prepare_log_attrs(PangoLogAttr * log_attrs, gint attrs_len,
                              GtkWidget * widget, gchar * line);
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
    PangoLogAttr *log_attrs = NULL;

    gtk_text_buffer_get_start_iter(buffer, &iter);

    /* Loop over original lines. */
    while (!gtk_text_iter_is_end(&iter)) {
        GtkTextTag *quote_tag;
        gchar *quote_string;
        gint quote_len;

        quote_tag = get_quote_tag(&iter);
        quote_len = get_quote_depth(&iter, &quote_string);
        if (quote_string && quote_string[quote_len])
            quote_len++;

        /* Loop over breaks within the original line. */
        while (!gtk_text_iter_ends_line(&iter)) {
            gchar *line;
            gint num_chars;
            gint attrs_len;
            gint offset;
            gint len;
            gint brk_offset = 0;
            gunichar c = 0;

            line = get_line(buffer, &iter);
            num_chars = g_utf8_strlen(line, -1);
            attrs_len = num_chars + 1;
            log_attrs = g_renew(PangoLogAttr, log_attrs, attrs_len);
            prepare_log_attrs(log_attrs, attrs_len, GTK_WIDGET(view),
                              line);
            g_free(line);

            for (len = offset = quote_len;
                 len < length && offset < num_chars; offset++) {
                gtk_text_iter_set_line_offset(&iter, offset);
                c = gtk_text_iter_get_char(&iter);
                if (c == '\t')
                    len = ((len + 8)/8)*8;
                else if (g_unichar_isprint(c))
                    len++;

                if (log_attrs[offset].is_line_break
                    && !is_in_url(&iter, offset, url_tag))
                    brk_offset = offset;
            }

            if (len < length)
                break;

            if (brk_offset > quote_len && !g_unichar_isspace(c))
                /* Break at the last break we saw. */
                gtk_text_iter_set_line_offset(&iter, brk_offset);
            else {
                /* Break at the next line break. */
                if (offset <= quote_len)
                    offset = quote_len + 1;
                while (offset < num_chars
                       && !(log_attrs[offset].is_line_break
                            && !is_in_url(&iter, offset, url_tag)))
                    offset++;
                if (offset >= num_chars)
                    /* No next line break. */
                    break;
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

    for (list = tag_list; list; list = g_slist_next(list)) {
        GtkTextTag *tag = list->data;
        gchar *name;
        g_object_get(G_OBJECT(tag), "name", &name, NULL);
        if (!strncmp(name, "quote-", 6))
            quote_tag = tag_list->data;
        g_free(name);
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

static void
prepare_log_attrs(PangoLogAttr * log_attrs, gint attrs_len,
                  GtkWidget * widget, gchar * line)
{
    /* Use pango_break to find possible breaks. First we need a
     * PangoAnalysis, which is provided by pango_itemize.
     * However, to do that we need a PangoAttrList, which
     * presumably should come from the GtkTextView widget; I
     * can't see how to do that, so we'll create an empty list
     * and use that. */
    PangoContext *context;
    PangoAttrList *attrs;
    GList *item_list, *l;
    gint num_chars = 0;

    context = gtk_widget_get_pango_context(widget);
    attrs = pango_attr_list_new();
    item_list = pango_itemize(context, line, 0, strlen(line), attrs, NULL);
    pango_attr_list_unref(attrs);

    for (l = item_list; l; l = g_list_next(l)) {
        PangoItem *pango_item = l->data;
        gchar *p, *q;

        pango_break(&line[pango_item->offset],
                    pango_item->length, &pango_item->analysis,
                    &log_attrs[num_chars], attrs_len - num_chars);

        /* We'll assume that it's OK to break at the start of a new
         * item, if it's preceded by a space. */
        q = &line[pango_item->offset];
        p = g_utf8_find_prev_char(line, q);
        if (p && g_unichar_isspace(g_utf8_get_char(p))
            && !g_unichar_isspace(g_utf8_get_char(q)))
            log_attrs[num_chars].is_line_break = TRUE;
        num_chars += pango_item->num_chars;
    }

    g_list_foreach(item_list, (GFunc) pango_item_free, NULL);
    g_list_free(item_list);
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
static regex_t *get_url_reg(void);

void
libbalsa_unwrap_buffer(GtkTextBuffer * buffer, GtkTextIter * iter,
                       gint num_paras)
{
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *soft_tag = gtk_text_tag_table_lookup(table, "soft");
    GtkTextTag *url_tag = gtk_text_tag_table_lookup(table, "url");
    gboolean soft;

    /* Check whether the previous line flowed into this one. */
    gtk_text_iter_set_line_offset(iter, 0);
    soft = gtk_text_iter_ends_tag(iter, soft_tag);

    if (gtk_text_iter_backward_line(iter) && !soft
        && !gtk_text_iter_ends_line(iter)) {
        /* Remove spaces preceding a hard newline. */
        GtkTextIter end;

        gtk_text_iter_forward_to_line_end(iter);
        end = *iter;
        while (!gtk_text_iter_starts_line(iter)) {
            gtk_text_iter_backward_char(iter);
            if (gtk_text_iter_get_char(iter) != ' ') {
                gtk_text_iter_forward_char(iter);
                break;
            }
        }
        gtk_text_buffer_delete(buffer, iter, &end);
        gtk_text_iter_forward_line(iter);
    }

    for (; num_paras; num_paras--) {
        gint quote_depth;
        GtkTextIter start;
        gchar *line;

        gtk_text_iter_set_line_offset(iter, 0);
        quote_depth = get_quote_depth(iter, NULL);

        for (;;) {
            gint qd;
            gchar *quote_string;

            if (!gtk_text_iter_forward_to_line_end(iter))
                return;
            start = *iter;
            if (!gtk_text_iter_forward_line(iter))
                return;

            qd = get_quote_depth(iter, &quote_string);
            if (!gtk_text_iter_has_tag(&start, soft_tag)
                || qd != quote_depth)
                break;

            if (qd > 0 && quote_string[qd] != ' ') {
                /* User deleted the space following the '>' chars; we'll
                 * delete the space at the end of the previous line, if
                 * there was one. */
                gtk_text_iter_backward_char(&start);
                if (gtk_text_iter_get_char(&start) != ' ')
                    gtk_text_iter_forward_char(&start);
            }
            gtk_text_buffer_delete(buffer, &start, iter);
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
}

/*
 * End of wrap/unwrap view.
 */

/* Delete the contents of a directory (not the directory itself).
   Return TRUE if everything was OK.
   If FALSE is returned then errno will be set to some useful value.
*/
gboolean
libbalsa_delete_directory_contents(const gchar *path)
{
    struct stat sb;
    DIR *d;
    struct dirent *de;
    gchar *new_path;

    d = opendir(path);
    g_return_val_if_fail(d, FALSE);

    for (de = readdir(d); de; de = readdir(d)) {
	if (strcmp(de->d_name, ".") == 0 ||
	    strcmp(de->d_name, "..") == 0)
	    continue;
	new_path = g_strdup_printf("%s/%s", path, de->d_name);

	stat(new_path, &sb);
	if (S_ISDIR(sb.st_mode)) {
	    if (!libbalsa_delete_directory_contents(new_path) ||
		rmdir(new_path) == -1) {
		g_free(new_path);
		closedir(d);
		return FALSE;
	    }
	} else {
	    if (unlink( new_path ) == -1) {
		g_free(new_path);
		closedir(d);
		return FALSE;
	    }
	}
	g_free(new_path);
	new_path = 0;
    }

    closedir(d);
    return TRUE;
}

/* libbalsa_truncate_string
 *
 * Arguments:
 *   str    the string to be truncated;
 *   length truncation length;
 *   dots   the number of trailing dots to be used to indicate
 *          truncation.
 *
 * Value:
 *   pointer to a newly allocated string; free with `g_free' when it's
 *   no longer needed.
 */
gchar *
libbalsa_truncate_string(const gchar *str, gint length, gint dots)
{
    gchar *res;
    gchar *p;

    if (str == NULL)
        return NULL;

    if (length <= 0 || strlen(str) <= (guint) length)
        return g_strdup(str);

    res = g_strndup(str, length);

    p = res + length - dots;
    while (--dots >= 0)
        *p++ = '.';

    return res;
}

/* libbalsa_expand_path:
   do different kind of filename expansions, e.g. ~ -> $HOME, etc.
*/
gchar*
libbalsa_expand_path(const gchar * path)
{
    char buf[_POSIX_PATH_MAX];
    char* res;
    strcpy(buf, path);
    libbalsa_lock_mutt();
    res = mutt_expand_path(buf, sizeof(buf));
    libbalsa_unlock_mutt();
    return g_strdup(res);
}

/* libbalsa_contract_path:
   do a reverse transformation.
*/
void
libbalsa_contract_path(gchar *path)
{
    libbalsa_lock_mutt();
    mutt_pretty_mailbox(path);
    libbalsa_unlock_mutt();
}

/* libbalsa_utf8_sanitize
 *
 * Validate utf-8 text, and if validation fails, replace each offending
 * byte with either '?' or assume a reasonable codeset for conversion.
 *
 * Arguments:
 *   text       The text to be sanitized; NULL is OK.
 *   fallback   if TRUE and *text is not clean, convert using codeset
 *   codeset    the codeset to use for fallback conversion
 *   target     if not NULL filled with the name of the used codeset or NULL
 *              or error/"?" conversion
 *
 * Return value:
 *   TRUE if *text was clean and FALSE otherwise
 *
 * NOTE:    The text is either modified in place or replaced and freed.
 */
gboolean
libbalsa_utf8_sanitize(gchar **text, gboolean fallback, LibBalsaCodeset codeset,
		       gchar const **target)
{
    if (target)
	*target = NULL;
    if (!*text)
        return TRUE;

    if (g_utf8_validate(*text, -1, NULL))
	return TRUE;
    if (!fallback) {
	gchar *p = *text;
	while (!g_utf8_validate(p, -1, (const gchar **) &p))
	    *p = '?';
    } else {
	/* */
	gint b_written;
	GError *conv_error = NULL;
	const gchar *use_enc = libbalsa_get_codeset_name(*text, codeset);
	gchar *p = g_convert(*text, strlen(*text), "utf-8", use_enc, NULL,
			     &b_written, &conv_error);

	if (p) {
	    g_free(*text);
	    *text = p;
	    if (target)
		*target = use_enc;
	} else {
	    gchar *p = *text;
	    while (!g_utf8_validate(p, -1, (const gchar **) &p))
		*p = '?';
	    g_warning("conversion %s -> utf8 failed: %s", use_enc,
		      conv_error->message);
	    g_error_free(conv_error);
	}
    }
    return FALSE;
}

static gchar *std_codesets[LIBBALSA_NUM_CODESETS] = 
     { "iso-8859-1", "iso-8859-2", "iso-8859-3", "iso-8859-4", "iso-8859-5", 
       "iso-8859-6", "iso-8859-7", "iso-8859-8", "iso-8859-9", "iso-8859-10",
       "iso-8859-11", "iso-8859-13", "iso-8859-14", "iso-8859-15", "koi-8r",
       "koi-8u", "euc-jp", "euc-kr" };

static gchar *win_codesets[LIBBALSA_NUM_CODESETS] =
    { "windows-1252", "windows-1250", NULL, NULL, "windows-1251", 
      "windows-1256", "windows-1253", "windows-1255", "windows-1254", NULL,
      NULL, "windows-1257", NULL, NULL, NULL,
      NULL, NULL, NULL };

/*
 * Return the name of a codeset according to Codeset. If txt is not NULL, is
 * is scanned for chars between 0x80 and 0x9f. If such a char is found, this
 * usually means that txt contains windows (not iso) characters.
 */
static const gchar *
libbalsa_get_codeset_name(const gchar *txt, LibBalsaCodeset Codeset)
{
  if (txt && win_codesets[Codeset])
    while (*txt) {
      if (*(unsigned char *)txt >= 0x80 && *(unsigned char *)txt < 0x9f)
	return win_codesets[Codeset];
      txt++;
    }
  return std_codesets[Codeset];
}

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

static regex_t *
get_url_reg(void)
{
    static regex_t *url_reg = NULL;

    if (!url_reg) {
        /* one-time compilation of a constant url_str expression */
        static const char url_str[] =
#ifdef HAVE_PCRE
            "\\b((https?|ftps?|nntp)://|(mailto|news):)"
            "(%[0-9A-F]{2}|[-_.!~*';/?:@&=+$,#\\w])+";
#else
            "(((https?|ftps?|nntp)://)|(mailto:|news:))"
            "(%[0-9A-F]{2}|[-_.!~*';/?:@&=+$,#[:alnum:]])+";
#endif

        url_reg = g_new(regex_t, 1);
        if (regcomp(url_reg, url_str, REG_EXTENDED | REG_ICASE) != 0)
            g_warning("libbalsa_insert_with_url: "
                      "url regex compilation failed.");
    }

    return url_reg;
}

void
libbalsa_insert_with_url(GtkTextBuffer * buffer,
                         const char *chars,
                         GtkTextTag * tag,
                         void (*callback) (GtkTextBuffer *,
                                           GtkTextIter *,
                                           const gchar *,
                                           gpointer),
                         gpointer callback_data)
{
    gchar *p, *buf;
    GtkTextIter iter;

    buf = p = g_strdup(chars);
    gtk_text_buffer_get_iter_at_mark(buffer, &iter,
                                     gtk_text_buffer_get_insert(buffer));

    if (prescanner(p)) {
        GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
        GtkTextTag *url_tag = gtk_text_tag_table_lookup(table, "url");
        gint offset = 0;
        regex_t *url_reg = get_url_reg();
        regmatch_t url_match;
        gint match = regexec(url_reg, p, 1, &url_match, 0);

        while (!match) {
            gchar *buf;

            if (url_match.rm_so) {
                buf = g_strndup(p, url_match.rm_so);
                gtk_text_buffer_insert_with_tags(buffer, &iter,
                                                 buf, -1, tag, NULL);
                g_free(buf);
            }

            buf = g_strndup(p + url_match.rm_so,
                            url_match.rm_eo - url_match.rm_so);
            gtk_text_buffer_insert_with_tags(buffer, &iter, buf, -1,
                                             url_tag, tag, NULL);

            /* remember the URL and its position within the text */
            if (callback)
                callback(buffer, &iter, buf, callback_data);
            g_free(buf);

            p += url_match.rm_eo;
            offset += url_match.rm_eo;
            if (prescanner(p))
                match = regexec(url_reg, p, 1, &url_match, 0);
            else
                match = -1;
        }
    }

    if (*p)
        gtk_text_buffer_insert_with_tags(buffer, &iter, p, -1, tag, NULL);
    g_free(buf);
}

/* Helper for "Select All" callbacks: if the currently focused widget
 * supports any concept of "select-all", do it.
 *
 * It would be nice if all such widgets had a "select-all" signal, but
 * they don't; in fact, the only one that does (GtkTreeView) is
 * broken--if we emit it when the tree is not in multiple selection
 * mode, bad stuff happens.
 */
void
libbalsa_window_select_all(GtkWindow * window)
{
    GtkWidget *focus_widget = gtk_window_get_focus(window);

    if (!focus_widget)
	return;

    if (GTK_IS_TEXT_VIEW(focus_widget)) {
        GtkTextBuffer *buffer =
            gtk_text_view_get_buffer(GTK_TEXT_VIEW(focus_widget));
        GtkTextIter start, end;

        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gtk_text_buffer_place_cursor(buffer, &start);
        gtk_text_buffer_move_mark_by_name(buffer, "selection_bound", &end);
    } else if (GTK_IS_EDITABLE(focus_widget)) {
        gtk_editable_select_region(GTK_EDITABLE(focus_widget), 0, -1);
    } else if (GTK_IS_TREE_VIEW(focus_widget)) {
        GtkTreeSelection *selection =
            gtk_tree_view_get_selection(GTK_TREE_VIEW(focus_widget));
        if (gtk_tree_selection_get_mode(selection) ==
            GTK_SELECTION_MULTIPLE)
            gtk_tree_selection_select_all(selection);
    }
}

void
libbalsa_unwrap_selection(GtkTextBuffer * buffer, regex_t * rex)
{
    GtkTextIter start, end;
    gchar *line;
    guint quote_depth;
    guint index;
    GtkTextMark *selection_end;

    gtk_text_buffer_get_selection_bounds(buffer, &start, &end);
    gtk_text_iter_order(&start, &end);
    selection_end = gtk_text_buffer_create_mark(buffer, NULL, &end, FALSE);

    /* Find quote depth and index of first non-quoted character. */
    line = get_line(buffer, &start);
    if (libbalsa_match_regex(line, rex, &quote_depth, &index)) {
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
    while (gtk_text_iter_ends_line(&start)
	   || gtk_text_iter_forward_to_line_end(&start)) {
	gtk_text_buffer_get_iter_at_mark(buffer, &end, selection_end);
	if (gtk_text_iter_compare(&start, &end) >= 0)
	    break;
	end = start;
	if (!gtk_text_iter_forward_line(&end))
	    break;
	line = get_line(buffer, &end);
	libbalsa_match_regex(line, rex, NULL, &index);
	g_free(line);
	gtk_text_iter_set_line_index(&end, index);
	gtk_text_buffer_delete(buffer, &start, &end);
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
