/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2005 Stuart Parmenter and others,
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

#include <string.h>
#include "libbalsa-conf.h"

#if !BALSA_USE_G_KEY_FILE

#define BALSA_CONFIG_PREFIX "balsa/"    /* FIXME */

/* 
 * Call @func for each section name that begins with @prefix.
 * @func is called with arguments:
 *   const gchar * @key		the section;
 *   const gchar * @value	the trailing part of the section name,
 *   				following the @prefix;
 *   gpointer @data		the @data passed in.
 * Iteration terminates when @func returns TRUE.
 */
void
libbalsa_conf_foreach_group(const gchar * prefix,
                            LibBalsaConfForeachFunc func, gpointer data)
{
    gsize pref_len;
    void *iterator;
    gchar *key;

    pref_len = strlen(prefix);
    iterator = gnome_config_init_iterator_sections(BALSA_CONFIG_PREFIX);
    while ((iterator = gnome_config_iterator_next(iterator, &key, NULL))) {
        if (strncmp(key, prefix, pref_len) == 0
            && func(key, key + pref_len, data)) {
            g_free(key);
            g_free(iterator);
            break;
        }
        g_free(key);
    }
}

void
libbalsa_conf_push_group(const char *group)
{
    gchar *prefix = g_strconcat(BALSA_CONFIG_PREFIX, group, "/", NULL);
    gnome_config_push_prefix(prefix);
    g_free(prefix);
}

void
libbalsa_conf_remove_group_(const char *group, gboolean priv)
{
    gchar *prefix = g_strconcat(BALSA_CONFIG_PREFIX, group, "/", NULL);
    gnome_config_clean_section_(prefix, priv);
    g_free(prefix);
}

gboolean
libbalsa_conf_has_group(const char *group)
{
    gchar *prefix = g_strconcat(BALSA_CONFIG_PREFIX, group, "/", NULL);
    gboolean retval = gnome_config_has_section(prefix);
    g_free(prefix);
    return retval;
}

#else                           /* BALSA_USE_G_KEY_FILE */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#include "libbalsa.h"
#include "misc.h"

static GKeyFile *lbc_key_file;
static gchar *lbc_key_file_name;
static guint lbc_key_file_changes;
static GKeyFile *lbc_key_file_priv;
static gchar *lbc_key_file_priv_name;
static guint lbc_key_file_priv_changes;
static GSList *lbc_groups;

#define BALSA_KEY_FILE "config"

static gchar *
lbc_readfile(const gchar * filename)
{
    FILE *fp;
    gchar *buf;
    gchar **split;

    fp = fopen(filename, "r");
    libbalsa_readfile(fp, &buf);
    fclose(fp);

    split = g_strsplit(buf, "\\\\ ", 0);
    g_free(buf);
    buf = g_strjoinv("\\ ", split);
    g_strfreev(split);

    return buf;
}

#define DEBUG TRUE

static void
lbc_init(void)
{
    GError *error = NULL;

    lbc_key_file = g_key_file_new();
    g_key_file_set_list_separator(lbc_key_file, ' ');
    lbc_key_file_name =
        g_build_filename(g_get_home_dir(), ".balsa", BALSA_KEY_FILE, NULL);
    libbalsa_assure_balsa_dir();
    if (!g_key_file_load_from_file
        (lbc_key_file, lbc_key_file_name, G_KEY_FILE_NONE, &error)) {
        gchar *filename;
        gchar *buf;

#if DEBUG
        g_message("Could not load config from \"%s\": %s",
                  lbc_key_file_name, error->message);
	g_message("Trying ~/.gnome2.");
#endif /* DEBUG */
        g_error_free(error);
        error = NULL;

        filename =
            g_build_filename(g_get_home_dir(), ".gnome2", "balsa", NULL);
        buf = lbc_readfile(filename);
        g_key_file_load_from_data(lbc_key_file, buf, -1,
                                  G_KEY_FILE_NONE, &error);
        g_free(buf);
        if (error) {
#if DEBUG
            g_message("Cannot load key file from file \"%s\": %s",
                      filename, error->message);
#endif /* DEBUG */
            g_error_free(error);
            error = NULL;
        }
        g_free(filename);
    }

    lbc_key_file_priv = g_key_file_new();
    g_key_file_set_list_separator(lbc_key_file_priv, ' ');
    lbc_key_file_priv_name =
        g_build_filename(g_get_home_dir(), ".balsa",
                         BALSA_KEY_FILE "-private", NULL);
    if (!g_key_file_load_from_file
        (lbc_key_file_priv, lbc_key_file_priv_name, G_KEY_FILE_NONE,
         &error)) {
        gchar *filename;
        gchar *buf;

#if DEBUG
        g_message("Could not load private config from \"%s\": %s",
                  lbc_key_file_priv_name, error->message);
	g_message("Trying ~/.gnome2_private.");
#endif /* DEBUG */
        g_error_free(error);
        error = NULL;

        filename =
            g_build_filename(g_get_home_dir(), ".gnome2_private", "balsa",
                             NULL);
        buf = lbc_readfile(filename);
        lbc_key_file_priv = g_key_file_new();
        g_key_file_set_list_separator(lbc_key_file_priv, ' ');
        g_key_file_load_from_data(lbc_key_file_priv, buf, -1,
                                  G_KEY_FILE_NONE, &error);
        g_free(buf);
        if (error) {
#if DEBUG
            g_message("Cannot load private key file from file \"%s\": %s",
                      filename, error->message);
#endif /* DEBUG */
            g_error_free(error);
        }
        g_free(filename);
    }
}

