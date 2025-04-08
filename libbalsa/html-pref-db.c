/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2021 Stuart Parmenter and others,
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

#ifdef HAVE_HTML_WIDGET

#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "html"

#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <sqlite3.h>
#include "geometry-manager.h"
#include "html-pref-db.h"


enum {
	PREFS_ADDRESS_COLUMN = 0,
	PREFS_PREFER_HTML_COLUMN,
	PREFS_LOAD_EXT_CONTENT,
	PREFS_DB_VIEW_COLUMNS
};


/* Note: the database column prefer_load_img actually indicates if any external content shall be loaded.  The naming is kept for
 * backward compatibility from previous versions where only loading external images automatically could be configured. */
#define DB_SCHEMA								\
	"PRAGMA auto_vacuum = 1;"					\
	"CREATE TABLE html_prefs("					\
		"addr TEXT PRIMARY KEY NOT NULL, "		\
		"prefer_html BOOLEAN DEFAULT 0, "		\
		"prefer_load_img BOOLEAN DEFAULT 0);"
#define NUM_QUERIES			5


static sqlite3 *pref_db = NULL;
static sqlite3_stmt *query[NUM_QUERIES] = { NULL, NULL, NULL, NULL, NULL };
G_LOCK_DEFINE_STATIC(db_mutex);


static gboolean pref_db_check(void);

static gboolean pref_db_get(InternetAddressList *from,
                            int                  col);
static void pref_db_set_ial(InternetAddressList *from,
                            int                  pref_idx,
                            gboolean             value);
static gboolean pref_db_set_name(const gchar *sender,
                                 int          pref_idx,
                                 gboolean     value);

static gboolean popup_menu_cb(GtkWidget *widget,
                              gpointer   user_data);
static void button_press_cb(GtkGestureMultiPress *multi_press_gesture,
                            gint                  n_press,
                            gdouble               x,
                            gdouble               y,
                            gpointer              user_data);
static void popup_menu_real(GtkWidget      *widget,
                            const GdkEvent *event);
static void remove_item_cb(GtkMenuItem G_GNUC_UNUSED *menuitem,
                           gpointer                   user_data);
static void on_prefs_button_toggled(GtkCellRendererToggle *cell_renderer,
                                    gchar                 *path,
                                    gpointer               user_data);

static void html_pref_db_close(void);


gboolean
libbalsa_html_get_prefer_html(InternetAddressList *from)
{
	return pref_db_get(from, 1);
}


gboolean
libbalsa_html_get_load_content(InternetAddressList *from)
{
	return pref_db_get(from, 2);
}


void
libbalsa_html_prefer_set_prefer_html(InternetAddressList *from, gboolean state)
{
	pref_db_set_ial(from, 1, state);
}


void
libbalsa_html_prefer_set_load_content(InternetAddressList *from, gboolean state)
{
	pref_db_set_ial(from, 2, state);
}


