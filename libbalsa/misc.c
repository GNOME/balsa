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

#define _SVID_SOURCE           1
#define _XOPEN_SOURCE          500
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#if HAVE_COMPFACE
#include <compface.h>
#endif                          /* HAVE_COMPFACE */

#ifdef HAVE_GNOME
#include <libgnomevfs/gnome-vfs.h>
#endif

#if HAVE_GTKSOURCEVIEW
#include <gtksourceview/gtksourceview.h>
#include <gtksourceview/gtksourcebuffer.h>
#include <gtksourceview/gtksourcetag.h>
#include <gtksourceview/gtksourcetagstyle.h>
#endif

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "misc.h"
#include "html.h"
#include "i18n.h"

static const gchar *libbalsa_get_codeset_name(const gchar *txt, 
					      LibBalsaCodeset Codeset);
static int getdnsdomainname(char *s, size_t l);

/* libbalsa_lookup_mime_type:
   find out mime type of a file. Must work for both relative and absolute
   paths.
*/
gchar*
libbalsa_lookup_mime_type(const gchar * path)
{
#ifdef HAVE_GNOME
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
    return mime_type ? mime_type : g_strdup("application/octet-stream");
#else
    return g_strdup("application/octet-stream");
#endif/* HAVE_GNOME */
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

static int 
getdnsdomainname (char *s, size_t l)
{
  FILE *f;
  char tmp[1024];
  char *p = NULL;
  char *q;

  if ((f = fopen ("/etc/resolv.conf", "r")) == NULL) return (-1);

  tmp[sizeof (tmp) - 1] = 0;

  l--; /* save room for the terminal \0 */

  while (fgets (tmp, sizeof (tmp) - 1, f) != NULL)
  {
    p = tmp;
    while ( g_ascii_isspace (*p)) p++;
    if (strncmp ("domain", p, 6) == 0 || strncmp ("search", p, 6) == 0)
    {
      p += 6;
      
      for (q = strtok (p, " \t\n"); q; q = strtok (NULL, " \t\n"))
	if (strcmp (q, "."))
	  break;

      if (q)
      {
	  char *a = q;
	  
	  for (; *q; q++)
	      a = q;
	  
	  if (*a == '.')
	      *a = '\0';
	  
	  g_stpcpy (s, q);
	  fclose (f);
	  return 0;
      }
      
    }
  }

  fclose (f);
  return (-1);
}

gchar *
libbalsa_get_domainname(void)
{
    char domainname[256]; /* arbitrary length */
    struct utsname utsname;
    gchar *d;
 
    uname(&utsname);
    d = strchr( utsname.nodename, '.' );
    if(d) {
        return g_strdup( d+1 );
    }
 
    if ( getdnsdomainname(domainname, sizeof(domainname)) == 0 ) {
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

gchar *
libbalsa_urldecode(const gchar * str)
{
    gchar *retval;
    gchar *x;

    retval = g_new(char, strlen(str));

    for (x = retval; *str != '\0'; str++, x++) {
	*x = *str;
	if (*x == '+')
	    *x = ' ';
	else if (*x == '%') {
	    if (!*++str || !g_ascii_isxdigit(*str))
		break;
	    *x = g_ascii_xdigit_value(*str);
	    if (!*++str || !g_ascii_isxdigit(*str))
		break;
	    *x = *x << 4 | g_ascii_xdigit_value(*str);
	}
    }

    *x = '\0';
    return retval;
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
static regex_t *get_url_reg(void);

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
   We handle only references to ~/.
*/
gchar*
libbalsa_expand_path(const gchar * path)
{
    const gchar *home = g_get_home_dir();
   
    if(path[0] == '~') {
        if(path[1] == '/')
            return g_strconcat(home, path+1, NULL);
        else if(path[1] == '\0')
            return g_strdup(home);
        /* else: unrecognized combination */
    }
    return g_strdup(path);
}



/* create a uniq directory, resulting name should be freed with g_free */
gboolean 
libbalsa_mktempdir (char **s)
{
    gchar *name;
    int fd;

    g_return_val_if_fail(s != NULL, FALSE);

    do {
	GError *error = NULL;
	fd = g_file_open_tmp("balsa-tmpdir-XXXXXX", &name, &error);
	close(fd);
	unlink(name);
	/* Here is a short time that the name could be reused */
	fd = mkdir(name, 0700);
	if (fd == -1) {
	    g_free(name);
	    if (!g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_EXIST))
		return FALSE;
	}
	if (error)
	    g_error_free(error);
    } while (fd == -1);
    *s = name;
    /* FIXME: rmdir(name) at sometime */
    return TRUE;
}

/* libbalsa_set_fallback_codeset: sets the codeset for incorrectly
 * encoded characters. */
static LibBalsaCodeset sanitize_fallback_codeset = WEST_EUROPE;
LibBalsaCodeset
libbalsa_set_fallback_codeset(LibBalsaCodeset codeset)
{     
    LibBalsaCodeset ret = sanitize_fallback_codeset;
    sanitize_fallback_codeset = codeset;
    return ret;
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
libbalsa_utf8_sanitize(gchar **text, gboolean fallback,
		       gchar const **target)
{
    gchar *p;

    if (target)
	*target = NULL;
    if (!*text || g_utf8_validate(*text, -1, NULL))
	return TRUE;

    if (fallback) {
	gsize b_written;
	GError *conv_error = NULL;
	const gchar *use_enc =
            libbalsa_get_codeset_name(*text, sanitize_fallback_codeset);
	p = g_convert(*text, strlen(*text), "utf-8", use_enc, NULL,
                      &b_written, &conv_error);

	if (p) {
	    g_free(*text);
	    *text = p;
	    if (target)
		*target = use_enc;
	    return FALSE;
	}
	g_message("conversion %s -> utf8 failed: %s", use_enc,
                  conv_error->message);
	g_error_free(conv_error);
    }
    p = *text;
    while (!g_utf8_validate(p, -1, (const gchar **) &p))
	*p++ = '?';

    return FALSE;
}

/* libbalsa_utf8_strstr() returns TRUE if s2 is a substring of s1.
 * libbalsa_utf8_strstr is case insensitive
 * this functions understands utf8 strings (as you might have guessed ;-)
 */
gboolean
libbalsa_utf8_strstr(const gchar *s1, const gchar *s2)
{
    const gchar * p,* q;

    /* convention : NULL string is contained in anything */
    if (!s2) return TRUE;
    /* s2 is non-NULL, so if s1==NULL we return FALSE :)*/
    if (!s1) return FALSE;
    /* OK both are non-NULL now*/
    /* If s2 is the empty string return TRUE */
    if (!*s2) return TRUE;
    while (*s1) {
	/* We look for the first char of s2*/
	for (;*s1 &&
		 g_unichar_toupper(g_utf8_get_char(s2))!=g_unichar_toupper(g_utf8_get_char(s1));
	     s1 = g_utf8_next_char(s1));
	if (*s1) {
	    /* We found the first char let see if this potential match is an actual one */
	    s1 = g_utf8_next_char(s1);
	    q = s1;
	    p = g_utf8_next_char(s2);
	    while (*q && *p && 
		   g_unichar_toupper(g_utf8_get_char(p))
		   ==g_unichar_toupper(g_utf8_get_char(q))) {
		p = g_utf8_next_char(p);
		q = g_utf8_next_char(q);
	    }
	    /* We have a match if p has reached the end of s2, ie *p==0 */
	    if (!*p) return TRUE;
	}
    }
    return FALSE;
}

/* The LibBalsaCodeset enum is not used for anything currently, but this
 * list must be the same length, and should probably be kept consistent: */
LibBalsaCodesetInfo libbalsa_codeset_info[LIBBALSA_NUM_CODESETS] = {
    {N_("west european"),       /* WEST_EUROPE          */
     "iso-8859-1", "windows-1252"} ,
    {N_("east european"),       /* EAST_EUROPE          */
     "iso-8859-2", "windows-1250"} ,
    {N_("south european"),      /* SOUTH_EUROPE         */
     "iso-8859-3"} ,
    {N_("north european"),      /* NORTH_EUROPE         */
     "iso-8859-4"} ,
    {N_("cyrillic"),            /* CYRILLIC             */
     "iso-8859-5", "windows-1251"} ,
    {N_("arabic"),              /* ARABIC               */
     "iso-8859-6", "windows-1256"} ,
    {N_("greek"),               /* GREEK                */
     "iso-8859-7", "windows-1253"} ,
    {N_("hebrew"),              /* HEBREW               */
     "iso-8859-8", "windows-1255"} ,
    {N_("turkish"),             /* TURKISH              */
     "iso-8859-9", "windows-1254"} ,
    {N_("nordic"),              /* NORDIC               */
     "iso-8859-10"} ,
    {N_("thai"),                /* THAI                 */
     "iso-8859-11"} ,
    {N_("baltic"),              /* BALTIC               */
     "iso-8859-13", "windows-1257"} ,
    {N_("celtic"),              /* CELTIC               */
     "iso-8859-14"} ,
    {N_("west europe (euro)"),  /* WEST_EUROPE_EURO     */
     "iso-8859-15"} ,
    {N_("russian"),             /* RUSSIAN              */
     "koi-8r"} ,
    {N_("ukrainian"),           /* UKRAINE              */
     "koi-8u"} ,
    {N_("japanese"),            /* JAPAN                */
     "iso-2022-jp"} ,
    {N_("korean"),              /* KOREA                */
     "euc-kr"} ,
    {N_("east european"),       /* EAST_EUROPE_WIN      */
     "windows-1250"} ,
    {N_("cyrillic"),            /* CYRILLIC_WIN         */
     "windows-1251"} ,
    {N_("greek"),               /* GREEK_WIN            */
     "windows-1253"} ,
    {N_("hebrew"),              /* HEBREW_WIN           */
     "windows-1255"} ,
    {N_("arabic"),              /* ARABIC_WIN           */
     "windows-1256"} ,
    {N_("baltic"),              /* BALTIC_WIN           */
     "windows-1257"} ,
};

/*
 * Return the name of a codeset according to Codeset. If txt is not NULL, is
 * is scanned for chars between 0x80 and 0x9f. If such a char is found, this
 * usually means that txt contains windows (not iso) characters.
 */
static const gchar *
libbalsa_get_codeset_name(const gchar * txt, LibBalsaCodeset Codeset)
{
    LibBalsaCodesetInfo *info = &libbalsa_codeset_info[Codeset];

    if (txt && info->win) {
        LibBalsaTextAttribute attr = libbalsa_text_attr_string(txt);
        if (attr & LIBBALSA_TEXT_HI_CTRL)
            return info->win;
    }
    return info->std;
}

/* Create a GtkComboBox with the national charsets as options;
 * called when some text is found to be neither US-ASCII nor UTF-8, so
 * the list includes neither of these. */
GtkWidget *
libbalsa_charset_button_new(void)
{
    GtkWidget *combo_box;
    LibBalsaCodeset n, active = WEST_EUROPE;
    const gchar *locale_charset;

    combo_box = gtk_combo_box_new_text();
    locale_charset = g_mime_locale_charset();

    for (n = 0; n < LIBBALSA_NUM_CODESETS; n++) {
        LibBalsaCodesetInfo *info = &libbalsa_codeset_info[n];
        gchar *tmp = g_strdup_printf("%s (%s)", _(info->label), info->std);
        gtk_combo_box_append_text(GTK_COMBO_BOX(combo_box), tmp);
        g_free(tmp);

	if (!g_ascii_strcasecmp(info->std, locale_charset))
	    active = n;
    }

    /* locale_charset may be UTF-8, in which case it was not found,
     * and the initial choice will be WEST_EUROPE (= 0). */
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo_box), active);

    return combo_box;
}

/* Helper */
static void
lb_text_attr(const gchar * text, gboolean * has_esc, gboolean * has_hi_bit,
             gboolean * has_hi_ctrl)
{
    for (; *text; text++) {
	guchar c = *text;
        if (c == 0x1b)
            *has_esc = TRUE;
        if (c >= 0x80) {
            *has_hi_bit = TRUE;
            if (c <= 0x9f)
                *has_hi_ctrl = TRUE;
        }
    }
}

/* Return text attributes of a string. */
LibBalsaTextAttribute
libbalsa_text_attr_string(const gchar * string)
{
    LibBalsaTextAttribute attr;
    gboolean has_esc = FALSE;
    gboolean has_hi_bit = FALSE;
    gboolean has_hi_ctrl = FALSE;
    gboolean is_utf8 = TRUE;

    lb_text_attr(string, &has_esc, &has_hi_bit, &has_hi_ctrl);
    is_utf8 = g_utf8_validate(string, -1, NULL);

    attr = 0;
    if (has_esc)
        attr |= LIBBALSA_TEXT_ESC;
    if (has_hi_bit)
        attr |= LIBBALSA_TEXT_HI_BIT;
    if (has_hi_ctrl)
        attr |= LIBBALSA_TEXT_HI_CTRL;
    if (is_utf8 && has_hi_bit)
        attr |= LIBBALSA_TEXT_HI_UTF8;

    return attr;
}

/* Return text attributes of the contents of a file. */
LibBalsaTextAttribute
libbalsa_text_attr_file(const gchar * filename)
{
    LibBalsaTextAttribute attr;
    FILE *fp;
    gchar buf[80];
    gchar *new_chars = buf;
    gboolean has_esc = FALSE;
    gboolean has_hi_bit = FALSE;
    gboolean has_hi_ctrl = FALSE;
    gboolean is_utf8 = TRUE;

    fp = fopen(filename, "r");
    if (!fp)
        return 0;

    while (fgets(new_chars, (sizeof buf) - (new_chars - buf), fp)) {
	gboolean test_bits = !has_esc || !has_hi_bit || !has_hi_ctrl;

	if (!test_bits && !is_utf8)
	    break;

        if (test_bits)
            lb_text_attr(new_chars, &has_esc, &has_hi_bit, &has_hi_ctrl);

        if (is_utf8) {
            const gchar *end;

            new_chars = buf;
            if (!g_utf8_validate(buf, -1, &end)) {
                if (g_utf8_get_char_validated(end, -1) == (gunichar) (-1))
                    is_utf8 = FALSE;
                else
                    /* copy any remaining bytes, including the
                     * terminating '\0', to start of buffer */
                    while ((*new_chars = *end++) != '\0')
                        new_chars++;
            }
        }
    }

    fclose(fp);

    attr = 0;
    if (has_esc)
        attr |= LIBBALSA_TEXT_ESC;
    if (has_hi_bit)
        attr |= LIBBALSA_TEXT_HI_BIT;
    if (has_hi_ctrl)
        attr |= LIBBALSA_TEXT_HI_CTRL;
    if (is_utf8 && has_hi_bit)
        attr |= LIBBALSA_TEXT_HI_UTF8;

    return attr;
}

/* Check whether a file is all ascii or utf-8, and return charset
 * accordingly (NULL if it's neither).
 * This function is called only as a last resort when a message is being
 * prepared for sending.  The charset should always be set when the file
 * is being attached.
 */
const gchar *
libbalsa_file_get_charset(const gchar * filename)
{
    LibBalsaTextAttribute attr = libbalsa_text_attr_file(filename);

    if (!(attr & LIBBALSA_TEXT_HI_BIT))
	return "us-ascii";
    if (attr & LIBBALSA_TEXT_HI_UTF8)
	return "utf-8";
    return NULL;
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

static regex_t *
get_ml_url_reg(void)
{
    static regex_t *url_reg = NULL;
    
    if (!url_reg) {
        /* one-time compilation of a constant url_str expression */
        static const char url_str[] =
#ifdef HAVE_PCRE
            "(%[0-9A-F]{2}|[-_.!~*';/?:@&=+$,#\\w]|[ \\t]*[\\r\\n]+[ \\t]*)+>";
#else
	    "(%[0-9A-F]{2}|[-_.!~*';/?:@&=+$,#[:alnum:]]|[ \t]*[\r\n]+[ \t]*)+>";
#endif

	url_reg = g_new(regex_t, 1);
        if (regcomp(url_reg, url_str, REG_EXTENDED | REG_ICASE) != 0)
            g_warning("libbalsa_insert_with_url: "
                      "multiline url regex compilation failed.");
    }
    
    return url_reg;
}

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
	    regex_t *url_reg = get_url_reg();
	    regmatch_t url_match;
	    gint match = regexec(url_reg, p, 1, &url_match, 0);

	    while (!match) {
		gchar *buf;

		if (url_match.rm_so) {
		    /* check if we hit a multi-line URL... (see RFC 1738) */
		    if (all_p && (p[url_match.rm_so - 1] == '<' ||
				  (url_match.rm_so > 4 &&
				   g_ascii_strcasecmp(p + url_match.rm_so - 5, "<URL:") == 0)) &&
			!strchr(p + url_match.rm_eo, '>')) {
			regex_t *ml_url_reg = get_ml_url_reg();
			regmatch_t ml_url_match;
		    
			if (!regexec(ml_url_reg,
				     all_chars + offset + url_match.rm_eo, 1,
				     &ml_url_match, 0) && ml_url_match.rm_so == 0) {
			    GString *ml_url = g_string_new("");
			    const gchar *ml_p = all_chars + offset + url_match.rm_so;
			    gint ml_cnt =
				url_match.rm_eo - url_match.rm_so + ml_url_match.rm_eo - 1;

			    for (; ml_cnt; (ml_p++, ml_cnt--))
				if (*ml_p > ' ')
				    ml_url = g_string_append_c(ml_url, *ml_p);
			    url_info->ml_url = ml_url->str;
			    g_string_free(ml_url, FALSE);
			}
		    }

		    buf = g_strndup(p, url_match.rm_so);
		    gtk_text_buffer_insert_with_tags(buffer, &iter,
						     buf, -1, tag, NULL);
		    g_free(buf);
		}

		if (url_info->ml_url) {
		    url_info->ml_url_buffer = g_string_new(p + url_match.rm_so);
		    url_info->ml_url_buffer =
			g_string_append_c(url_info->ml_url_buffer, '\n');
		    return TRUE;
		}

		buf = g_strndup(p + url_match.rm_so,
				url_match.rm_eo - url_match.rm_so);
		gtk_text_buffer_insert_with_tags(buffer, &iter, buf, -1,
						 url_tag, tag, NULL);

		/* remember the URL and its position within the text */
		if (url_info->callback)
		    url_info->callback(buffer, &iter, buf, url_info->callback_data);
		g_free(buf);

		p += url_match.rm_eo;
		offset += url_match.rm_eo;
		if (prescanner(p))
		    match = regexec(url_reg, p, 1, &url_match, 0);
		else
		    match = -1;
	    }
	}
    }

    if (*p)
        gtk_text_buffer_insert_with_tags(buffer, &iter, p, -1, tag, NULL);
    g_free(buf);

    return FALSE;
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


#define compare_stat(osb, nsb)  ( (osb.st_dev != nsb.st_dev || osb.st_ino != nsb.st_ino || osb.st_rdev != nsb.st_rdev) ? -1 : 0 )

int 
libbalsa_safe_open (const char *path, int flags, mode_t mode)
{
  struct stat osb, nsb;
  int fd;
 
  if ((fd = open (path, flags, mode)) < 0)
    return fd;
 
  /* make sure the file is not symlink */
  if (lstat (path, &osb) < 0 || fstat (fd, &nsb) < 0 ||
      compare_stat(osb, nsb) == -1)
      {
	  g_warning("safe_open(): %s is a symlink!\n", path);
	  close (fd);
	  return (-1);
      }
 
  return (fd);
}

/* 
 * This function is supposed to do nfs-safe renaming of files.
 * 
 * Warning: We don't check whether src and target are equal.
 */

int 
libbalsa_safe_rename (const char *src, const char *target)
{
  struct stat ssb, tsb;

  if (!src || !target)
    return -1;

  if (link (src, target) != 0)
  {

    /*
     * Coda does not allow cross-directory links, but tells
     * us it's a cross-filesystem linking attempt.
     * 
     * However, the Coda rename call is allegedly safe to use.
     * 
     * With other file systems, rename should just fail when 
     * the files reside on different file systems, so it's safe
     * to try it here.
     *
     */

    if (errno == EXDEV)
      return rename (src, target);
    
    return -1;
  }

  /*
   * Stat both links and check if they are equal.
   */
  
  if (stat (src, &ssb) == -1)
  {
    return -1;
  }
  
  if (stat (target, &tsb) == -1)
  {
    return -1;
  }

  /* 
   * pretend that the link failed because the target file
   * did already exist.
   */

  if (compare_stat (ssb, tsb) == -1)
  {
    errno = EEXIST;
    return -1;
  }

  /*
   * Unlink the original link.  Should we really ignore the return
   * value here? XXX
   */

  unlink (src);

  return 0;
}


#define MAXLOCKATTEMPT 5

/* Args:
 *      excl            if excl != 0, request an exclusive lock
 *      dot             if dot != 0, try to dotlock the file
 *      timeout         should retry locking?
 */
int 
libbalsa_lock_file (const char *path, int fd, int excl, int dot, int timeout)
{
#if defined (USE_FCNTL) || defined (USE_FLOCK)
    int count;
    int attempt;
    struct stat prev_sb = { 0 };
#endif
    int r = 0;

#ifdef USE_FCNTL
    struct flock lck;

    memset (&lck, 0, sizeof (struct flock));
    lck.l_type = excl ? F_WRLCK : F_RDLCK;
    lck.l_whence = SEEK_SET;

    count = 0;
    attempt = 0;
    while (fcntl (fd, F_SETLK, &lck) == -1)
	{
	    struct stat sb;
	    g_print("%s(): fcntl errno %d.\n", __FUNCTION__, errno);
    if (errno != EAGAIN && errno != EACCES)
	{
	    libbalsa_information
		(LIBBALSA_INFORMATION_DEBUG, "fcntl failed, errno=%d.", errno);
	    return -1;
	}
 
    if (fstat (fd, &sb) != 0)
	sb.st_size = 0;
     
    if (count == 0)
	prev_sb = sb;
 
    /* only unlock file if it is unchanged */
    if (prev_sb.st_size == sb.st_size && ++count >= (timeout?MAXLOCKATTEMPT:0))
	{
	    if (timeout)
		libbalsa_information
		    (LIBBALSA_INFORMATION_WARNING,
		     _("Timeout exceeded while attempting fcntl lock!"));
	    return (-1);
	}
 
    prev_sb = sb;
 
    libbalsa_information(LIBBALSA_INFORMATION_MESSAGE,
			 _("Waiting for fcntl lock... %d"), ++attempt);
    sleep (1);
}
#endif /* USE_FCNTL */
 
#ifdef USE_FLOCK
count = 0;
attempt = 0;
while (flock (fd, (excl ? LOCK_EX : LOCK_SH) | LOCK_NB) == -1)
{
    struct stat sb;
    if (errno != EWOULDBLOCK)
	{
	    libbalsa_message ("flock: %s", strerror(errno));
	    r = -1;
	    break;
	}
 
    if (fstat(fd,&sb) != 0 )
	sb.st_size=0;
     
    if (count == 0)
	prev_sb=sb;
 
    /* only unlock file if it is unchanged */
    if (prev_sb.st_size == sb.st_size && ++count >= (timeout?MAXLOCKATTEMPT:0))
	{
	    if (timeout)
		libbalsa_message (_("Timeout exceeded while attempting flock lock!"));
	    r = -1;
	    break;
	}
 
    prev_sb = sb;
 
    libbalsa_message (_("Waiting for flock attempt... %d"), ++attempt);
    sleep (1);
}
#endif /* USE_FLOCK */
 
#ifdef USE_DOTLOCK
if (r == 0 && dot)
     r = dotlock_file (path, fd, timeout);
#endif /* USE_DOTLOCK */
 
     if (r == -1)
{
    /* release any other locks obtained in this routine */
 
#ifdef USE_FCNTL
    lck.l_type = F_UNLCK;
    fcntl (fd, F_SETLK, &lck);
#endif /* USE_FCNTL */
 
#ifdef USE_FLOCK
    flock (fd, LOCK_UN);
#endif /* USE_FLOCK */
 
    return (-1);
}
 
return 0;
}

int 
libbalsa_unlock_file (const char *path, int fd, int dot)
{
#ifdef USE_FCNTL
    struct flock unlockit = { F_UNLCK, 0, 0, 0 };

    memset (&unlockit, 0, sizeof (struct flock));
    unlockit.l_type = F_UNLCK;
    unlockit.l_whence = SEEK_SET;
    fcntl (fd, F_SETLK, &unlockit);
#endif

#ifdef USE_FLOCK
    flock (fd, LOCK_UN);
#endif

#ifdef USE_DOTLOCK
    if (dot)
	undotlock_file (path, fd);
#endif

    return 0;
}

#if !GLIB_CHECK_VERSION(2, 2, 0)
gboolean
libbalsa_str_has_prefix(const gchar * str, const gchar * prefix)
{
    g_return_val_if_fail(str != NULL, FALSE);
    g_return_val_if_fail(prefix != NULL, FALSE);

    while (*prefix == *str && *prefix) {
	++prefix;
	++str;
    }

    return *prefix == '\0';
}
#endif				/* !GLIB_CHECK_VERSION(2, 2, 0) */


/* libbalsa_ia_rfc2821_equal
   compares two addresses according to rfc2821: local-part@domain is equal,
   if the local-parts are case sensitive equal, but the domain case-insensitive
*/
gboolean
libbalsa_ia_rfc2821_equal(const InternetAddress * a,
			  const InternetAddress * b)
{
    const gchar *a_atptr, *b_atptr;
    gint a_atpos, b_atpos;

    if (!a || !b || a->type != INTERNET_ADDRESS_NAME ||
	b->type != INTERNET_ADDRESS_NAME)
        return FALSE;

    /* first find the "@" in the two addresses */
    a_atptr = strchr(a->value.addr, '@');
    b_atptr = strchr(b->value.addr, '@');
    if (!a_atptr || !b_atptr)
        return FALSE;
    a_atpos = a_atptr - a->value.addr;
    b_atpos = b_atptr - b->value.addr;

    /* now compare the strings */
    if (!a_atpos || !b_atpos || a_atpos != b_atpos || 
        strncmp(a->value.addr, b->value.addr, a_atpos) ||
        g_ascii_strcasecmp(a_atptr, b_atptr))
        return FALSE;
    else
        return TRUE;
}

/*
 * Face and X-Face header support.
 */
gchar *
libbalsa_get_header_from_path(const gchar * header, const gchar * path,
                              gsize * size, GError ** err)
{
    gchar *buf, *content;
    size_t name_len;
    gchar *p, *q;

    if (!g_file_get_contents(path, &buf, size, err))
        return NULL;

    content = buf;
    name_len = strlen(header);
    if (g_ascii_strncasecmp(content, header, name_len) == 0)
        /* Skip header and trailing colon: */
        content += name_len + 1;

    /* Unfold. */
    for (p = q = content; *p; p++)
        if (*p != '\r' && *p != '\n')
            *q++ = *p;
    *q = '\0';

    content = g_strdup(content);
    g_free(buf);

    return content;
}

GtkWidget *
libbalsa_get_image_from_face_header(const gchar * content, GError ** err)
{
    GMimeStream *stream;
    GMimeStream *stream_filter;
    GMimeFilter *filter;
    GByteArray *array;
    GtkWidget *image = NULL;

    stream = g_mime_stream_mem_new();
    stream_filter = g_mime_stream_filter_new_with_stream(stream);

    filter = g_mime_filter_basic_new_type(GMIME_FILTER_BASIC_BASE64_DEC);
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(stream_filter), filter);
    g_object_unref(filter);

    g_mime_stream_write_string(stream_filter, content);
    g_object_unref(stream_filter);

    array = GMIME_STREAM_MEM(stream)->buffer;
    if (array->len == 0)
        g_set_error(err, LIBBALSA_IMAGE_ERROR,
                    LIBBALSA_IMAGE_ERROR_NO_DATA, _("No image data"));
    else {
        GdkPixbufLoader *loader =
            gdk_pixbuf_loader_new_with_type("png", NULL);

        gdk_pixbuf_loader_write(loader, array->data, array->len, err);
        gdk_pixbuf_loader_close(loader, *err ? NULL : err);

        if (!*err)
            image = gtk_image_new_from_pixbuf(gdk_pixbuf_loader_get_pixbuf
                                              (loader));
        g_object_unref(loader);
    }
    g_object_unref(stream);

    return image;
}

