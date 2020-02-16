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
 * Borrowed for libbalsa on the deprecation of GCompletion (2010-10-16).
 *
 * Adapted to the LibBalsa namespace to avoid conflicts with the
 * deprecated code.
 */

G_BEGIN_DECLS

typedef struct _LibBalsaCompletion LibBalsaCompletion;

typedef gchar *(*LibBalsaCompletionFunc) (gpointer);

/* LibBalsaCompletion
 */

typedef gint (*LibBalsaCompletionStrncmpFunc) (const gchar * s1,
                                               const gchar * s2,
                                               gsize         n);

struct _LibBalsaCompletion {
    GList                        *items;
    LibBalsaCompletionFunc        func;

    gchar                        *prefix;
    GList                        *cache;
    LibBalsaCompletionStrncmpFunc strncmp_func;
};

LibBalsaCompletion *
libbalsa_completion_new          (LibBalsaCompletionFunc func);

void
libbalsa_completion_add_items    (LibBalsaCompletion * cmp,
                                  GList              * items);

void
libbalsa_completion_clear_items  (LibBalsaCompletion * cmp);

GList *
libbalsa_completion_complete     (LibBalsaCompletion * cmp,
                                  const gchar        * prefix);

void
libbalsa_completion_set_compare  (LibBalsaCompletion * cmp,
                                  LibBalsaCompletionStrncmpFunc
                                                       strncmp_func);

void
libbalsa_completion_free         (LibBalsaCompletion * cmp);

G_END_DECLS
