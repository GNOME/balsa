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
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
#	include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include "regex.h"

#include "libbalsa.h"

#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "regex"

/*
 * Wrappers for PCRE2 regex functions, as replacments for GRegex
 * functions.
 *
 * GRegex is based on version 1 of PCRE, and will not be ported to
 * PCRE2. Soon to be deprecated (as of 2020-09-04):
 * https://gitlab.gnome.org/GNOME/glib/-/issues/1085
 */

/*
 * libbalsa_regex_new:
 * pattern: the regular expression
 * compile_options: PCRE2 compile options for the regular expression, or 0
 * error: return location for a GError
 *
 * Compiles the regular expression to an internal form.
 *
 * Returns: a LibBalsaRegex structure, or NULL if an error occurred.
 * Free with libbalsa_regex_free().
 */
LibBalsaRegex *
libbalsa_regex_new(const gchar *pattern,
                   guint        compile_options,
                   GError     **error)
{
    pcre2_code *regex;
    gint errorcode;
    PCRE2_SIZE erroroffset;

    compile_options |= PCRE2_UTF | PCRE2_NO_UTF_CHECK;
    regex = pcre2_compile((PCRE2_SPTR) pattern, PCRE2_ZERO_TERMINATED, compile_options,
                          &errorcode, &erroroffset, NULL);

    /* In pcre2.h, error codes begin at 101: */
    if (errorcode > 100 && error != NULL) {
        gchar errorbuf[120];

        pcre2_get_error_message(errorcode, (PCRE2_UCHAR *) errorbuf, sizeof errorbuf);
        g_set_error(error, LIBBALSA_REGEX_ERROR, LIBBALSA_REGEX_ERROR_COMPILE,
                    "Error while compiling regular expression “%s”: %s",
                    pattern, errorbuf);
    }

    return regex;
}

/*
 * libbalsa_regex_free:
 * regex: a LibBalsaRegex
 *
 * Frees the LibBalsaRegex by passing it to pcre2_code_free().
 * Note that pcre2_code_free() does nothing if regex is NULL.
 */
void
libbalsa_regex_free(LibBalsaRegex *regex)
{
    pcre2_code_free(regex);
}

/*
 * libbalsa_regex_match:
 * regex: a LibBalsaRegex structure from libbalsa_regex_new()
 * string: the string to scan for matches; must be valid UTF-8
 * match_options: PCRE2 match options
 * match_data: pointer to location where to store the LibBalsaMatchData,
 *   used to get information on the match, or NULL.
 *
 * Scans for a match in string for the pattern in regex.
 *
 * Returns: TRUE if the string matched, FALSE otherwise
 */
gboolean
libbalsa_regex_match(const LibBalsaRegex *regex,
                     const gchar         *string,
                     guint                match_options,
                     LibBalsaMatchData  **match_data)
{
    LibBalsaMatchData *data;
    gint status;

    g_return_val_if_fail(regex != NULL, FALSE);
    g_return_val_if_fail(string != NULL, FALSE);

    match_options |= PCRE2_NO_UTF_CHECK;
    data = pcre2_match_data_create_from_pattern(regex, NULL);

    status = pcre2_match(regex,
                         (PCRE2_SPTR) string,
                         PCRE2_ZERO_TERMINATED,
                         0,
                         match_options,
                         data,
                         NULL);

    if (match_data != NULL)
        *match_data = data;
    else
        pcre2_match_data_free(data);

    return status > 0;
}

/*
 * libbalsa_regex_match_simple:
 * pattern: the regular expression
 * string: the string to scan for matches
 * compile_options: PCRE2 compile options for the regular expression, or 0
 * match_options: PCRE2 match options, or 0
 *
 * Scans for a match in string for pattern.
 *
 * This function is equivalent to libbalsa_regex_match() but it does not
 * require to compile the pattern with libbalsa_regex_new(), avoiding some
 * lines of code when you need just to do a match without extracting
 * substrings, capture counts, and so on.
 *
 * If this function is to be called on the same pattern more than
 * once, it's more efficient to compile the pattern once with
 * libbalsa_regex_new() and then use libbalsa_regex_match().
 *
 * Returns: TRUE if the string matched, FALSE otherwise
 */
gboolean
libbalsa_regex_match_simple(const gchar *pattern,
                            const gchar *string,
                            guint        compile_options,
                            guint        match_options)
{
    LibBalsaRegex *regex;
    gboolean match = FALSE;

    g_return_val_if_fail(pattern != NULL, FALSE);
    g_return_val_if_fail(string != NULL, FALSE);

    regex = libbalsa_regex_new(pattern, compile_options, NULL);

    if (regex != NULL) {
        match = libbalsa_regex_match(regex, string, match_options, NULL);
        libbalsa_regex_free(regex);
    }

    return match;
}

