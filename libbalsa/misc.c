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

/* The routines that go here should depend only on common libraries - so that
   this file can be linked against the address book program balsa-ab
   without introducing extra dependencies. External library
   dependencies should go to libbalsa.c */
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

#ifdef HAVE_GNOME
#include <libgnomevfs/gnome-vfs.h>
#endif

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "misc.h"
#include "html.h"
#include <glib/gi18n.h>

static const gchar *libbalsa_get_codeset_name(const gchar *txt, 
					      LibBalsaCodeset Codeset);
static int getdnsdomainname(char *s, size_t l);

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
#if defined(HAVE_GMIME_2_2_7)
    const gchar *charsets[] = {
        g_strdup("UTF-8"),
        g_strdup(libbalsa_get_codeset_name(NULL, codeset)),
        NULL
    };

    g_mime_set_user_charsets(charsets);
    /* GMime will free the strings. */
#endif                          /* HAVE_GMIME_2_2_7 */

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

#define compare_stat(osb, nsb)  ( (osb.st_dev != nsb.st_dev || osb.st_ino != nsb.st_ino || osb.st_rdev != nsb.st_rdev) ? -1 : 0 )

int 
libbalsa_safe_open (const char *path, int flags, mode_t mode, GError **err)
{
  struct stat osb, nsb;
  int fd;
 
  if ((fd = open (path, flags, mode)) < 0) {
      g_set_error(err, LIBBALSA_ERROR_QUARK, errno,
                  _("Cannot open %s: %s"), path, g_strerror(errno));
      return fd;
  }
 
  /* make sure the file is not symlink */
  if (lstat (path, &osb) < 0 || fstat (fd, &nsb) < 0 ||
      compare_stat(osb, nsb) == -1) {
      close (fd);
      g_set_error(err, LIBBALSA_ERROR_QUARK, errno,
                  _("Cannot open %s: is a symbolic link"), path);
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

void
libbalsa_assure_balsa_dir(void)
{
    gchar* dir = g_strconcat(g_get_home_dir(), "/.balsa", NULL);
    mkdir(dir, S_IRUSR|S_IWUSR|S_IXUSR);
    g_free(dir);
}

/* Some more "guess" functions symmetric to libbalsa_guess_mail_spool()... */

#define POP_SERVER "pop"
#define IMAP_SERVER "mx"
#define LDAP_SERVER "ldap"

static gchar*
qualified_hostname(const char *name)
{
    gchar *domain=libbalsa_get_domainname();

    if(domain) {
	gchar *host=g_strdup_printf("%s.%s", name, domain);
	
	g_free(domain);

	return host;
    } else
	return g_strdup(name);
}


gchar *libbalsa_guess_pop_server()
{
    return qualified_hostname(POP_SERVER);
}

gchar *libbalsa_guess_imap_server()
{
    return qualified_hostname(IMAP_SERVER);
}

gchar *libbalsa_guess_ldap_server()
{
    return qualified_hostname(LDAP_SERVER);
}

gchar *libbalsa_guess_imap_inbox()
{
    gchar *server = libbalsa_guess_imap_server();

    if(server) {
	gchar *url = g_strdup_printf("imap://%s/INBOX", server);
	
	g_free(server);

	return url;
    }

    return NULL;
}

gchar *libbalsa_guess_ldap_base()
{
    gchar *server = libbalsa_guess_ldap_server();

    /* Note: Assumes base dn is "o=<domain name>". Somewhat speculative... */
    if(server) {
	gchar *base=NULL, *domain;

	if((domain=strchr(server, '.')))
	   base = g_strdup_printf("o=%s", domain+1);
	
	g_free(server);

	return base;
    }
    return NULL;
}

gchar *libbalsa_guess_ldap_name()
{
    gchar *base = libbalsa_guess_ldap_base();

    if(base) {
	gchar *name = strchr(base, '=');
	gchar *dir_name = g_strdup_printf(_("LDAP Directory for %s"), 
					  (name?name+1:base));
	g_free(base);

	return dir_name;
    } 

    return NULL;
}

gchar *libbalsa_guess_ldif_file()
{
    int i;
    gchar *ldif;

    static const gchar *guesses[] = {
	"address.ldif",
	".address.ldif",
	"address-book.ldif",
	".address-book.ldif",
	".addressbook.ldif",
	NULL
    };

    for (i = 0; guesses[i] != NULL; i++) {
	ldif =  g_strconcat(g_get_home_dir(), G_DIR_SEPARATOR_S, 
			    guesses[i], NULL);
	if (g_file_test(ldif, G_FILE_TEST_EXISTS))
	     return ldif;
	  
	g_free(ldif);
    }
    return g_strconcat(g_get_home_dir(), G_DIR_SEPARATOR_S, 
			guesses[i], NULL); /* *** Or NULL */
    
}

gboolean
libbalsa_path_is_below_dir(const gchar * path, const gchar * dir)
{
    gsize len;

    if (!path || !dir || !g_str_has_prefix(path, dir))
        return FALSE;

    len = strlen(dir);

    return dir[len - 1] == G_DIR_SEPARATOR || path[len] == G_DIR_SEPARATOR;
}
