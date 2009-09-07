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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "libbalsa-conf.h"

#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <glib/gstdio.h>

#include "libbalsa.h"
#include "misc.h"
#include <glib/gi18n.h>

typedef struct {
    GKeyFile *key_file;
    gchar *path;
    guint changes;
    time_t mtime;
} LibBalsaConf;

static LibBalsaConf lbc_conf;
static LibBalsaConf lbc_conf_priv;
static GSList *lbc_groups;

#define BALSA_KEY_FILE "config"
#define DEBUG FALSE
#define LBC_KEY_FILE(priv) \
    ((priv) ? lbc_conf_priv.key_file : lbc_conf.key_file)
#define LBC_CHANGED(priv) \
    ((priv) ? ++lbc_conf_priv.changes : ++lbc_conf.changes)

static gchar *
lbc_readfile(const gchar * filename)
{
    gchar *buf;
    gchar **split;

    if (!g_file_get_contents(filename, &buf, NULL, NULL)) {
#if DEBUG
        g_message("Failed to read \"%s\"\n", filename);
#endif                          /* DEBUG */
        return NULL;
    }

    split = g_strsplit(buf, "\\\\ ", 0);
    g_free(buf);
    buf = g_strjoinv("\\ ", split);
    g_strfreev(split);

    return buf;
}

static void
lbc_init(LibBalsaConf * conf, const gchar * filename,
         const gchar * old_dir)
{
    struct stat buf;
    GError *error = NULL;
    gint rc;

    if (!conf->path)
        conf->path =
            g_build_filename(g_get_home_dir(), ".balsa", filename, NULL);
    rc = stat(conf->path, &buf);
    if (conf->key_file) {
        if (rc >= 0 && buf.st_mtime <= conf->mtime)
            /* found the config file, and it hasn't been touched since
             * we loaded it */
            return;
    } else {
        conf->key_file = g_key_file_new();
        if (rc < 0)
            /* no config file--must be first time startup */
            return;
    }
    conf->mtime = buf.st_mtime;

    libbalsa_assure_balsa_dir();
    if (!g_key_file_load_from_file
        (conf->key_file, conf->path, G_KEY_FILE_NONE, &error)) {
        gchar *old_path;
        gchar *buf;
        static gboolean warn = TRUE;

        old_path =
            g_build_filename(g_get_home_dir(), old_dir, "balsa", NULL);
#if DEBUG
        g_message("Could not load config from \"%s\":\n %s\n"
                  " trying \"%s\"", conf->path, error->message, old_path);
#endif                          /* DEBUG */
        g_clear_error(&error);

        buf = lbc_readfile(old_path);
        if (buf) {
            /* GnomeConfig used ' ' as the list separator... */
            g_key_file_set_list_separator(conf->key_file, ' ');
            g_key_file_load_from_data(conf->key_file, buf, -1,
                                      G_KEY_FILE_KEEP_COMMENTS, &error);
            g_free(buf);
            /* ...but GKeyFile doesn't handle it properly, so we'll
             * revert to the default ';'. */
            g_key_file_set_list_separator(conf->key_file, ';');
        }
        if (!buf || error) {
#if DEBUG
            g_message("Could not load key file from file \"%s\": %s",
                      old_path,
                      error ? error->message : g_strerror(errno));
#endif                          /* DEBUG */
            g_clear_error(&error);
            warn = FALSE;
        }
        g_free(old_path);
        if (warn)
            libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                 _("Your Balsa configuration "
                                   "is now stored in "
                                   "\"~/.balsa/config\"."));
    }
}

#ifdef BALSA_USE_THREADS
static GStaticRecMutex lbc_mutex = G_STATIC_REC_MUTEX_INIT;

static void
lbc_lock(void)
{
    g_static_rec_mutex_lock(&lbc_mutex);
    lbc_init(&lbc_conf, "config", ".gnome2");
    lbc_init(&lbc_conf_priv, "config-private", ".gnome2_private");
}

static void
lbc_unlock(void)
{
    g_static_rec_mutex_unlock(&lbc_mutex);
}
#else                           /* BALSA_USE_THREADS */
static void
lbc_lock(void)
{
    lbc_init(&lbc_conf, "config", ".gnome2");
    lbc_init(&lbc_conf_priv, "config-private", ".gnome2_private");
}

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

    groups = g_key_file_get_groups(lbc_conf.key_file, NULL);
    for (group = groups; *group; group++) {
        if (g_str_has_prefix(*group, prefix)
            && func(*group, *group + pref_len, data))
            break;
    }
    g_strfreev(groups);

    lbc_unlock();
}

void
libbalsa_conf_foreach_keys(const gchar * group,
			   LibBalsaConfForeachFunc func, gpointer data)
{
    gchar **keys, **key;

    lbc_lock();

    if ((keys = g_key_file_get_keys(lbc_conf.key_file, group, NULL, NULL))) {
	for (key = keys; *key; key++) {
	    gchar * val = g_key_file_get_value(lbc_conf.key_file, group, *key, NULL);
	    if (func(*key, val, data)) {
		g_free(val);
		break;
	    }
	    g_free(val);
	}
	g_strfreev(keys);
    }

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
    g_key_file_remove_group(LBC_KEY_FILE(priv), group, NULL);
    LBC_CHANGED(priv);
    lbc_unlock();
}

gboolean
libbalsa_conf_has_group(const char *group)
{
    return (g_key_file_has_group(lbc_conf.key_file, group) ||
            g_key_file_has_group(lbc_conf_priv.key_file, group));
}