/*
 * libbalsa_regex_split:
 * regex: a LibBalsaRegex structure
 * string: the string to split with the pattern
 * match_options: PCRE2 match options, or 0
 *
 * Breaks the string on the pattern, and returns an array of the tokens.
 * If the pattern contains capturing parentheses, then the text for each
 * of the substrings will also be returned. If the pattern does not match
 * anywhere in the string, then the whole string is returned as the first
 * token.
 *
 * A pattern that can match empty strings splits string into separate
 * characters wherever it matches the empty string between characters.
 * For example splitting "ab c" using as a separator "\s*", you will get
 * "a", "b" and "c".
 *
 * Returns: a NULL-terminated gchar ** array. Free it using g_strfreev().
 */
gchar **
libbalsa_regex_split(const LibBalsaRegex *regex,
                     const gchar         *string,
                     guint                match_options)
{
    GPtrArray *tokens;
    LibBalsaMatchData *match_data;
    gint status;
    PCRE2_SIZE pos;

    g_return_val_if_fail(regex != NULL, FALSE);
    g_return_val_if_fail(string != NULL, FALSE);

    match_options |= PCRE2_NO_UTF_CHECK;
    match_data = pcre2_match_data_create_from_pattern(regex, NULL);
    tokens = g_ptr_array_new();
    pos = 0;

    while ((status = pcre2_match(regex,
                                 (PCRE2_SPTR) string,
                                 PCRE2_ZERO_TERMINATED,
                                 pos,
                                 match_options,
                                 match_data,
                                 NULL)) > 0) {
        PCRE2_SIZE *ovector;
        gint i;

        ovector = pcre2_get_ovector_pointer(match_data);

        /* Token is the string from pos to the start of the match: */
        g_ptr_array_add(tokens, g_strndup(string + pos, ovector[0] - pos));

        /* Also return any substring matches: */
        for (i = 1; i < status; i++) {
            PCRE2_SIZE start = ovector[2 * i];
            PCRE2_SIZE end   = ovector[2 * i + 1];

            /* end == start means an empty match, which we ignore;
             * end < start is possible if the pattern included \K,
             * which we also ignore, although perhaps we shouldn't */
            if (end > start)
                g_ptr_array_add(tokens, g_strndup(string + start, end - start));
        }

        /* Avoid an infinite loop: */
        if (ovector[1] > pos)
            pos = ovector[1];
        else
            pos = g_utf8_next_char(string + pos) - string;
    }
    pcre2_match_data_free(match_data);

    if (status != PCRE2_ERROR_NOMATCH) {
        g_ptr_array_free(tokens, TRUE);
        return NULL;
    }

    /* Last token is the remainder of the string */
    g_ptr_array_add(tokens, g_strdup(string + pos));
    g_ptr_array_add(tokens, NULL);

    return (gchar **) g_ptr_array_free(tokens, FALSE);
}

/*
 * libbalsa_match_info_fetch_pos:
 * match_data: LibBalsaMatchData structure
 * match_num: number of the sub expression
 * start_pos: pointer to location where to store the start position, or NULL
 * end_pos: pointer to location where to store the end position, or NULL
 *
 * Retrieves the position in bytes of the match_num'th capturing
 * parentheses. 0 is the full text of the match, 1 is the first
 * paren set, 2 the second, and so on.
 *
 * If match_num is a valid sub pattern but it didn't match anything
 * (e.g. sub pattern 1, matching "b" against "(a)?b") then start_pos
 * and end_pos are set to -1 and TRUE is returned.
 *
 * Returns: TRUE if the position was fetched, FALSE otherwise. If
 *   the position cannot be fetched, start_pos and end_pos are left
 *   unchanged
 */
gboolean
libbalsa_match_data_fetch_pos(LibBalsaMatchData *match_data,
                              gint               match_num,
                              gint              *start_pos,
                              gint              *end_pos)
{
    PCRE2_SIZE *ovector;

    g_return_val_if_fail (match_data != NULL, FALSE);
    g_return_val_if_fail (match_num >= 0, FALSE);

    /* make sure the sub expression number they're requesting is less than
     * the total number of sub expressions that were matched. */
    if (match_num >= (gint) pcre2_get_ovector_count(match_data))
        return FALSE;

    ovector = pcre2_get_ovector_pointer(match_data);

    if (start_pos != NULL)
        *start_pos = ovector[2 * match_num];

    if (end_pos != NULL)
        *end_pos = ovector[2 * match_num + 1];

    return TRUE;
}

/*
 * libbalsa_match_data_free:
 * match_data: a LibBalsaMatchData
 *
 * Frees the LibBalsaMatchData by passing it to pcre2_match_data_free().
 * Note that pcre2_code_free() does nothing if match_data is NULL.
 */
void
libbalsa_match_data_free(LibBalsaMatchData *match_data)
{
    pcre2_match_data_free(match_data);
}
