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

#ifndef __LIBBALSA_MISC_H__
#define __LIBBALSA_MISC_H__

#include <stdio.h>
#include <gtk/gtk.h>

#if GTK_CHECK_VERSION(2, 4, 0)
# define NEW_CHARSET_WIDGET TRUE
#endif

#if ENABLE_ESMTP
#include <auth-client.h>
#endif

#ifdef HAVE_PCRE
#  include <pcreposix.h>
#else
#  include <sys/types.h>
#  include <regex.h>
#endif

typedef enum _LibBalsaCodeset LibBalsaCodeset;

enum _LibBalsaCodeset {
    WEST_EUROPE,        /* iso-8859-1 or windows-1252 */
    EAST_EUROPE,        /* iso-8859-2 or windows-1250 */
    SOUTH_EUROPE,       /* iso-8859-3 */
    NORTH_EUROPE,       /* iso-8859-4 */
    CYRILLIC,           /* iso-8859-5 or windows-1251 */
    ARABIC,             /* iso-8859-6 or windows-1256 */
    GREEK,              /* iso-8859-7 or windows-1253 */
    HEBREW,             /* iso-8859-8 or windows-1255 */
    TURKISH,            /* iso-8859-9 or windows-1254 */
    NORDIC,             /* iso-8859-10 */
    THAI,               /* iso-8859-11 */
    BALTIC,             /* iso-8859-13 or windows-1257 */
    CELTIC,             /* iso-8859-14 */
    WEST_EUROPE_EURO,   /* iso-8859-15 */
    RUSSIAN,            /* koi-8r */
    UKRAINE,            /* koi-8u */
    JAPAN,              /* iso-2022-jp */
    KOREA,              /* euc-kr */
    EAST_EUROPE_WIN,    /* windows-1250 */
    CYRILLIC_WIN,       /* windows-1251 */
    GREEK_WIN,          /* windows-1253 */
    HEBREW_WIN,         /* windows-1255 */
    ARABIC_WIN,         /* windows-1256 */
    BALTIC_WIN,         /* windows-1257 */
    LIBBALSA_NUM_CODESETS
};

typedef enum _LibBalsaTextAttribute LibBalsaTextAttribute;
enum _LibBalsaTextAttribute {
    LIBBALSA_TEXT_ESC     = 1 << 0,     /* ESC char(s)     */
    LIBBALSA_TEXT_HI_BIT  = 1 << 1,     /* 8-bit char(s)   */
    LIBBALSA_TEXT_HI_CTRL = 1 << 2,     /* 0x80 - 0x9f     */
    LIBBALSA_TEXT_HI_UTF8 = 1 << 3      /* 8-bit utf-8     */
};

typedef struct _LibBalsaCodesetInfo LibBalsaCodesetInfo;
struct _LibBalsaCodesetInfo {
    const gchar *label;
    const gchar *std;
    const gchar *win;
};
extern LibBalsaCodesetInfo libbalsa_codeset_info[];
#if NEW_CHARSET_WIDGET
GtkWidget *libbalsa_charset_button_new(void);
#endif                          /* NEW_CHARSET_WIDGET */
LibBalsaTextAttribute libbalsa_text_attr_string(const gchar * string);
LibBalsaTextAttribute libbalsa_text_attr_file(const gchar * filename);
const gchar *libbalsa_file_get_charset(const gchar * filename);

gchar *libbalsa_lookup_mime_type(const gchar * path);
gchar *libbalsa_make_string_from_list(const GList *);

size_t libbalsa_readfile(FILE * fp, char **buf);
size_t libbalsa_readfile_nostat(FILE * fp, char **buf);

gchar *libbalsa_get_hostname(void);
gchar *libbalsa_get_domainname(void);
gchar *libbalsa_urlencode(const gchar* str);
gchar *libbalsa_urldecode(const gchar * str);

gboolean libbalsa_find_word(const gchar * word, const gchar * str);
void libbalsa_wrap_string(gchar * str, int width);
GString *libbalsa_process_text_rfc2646(gchar * par, gint width,
				       gboolean from_screen,
				       gboolean to_screen, gboolean quote,
				       gboolean delsp);
gchar *libbalsa_wrap_rfc2646(gchar * par, gint width,
                             gboolean from_screen, gboolean to_screen,
			     gboolean delsp);
void libbalsa_wrap_view(GtkTextView * view, gint length);
void libbalsa_unwrap_buffer(GtkTextBuffer * buffer, GtkTextIter * iter,
                            gint lines);
void libbalsa_prepare_delsp(GtkTextBuffer * buffer);

const char* libbalsa_set_charset(const gchar * charset);
const char* libbalsa_set_send_charset(const gchar * charset);

gboolean libbalsa_delete_directory_contents(const gchar *path);
gchar *libbalsa_truncate_string(const gchar * str, gint length, gint dots);
gchar *libbalsa_expand_path(const gchar *path);
void libbalsa_contract_path(gchar *path);
gboolean libbalsa_mktempdir(gchar ** name);
LibBalsaCodeset libbalsa_set_fallback_codeset(LibBalsaCodeset codeset);
gboolean libbalsa_utf8_sanitize(gchar ** text, gboolean fallback,
                                gchar const **target);
gboolean libbalsa_utf8_strstr(const gchar *s1,const gchar *s2);
void libbalsa_insert_with_url(GtkTextBuffer * buffer,
                              const char *chars,
                              GtkTextTag * tag,
                              void (*callback) (GtkTextBuffer *,
                                                GtkTextIter *,
                                                const gchar *,
                                                gpointer),
                              gpointer callback_data);
void libbalsa_unwrap_selection(GtkTextBuffer * buffer, regex_t * rex);
gboolean libbalsa_match_regex(const gchar * line, regex_t * rex,
			      guint * count, guint * index);

int libbalsa_safe_open (const char *path, int flags);
int libbalsa_lock_file (const char *path, int fd, int excl, int dot, int timeout);
int libbalsa_unlock_file (const char *path, int fd, int dot);
int libbalsa_safe_rename (const char *src, const char *target);
#ifdef HAVE_GLIB22
#define libbalsa_str_has_prefix(str, prefix) g_str_has_prefix((str), (prefix))
#else				/* HAVE_GLIB22 */
gboolean libbalsa_str_has_prefix(const gchar * str, const gchar * prefix);
#endif				/* HAVE_GLIB22 */

#endif				/* __LIBBALSA_MISC_H__ */