#if HAVE_COMPFACE
GtkWidget *
libbalsa_get_image_from_x_face_header(const gchar * content, GError ** err)
{
    gchar buf[2048];
    GdkPixbuf *pixbuf;
    guchar *pixels;
    gint lines;
    const gchar *p;
    GtkWidget *image = NULL;

    strncpy(buf, content, sizeof buf - 1);

    switch (uncompface(buf)) {
    case -1:
        g_set_error(err, LIBBALSA_IMAGE_ERROR, LIBBALSA_IMAGE_ERROR_FORMAT,
                    _("Invalid input format"));
        return image;
    case -2:
        g_set_error(err, LIBBALSA_IMAGE_ERROR, LIBBALSA_IMAGE_ERROR_BUFFER,
                    _("Internal buffer overrun"));
        return image;
    }

    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 48, 48);
    pixels = gdk_pixbuf_get_pixels(pixbuf);

    p = buf;
    for (lines = 48; lines > 0; --lines) {
        guint x[3];
        gint j, k;
        guchar *q;

        if (sscanf(p, "%x,%x,%x,", &x[0], &x[1], &x[2]) != 3) {
            g_set_error(err, LIBBALSA_IMAGE_ERROR,
                        LIBBALSA_IMAGE_ERROR_BAD_DATA,
                        /* Translators: please do not translate Face. */
                        _("Bad X-Face data"));
            g_object_unref(pixbuf);
            return image;
        }
        for (j = 0, q = pixels; j < 3; j++)
            for (k = 15; k >= 0; --k){
                guchar c = x[j] & (1 << k) ? 0x00 : 0xff;
                *q++ = c;       /* red   */
                *q++ = c;       /* green */
                *q++ = c;       /* blue  */
            }
        p = strchr(p, '\n') + 1;
        pixels += gdk_pixbuf_get_rowstride(pixbuf);
    }

    image = gtk_image_new_from_pixbuf(pixbuf);
    g_object_unref(pixbuf);

    return image;
}
#endif                          /* HAVE_COMPFACE */