void
libbalsa_html_pref_dialog_run(GtkWindow *parent)
{
    GtkDialogFlags flags;
	GtkWidget *dialog;
	GtkWidget *vbox;
	GtkWidget *scrolled_window;
	GtkListStore *model;
	GtkWidget *tree_view;
	GtkGesture *gesture;
	GtkTreeSelection *selection;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	int sqlite_res;

	if (!pref_db_check()) {
		return;
	}

	flags = GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags();
	dialog = gtk_dialog_new_with_buttons(_("HTML preferences"), parent, flags,
		_("_Close"), GTK_RESPONSE_CLOSE, NULL);
	geometry_manager_attach(GTK_WINDOW(dialog), "HTMLPrefsDB");

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2 * HIG_PADDING);
	gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), vbox);
	gtk_widget_set_vexpand(vbox, TRUE);

	scrolled_window = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_set_border_width(GTK_CONTAINER(scrolled_window), 2 * HIG_PADDING);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled_window), GTK_SHADOW_ETCHED_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_widget_set_vexpand(scrolled_window, TRUE);
        gtk_widget_set_valign(scrolled_window, GTK_ALIGN_FILL);
	gtk_container_add(GTK_CONTAINER(vbox), scrolled_window);

	model = gtk_list_store_new(PREFS_DB_VIEW_COLUMNS,
		G_TYPE_STRING,			/* address */
		G_TYPE_BOOLEAN,			/* prefer html over plain text */
		G_TYPE_BOOLEAN);		/* auto-load external content */

	tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));

	gesture = gtk_gesture_multi_press_new(tree_view);
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
	g_signal_connect(gesture, "pressed", G_CALLBACK(button_press_cb), NULL);
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(gesture), GTK_PHASE_CAPTURE);
	g_signal_connect(tree_view, "popup-menu", G_CALLBACK(popup_menu_cb), NULL);

	gtk_container_add(GTK_CONTAINER(scrolled_window), tree_view);
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

	/* add all database items */
	G_LOCK(db_mutex);
	sqlite_res = sqlite3_step(query[4]);
	while (sqlite_res == SQLITE_ROW) {
		GtkTreeIter iter;

		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
			PREFS_ADDRESS_COLUMN, sqlite3_column_text(query[4], 0),
			PREFS_PREFER_HTML_COLUMN, sqlite3_column_int(query[4], 1),
			PREFS_LOAD_EXT_CONTENT, sqlite3_column_int(query[4], 2),
			-1);
		sqlite_res = sqlite3_step(query[4]);
	}
	sqlite3_reset(query[4]);
	G_UNLOCK(db_mutex);

	/* set up the tree view */
	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(_("Sender"), renderer, "text", PREFS_ADDRESS_COLUMN, NULL);
	gtk_tree_view_column_set_sort_column_id(column, PREFS_ADDRESS_COLUMN);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
	gtk_tree_view_column_set_resizable(column, TRUE);

	renderer = gtk_cell_renderer_toggle_new();
	g_object_set_data(G_OBJECT(renderer), "dbcol", GINT_TO_POINTER(PREFS_PREFER_HTML_COLUMN));
	g_signal_connect(renderer, "toggled", G_CALLBACK(on_prefs_button_toggled), model);
	column = gtk_tree_view_column_new_with_attributes(_("Prefer HTML"), renderer, "active", PREFS_PREFER_HTML_COLUMN, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_widget_show_all(vbox);

	renderer = gtk_cell_renderer_toggle_new();
	g_object_set_data(G_OBJECT(renderer), "dbcol", GINT_TO_POINTER(PREFS_LOAD_EXT_CONTENT));
	g_signal_connect(renderer, "toggled", G_CALLBACK(on_prefs_button_toggled), model);
	column = gtk_tree_view_column_new_with_attributes(_("Auto-load external content"), renderer, "active", PREFS_LOAD_EXT_CONTENT,
		NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_widget_show_all(vbox);

	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(model), PREFS_ADDRESS_COLUMN, GTK_SORT_ASCENDING);
	g_object_unref(model);

	g_signal_connect(dialog, "response", G_CALLBACK(gtk_widget_destroy), NULL);
	gtk_widget_show_all(dialog);
}


/** \brief Open and if necessary initialise the HTML preferences database
 *
 * \return TRUE on success
 */
static gboolean
pref_db_check(void)
{
	static const gchar * const prepare_statements[NUM_QUERIES] = {
		"SELECT * FROM html_prefs WHERE addr = LOWER(?)",
		"INSERT INTO html_prefs (addr, prefer_html) VALUES (LOWER(?1), ?2) "
				"ON CONFLICT (addr) DO UPDATE SET prefer_html = ?2",
		"INSERT INTO html_prefs (addr, prefer_load_img) VALUES (LOWER(?1), ?2) "
				"ON CONFLICT (addr) DO UPDATE SET prefer_load_img = ?2",
		"DELETE FROM html_prefs WHERE addr = LOWER(?1)",
		"SELECT addr, prefer_html, prefer_load_img FROM html_prefs ORDER BY addr ASC"
	};
	gboolean result = TRUE;

	G_LOCK(db_mutex);
	if (pref_db == NULL) {
		gchar *db_path;
		gboolean require_init;
		int sqlite_res;

		g_debug("open HTML preferences database");
		db_path = g_build_filename(g_get_user_config_dir(), "balsa", "html-prefs.db", NULL);
		require_init = (g_access(db_path, R_OK + W_OK) != 0);
		sqlite_res = sqlite3_open(db_path, &pref_db);
		if (sqlite_res == SQLITE_OK) {
			guint n;

			/* write the schema if the database is new */
			if (require_init) {
				sqlite_res = sqlite3_exec(pref_db, DB_SCHEMA, NULL, NULL, NULL);
			}

			/* always vacuum the database */
			if (sqlite_res == SQLITE_OK) {
				sqlite_res = sqlite3_exec(pref_db, "VACUUM", NULL, NULL, NULL);
			}

			/* prepare statements */
			for (n = 0U; (sqlite_res == SQLITE_OK) && (n < NUM_QUERIES); n++) {
				sqlite_res = sqlite3_prepare_v2(pref_db, prepare_statements[n], -1, &query[n], NULL);
			}
		}
		G_UNLOCK(db_mutex);

		/* error checks... */
		if (sqlite_res != SQLITE_OK) {
			/* Translators: #1 database path; #2 error message */
			libbalsa_information(LIBBALSA_INFORMATION_ERROR, _("Cannot initialise HTML preferences database “%s”: %s"), db_path,
				sqlite3_errmsg(pref_db));
			html_pref_db_close();
			result = FALSE;
		} else {
			atexit(html_pref_db_close);
		}
		g_free(db_path);
	} else {
		G_UNLOCK(db_mutex);
	}

	return result;
}


