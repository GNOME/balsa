/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* vim:set ts=4 sw=4 ai et: */
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

#include <gnome.h>

#ifdef HAVE_GNOME_VFS
# include <libgnomevfs/gnome-vfs-file-info.h>
# include <libgnomevfs/gnome-vfs-ops.h>
#else
# define GNOME_MIME_BUG_WORKAROUND 1
#endif

#include "misc.h"
#include "libbalsa_private.h"

/* libbalsa_lookup_mime_type:
   returns an allocated mime-type.
   gnome_vfs_file_info_get_mime_type() theoretically returns a const value,
   but that string is const only until gnome_vfs_file_info_unref();
   Must work for both absolute and relative file names.
*/
gchar*
libbalsa_lookup_mime_type(const gchar * path)
{
#ifdef HAVE_GNOME_VFS
    gchar *mime_type;
    GnomeVFSFileInfo* vi = gnome_vfs_file_info_new();
    gchar* uri;

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
#else
    const gchar *mime_type;
    mime_type =
	gnome_mime_type_or_default_of_file(path, "application/octet-stream");
# ifdef GNOME_MIME_BUG_WORKAROUND
    /* the function above returns for certain files a string which is
       not a proper MIME type, e.g. "PDF document". Surprizingly,
       gnome_mime_type() does not fail in this case. This bug has been
       filed in bugzilla. Still not fixed.
    */
    if(strchr(mime_type, '/') == NULL)
	mime_type =
            gnome_mime_type_or_default(path, "application/octet-stream");
# endif
    return g_strdup(mime_type);
#endif
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
    gchar *retc, *str;
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

    retc = g_strdup(gs->str);
    g_string_free(gs, 1);

    return retc;
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
    *buf = (char *) g_malloc(size + 1);
    if (*buf == NULL) {
	g_string_free(gstr, TRUE);
	return -1;
    }

    strncpy(*buf, gstr->str, size);
    (*buf)[size] = '\0';

    g_string_free(gstr, TRUE);

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
	if (g_strncasecmp(word, ptr, len) == 0)
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
static void
unwrap_rfc2646(gchar * par, gboolean from_screen, GString * result)
{
    gchar *str = NULL;
    gchar **lines, **l;
    gint ql = 0;

    lines = l = g_strsplit(par, "\n", -1);

    while (*lines) {
        gint quote_level;
        gboolean flowed = FALSE;
        gchar *pending = NULL;
        gboolean tagged = FALSE;
        gint tmp;
        gboolean is_sig;

        if (!str) {
            str = *lines++;
            ql = strspn(str, QUOTE_STRING);
        }
        quote_level = ql;
        tmp = str[ql];
        str[ql] = '\0';
        g_string_append(result, str);
        str[ql] = tmp;

        do {
            gchar *dq = &str[ql];

            is_sig = (strcmp(dq, "-- ") == 0);
            if (is_sig && pending)
                break;
            /* destuff if coming off the wire or quoted: */
            if (*dq == ' ' && (!from_screen || quote_level > 0))
                ++dq;
            /* flush any pending line */
            if (pending) {
                if (!tagged) {
                    /* 
                     * insert a tagging character to show whether this
                     * object is a paragraph or a fixed line
                     *
                     * we don't currently use this information, but we
                     * might want to later
                     *
                     * it also serves to separate the quote string from
                     * the text, which might begin with '>'
                     * */
                    g_string_append_c(result, flowed ? 'p' : 'f');
                    tagged = TRUE;
                }
                g_string_append(result, pending);
            }
            /* hold this line as pending */
            pending = dq;
            flowed = (!is_sig) && *dq && dq[strlen(dq) - 1] == ' ';
            if (!flowed || !*lines) {
                str = NULL;
                break;
            }
            str = *lines++;
            ql = strspn(str, QUOTE_STRING);
        } while (ql == quote_level);
        /* 
         * end of paragraph; either:
         * - str is a sig-separator; or
         * - str has a new quote level; or
         * - the last line was fixed, not flowed; or
         * - we ran out of data.
         *
         * if it's a new quote level, we must trim trailing spaces from
         * any pending line
         * */
        if (flowed && (is_sig || ql != quote_level)) {
            gchar *p = pending;
            while (*p)
                p++;
            while (--p >= pending && *p == ' ');
            *++p = '\0';
        }
        if (!tagged)
            g_string_append_c(result, flowed ? 'p' : 'f');
        g_string_append(result, pending);
        g_string_append_c(result, '\n');
    }

    g_strfreev(l);
}
/*
 * we'll use one routine to wrap the paragraphs
 *
 * if the text is going to the wire, use the RFC specs
 * if it's going to the screen, don't space-stuff unquoted lines
 * */
static void
dowrap_rfc2646(gchar * par, gint width, gboolean to_screen,
               gboolean quote, GString * result)
{
    gchar **lines, **l;

    lines = l = g_strsplit(par, "\n", -1);

    /* outer loop over paragraphs */
    while (*lines) {
        gchar *str, *quote_string;
        size_t ql;

        str = *lines++;
        ql = strspn(str, QUOTE_STRING);
        if (quote || ql) {
            if (quote) {
                gint tmp = str[ql];
                str[ql] = '\0';
                quote_string = g_strconcat(QUOTE_STRING, str, NULL);
                str[ql] = tmp;
            } else
                quote_string = g_strndup(str, ql);
        } else
            quote_string = "";
        /* skip over quote string and tag character: */
        str += ql + 1;
        /* one output line per middle loop */
        do {                    /* ... while (*str); */
            gboolean first_word = TRUE;
            gchar *start = str;
            gchar *line_break = start;
            gint len = ql;

            /* start of line: emit quote string */
            g_string_append(result, quote_string);
            if (quote)
                ++len;
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
                    gint tmp;
                    /* allow an overlong first word, otherwise back up
                     * str */
                    if (len > width && !first_word)
                        str = line_break;
                    tmp = *str;
                    *str = '\0';
                    g_string_append(result, start);
                    *str = tmp;
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
            g_string_append_c(result, '\n');
        } while (*str);         /* end of loop over output lines */

        if (*quote_string)
            g_free(quote_string);
    }                           /* end of paragraph */

    g_strfreev(l);
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
    gchar *str;

    unwrap_rfc2646(par, from_screen, result);
    str = result->str;
    len = result->len;
    g_string_free(result, FALSE);

    result = g_string_sized_new(len);
    dowrap_rfc2646(str, width, to_screen, quote, result);
    g_free(str);

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
    gchar *str;

    result = libbalsa_process_text_rfc2646(par, width, from_screen,
                                           to_screen, FALSE);
    g_free(par);
    str = result->str;
    g_string_free(result, FALSE);

    return str;
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
    if (g_strcasecmp(content_type, "text/plain"))
        flowed = FALSE;
    else {
        format = libbalsa_message_body_get_parameter(body, "format");
        flowed = format && (g_strcasecmp(format, "flowed") == 0);
        g_free(format);
    }
    g_free(content_type);

    return flowed;
}

/* libbalsa_set_charset:
   is a thin wrapper around mutt_set_charset() to get rid of mutt dependices
   in balsa.
*/
void mutt_set_charset (char *charset);
const char*
libbalsa_set_charset(const gchar * charset)
{
    const char * old_charset = Charset;
    mutt_set_charset(g_strdup(charset)); /*small leak*/
    return old_charset;
}

/* libbalsa_marshal_POINTER__NONE:
   Marshalling function
*/
typedef gpointer(*GtkSignal_POINTER__NONE) (GtkObject *object, 
					    gpointer user_data);
void
libbalsa_marshal_POINTER__NONE(GtkObject *object, GtkSignalFunc func,
				gpointer func_data, GtkArg *args)
{
    GtkSignal_POINTER__NONE rfunc = (GtkSignal_POINTER__NONE) func;
    gpointer *return_val = GTK_RETLOC_POINTER(args[0]);

    *return_val = (*rfunc) (object, func_data);
}

/* libbalsa_marshal_POINTER__OBJECT:
   Marshalling function 
*/
typedef gpointer(*GtkSignal_POINTER__OBJECT) (GtkObject * object,
					      GtkObject * parm,
					      gpointer user_data);

void
libbalsa_marshal_POINTER__OBJECT(GtkObject * object, GtkSignalFunc func,
				 gpointer func_data, GtkArg * args)
{
    GtkSignal_POINTER__OBJECT rfunc;
    gpointer *return_val;

    return_val = GTK_RETLOC_POINTER(args[1]);
    rfunc = (GtkSignal_POINTER__OBJECT) func;
    *return_val = (*rfunc) (object, GTK_VALUE_OBJECT(args[0]), func_data);
}

/* libbalsa_marshall_POINTER__POINTER_POINTER:
   Marshalling function
*/
typedef gpointer(*GtkSignal_POINTER__POINTER_POINTER) (GtkObject *object,
						       gpointer param1,
						       gpointer param2,
						       gpointer user_data);
void
libbalsa_marshall_POINTER__POINTER_POINTER(GtkObject *object, 
                                           GtkSignalFunc func,
					   gpointer func_data, GtkArg *args)
{
    GtkSignal_POINTER__POINTER_POINTER rfunc;
    gpointer *return_val;

    return_val = GTK_RETLOC_POINTER(args[2]);
    rfunc = (GtkSignal_POINTER__POINTER_POINTER) func;
    *return_val = (*rfunc) (object, GTK_VALUE_POINTER(args[0]), 
                            GTK_VALUE_POINTER(args[1]), func_data);
}

/* libbalsa_marshall_POINTER__INT_POINTER:
   Marshalling function
*/
typedef gpointer(*GtkSignal_POINTER__INT_POINTER) (GtkObject *object,
                                                   int param1,
                                                   gpointer param2,
                                                   gpointer user_data);
void
libbalsa_marshal_POINTER__INT_POINTER(GtkObject *object, 
                                      GtkSignalFunc func,
                                      gpointer func_data, GtkArg *args)
{
    GtkSignal_POINTER__INT_POINTER rfunc;
    gpointer *return_val;

    return_val = GTK_RETLOC_POINTER(args[2]);
    rfunc = (GtkSignal_POINTER__INT_POINTER) func;
    *return_val = (*rfunc) (object, GTK_VALUE_INT(args[0]), 
                            GTK_VALUE_POINTER(args[1]), func_data);
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
	if (g_strcasecmp(de->d_name, ".") == 0 ||
	    g_strcasecmp(de->d_name, "..") == 0)
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