GQuark
libbalsa_image_error_quark(void)
{
    static GQuark quark = 0;
    if (quark == 0)
        quark = g_quark_from_static_string("libbalsa-image-error-quark");
    return quark;
}

#if HAVE_GTKSOURCEVIEW
GtkWidget *
libbalsa_source_view_new(gboolean highlight_phrases, GdkColor *q_colour)
{
    GtkTextTag * text_tag;
    GtkSourceTagStyle *tag_style;
    GtkSourceTagTable *tag_table;
    GSList *tag_list;
    GtkSourceBuffer *sbuffer;
    GtkWidget *sview;

    /* create the tag table */
    tag_list = NULL;
    tag_table = gtk_source_tag_table_new();

    /* add highlighting for quoted text if requested */
    if (q_colour) {
	int k;

	for (k = 1; k <= 9; k++) {
	    gchar * tag_id;
	    gchar * pattern;

	    tag_id = g_strdup_printf("Quote-%d", k);
	    if (k == 1)
		pattern = g_strdup("^> ?($|[^|>:}#])");
	    else
		pattern = g_strdup_printf("^(> ?){%d}($|[^|>:}#])", k);
	    printf("%d: %s\n", k, pattern);
	    text_tag = gtk_line_comment_tag_new(tag_id, tag_id, pattern);
	    g_free(pattern);
	    g_free(tag_id);
	    tag_style = gtk_source_tag_style_new();
	    tag_style->mask = GTK_SOURCE_TAG_STYLE_USE_FOREGROUND;
	    tag_style->foreground = q_colour[(k - 1) & 1];
	    gtk_source_tag_set_style(GTK_SOURCE_TAG(text_tag), tag_style);
	    gtk_source_tag_style_free(tag_style);
	    tag_list = g_slist_prepend(tag_list, text_tag);
	}
    }

    /* if requested create the patterns for bold, italic and underline */
    if (highlight_phrases) {
	text_tag = gtk_pattern_tag_new("Bold", "Bold",
				       "(^|[[:space:]])\\*[[:alnum:]][^*\n]*[[:alnum:]]\\*");
	tag_style = gtk_source_tag_style_new();
	tag_style->bold = TRUE;
	gtk_source_tag_set_style(GTK_SOURCE_TAG(text_tag), tag_style);
	gtk_source_tag_style_free(tag_style);
	tag_list = g_slist_prepend(tag_list, text_tag);

	text_tag = gtk_pattern_tag_new("Italic", "Italic",
				       "(^|[[:space:]])/[[:alnum:]][^/\n]*[[:alnum:]]/");
	tag_style = gtk_source_tag_style_new();
	tag_style->italic = TRUE;
	gtk_source_tag_set_style(GTK_SOURCE_TAG(text_tag), tag_style);
	gtk_source_tag_style_free(tag_style);
	tag_list = g_slist_prepend(tag_list, text_tag);

	text_tag = gtk_pattern_tag_new("Underline", "Underline",
				       "(^|[[:space:]])_[[:alnum:]][^_\n]*[[:alnum:]]_");
	tag_style = gtk_source_tag_style_new();
	tag_style->underline = TRUE;
	gtk_source_tag_set_style(GTK_SOURCE_TAG(text_tag), tag_style);
	gtk_source_tag_style_free(tag_style);
	tag_list = g_slist_prepend(tag_list, text_tag);
    }

    /* add tags to the table if present */
    if (tag_list) {
	gtk_source_tag_table_add_tags(tag_table, tag_list);
	g_slist_foreach(tag_list, (GFunc)g_object_unref, NULL);
	g_slist_free(tag_list);
    }

    /* create the source buffer */
    sbuffer = gtk_source_buffer_new(tag_table);
    g_object_unref(tag_table);
    gtk_source_buffer_set_highlight(sbuffer, highlight_phrases || q_colour);
    gtk_source_buffer_set_check_brackets(sbuffer, FALSE);

    /* create & return the source view */
    sview = gtk_source_view_new_with_buffer(sbuffer);
    g_object_unref(sbuffer);

    return sview;
}
#endif  /* HAVE_GTKSOURCEVIEW */

