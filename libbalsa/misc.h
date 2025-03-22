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

#ifndef __LIBBALSA_MISC_H__
#define __LIBBALSA_MISC_H__

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#include <stdio.h>
#include <gtk/gtk.h>
#include <gmime/gmime.h>

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

typedef void (*libbalsa_url_cb_t) (GtkTextBuffer *, GtkTextIter *,
				   const gchar *, guint, gpointer);
typedef struct _LibBalsaUrlInsertInfo LibBalsaUrlInsertInfo;
struct _LibBalsaUrlInsertInfo {
    libbalsa_url_cb_t callback;
    gpointer callback_data;
    gboolean buffer_is_flowed;
    GString *ml_url_buffer;
};

#define LIBBALSA_ERROR_QUARK (g_quark_from_static_string("libbalsa"))

extern LibBalsaCodesetInfo libbalsa_codeset_info[];
GtkWidget *libbalsa_charset_button_new(void);
LibBalsaTextAttribute libbalsa_text_attr_string(const gchar * string);
LibBalsaTextAttribute libbalsa_text_attr_file(const gchar * filename);

gchar *libbalsa_get_domainname(void);
#define libbalsa_urlencode(str) (g_uri_escape_string((str), NULL, FALSE))
#define libbalsa_urldecode(str) (g_uri_unescape_string((str), NULL))

gboolean libbalsa_find_word(const gchar * word, const gchar * str);
void libbalsa_wrap_string(gchar * str, int width);
char *libbalsa_wrap_quoted_string(const char *str,
                                  unsigned    width,
                                  GRegex     *quote_regex);
GString *libbalsa_process_text_rfc2646(gchar * par, gint width,
				       gboolean from_screen,
				       gboolean to_screen, gboolean quote,
				       gboolean delsp);
gchar *libbalsa_wrap_rfc2646(gchar * par, gint width,
                             gboolean from_screen, gboolean to_screen,
			     gboolean delsp);
void libbalsa_unwrap_buffer(GtkTextBuffer * buffer, GtkTextIter * iter,
                            gint lines);

gboolean libbalsa_delete_directory(const gchar *path, GError **error);
gchar *libbalsa_expand_path(const gchar *path);
gboolean libbalsa_mktempdir(gchar ** name);
LibBalsaCodeset libbalsa_set_fallback_codeset(LibBalsaCodeset codeset);
gboolean libbalsa_utf8_sanitize(gchar ** text, gboolean fallback,
                                gchar const **target);
gboolean libbalsa_utf8_strstr(const gchar *s1,const gchar *s2);
gboolean libbalsa_insert_with_url(GtkTextBuffer * buffer,
				  const char *chars,
				  guint len,
				  GtkTextTag * tag,
				  LibBalsaUrlInsertInfo *url_info);
void libbalsa_unwrap_selection(GtkTextBuffer * buffer, GRegex * rex);
gboolean libbalsa_match_regex(const gchar * line, GRegex * rex,
			      guint * count, guint * index);

int libbalsa_lock_file (const char *path, int fd, int excl, int dot, int timeout);
int libbalsa_unlock_file (const char *path, int fd, int dot);
int libbalsa_safe_rename (const char *src, const char *target);

gboolean libbalsa_ia_rfc2821_equal(const InternetAddress * a,
				   const InternetAddress * b);

GtkWidget *libbalsa_create_grid(void);
GtkWidget *libbalsa_create_grid_label(const gchar * label, GtkWidget * grid,
                                      gint row);
GtkWidget *libbalsa_create_grid_entry(GtkWidget * grid, GCallback func,
                                      gpointer data, gint row,
                                      const gchar * initval,
                                      GtkWidget * hotlabel);
GtkWidget *libbalsa_create_grid_check(const gchar * label, GtkWidget * grid,
                                      gint row, gboolean initval);
GtkSizeGroup *libbalsa_create_size_group(GtkWidget * chooser);
GtkWidget *libbalsa_create_wrap_label(const gchar *text,
									  gboolean     markup);
void libbalsa_entry_config_passwd(GtkEntry *entry);

void libbalsa_assure_balsa_dirs(void);
gchar *libbalsa_guess_ldap_base(void);
gchar *libbalsa_guess_ldap_name(void);

gboolean libbalsa_path_is_below_dir(const gchar * path, const gchar * dir);

gchar *libbalsa_size_to_gchar(guint64 length);

gchar * libbalsa_text_to_html(const gchar * title, const gchar * body, const gchar * lang);
GString * libbalsa_html_encode_hyperlinks(GString * paragraph);
gchar *libbalsa_font_string_to_css(const gchar * font_string, const gchar * name);

GMimeParserOptions *libbalsa_parser_options(void);

/* Some margin helpers */
#define HIG_PADDING 6
static inline void
libbalsa_set_hmargins(GtkWidget *widget, int margin)
{
    gtk_widget_set_margin_start(widget, margin);
    gtk_widget_set_margin_end(widget, margin);
}
static inline void
libbalsa_set_vmargins(GtkWidget *widget, int margin)
{
    gtk_widget_set_margin_top(widget, margin);
    gtk_widget_set_margin_bottom(widget, margin);
}
static inline void
libbalsa_set_margins(GtkWidget *widget, int margin)
{
    libbalsa_set_hmargins(widget, margin);
    libbalsa_set_vmargins(widget, margin);
}

GtkWidget * libbalsa_add_mnemonic_button_to_box(const gchar *markup,
                                                GtkWidget   *box,
                                                GtkAlign     align);

gchar * libbalsa_pixmap_finder(const gchar * filename);

#endif				/* __LIBBALSA_MISC_H__ */
