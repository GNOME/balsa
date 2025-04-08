/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2025 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
 * Author: Copyright (C) Albrecht Dreß <albrecht.dress@posteo.de> 2025
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
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <glib/gi18n.h>
#include "misc.h"
#include "libbalsa.h"
#include "xdg-folders.h"


static gboolean migrate_to_xdg(GList **strange_dirs,
							   GError **error);
static gboolean migrate_config(const gchar *srcdir,
							   GList      **cfg_files,
							   GError     **error);
static GtkWidget *migrate_dialog(gboolean success,
								 GList   *strange_dirs,
								 GError  *error)
	G_GNUC_WARN_UNUSED_RESULT;


gboolean
xdg_config_check(void)
{
	gchar *xdg_cfg_file;
	gboolean result;

	xdg_cfg_file = g_build_filename(g_get_user_config_dir(), "balsa", "config", NULL);
	result = g_file_test(xdg_cfg_file, G_FILE_TEST_IS_REGULAR);
	if (!result) {
		gchar *old_cfg_file;

		old_cfg_file = g_build_filename(g_get_home_dir(), ".balsa", "config", NULL);
		if (g_file_test(old_cfg_file, G_FILE_TEST_IS_REGULAR)) {
			GList *strange_dirs = NULL;
			GError *error = NULL;
			GtkWidget *dialog;

			result = migrate_to_xdg(&strange_dirs, &error);
			dialog = migrate_dialog(result, strange_dirs, error);
			gtk_dialog_run(GTK_DIALOG(dialog));
			gtk_widget_destroy(dialog);
			g_list_free_full(strange_dirs, g_free);
			g_clear_error(&error);
		} else {
			result = TRUE;		/* configuration wizard will be launched */
		}
		g_free(old_cfg_file);
	}
	g_free(xdg_cfg_file);
	return result;
}


/** \brief Migrate files
 *
 * \param[in,out] strange_dirs filled with folder names in $HOME/balsa which may need the user's attention
 * \param[in,out] error location for error
 * \return TRUE on success, FALSE on error
 */
static gboolean
migrate_to_xdg(GList **strange_dirs, GError **error)
{
	/* standard files which shall be moved to $XDG_CONFIG_HOME, not to $XDG_STATE_HOME */
	static const gchar * const cfg_files[] =
		{"accelmap", "config-private", "autocrypt.db", "html-prefs.db", "certificates", NULL};
	/* cache folders which shall be ignored */
	static const gchar * const cache_folders[] =
		{"http-cache", "imap-cache", NULL};
	GList *extra_cfg_files = NULL;
	gchar *srcpath;
	GFile *srcdir;
	GFileEnumerator *srcenum;
	gboolean success = FALSE;

	/* migrate the main config file, and collect non-standard files which should go into $XDG_CONFIG_HOME/balsa */
	srcpath = g_build_filename(g_get_home_dir(), ".balsa", NULL);
	if (!migrate_config(srcpath, &extra_cfg_files, error)) {
		g_list_free_full(extra_cfg_files, g_free);
		g_free(srcpath);
		return success;
	}

	/* process the source folder */
	srcdir = g_file_new_for_path(srcpath);
	g_free(srcpath);

	srcenum = g_file_enumerate_children(srcdir, "standard::*", G_FILE_QUERY_INFO_NONE, NULL, error);
	if (srcenum != NULL) {
		GFile *srcfile;

		/* loop over all files and copy them, ignore cache folders, collect other folders */
		do {
			GFileInfo *srcinfo;

			success = g_file_enumerator_iterate(srcenum, &srcinfo, &srcfile, NULL, error);
			if (success && (srcfile != NULL)) {
				gchar *srcname;

				srcname = g_file_get_basename(srcfile);
				/* $HOME/.balsa/config has already been migrated */
				if ((g_file_info_get_file_type(srcinfo) == G_FILE_TYPE_REGULAR) && (strcmp(srcname, "config") != 0)) {
					gchar *dstname;
					GFile *dstfile;

					if (g_strv_contains(cfg_files, srcname) ||
						(g_list_find_custom(extra_cfg_files, srcname, (GCompareFunc) strcmp) != NULL)) {
						dstname = g_build_filename(g_get_user_config_dir(), "balsa", srcname, NULL);
					} else {
						dstname = g_build_filename(g_get_user_state_dir(), "balsa", srcname, NULL);
					}
					dstfile = g_file_new_for_path(dstname);
					g_free(dstname);
					success = g_file_copy(srcfile, dstfile, G_FILE_COPY_ALL_METADATA, NULL, NULL, NULL, error);
					g_object_unref(dstfile);
				} else if ((g_file_info_get_file_type(srcinfo) == G_FILE_TYPE_DIRECTORY) &&
						   !g_strv_contains(cache_folders, srcname)) {
					*strange_dirs = g_list_prepend(*strange_dirs, g_file_get_path(srcfile));
				} else {
					/* ignore everything else */
				}
				g_free(srcname);
			}
		} while (success && (srcfile != NULL));
		g_object_unref(srcenum);
	}
	g_list_free_full(extra_cfg_files, g_free);

	/* on error remove partially migrated stuff */
	if (!success) {
		gchar *del_dir;

		del_dir = g_build_filename(g_get_user_config_dir(), "balsa", NULL);
		(void) libbalsa_delete_directory(del_dir, NULL);
		g_free(del_dir);
		del_dir = g_build_filename(g_get_user_state_dir(), "balsa", NULL);
		(void) libbalsa_delete_directory(del_dir, NULL);
		g_free(del_dir);
	}

	return success;
}