/*
 * Utilities for making consistent dialogs.
 */

#define LB_PADDING 12           /* per HIG */

GtkWidget *
libbalsa_create_table(guint rows, guint columns)
{
    GtkWidget *table;

    table = gtk_table_new(rows, columns, FALSE);

    gtk_table_set_row_spacings(GTK_TABLE(table), LB_PADDING);
    gtk_table_set_col_spacings(GTK_TABLE(table), LB_PADDING);

    return table;
}

/* create_label:
   Create a label and add it to a table in the first column of given row,
   setting the keyval to found accelerator value, that can be later used 
   in create_entry.
*/
GtkWidget *
libbalsa_create_label(const gchar * text, GtkWidget * table, gint row)
{
    GtkWidget *label = gtk_label_new_with_mnemonic(text);

    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);

    gtk_table_attach(GTK_TABLE(table), label, 0, 1, row, row + 1,
                     GTK_FILL, GTK_FILL, 0, 0);

    return label;
}

/* create_check:
   creates a checkbox with a given label and places them in given array.
*/
GtkWidget *
libbalsa_create_check(const gchar * text, GtkWidget * table, gint row,
                      gboolean initval)
{
    GtkWidget *check_button;

    check_button = gtk_check_button_new_with_mnemonic(text);

    gtk_table_attach(GTK_TABLE(table), check_button, 0, 2, row, row + 1,
                     GTK_FILL, 0, 0, 0);

    if (initval)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button),
                                     TRUE);

    return check_button;
}

