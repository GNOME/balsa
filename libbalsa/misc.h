/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* vim:set ts=4 sw=4 ai et: */
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

#ifdef BALSA_USE_THREADS
#include <pthread.h>
pthread_t libbalsa_get_main_thread(void);
#endif /* BALSA_USE_THREADS */

#include <stdio.h>
#include <gtk/gtk.h>

#if ENABLE_ESMTP
#include <auth-client.h>
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
    JAPAN,              /* euc-jp */
    KOREA,              /* euc-kr */
    LIBBALSA_NUM_CODESETS
};

gchar *libbalsa_lookup_mime_type(const gchar * path);
gchar *libbalsa_make_string_from_list(const GList *);

size_t libbalsa_readfile(FILE * fp, char **buf);
size_t libbalsa_readfile_nostat(FILE * fp, char **buf);

gchar *libbalsa_get_hostname(void);
gchar *libbalsa_get_domainname(void);
gchar *libbalsa_urlencode(const gchar* str);

gboolean libbalsa_find_word(const gchar * word, const gchar * str);
void libbalsa_wrap_string(gchar * str, int width);
GString *libbalsa_process_text_rfc2646(gchar * par, gint width,
                                       gboolean from_screen,
                                       gboolean to_screen,
                                       gboolean quote);
gchar *libbalsa_wrap_rfc2646(gchar * par, gint width,
                             gboolean from_screen, gboolean to_screen);
void libbalsa_wrap_view(GtkTextView * view, gint length);
void libbalsa_unwrap_buffer(GtkTextBuffer * buffer, GtkTextIter * iter,
                            gint lines);

const char* libbalsa_set_charset(const gchar * charset);
const char* libbalsa_set_send_charset(const gchar * charset);

gboolean libbalsa_delete_directory_contents(const gchar *path);
gchar *libbalsa_truncate_string(const gchar * str, gint length, gint dots);
gchar *libbalsa_expand_path(const gchar *path);
void libbalsa_contract_path(gchar *path);
void libbalsa_mktemp(gchar * name);
gboolean libbalsa_utf8_sanitize(gchar ** text, gboolean fallback,
                                LibBalsaCodeset codeset, gchar const **target);
void libbalsa_insert_with_url(GtkTextBuffer * buffer,
                              const char *chars,
                              GtkTextTag * tag,
                              void (*callback) (GtkTextBuffer *,
                                                GtkTextIter *,
                                                const gchar *,
                                                gpointer),
                              gpointer callback_data);


#endif				/* __LIBBALSA_MISC_H__ */
