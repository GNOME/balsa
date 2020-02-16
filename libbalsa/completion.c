/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2020; See the file AUTHORS for a list.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/* GLIB - Library of useful routines for C programming
 * Copyright (C) 1995-1997  Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

/*
 * Modified by the GLib Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GLib Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GLib at ftp://ftp.gtk.org/pub/gtk/.
 */

/*
 * MT safe
 */

/*
 * Borrowed for libbalsa on the deprecation of GCompletion (2010-10-16).
 *
 * Adapted to the LibBalsa namespace to avoid conflicts with the
 * deprecated code.
 */

#include <string.h>

#include <glib.h>
#include "completion.h"

/**
 * SECTION: completion
 * @title: Automatic String Completion
 * @short_description: support for automatic completion using a group
 *                     of target strings
 *
 * #LibBalsaCompletion provides support for automatic completion of a
 * string using any group of target strings. It is typically used for
 * file name completion as is common in many UNIX shells.
 *
 * A #LibBalsaCompletion is created using libbalsa_completion_new().
 * Target items are added with * libbalsa_completion_add_items(),
 * and libbalsa_completion_clear_items(). A completion attempt is
 * requested with libbalsa_completion_complete() or
 * libbalsa_completion_complete_utf8(). When no longer needed, the
 * #LibBalsaCompletion is freed with libbalsa_completion_free().
 *
 * Items in the completion can be simple strings (e.g. filenames), or
 * pointers to arbitrary data structures. If data structures are used
 * you must provide a #LibBalsaCompletionFunc in
 * libbalsa_completion_new(), which retrieves the item's string from the
 * data structure. You can change the way in which strings are compared
 * by setting a different #LibBalsaCompletionStrncmpFunc in
 * libbalsa_completion_set_compare().
 **/

/**
 * LibBalsaCompletion:
 * @items: list of target items (strings or data structures).
 * @func: function which is called to get the string associated with a
 *        target item. It is %NULL if the target items are strings.
 * @prefix: the last prefix passed to libbalsa_completion_complete() or
 *          libbalsa_completion_complete_utf8().
 * @cache: the list of items which begin with @prefix.
 * @strncmp_func: The function to use when comparing strings.  Use
 *                libbalsa_completion_set_compare() to modify this
 *                function.
 *
 * The data structure used for automatic completion.
 **/

/**
 * LibBalsaCompletionFunc:
 * @Param1: the completion item.
 * @Returns: the string corresponding to the item.
 *
 * Specifies the type of the function passed to
 * libbalsa_completion_new(). Itshould return the string corresponding
 * to the given target item. This is used when you use data structures
 * as #LibBalsaCompletion items.
 **/

/**
 * LibBalsaCompletionStrncmpFunc:
 * @s1: string to compare with @s2.
 * @s2: string to compare with @s1.
 * @n: maximal number of bytes to compare.
 * @Returns: an integer less than, equal to, or greater than zero if
 *           the first @n bytes of @s1 is found, respectively, to be
 *           less than, to match, or to be greater than the first @n
 *           bytes of @s2.
 *
 * Specifies the type of the function passed to
 * libbalsa_completion_set_compare(). This is used when you use strings as
 * #LibBalsaCompletion items.
 **/

/**
 * libbalsa_completion_new:
 * @func: the function to be called to return the string representing
 *        an item in the #LibBalsaCompletion, or %NULL if strings are
 *        going to be used as the #LibBalsaCompletion items.
 * @Returns: the new #LibBalsaCompletion.
 *
 * Creates a new #LibBalsaCompletion.
 **/
LibBalsaCompletion *
libbalsa_completion_new(LibBalsaCompletionFunc func)
{
    LibBalsaCompletion *gcomp;

    gcomp = g_new(LibBalsaCompletion, 1);
    gcomp->items = NULL;
    gcomp->cache = NULL;
    gcomp->prefix = NULL;
    gcomp->func = func;
    gcomp->strncmp_func = strncmp;

    return gcomp;
}

/**
 * libbalsa_completion_add_items:
 * @cmp: the #LibBalsaCompletion.
 * @items: the list of items to add.
 *
 * Adds items to the #LibBalsaCompletion.
 **/