/** \brief Get the HTML preferences setting for a sender
 *
 * \param from From: address list, may be NULL or empty
 * \param col 1 prefer HTML, 2 auto-load external content
 * \return the requested setting, FALSE on error, empty address list or missing entry
 */
static gboolean
pref_db_get(InternetAddressList *from, int col)
{
	gboolean result = FALSE;

	if (from != NULL) {
		InternetAddress *sender_address;

		sender_address = internet_address_list_get_address(from, 0);
		if (INTERNET_ADDRESS_IS_MAILBOX(sender_address)) {
			const gchar *sender;

			sender = internet_address_mailbox_get_addr(INTERNET_ADDRESS_MAILBOX(sender_address));
			if ((sender != NULL) && pref_db_check()) {
				G_LOCK(db_mutex);
				if (sqlite3_bind_text(query[0], 1, sender, -1, SQLITE_STATIC) == SQLITE_OK) {
					int sqlite_res;

					sqlite_res = sqlite3_step(query[0]);
					if (sqlite_res == SQLITE_ROW) {
						result = (sqlite3_column_int(query[0], col) != 0);
						sqlite_res = sqlite3_step(query[0]);
					}
					if (sqlite_res != SQLITE_DONE) {
						/* Translators: #1 message sender address; #2 error message */
						libbalsa_information(LIBBALSA_INFORMATION_ERROR, _("Cannot read HTML preferences for “%s”: %s"), sender,
							sqlite3_errmsg(pref_db));
						result = FALSE;
					}
				}
				sqlite3_reset(query[0]);
				G_UNLOCK(db_mutex);
			}
		}
	}

	return result;
}


/** \brief Set the HTML preferences setting for a sender
 *
 * \param from From: address list, must not be NULL
 * \param pref_idx 1 prefer HTML, 2 auto-load external content
 */
static void
pref_db_set_ial(InternetAddressList *from, int pref_idx, gboolean value)
{
	InternetAddress *sender_address;

	sender_address = internet_address_list_get_address(from, 0);
	if (INTERNET_ADDRESS_IS_MAILBOX(sender_address)) {
		const gchar *sender;

		sender = internet_address_mailbox_get_addr(INTERNET_ADDRESS_MAILBOX(sender_address));
		if (sender != NULL) {
			(void) pref_db_set_name(sender, pref_idx, value);
		}
	}
}


/** \brief Set the HTML preferences setting for a sender
 *
 * \param sender From: mailbox, must not be NULL
 * \param pref_idx 1 prefer HTML, 2 auto-load external content
 * \return TRUE if the operation was successful
 */
static gboolean
pref_db_set_name(const gchar *sender, int pref_idx, gboolean value)
{
	gboolean result = FALSE;

	if (pref_db_check()) {
		G_LOCK(db_mutex);
		if ((sqlite3_bind_text(query[pref_idx], 1, sender, -1, SQLITE_STATIC) != SQLITE_OK) ||
			(sqlite3_bind_int(query[pref_idx], 2, value) != SQLITE_OK) ||
			(sqlite3_step(query[pref_idx]) != SQLITE_DONE)) {
			/* Translators: #1 message sender address; #2 error message */
			libbalsa_information(LIBBALSA_INFORMATION_ERROR, _("Cannot save HTML preferences for “%s”: %s"), sender,
				sqlite3_errmsg(pref_db));
		} else {
			result = TRUE;
		}
		sqlite3_reset(query[pref_idx]);
		G_UNLOCK(db_mutex);
	}

	return result;
}


/* callback: popup menu key in html prefs database dialogue activated */
static gboolean
popup_menu_cb(GtkWidget *widget, gpointer G_GNUC_UNUSED user_data)
{
	GtkTreeView *tree_view = GTK_TREE_VIEW(widget);
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	GtkTreeIter iter;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
	if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
		GtkTreePath *path;

		path = gtk_tree_model_get_path(model, &iter);
		gtk_tree_view_scroll_to_cell(tree_view, path, NULL, FALSE, 0.0, 0.0);
		gtk_tree_path_free(path);
		popup_menu_real(widget, NULL);
	}

	return TRUE;
}