/** \brief Migrate $HOME/.balsa/config
 *
 * \param[in] srcdir source folder, i.e. $HOME/.balsa
 * \param[in,out] cfg_files list of files mentioned in $HOME/.balsa/config which shall be moved to $XDG_CONFIG_HOME/balsa
 * \param[in,out] error location for error
 * \return TRUE on success, FALSE on error
 *
 * The main config file may contain entries with @em Path in the key and $HOME/.balsa as dirname.  These files shall be moved to
 * $XDG_CONFIG_HOME/balsa instead of $XDG_STATE_HOME/balsa, and the migrated config must be adjusted accordingly.
 */
static gboolean
migrate_config(const gchar *srcdir, GList **cfg_files, GError **error)
{
	GKeyFile *config;
	gchar *srcname;
	gboolean success;

	srcname = g_build_filename(srcdir, "config", NULL);
	config = g_key_file_new();
	success = g_key_file_load_from_file(config, srcname, G_KEY_FILE_KEEP_COMMENTS, error);
	if (success) {
		gchar **groups;
		guint gidx;
		gchar *dstname;

		groups = g_key_file_get_groups(config, NULL);
		for (gidx = 0U; groups[gidx] != NULL; gidx++) {
			gchar **keys;
			guint kidx;

			keys = g_key_file_get_keys(config, groups[gidx], NULL, NULL);		/* call will never fail */
			for (kidx = 0U; keys[kidx] != NULL; kidx++) {
				if (g_str_has_suffix(keys[kidx], "Path")) {
					gchar *value;

					value = g_key_file_get_string(config, groups[gidx], keys[kidx], NULL);		/* call will never fail */
					if ((value != NULL) && (value[0] != '\0')) {
						gchar *path;
						gchar *dirname;

						path = libbalsa_expand_path(value);
						dirname = g_path_get_dirname(path);
						if (strcmp(dirname, srcdir) == 0) {
							gchar *itemname;
							gchar *new_value;

							itemname = g_path_get_basename(path);
							new_value = g_build_filename(g_get_user_config_dir(), "balsa", itemname, NULL);
							g_key_file_set_string(config, groups[gidx], keys[kidx], new_value);
							g_free(new_value);
							*cfg_files = g_list_prepend(*cfg_files, itemname);
						}
						g_free(dirname);
						g_free(path);
					}
					g_free(value);
				}
			}
			g_strfreev(keys);
		}
		g_strfreev(groups);

		/* dump the possibly modified file */
		dstname = g_build_filename(g_get_user_config_dir(), "balsa", "config", NULL);
		success = g_key_file_save_to_file(config, dstname, error);
		g_free(dstname);
	}
	g_key_file_free(config);
	g_free(srcname);
	return success;
}


/** \brief Create the message dialogue for informing the user about the migration result
 *
 * \param[in] success whether the migration was successful or not
 * \param[in] strange_dirs list of uncommon subfolders in $HOME/.balsa which require the user's attention
 * \param[in] error error iff the migration failed
 * \return message dialogue widget
 */
static GtkWidget *
migrate_dialog(gboolean success, GList *strange_dirs, GError *error)
{
	GtkWidget *dialog;

	if (success) {
		GString *message;

		dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL | libbalsa_dialog_flags(),
			GTK_MESSAGE_INFO, GTK_BUTTONS_OK, _("Balsa files migrated"));
		message = g_string_new(NULL);
		g_string_printf(message,
			/* Translators: #1 and #2 folder names returned by g_get_user_*_dir() */
			_("Your personal Balsa files have been migrated into the XDG standard folders “%s/balsa” and “%s/balsa”.\n"),
			  g_get_user_config_dir(), g_get_user_state_dir());
		if (strange_dirs == NULL) {
			g_string_append_printf(message,
				/* Translators: #1 folder name returned by g_get_home_dir() */
				_("Please double-check the configuration. You may then erase the old Balsa folder “%s/.balsa”."), g_get_home_dir());
		} else {
			GList *p;

			g_string_append_printf(message,
				/* Translators: #1 folder name returned by g_get_home_dir() */
				_("However, the following non-standard folders were detected in “%s/.balsa” and have not been moved:\n"),
				g_get_home_dir());
			for (p = strange_dirs; p != NULL; p = p->next) {
				g_string_append_printf(message, "\302\240\342\200\242\302\240%s\n", (const gchar *) p->data);
			}
			g_string_append_printf(message,
				/* Translators: #1 folder name returned by g_get_home_dir() */
				_("Please check and move them manually to appropriate locations before erasing the old Balsa folder “%s/.balsa”."),
				g_get_home_dir());
		}
		gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message->str);
		(void) g_string_free(message, TRUE);
	} else {
		dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL | libbalsa_dialog_flags(),
			GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, _("Error migrating Balsa files"));
		gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
			/* Translators: #1 and #2 folder names returned by g_get_user_*_dir(); #3 error message */
			_("Migrating your personal Balsa files to the XDG standard folders “%s/balsa” and “%s/balsa” failed:\n%s\n"
			  "Balsa will be terminated. Please check and fix the reason, and try again."),
			  g_get_user_config_dir(), g_get_user_state_dir(), error->message);
	}
	return dialog;
}