#ifdef BALSA_USE_THREADS
static GStaticRecMutex lbc_mutex = G_STATIC_REC_MUTEX_INIT;

static void
lbc_lock(void)
{
    g_static_rec_mutex_lock(&lbc_mutex);
    if (!lbc_key_file)
        lbc_init();
}

static void
lbc_unlock(void)
{
    g_static_rec_mutex_unlock(&lbc_mutex);
}
#else                           /* BALSA_USE_THREADS */
#define lbc_lock() lbc_init()
#define lbc_unlock()
#endif                          /* BALSA_USE_THREADS */

/* 
 * Call @func for each group that begins with @prefix.
 * @func is called with arguments:
 *   const gchar * @key		the group;
 *   const gchar * @value	the trailing part of the group name,
 *   				following the @prefix;
 *   gpointer @data		the @data passed in.
 * Iteration terminates when @func returns TRUE.
 */
void
libbalsa_conf_foreach_group(const gchar * prefix,
                            LibBalsaConfForeachFunc func, gpointer data)
{
    gchar **groups, **group;
    gsize pref_len = strlen(prefix);

    lbc_lock();

    groups = g_key_file_get_groups(lbc_key_file, NULL);
    for (group = groups; *group; group++) {
        if (g_str_has_prefix(*group, prefix)
            && func(*group, *group + pref_len, data))
            break;
    }
    g_strfreev(groups);

    lbc_unlock();
}

void
libbalsa_conf_push_group(const gchar * group)
{

    lbc_lock();                 /* Will be held until prefix is popped. */

    lbc_groups = g_slist_prepend(lbc_groups, g_strdup(group));
}

void
libbalsa_conf_pop_group(void)
{
    g_free(lbc_groups->data);
    lbc_groups = g_slist_delete_link(lbc_groups, lbc_groups);

    lbc_unlock();               /* Held since prefix was pushed. */
}

void
libbalsa_conf_remove_group_(const char *group, gboolean priv)
{
    lbc_lock();
    g_key_file_remove_group(priv ? lbc_key_file_priv : lbc_key_file, group,
                            NULL);
    priv ? ++lbc_key_file_priv_changes : ++lbc_key_file_changes;
    lbc_unlock();
}

gboolean
libbalsa_conf_has_group(const char *group)
{
    return (g_key_file_has_group(lbc_key_file, group) ||
            g_key_file_has_group(lbc_key_file_priv, group));
}

void
libbalsa_conf_clean_key(const char *key)
{
    g_key_file_remove_key(lbc_key_file, lbc_groups->data, key, NULL);
    g_key_file_remove_key(lbc_key_file_priv, lbc_groups->data, key, NULL);
    ++lbc_key_file_changes;
    ++lbc_key_file_priv_changes;
}

void
libbalsa_conf_set_bool_(const char *path, gboolean value, gboolean priv)
{
    g_key_file_set_boolean(priv ? lbc_key_file_priv : lbc_key_file,
                           lbc_groups->data, path, value);
    priv ? ++lbc_key_file_priv_changes : ++lbc_key_file_changes;
}

static gchar *
lbc_get_key(const char *path, const char **defval)
{
    const gchar *equals;
    gchar *key;

    equals = strchr(path, '=');
    key = equals ? g_strndup(path, equals - path) : g_strdup(path);
    if (defval)
        *defval = equals ? ++equals : NULL;

    return key;
}

