/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 2020 Peter Bloomfield
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
 *
 * Note: see https://autocrypt.org/level1.html for the Autocrypt specs
 */

#ifndef __LIBBALSA_REGEX_H__
#define __LIBBALSA_REGEX_H__

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#include <glib.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

G_BEGIN_DECLS

typedef pcre2_code LibBalsaRegex;
typedef pcre2_match_data LibBalsaMatchData;

LibBalsaRegex *libbalsa_regex_new           (const gchar        *pattern,
                                             guint               compile_options,
                                             GError            **error);
void           libbalsa_regex_free          (LibBalsaRegex *regex);
gboolean       libbalsa_regex_match         (const LibBalsaRegex *regex,
                                             const gchar         *string,
                                             guint                match_options,
                                             LibBalsaMatchData  **match_data);
gboolean       libbalsa_regex_match_simple  (const gchar         *pattern,
                                             const gchar         *string,
                                             guint                compile_options,
                                             guint                match_options);
gchar        **libbalsa_regex_split         (const LibBalsaRegex *regex,
                                             const gchar         *string,
                                             guint                options);
gboolean       libbalsa_match_data_fetch_pos(LibBalsaMatchData   *match_data,
                                             gint                 match_num,
                                             gint                *start_pos,
                                             gint                *end_pos);
void           libbalsa_match_data_free     (LibBalsaMatchData   *match_data);

G_END_DECLS

#endif	/* __LIBBALSA_REGEX_H__ */
