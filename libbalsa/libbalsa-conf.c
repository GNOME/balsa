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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "libbalsa-conf.h"

#include <string.h>
#include <sys/stat.h>
#include <glib/gstdio.h>

#include "libbalsa.h"
#include <glib/gi18n.h>

typedef struct {
    GKeyFile *key_file;
    gchar *path;
    guint changes;
    gboolean private;
} LibBalsaConf;

static LibBalsaConf lbc_conf;
static LibBalsaConf lbc_conf_priv;
static GSList *lbc_groups;

static gchar * libbalsa_rot(const gchar * pass)
	G_GNUC_WARN_UNUSED_RESULT;

#define LBC_KEY_FILE(priv) \
    ((priv) ? lbc_conf_priv.key_file : lbc_conf.key_file)
#define LBC_CHANGED(priv) \
    ((priv) ? ++lbc_conf_priv.changes : ++lbc_conf.changes)

static void
lbc_init(LibBalsaConf * conf, const gchar * filename,
         gboolean private)
{
    GError *error = NULL;

    conf->private = private;
    conf->path = g_build_filename(g_get_user_config_dir(), "balsa", filename, NULL);
    conf->key_file = g_key_file_new();
    if (!g_file_test(conf->path, G_FILE_TEST_IS_REGULAR)) {
        /* no config file--must be first time startup */
        return;
    }

    if (!g_key_file_load_from_file
        (conf->key_file, conf->path, G_KEY_FILE_NONE, &error)) {
        g_debug("Could not load config from “%s”: %s;",
                  conf->path, error->message);
        g_clear_error(&error);
    }
}

static GRecMutex lbc_mutex;

static void
lbc_lock(void)
{
    static gboolean initialized = FALSE;

    g_rec_mutex_lock(&lbc_mutex);
    if (!initialized) {
        lbc_init(&lbc_conf, "config", FALSE);
        lbc_init(&lbc_conf_priv, "config-private", TRUE);
        initialized = TRUE;
    }
}

static void
lbc_unlock(void)
{
    g_rec_mutex_unlock(&lbc_mutex);
}

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
    lbc_lock();

    if (lbc_conf.key_file != NULL) {
        gchar **groups, **group;
        gsize pref_len = strlen(prefix);

        groups = g_key_file_get_groups(lbc_conf.key_file, NULL);

        for (group = groups; *group; group++) {
            if (g_str_has_prefix(*group, prefix)
                && func(*group, *group + pref_len, data))
                break;
        }

        g_strfreev(groups);
    }

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

gboolean
libbalsa_conf_has_key(const gchar * key)
{
    /* g_key_file_has_key returns FALSE on error, but that is OK */
    return (g_key_file_has_key(lbc_conf.key_file, lbc_groups->data,
                               key, NULL) ||
            g_key_file_has_key(lbc_conf_priv.key_file, lbc_groups->data,
                                  key, NULL));
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

void
libbalsa_conf_private_set_string(const gchar *path, const gchar *value, gboolean obfuscated)
{
	if (obfuscated && (value != NULL)) {
		gchar *obf;

		obf = libbalsa_rot(value);
		libbalsa_conf_set_string_(path, obf, TRUE);
		memset(obf, 0, strlen(obf));
		g_free(obf);
	} else {
		libbalsa_conf_set_string_(path, value, TRUE);
	}
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

gchar *
libbalsa_conf_private_get_string(const gchar *path, gboolean obfuscated)
{
	gchar *result;

	result = libbalsa_conf_get_string_with_default_(path, NULL, TRUE);
	if (obfuscated && (result != NULL)) {
		gchar *deob;

		deob = libbalsa_rot(result);
		memset(result, 0, strlen(result));
		g_free(result);
		result = deob;
	}
	return result;
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
	if (conf->changes > 0U) {
		GError *error = NULL;

		if (g_key_file_save_to_file(conf->key_file, conf->path, &error)) {
			g_debug("Sync'ed config file “%s”", conf->path);
			conf->changes = 0U;
			if (conf->private) {
				g_chmod(conf->path, S_IRUSR | S_IWUSR);
			}
		} else {
			if (error != NULL) {
				g_warning("Failed to rewrite config file “%s”: %s; changes not saved", conf->path, error->message);
				g_error_free(error);
			} else {
				g_warning("Failed to rewrite config file “%s”; changes not saved", conf->path);
			}
		}
	}
}

static guint lbc_sync_idle_id;
G_LOCK_DEFINE_STATIC(lbc_sync_idle_id);

void
libbalsa_conf_sync(void)
{
    G_LOCK(lbc_sync_idle_id);
    g_debug("%s id %d, will be cleared", __func__, lbc_sync_idle_id);
    if (lbc_sync_idle_id) {
        g_source_remove(lbc_sync_idle_id);
        lbc_sync_idle_id = 0;
    }
    G_UNLOCK(lbc_sync_idle_id);
    lbc_lock();
    lbc_sync(&lbc_conf);
    lbc_sync(&lbc_conf_priv);
    lbc_unlock();
}

static gboolean
libbalsa_conf_sync_idle_cb(gpointer data)
{
    libbalsa_conf_sync();

    return FALSE;
}

void
libbalsa_conf_queue_sync(void)
{
    G_LOCK(lbc_sync_idle_id);
    g_debug("%s id %d, will be set if zero", __func__, lbc_sync_idle_id);
    if (!lbc_sync_idle_id)
        lbc_sync_idle_id =
            g_idle_add((GSourceFunc) libbalsa_conf_sync_idle_cb, NULL);
    g_debug("%s id now %d", __func__, lbc_sync_idle_id);
    G_UNLOCK(lbc_sync_idle_id);
}


/* libbalsa_rot:
   return rot13'ed string.
*/
static gchar *
libbalsa_rot(const gchar * pass)
{
    gchar *buff;
    gint len = 0, i = 0;

    /*PKGW: let's do the assert() BEFORE we coredump... */

    len = strlen(pass);
    buff = g_strdup(pass);

    for (i = 0; i < len; i++) {
	if ((buff[i] <= 'M' && buff[i] >= 'A')
	    || (buff[i] <= 'm' && buff[i] >= 'a'))
	    buff[i] += 13;
	else if ((buff[i] <= 'Z' && buff[i] >= 'N')
		 || (buff[i] <= 'z' && buff[i] >= 'n'))
	    buff[i] -= 13;
    }
    return buff;
}

#if defined(HAVE_LIBSECRET)
gboolean
libbalsa_conf_use_libsecret(void)
{
	static gboolean use_libsecret = TRUE;
	static gint check_done = 0;

	if (g_atomic_int_get(&check_done) == 0) {
		const gchar *libsecret_env;

		libsecret_env = g_getenv("BALSA_DISABLE_LIBSECRET");
		if ((libsecret_env != NULL) && (atoi(libsecret_env) == 1)) {
			use_libsecret = FALSE;
			g_info("Secret Service is disabled, credentials are stored in the config file.");
		}
		g_atomic_int_set(&check_done, 1);
	}
	return use_libsecret;
}
#endif		/* HAVE_LIBSECRET */
