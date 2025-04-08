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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/* The routines that go here should depend only on common libraries - so that
   this file can be linked against the address book program balsa-ab
   without introducing extra dependencies. External library
   dependencies should go to libbalsa.c */
#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "misc.h"

#define _SVID_SOURCE           1
#include <ctype.h>
#include <errno.h>
#ifdef USE_FCNTL
# include <fcntl.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "html.h"
#include <glib/gi18n.h>
#include <glib/gstdio.h>

static const gchar *permanent_prefixes[] = {
    BALSA_DATA_PREFIX,
    "src",
    "."
};

static const gchar *libbalsa_get_codeset_name(const gchar *txt, 
					      LibBalsaCodeset Codeset);
#ifndef HAVE_STRUCT_UTSNAME_DOMAINNAME
static int getdnsdomainname(char *s, size_t l);

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
#endif                          /* !HAVE_STRUCT_UTSNAME_DOMAINNAME */

gchar *
libbalsa_get_domainname(void)
{
    struct utsname utsname;
#ifdef HAVE_STRUCT_UTSNAME_DOMAINNAME

    uname(&utsname);

    return g_strdup(utsname.domainname);
#else                           /* HAVE_STRUCT_UTSNAME_DOMAINNAME */
    char domainname[256]; /* arbitrary length */
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
#endif                          /* HAVE_STRUCT_UTSNAME_DOMAINNAME */
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


/* Delete the contents of a directory and the directory itself.
   Return TRUE if everything was OK.
   If FALSE is returned then error will be set to some useful value.
*/
gboolean
libbalsa_delete_directory(const gchar *path, GError **error)
{
	GDir *dir;
	gboolean result;

	g_return_val_if_fail(path != NULL, FALSE);
	dir = g_dir_open(path, 0, error);
	if (dir == NULL) {
		result = FALSE;
	} else {
		const gchar *item;

		result = TRUE;
		item = g_dir_read_name(dir);
		while (result && (item != NULL)) {
			gchar *full_path;

			full_path = g_build_filename(path, item, NULL);
			if (g_file_test(full_path, G_FILE_TEST_IS_DIR)) {
				result = libbalsa_delete_directory(full_path, error);
			} else {
				if (g_unlink(full_path) != 0) {
					g_set_error(error, LIBBALSA_ERROR_QUARK, errno, _("cannot delete “%s”: %s"), full_path, g_strerror(errno));
					result = FALSE;
				}
			}
			g_free(full_path);
			item = g_dir_read_name(dir);
		}
		g_dir_close(dir);
		if (g_rmdir(path) != 0) {
			g_set_error(error, LIBBALSA_ERROR_QUARK, errno, _("cannot delete “%s”: %s"), path, g_strerror(errno));
			result = FALSE;
		}
	}
	return result;
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
    g_return_val_if_fail(s != NULL, FALSE);
    *s = g_build_filename(g_get_tmp_dir(), "balsa-tmpdir-XXXXXX", NULL);
    return g_mkdtemp_full(*s, 0700) != NULL;
}

/* libbalsa_set_fallback_codeset: sets the codeset for incorrectly
 * encoded characters.
 * Returns the previous codeset. */
static LibBalsaCodeset sanitize_fallback_codeset = WEST_EUROPE;
LibBalsaCodeset
libbalsa_set_fallback_codeset(LibBalsaCodeset codeset)
{
    LibBalsaCodeset ret = sanitize_fallback_codeset;
    const gchar *charsets[] = {
        "UTF-8",
        libbalsa_get_codeset_name(NULL, codeset),
        NULL
    };

    g_mime_parser_options_set_fallback_charsets(libbalsa_parser_options(), charsets);

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
	g_debug("conversion %s -> utf8 failed: %s", use_enc,
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
    {N_("West European"),       /* WEST_EUROPE          */
     "iso-8859-1", "windows-1252"} ,
    {N_("East European"),       /* EAST_EUROPE          */
     "iso-8859-2", "windows-1250"} ,
    {N_("South European"),      /* SOUTH_EUROPE         */
     "iso-8859-3"} ,
    {N_("North European"),      /* NORTH_EUROPE         */
     "iso-8859-4"} ,
    {N_("Cyrillic"),            /* CYRILLIC             */
     "iso-8859-5", "windows-1251"} ,
    {N_("Arabic"),              /* ARABIC               */
     "iso-8859-6", "windows-1256"} ,
    {N_("Greek"),               /* GREEK                */
     "iso-8859-7", "windows-1253"} ,
    {N_("Hebrew"),              /* HEBREW               */
     "iso-8859-8", "windows-1255"} ,
    {N_("Turkish"),             /* TURKISH              */
     "iso-8859-9", "windows-1254"} ,
    {N_("Nordic"),              /* NORDIC               */
     "iso-8859-10"} ,
    {N_("Thai"),                /* THAI                 */
     "iso-8859-11"} ,
    {N_("Baltic"),              /* BALTIC               */
     "iso-8859-13", "windows-1257"} ,
    {N_("Celtic"),              /* CELTIC               */
     "iso-8859-14"} ,
    {N_("West European (euro)"),  /* WEST_EUROPE_EURO     */
     "iso-8859-15"} ,
    {N_("Russian"),             /* RUSSIAN              */
     "koi-8r"} ,
    {N_("Ukrainian"),           /* UKRAINE              */
     "koi-8u"} ,
    {N_("Japanese"),            /* JAPAN                */
     "iso-2022-jp"} ,
    {N_("Korean"),              /* KOREA                */
     "euc-kr"} ,
    {N_("East European"),       /* EAST_EUROPE_WIN      */
     "windows-1250"} ,
    {N_("Cyrillic"),            /* CYRILLIC_WIN         */
     "windows-1251"} ,
    {N_("Greek"),               /* GREEK_WIN            */
     "windows-1253"} ,
    {N_("Hebrew"),              /* HEBREW_WIN           */
     "windows-1255"} ,
    {N_("Arabic"),              /* ARABIC_WIN           */
     "windows-1256"} ,
    {N_("Baltic"),              /* BALTIC_WIN           */
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

    combo_box = gtk_combo_box_text_new();
    locale_charset = g_mime_locale_charset();

    for (n = 0; n < LIBBALSA_NUM_CODESETS; n++) {
        LibBalsaCodesetInfo *info = &libbalsa_codeset_info[n];
        gchar *tmp = g_strdup_printf("%s (%s)", _(info->label), info->std);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_box), tmp);
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
lb_text_attr(const gchar * text, gssize len, gboolean * has_esc,
             gboolean * has_hi_bit, gboolean * has_hi_ctrl)
{
    if (len < 0)
        len = strlen(text);

    for (; --len >= 0; text++) {
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
    gboolean is_utf8;

    lb_text_attr(string, -1, &has_esc, &has_hi_bit, &has_hi_ctrl);
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

/* Return text attributes of the contents of a file;
 * filename is in URI form;
 * returns -1 on error. */
LibBalsaTextAttribute
libbalsa_text_attr_file(const gchar * filename)
{
    GFile *file;
    GFileInputStream *stream;
    gssize bytes;
    LibBalsaTextAttribute attr;
    gchar buf[80];
    gchar *new_chars = buf;
    gboolean has_esc = FALSE;
    gboolean has_hi_bit = FALSE;
    gboolean has_hi_ctrl = FALSE;
    gboolean is_utf8 = TRUE;

    file = g_file_new_for_uri(filename);
    stream = g_file_read(file, NULL, NULL);
    g_object_unref(file);
    if (!stream)
        return -1;

    while ((bytes = g_input_stream_read(G_INPUT_STREAM(stream), new_chars,
                               (sizeof buf) - (new_chars - buf), NULL,
                               NULL)) > 0) {
	gboolean test_bits;

	test_bits = !has_esc || !has_hi_bit || !has_hi_ctrl;
	if (!test_bits && !is_utf8)
	    break;

        if (test_bits)
            lb_text_attr(new_chars, bytes, &has_esc, &has_hi_bit,
                         &has_hi_ctrl);

        if (is_utf8) {
            const gchar *end;

            bytes += new_chars - buf;
            new_chars = buf;
            if (!g_utf8_validate(buf, bytes, &end)) {
                bytes -= (end - buf);
                if (g_utf8_get_char_validated(end, bytes) ==
                    (gunichar) (-1)) {
                    is_utf8 = FALSE;
                } else {
                    /* copy remaining bytes to start of buffer */
                    memmove(buf, end, bytes);
                    new_chars += bytes;
                }
            }
        }
    }

    g_object_unref(stream);

    if (bytes < 0)
        return -1;

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
	    g_debug("%s(): fcntl errno %d.", __func__, errno);
    if (errno != EAGAIN && errno != EACCES)
	{
	    libbalsa_information
		(LIBBALSA_INFORMATION_MESSAGE, _("fcntl failed: %s."), g_strerror(errno));
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
			 _("Waiting for fcntl lock… %d"), ++attempt);
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
	    libbalsa_information(LIBBALSA_INFORMATION_WARNING, "flock: %s", g_strerror(errno));
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
	    	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
	    		_("Timeout exceeded while attempting flock lock!"));
	    r = -1;
	    break;
	}
 
    prev_sb = sb;
 
    libbalsa_information(LIBBALSA_INFORMATION_MESSAGE,
    	_("Waiting for flock attempt… %d"), ++attempt);
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


/* libbalsa_ia_rfc2821_equal
   compares two addresses according to rfc2821: local-part@domain is equal,
   if the local-parts are case sensitive equal, but the domain case-insensitive
*/
gboolean
libbalsa_ia_rfc2821_equal(const InternetAddress * a,
			  const InternetAddress * b)
{
    const gchar *a_atptr, *b_atptr;
    const gchar *a_addr, *b_addr;
    gint a_atpos, b_atpos;

    if (!INTERNET_ADDRESS_IS_MAILBOX(a) || !INTERNET_ADDRESS_IS_MAILBOX(b))
        return FALSE;

    /* first find the "@" in the two addresses */
    a_addr = INTERNET_ADDRESS_MAILBOX(a)->addr;
    b_addr = INTERNET_ADDRESS_MAILBOX(b)->addr;
    a_atptr = strchr(a_addr, '@');
    b_atptr = strchr(b_addr, '@');
    if (!a_atptr || !b_atptr)
        return FALSE;
    a_atpos = a_atptr - a_addr;
    b_atpos = b_atptr - b_addr;

    /* now compare the strings */
    if (!a_atpos || !b_atpos || a_atpos != b_atpos || 
        strncmp(a_addr, b_addr, a_atpos) ||
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
libbalsa_create_grid(void)
{
    GtkWidget *grid;

    grid = gtk_grid_new();

    gtk_grid_set_row_spacing(GTK_GRID(grid), LB_PADDING);
    gtk_grid_set_column_spacing(GTK_GRID(grid), LB_PADDING);

    return grid;
}

/* create_label:
   Create a label and add it to a table in the first column of given row,
   setting the keyval to found accelerator value, that can be later used 
   in create_entry.
*/
GtkWidget *
libbalsa_create_grid_label(const gchar * text, GtkWidget * grid, gint row)
{
    GtkWidget *label;

    label = gtk_label_new_with_mnemonic(text);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0F);

    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);

    return label;
}

/** \brief Create a properly aligned label with line wrap
 *
 * \param text label text
 * \param markup TRUE if the label text contains markup
 * \return the new label widget
 *
 * Create a new label, enable word wrap, and set set xalign property.
 */
GtkWidget *
libbalsa_create_wrap_label(const gchar *text,
						   gboolean     markup)
{
    GtkWidget *label;

    if (markup) {
    	label = gtk_label_new(NULL);
    	gtk_label_set_markup(GTK_LABEL(label), text);
    } else {
    	label = gtk_label_new(text);
    }

    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0F);

    return label;
}

/* create_check:
   creates a checkbox with a given label and places them in given array.
*/
GtkWidget *
libbalsa_create_grid_check(const gchar * text, GtkWidget * grid, gint row,
                           gboolean initval)
{
    GtkWidget *check_button;

    check_button = gtk_check_button_new_with_mnemonic(text);

    gtk_grid_attach(GTK_GRID(grid), check_button, 0, row, 2, 1);

    if (initval)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button),
                                     TRUE);

    return check_button;
}

/* Create a text entry and add it to the table */
GtkWidget *
libbalsa_create_grid_entry(GtkWidget * grid, GCallback changed_func,
                           gpointer data, gint row, const gchar * initval,
                           GtkWidget * hotlabel)
{
    GtkWidget *entry;

    entry = gtk_entry_new();
    gtk_widget_set_hexpand(entry, TRUE);

    gtk_grid_attach(GTK_GRID(grid), entry, 1, row, 1, 1);

    if (initval)
        gtk_entry_set_text(GTK_ENTRY(entry), initval);

    gtk_label_set_mnemonic_widget(GTK_LABEL(hotlabel), entry);

    /* Watch for changes... */
    if (changed_func)
        g_signal_connect(entry, "changed", changed_func, data);

    return entry;
}

/* Create a GtkSizeGroup and add to it any GtkLabel packed in a GtkGrid
 * inside the chooser widget; size_group will be unreffed when the
 * chooser widget is finalized. */
static void
lb_create_size_group_func(GtkWidget * widget, gpointer data)
{
    if (GTK_IS_LABEL(widget) &&
        GTK_IS_GRID(gtk_widget_get_parent(widget)))
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

static void
on_view_pwd_icon_press(GtkWidget *widget, gpointer data)
{
	gboolean visible = gtk_entry_get_visibility(GTK_ENTRY(widget));

	gtk_entry_set_visibility(GTK_ENTRY(widget), !visible);
	gtk_entry_set_icon_from_icon_name(GTK_ENTRY(widget), GTK_ENTRY_ICON_SECONDARY,
		visible ? "view-reveal-symbolic" : "view-conceal-symbolic");
}

void
libbalsa_entry_config_passwd(GtkEntry *entry)
{
	g_return_if_fail(GTK_IS_ENTRY(entry));

	gtk_entry_set_visibility(entry, FALSE);
	g_object_set(entry, "input-purpose", GTK_INPUT_PURPOSE_PASSWORD, NULL);
	gtk_entry_set_icon_from_icon_name(entry, GTK_ENTRY_ICON_SECONDARY, "view-reveal-symbolic");
	gtk_entry_set_icon_activatable(entry, GTK_ENTRY_ICON_SECONDARY, TRUE);
	g_signal_connect(entry, "icon-press", G_CALLBACK (on_view_pwd_icon_press), NULL);
}

void
libbalsa_assure_balsa_dirs(void)
{
	const gchar *(*dir_fn[3])(void) = { g_get_user_config_dir, g_get_user_state_dir, g_get_user_cache_dir };
	guint n;

	for (n = 0U; n < G_N_ELEMENTS(dir_fn); n++) {
		gchar *folder;

		folder = g_build_filename(dir_fn[n](), "balsa", NULL);
		if (g_mkdir_with_parents(folder, S_IRWXU) == -1) {
			g_error("cannot create folder %s: %s", folder, g_strerror(errno));
		}
		g_free(folder);
	}
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


gchar *libbalsa_guess_ldap_server()
{
    return qualified_hostname(LDAP_SERVER);
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


gboolean
libbalsa_path_is_below_dir(const gchar * path, const gchar * dir)
{
    gsize len;

    if (!path || !dir || !g_str_has_prefix(path, dir))
        return FALSE;

    len = strlen(dir);

    return dir[len - 1] == G_DIR_SEPARATOR || path[len] == G_DIR_SEPARATOR;
}

#define LIBBALSA_RADIX 1024.0
#define MAX_WITHOUT_SUFFIX 9999

gchar *
libbalsa_size_to_gchar(guint64 size)
{
    if (size > MAX_WITHOUT_SUFFIX) {
        gdouble displayed_size = (gdouble) size;
        gchar *s, suffix[] = "KMGT";

        for (s = suffix; /* *s != '\0' */; s++) {
            displayed_size /= LIBBALSA_RADIX;
            if (displayed_size < 9.995)
                return g_strdup_printf("%.2f%c", displayed_size, *s);
            if (displayed_size < 99.95)
                return g_strdup_printf("%.1f%c", displayed_size, *s);
            if (displayed_size < LIBBALSA_RADIX - 0.5 || !s[1])
                return g_strdup_printf("%" G_GUINT64_FORMAT "%c",
                                       ((guint64) (displayed_size + 0.5)),
                                       *s);
        }
    }

    return g_strdup_printf("%" G_GUINT64_FORMAT, size);
}

/*
 * libbalsa_font_string_to_css: construct CSS text corresponding to a
 * PangoFontDescription string.
 *
 * font_string: the PangoFontDescription string;
 * name:        the name of the widget to which the font is to be applied.
 *
 * Returns:     a newly allocated string with the CSS text;
 *              g_free() when no longer needed.
 */

gchar *
libbalsa_font_string_to_css(const gchar * font_string,
                            const gchar * name)
{
    PangoFontDescription *desc;
    PangoFontMask mask;
    GString *string;

    g_return_val_if_fail(font_string != NULL, NULL);
    g_return_val_if_fail(name != NULL, NULL);

    desc = pango_font_description_from_string(font_string);
    mask = pango_font_description_get_set_fields(desc);

    string = g_string_new(NULL);
    g_string_printf(string, "#%s {\n", name);

    if (mask & PANGO_FONT_MASK_FAMILY) {
        g_string_append_printf(string, "font-family: \"%s\";\n",
                               pango_font_description_get_family(desc));
    }
    if (mask & PANGO_FONT_MASK_STYLE) {
        PangoStyle style; /* An enum with default values,
                           * so use it to index the array. */
        static const gchar *styles[] = {
            "normal",
            "oblique",
            "italic"
        };

        style = pango_font_description_get_style(desc);
        g_string_append_printf(string, "font-style: %s;\n", styles[style]);
    }
    if (mask & PANGO_FONT_MASK_VARIANT) {
        PangoVariant variant; /* An enum with default values,
                               * so use it to index the array. */
        static const gchar *variants[] = {
            "normal",
            "small-caps"
        };

        variant = pango_font_description_get_variant(desc);
        g_string_append_printf(string, "font-variant: %s;\n", variants[variant]);
    }
    if (mask & PANGO_FONT_MASK_WEIGHT) {
        PangoWeight weight; /* An enum with weight values, so use the value. */

        weight = pango_font_description_get_weight(desc);
        g_string_append_printf(string, "font-weight: %d;\n", weight);
    }
    if (mask & PANGO_FONT_MASK_STRETCH) {
        PangoStretch stretch; /* An enum with default values,
                               * so use it to index the array. */
        static const gchar *stretches[] = {
            "ultra-condensed",
            "extra-condensed",
            "condensed",
            "semi-condensed",
            "normal",
            "semi-expanded",
            "expanded",
            "extra-expanded",
            "ultra-expanded"
        };

        stretch = pango_font_description_get_stretch(desc);
        g_string_append_printf(string, "font-stretch: %s;\n", stretches[stretch]);
    }
    if (mask & PANGO_FONT_MASK_SIZE) {
        gdouble size;
        const gchar *units;

        size = (gdouble) pango_font_description_get_size(desc) / PANGO_SCALE;
        units = pango_font_description_get_size_is_absolute(desc) ? "px" : "pt";
        g_string_append_printf(string, "font-size: %.1f%s;\n", size, units);
    }
    g_string_append_c(string, '}');

    pango_font_description_free(desc);

    return g_string_free(string, FALSE);
}

GMimeParserOptions *
libbalsa_parser_options(void)
{
    static GMimeParserOptions *parser_options = NULL;

    if (g_once_init_enter(&parser_options)) {
        GMimeParserOptions *tmp = g_mime_parser_options_new();

        g_mime_parser_options_set_rfc2047_compliance_mode(tmp, GMIME_RFC_COMPLIANCE_LOOSE);
        g_once_init_leave(&parser_options, tmp);
    }
    return parser_options;
}

/*
 * libbalsa_add_mnemonic_button_to_box
 *
 * Create a button widget and add it to a GtkBox with the same
 * look as a regular button in a GtkButtonBox; returns the button.
 *
 * markup: mnemonic text for the button;
 * box:    the box;
 * align:  how to align the button in its allocated space.
 *
 * To replace a GtkButtonBox with the default GTK_BUTTONBOX_EDGE style,
 * align should be GTK_ALIGN_START for the first button, GTK_ALIGN_END
 * for the last button, and GTK_ALIGN_CENTER for other buttons.
 * To replace a GtkButtonBox with style GTK_BUTTONBOX_SPREAD,
 * align should be GTK_ALIGN_CENTER for all buttons;
 * To replace a GtkButtonBox with style GTK_BUTTONBOX_EXPAND,
 * align should be GTK_ALIGN_FILL for all buttons.
 *
 * In the EDGE case with more than three buttons, and in the SPREAD case,
 * the spacing is not identical to GtkButtonBox's.
 */

/* Margin to increase the width of the button, to approximate the look
 * of a GtkButtonBox; may be a pixel or two too small: */
#define LIBBALSA_LABEL_MARGIN 12

#define LIBBALSA_SIZE_GROUP_KEY "libbalsa-size-group-key"

GtkWidget *
libbalsa_add_mnemonic_button_to_box(const gchar *markup,
                                    GtkWidget   *box,
                                    GtkAlign     align)
{
    GtkSizeGroup *size_group;
    GtkWidget *label;
    GtkWidget *button;

    size_group = g_object_get_data(G_OBJECT(box), LIBBALSA_SIZE_GROUP_KEY);
    if (size_group == NULL) {
        size_group = gtk_size_group_new(GTK_SIZE_GROUP_BOTH);
        g_object_set_data_full(G_OBJECT(box), LIBBALSA_SIZE_GROUP_KEY, size_group, g_object_unref);
    }

    label = gtk_label_new(NULL);
    gtk_label_set_markup_with_mnemonic(GTK_LABEL(label), markup);
    gtk_widget_set_margin_start(label, LIBBALSA_LABEL_MARGIN);
    gtk_widget_set_margin_end(label, LIBBALSA_LABEL_MARGIN);
    gtk_widget_show(label);
    gtk_size_group_add_widget(size_group, label);

    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button), label);
    gtk_widget_set_hexpand(button, TRUE);
    gtk_widget_set_halign(button, align);
    gtk_widget_show(button);

    gtk_container_add(GTK_CONTAINER(box), button);

    return button;
}

/* filename is the filename (naw!)
 * We ignore proper slashing of names. Ie, /prefix//pixmaps//file won't be caught.
 */
gchar *
libbalsa_pixmap_finder(const gchar  * filename)
{
    gchar *cat;
    guint i;

    g_return_val_if_fail(filename, NULL);

    for (i = 0; i < G_N_ELEMENTS(permanent_prefixes); i++) {
	cat = g_build_filename(permanent_prefixes[i], "pixmaps", filename, NULL);

	if (g_file_test(cat, G_FILE_TEST_IS_REGULAR))
	    return cat;

	g_free(cat);
    }

    cat = g_build_filename("images", "pixmaps", NULL);
    if (g_file_test(cat, G_FILE_TEST_IS_REGULAR))
        return cat;
    g_free(cat);

    g_warning("Cannot find expected pixmap file “%s”", filename);

    return NULL;
}
