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

    if ( getdnsdomainname(domainname, SYS_NMLN) == 0 ) {
	return g_strdup(domainname);
    }
    return NULL;
}

struct {
    char escaped_char;
    char replacement;
} char_translations[] = {
    { '\t','t' },
    { '\n','n' }
};
gchar *
libbalsa_escape_specials(const gchar* str)
{
    int special_cnt = 0, length = 0;
    unsigned i;
    const gchar *str_ptr;
    gchar *res, *res_ptr;
    
    g_return_val_if_fail(str, NULL);
    for(str_ptr = str; *str_ptr; str_ptr++, length++)
	for(i = 0; i<ELEMENTS(char_translations); i++)
	    if(*str_ptr == char_translations[i].escaped_char) {
		special_cnt++;
		break;
	    }
    
    res = res_ptr = g_new(gchar, length+special_cnt+1);
    
    for(str_ptr = str; *str_ptr; str_ptr++) {
	for(i = 0; i<ELEMENTS(char_translations); i++)
	    if(*str_ptr == char_translations[i].escaped_char) 
		break;
	if(i<ELEMENTS(char_translations)) {
	    *res_ptr++ = '\\';
	    *res_ptr++ = char_translations[i].replacement;
	} else 
	    *res_ptr++ = *str_ptr;
    }
    *res_ptr = '\0';
    return res;
}
	    
gchar *
libbalsa_deescape_specials(const gchar* str)
{
    const gchar *src;
    gchar *dest;
    unsigned i;
    gchar* res = g_strdup(str);

    g_return_val_if_fail(str, NULL);
    src = dest = res;
    while(*src) {
	if(*src == '\\') {
	    for(i = 0; i<ELEMENTS(char_translations); i++)
		if(src[1] == char_translations[i].replacement) 
		    break;
	    if(i<ELEMENTS(char_translations)) {
		*dest++ = char_translations[i].escaped_char;
		src += 2;
		continue;
	    }
	}
	*dest++ = *src++;
    }
    *dest = '\0';
    return res;
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
    gchar *str;
    GList *list;
    GString *gs = g_string_new(NULL);
    LibBalsaAddress *addy;

    list = g_list_first((GList *) the_list);

    while (list) {
	addy = list->data;
	str = libbalsa_address_to_gchar_p(addy, 0);
	if (str)
	    g_string_append(gs, str);

	g_free(str);

	if (list->next)
	    g_string_append(gs, ", ");

	list = list->next;
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
    gchar *lnbeg, *sppos, *ptr;
    gint te = 0;

    g_return_if_fail(str != NULL);
    lnbeg = sppos = ptr = str;

    while (*ptr) {
	switch (*ptr) {
	case '\t':
	    te += 7;
	    break;
	case '\n':
	    lnbeg = ptr + 1;
	    te = 0;
	    break;
	case ' ':
	    sppos = ptr;
	    break;
	}
	if (ptr - lnbeg >= width - te && sppos >= lnbeg + minl) {
	    *sppos = '\n';
	    lnbeg = sppos + 1;
	    te = 0;
	}
	ptr++;
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
#define QUOTE_STRING	">"

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

            for (p = str; *p == '>'; p++)
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
                g_string_append_c(result, '>');
            /* space-stuffing:
             * - for the wire, stuffing is required for lines beginning
             *   with ` ', `>', or `From '
             * - for the screen and for the wire, we'll use optional
             *   stuffing of quoted lines to provide a visual separation
             *   of quoting string and text
             * - ...but we mustn't stuff `-- ' */
            if (((!to_screen
                  && (*str == ' ' || *str == QUOTE_STRING[0]
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
                while (*str && !isspace((int)*str) && len < MAX_WIDTH) {
                    len++;
                    str++;
                }
                while (len < MAX_WIDTH && isspace((int)*str)) {
                    if (*str == '\t')
                        len += 8 - len % 8;
                    else
                        len++;
                    str++;
                }
                /*
                 * to avoid some unnecessary space-stuffing,
                 * we won't wrap at '>', ' ', or "From "
                 * (we already passed any spaces, so just check for '>'
                 * and "From ")
                 * */
                if (len < MAX_WIDTH && *str
                    && (*str == QUOTE_STRING[0]
                        || !strncmp(str, "From ", 5)))
                    continue;

                if (!*str || len > width) {
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

/* libbalsa_flowed_rfc2646:
 * test whether a message body is format=flowed
 * */
gboolean
libbalsa_flowed_rfc2646(LibBalsaMessageBody * body)
{
    gchar *content_type;
    gchar *format;
    gboolean flowed;

    content_type = libbalsa_message_body_get_content_type(body);
    if (g_ascii_strcasecmp(content_type, "text/plain"))
        flowed = FALSE;
    else {
        format = libbalsa_message_body_get_parameter(body, "format");
        flowed = format && (g_ascii_strcasecmp(format, "flowed") == 0);
        g_free(format);
    }
    g_free(content_type);

    return flowed;
}

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
 * byte with '?'.
 *
 * Argument:
 *   text   The text to be sanitized; NULL is OK.
 *
 * Return value:
 *   none
 *
 * NOTE:    The text is modified in place.
 */
void
libbalsa_utf8_sanitize(gchar * text)
{
    if (!text)
        return;

    while (!g_utf8_validate(text, -1, (const gchar **) &text))
        *text = '?';
}