static void
lbc_remove_key(LibBalsaConf * conf, const char *key)
{
    GError *error = NULL;

    g_key_file_remove_key(conf->key_file, lbc_groups->data, key, &error);
    if (error)
        g_error_free(error);
    else
        ++conf->changes;
}

void
libbalsa_conf_clean_key(const char *key)
{
    lbc_lock();
    lbc_remove_key(&lbc_conf, key);
    lbc_remove_key(&lbc_conf_priv, key);
    lbc_unlock();
}

void
libbalsa_conf_set_bool_(const char *path, gboolean value, gboolean priv)
{
    g_key_file_set_boolean(LBC_KEY_FILE(priv), lbc_groups->data, path,
                           value);
    LBC_CHANGED(priv);
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
        g_key_file_get_boolean(LBC_KEY_FILE(priv), lbc_groups->data, key,
                               &error);
    g_free(key);
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
        g_key_file_get_integer(LBC_KEY_FILE(priv), lbc_groups->data, key,
                               &error);
    g_free(key);
    if (error) {
        g_error_free(error);
        if (defval)
            retval = g_ascii_strtoull(defval, NULL, 10);
    }

    if (def)
        *def = error != NULL;

    return retval;
}

gdouble
libbalsa_conf_get_double_with_default_(const char *path,
				       gboolean * def, gboolean priv)
{
    gchar *key;
    const gchar *defval;
    gdouble retval;
    GError *error = NULL;

    key = lbc_get_key(path, &defval);
    retval =
        g_key_file_get_double(LBC_KEY_FILE(priv), lbc_groups->data, key,
			      &error);
    g_free(key);
    if (error) {
        g_error_free(error);
        if (defval)
            retval = g_ascii_strtod(defval, NULL);
    }

    if (def)
        *def = error != NULL;

    return retval;
}

void
libbalsa_conf_set_int_(const char *path, int value, gboolean priv)
{
    g_key_file_set_integer(LBC_KEY_FILE(priv), lbc_groups->data, path,
                           value);
    LBC_CHANGED(priv);
}

void
libbalsa_conf_set_double_(const char *path, double value, gboolean priv)
{
    g_key_file_set_double(LBC_KEY_FILE(priv), lbc_groups->data, path,
			  value);
    LBC_CHANGED(priv);
}

void
libbalsa_conf_set_string_(const char *path, const char *value,
                          gboolean priv)
{
    g_key_file_set_string(LBC_KEY_FILE(priv), lbc_groups->data, path,
                          value ? value : "");
    LBC_CHANGED(priv);
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
        g_key_file_get_string(LBC_KEY_FILE(priv), lbc_groups->data, key,
                              &error);
    g_free(key);
    if (error) {
        g_error_free(error);
        retval = g_strdup(defval);
    }

    if (def)
        *def = error != NULL;

    return retval;
}

void
libbalsa_conf_set_vector(const char *path, int argc,
                         const char *const argv[])
{
    g_key_file_set_string_list(lbc_conf.key_file, lbc_groups->data, path,
                               argv, argc);
    ++lbc_conf.changes;
}

void
libbalsa_conf_get_vector_with_default(const char *path, gint * argcp,
                                      char ***argvp, gboolean * def)
{
    GError *error = NULL;
    gsize len;

    *argvp =
        g_key_file_get_string_list(lbc_conf.key_file, lbc_groups->data,
                                   path, &len, &error);
    *argcp = len;
    if (error)
        g_error_free(error);

    if (def)
        *def = error != NULL;
}

static void
lbc_drop_all(LibBalsaConf * conf)
{
    g_key_file_free(conf->key_file);
    conf->key_file = NULL;
    g_free(conf->path);
    conf->path = NULL;
    conf->changes = 0;
}

void
libbalsa_conf_drop_all(void)
{
    lbc_lock();
    lbc_drop_all(&lbc_conf);
    lbc_drop_all(&lbc_conf_priv);
    lbc_unlock();
}

static void
lbc_sync(LibBalsaConf * conf)
{
    gchar **groups;
    gchar *buf;
    gsize len;
    GError *error = NULL;

    if (!conf->changes)
        return;

    groups = g_key_file_get_groups(conf->key_file, NULL);
    if (*groups) {
        gchar **group;

        for (group = &groups[1]; *group; group++)
            g_key_file_set_comment(conf->key_file, *group, NULL, "", NULL);
    }
    g_strfreev(groups);

    buf = g_key_file_to_data(conf->key_file, &len, &error);
    if (error) {
#if DEBUG
        g_message("Failed to sync config file \"%s\": %s\n"
                  " changes not saved", conf->path, error->message);
#endif                          /* DEBUG */
        g_error_free(error);
        g_free(buf);
        return;
    }

    if (!g_file_set_contents(conf->path, buf, len, &error)) {
        if (error) {
#if DEBUG
            g_message("Failed to rewrite config file \"%s\": %s\n"
                      " changes not saved", conf->path, error->message);
#endif                          /* DEBUG */
            g_error_free(error);
#if DEBUG
        } else {
                g_message("Failed to rewrite config file \"%s\";"
                          " changes not saved", conf->path);
#endif                          /* DEBUG */
        }
    }

    g_free(buf);
}

void
libbalsa_conf_sync(void)
{
    lbc_lock();
    lbc_sync(&lbc_conf);
    lbc_sync(&lbc_conf_priv);
    lbc_unlock();
}