void
libbalsa_completion_add_items(LibBalsaCompletion * cmp,
                              GList              * items)
{
    GList *it;

    g_return_if_fail(cmp != NULL);

    /* optimize adding to cache? */
    if (cmp->cache) {
        g_list_free(cmp->cache);
        cmp->cache = NULL;
    }

    if (cmp->prefix) {
        g_free(cmp->prefix);
        cmp->prefix = NULL;
    }

    it = items;
    while (it) {
        cmp->items = g_list_prepend(cmp->items, it->data);
        it = it->next;
    }
}

/**
 * libbalsa_completion_clear_items:
 * @cmp: the #LibBalsaCompletion.
 *
 * Removes all items from the #LibBalsaCompletion.
 **/
void
libbalsa_completion_clear_items(LibBalsaCompletion * cmp)
{
    g_return_if_fail(cmp != NULL);

    g_list_free(cmp->items);
    cmp->items = NULL;
    g_list_free(cmp->cache);
    cmp->cache = NULL;
    g_free(cmp->prefix);
    cmp->prefix = NULL;
}

/**
 * libbalsa_completion_complete:
 * @cmp: the #LibBalsaCompletion.
 * @prefix: the prefix string, typically typed by the user, which is
 *          compared with each of the items.
 * @Returns: the list of items whose strings begin with @prefix. This
 *           should not be changed.
 *
 * Attempts to complete the string @prefix using the #LibBalsaCompletion
 * target items.
 **/
GList *
libbalsa_completion_complete(LibBalsaCompletion * cmp,
                             const gchar        * prefix)
{
    gsize plen, len;
    gboolean done = FALSE;
    GList *list;

    g_return_val_if_fail(cmp != NULL, NULL);
    g_return_val_if_fail(prefix != NULL, NULL);

    len = strlen(prefix);
    if (cmp->prefix && cmp->cache) {
        plen = strlen(cmp->prefix);
        if (plen <= len && !cmp->strncmp_func(prefix, cmp->prefix, plen)) {
            /* use the cache */
            list = cmp->cache;
            while (list) {
                GList *next = list->next;

                if (cmp->strncmp_func(prefix,
                                      cmp->func ? cmp->func(list->
                                                            data) : (gchar
                                                                     *)
                                      list->data, len))
                    cmp->cache = g_list_delete_link(cmp->cache, list);

                list = next;
            }
            done = TRUE;
        }
    }

    if (!done) {
        /* normal code */
        g_list_free(cmp->cache);
        cmp->cache = NULL;
        list = cmp->items;
        while (*prefix && list) {
            if (!cmp->strncmp_func(prefix,
                                   cmp->func ? cmp->func(list->
                                                         data) : (gchar *)
                                   list->data, len))
                cmp->cache = g_list_prepend(cmp->cache, list->data);
            list = list->next;
        }
    }
    if (cmp->prefix) {
        g_free(cmp->prefix);
        cmp->prefix = NULL;
    }
    if (cmp->cache)
        cmp->prefix = g_strdup(prefix);

    return *prefix ? cmp->cache : cmp->items;
}

/**
 * libbalsa_completion_free:
 * @cmp: the #LibBalsaCompletion.
 *
 * Frees all memory used by the #LibBalsaCompletion.
 **/
void
libbalsa_completion_free(LibBalsaCompletion * cmp)
{
    g_return_if_fail(cmp != NULL);

    libbalsa_completion_clear_items(cmp);
    g_free(cmp);
}

/**
 * libbalsa_completion_set_compare:
 * @cmp: a #LibBalsaCompletion.
 * @strncmp_func: the string comparison function.
 *
 * Sets the function to use for string comparisons. The default string
 * comparison function is strncmp().
 **/
void
libbalsa_completion_set_compare(LibBalsaCompletion          * cmp,
                                LibBalsaCompletionStrncmpFunc strncmp_func)
{
    g_return_if_fail(cmp != NULL);
    g_return_if_fail(strncmp_func != NULL);

    cmp->strncmp_func = strncmp_func;
}