/* callback: mouse click in html prefs database dialogue activated */
static void
button_press_cb(GtkGestureMultiPress *multi_press_gesture, gint G_GNUC_UNUSED n_press, gdouble x,
	gdouble y, gpointer G_GNUC_UNUSED user_data)
{
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(multi_press_gesture));
	GtkTreeView *tree_view = GTK_TREE_VIEW(widget);
	GtkGesture *gesture;
	GdkEventSequence *sequence;
	const GdkEvent *event;

	gesture = GTK_GESTURE(multi_press_gesture);
	sequence = gtk_gesture_single_get_current_sequence(GTK_GESTURE_SINGLE(multi_press_gesture));
	event = gtk_gesture_get_last_event(gesture, sequence);
	if (gdk_event_triggers_context_menu(event) && (gdk_event_get_window(event) == gtk_tree_view_get_bin_window(tree_view))) {
		gint bx;
		gint by;
		GtkTreePath *path;

		gtk_tree_view_convert_widget_to_bin_window_coords(tree_view, (gint) x, (gint) y, &bx, &by);
		if (gtk_tree_view_get_path_at_pos(tree_view, bx, by, &path, NULL, NULL, NULL)) {
			GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
			GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
			GtkTreeIter iter;

			gtk_tree_selection_unselect_all(selection);
			gtk_tree_selection_select_path(selection, path);
			gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree_view), path, NULL, FALSE);
			if (gtk_tree_model_get_iter(model, &iter, path)) {
				popup_menu_real(GTK_WIDGET(tree_view), event);
			}
			gtk_tree_path_free(path);
		}
	}
}


/* html prefs database dialogue context menu */
static void
popup_menu_real(GtkWidget *widget, const GdkEvent *event)
{
	GtkWidget *popup_menu;
	GtkWidget* menu_item;

	popup_menu = gtk_menu_new();
	menu_item = gtk_menu_item_new_with_mnemonic(_("_Delete"));
	g_signal_connect(menu_item, "activate", G_CALLBACK(remove_item_cb), widget);
	gtk_menu_shell_append(GTK_MENU_SHELL(popup_menu), menu_item);
	gtk_widget_show_all(popup_menu);
	if (event != NULL) {
		gtk_menu_popup_at_pointer(GTK_MENU(popup_menu), event);
	} else {
		gtk_menu_popup_at_widget(GTK_MENU(popup_menu), widget, GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER, NULL);
	}
}


/* context menu callback: remove entry from database */
static void
remove_item_cb(GtkMenuItem G_GNUC_UNUSED *menuitem, gpointer user_data)
{
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	GtkTreeIter iter;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(user_data));
	if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
		gchar *addr;

		gtk_tree_model_get(model, &iter, PREFS_ADDRESS_COLUMN, &addr, -1);
		G_LOCK(db_mutex);
		if ((sqlite3_bind_text(query[3], 1, addr, -1, SQLITE_STATIC) != SQLITE_OK) ||
			(sqlite3_step(query[3]) != SQLITE_DONE)) {
			/* Translators: #1 error message */
			libbalsa_information(LIBBALSA_INFORMATION_ERROR, _("Cannot delete database entry: %s"), sqlite3_errmsg(pref_db));
		}
		gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
		sqlite3_reset(query[3]);
		G_UNLOCK(db_mutex);
		g_free(addr);
	}
}


/* callback: toggles setting in database dialogue */
static void
on_prefs_button_toggled(GtkCellRendererToggle *cell_renderer, gchar *path, gpointer user_data)
{
	GtkTreeIter iter;
	GtkListStore *model = GTK_LIST_STORE(user_data);

	if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(model), &iter, path)) {
		gchar *addr;
		gint column;
		gboolean new_state;

		new_state = !gtk_cell_renderer_toggle_get_active(cell_renderer);
		column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(cell_renderer), "dbcol"));
		gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, PREFS_ADDRESS_COLUMN, &addr, -1);
		if (pref_db_set_name(addr, column, new_state)) {
			gtk_list_store_set(model, &iter, column, new_state, -1);
		}
		g_free(addr);
	}
}


/* close the database and free prepared statements */
static void
html_pref_db_close(void)
{
	guint n;

	g_debug("close HTML preferences database");
	G_LOCK(db_mutex);
	for (n = 0U; n < NUM_QUERIES; n++) {
		sqlite3_finalize(query[n]);
		query[n] = NULL;
	}
	sqlite3_close(pref_db);
	pref_db = NULL;
	G_UNLOCK(db_mutex);
}

#endif		/* HAVE_HTML_WIDGET */