gboolean
libbalsa_conf_get_bool_with_default_(const char *path, gboolean * def,
                                     gboolean priv)
{
    gchar *key;
    const gchar *defval;
    gboolean retval;
    GError *error = NULL;

    key = lbc_get_key(path, &defval);
    retval =
        g_key_file_get_boolean(priv ? lbc_key_file_priv : lbc_key_file,
                               lbc_groups->data, key, &error);
    if (error) {
        g_error_free(error);
        if (defval)
            retval = strcmp(defval, "true") == 0;
    }

    if (def)
        *def = error != NULL;

    return retval;
}

gint
libbalsa_conf_get_int_with_default_(const char *path,
                                    gboolean * def, gboolean priv)
{
    gchar *key;
    const gchar *defval;
    gint retval;
    GError *error = NULL;

    key = lbc_get_key(path, &defval);
    retval =
        g_key_file_get_integer(priv ? lbc_key_file_priv : lbc_key_file,
                               lbc_groups->data, key, &error);
    if (error) {
        g_error_free(error);
        if (defval)
            retval = g_ascii_strtoull(defval, NULL, 10);
    }

    if (def)
        *def = error != NULL;

    return retval;
}

void
libbalsa_conf_set_int_(const char *path, int value, gboolean priv)
{
    g_key_file_set_integer(priv ? lbc_key_file_priv : lbc_key_file,
                           lbc_groups->data, path, value);
    priv ? ++lbc_key_file_priv_changes : ++lbc_key_file_changes;
}

void
libbalsa_conf_set_string_(const char *path, const char *value,
                          gboolean priv)
{
    g_key_file_set_string(priv ? lbc_key_file_priv : lbc_key_file,
                          lbc_groups->data, path, value);
    priv ? ++lbc_key_file_priv_changes : ++lbc_key_file_changes;
}

gchar *
libbalsa_conf_get_string_with_default_(const char *path, gboolean * def,
                                       gboolean priv)
{
    gchar *key;
    const gchar *defval;
    gchar *retval;
    GError *error = NULL;

    key = lbc_get_key(path, &defval);
    retval =
        g_key_file_get_string(priv ? lbc_key_file_priv : lbc_key_file,
                              lbc_groups->data, key, &error);
    if (error) {
        g_error_free(error);
        retval = g_strdup(defval);
    }
    g_free(key);

    if (def)
        *def = error != NULL;

    return retval;
}

void
libbalsa_conf_set_vector(const char *path, int argc,
                         const char *const argv[])
{
    g_key_file_set_string_list(lbc_key_file, lbc_groups->data, path, argv,
                               argc);
    ++lbc_key_file_changes;
}

void
libbalsa_conf_get_vector_with_default_(const char *path, gint * argcp,
                                       char ***argvp, gboolean * def,
                                       gboolean priv)
{
    GError *error = NULL;
    guint len;

    *argvp =
        g_key_file_get_string_list(priv ? lbc_key_file_priv : lbc_key_file,
                                   lbc_groups->data, path, &len, &error);
    *argcp = len;
    if (error)
        g_error_free(error);

    if (def)
        *def = error != NULL;
}

void
libbalsa_conf_drop_all(void)
{
    lbc_lock();
    g_key_file_free(lbc_key_file);
    lbc_key_file = NULL;
    lbc_key_file_changes = 0;
    g_key_file_free(lbc_key_file_priv);
    lbc_key_file_priv = NULL;
    lbc_key_file_priv_changes = 0;
    lbc_unlock();
}

static void
lbc_sync(GKeyFile * key_file, const gchar * filename)
{
    gchar *buf;
    gsize len;
    GError *error = NULL;
    gint fd;

    buf = g_key_file_to_data(key_file, &len, &error);
    if (error) {
#if DEBUG
        g_message("Failed to sync key file: %s", error->message);
#endif /* DEBUG */
        g_error_free(error);
        return;
    }

    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd >= 0) {
        write(fd, buf, len);
        close(fd);
    } else
#if DEBUG
        g_message("Failed to rewrite key file \"%s\".", filename);
#endif /* DEBUG */
    g_free(buf);
}

void
libbalsa_conf_sync(void)
{
    lbc_lock();
    if (lbc_key_file_changes) {
        lbc_sync(lbc_key_file, lbc_key_file_name);
        lbc_key_file_changes = 0;
    }
    if (lbc_key_file_priv_changes) {
        lbc_sync(lbc_key_file_priv, lbc_key_file_priv_name);
        lbc_key_file_priv_changes = 0;
    }
    lbc_unlock();
}

#endif                          /* BALSA_USE_G_KEY_FILE */