/* Create a text entry and add it to the table */
GtkWidget *
libbalsa_create_entry(GtkWidget * table, GCallback changed_func,
                      gpointer data, gint row, const gchar * initval,
                      GtkWidget * hotlabel)
{
    GtkWidget *entry;

    entry = gtk_entry_new();

    gtk_table_attach(GTK_TABLE(table), entry, 1, 2, row, row + 1,
                     GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);

    if (initval) {
        gint zero = 0;

        gtk_editable_insert_text(GTK_EDITABLE(entry), initval, -1, &zero);
    }

    gtk_label_set_mnemonic_widget(GTK_LABEL(hotlabel), entry);

    /* Watch for changes... */
    if (changed_func)
        g_signal_connect(entry, "changed", changed_func, data);

    return entry;
}

/* Create a GtkSizeGroup and add to it any GtkLabel packed in a GtkTable
 * inside the chooser widget; size_group will be unreffed when the
 * chooser widget is finalized. */
static void
lb_create_size_group_func(GtkWidget * widget, gpointer data)
{
    if (GTK_IS_LABEL(widget) && GTK_IS_TABLE(widget->parent))
        gtk_size_group_add_widget(GTK_SIZE_GROUP(data), widget);
    else if (GTK_IS_CONTAINER(widget))
        gtk_container_foreach(GTK_CONTAINER(widget),
                              lb_create_size_group_func, data);
}

GtkSizeGroup *
libbalsa_create_size_group(GtkWidget * chooser)
{
    GtkSizeGroup *size_group;

    size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
    g_object_weak_ref(G_OBJECT(chooser), (GWeakNotify) g_object_unref,
                      size_group);
    lb_create_size_group_func(chooser, size_group);

    return size_group;
}
