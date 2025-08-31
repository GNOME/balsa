/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
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
#include "folder-conf.h"

#include <string.h>
#include "balsa-app.h"
#include "balsa-icons.h"
#include "mailbox-conf.h"
#include "mailbox-node.h"
#include "save-restore.h"
#include "pref-manager.h"
#include "imap-server.h"
#include "server-config.h"
#include "folder-scanners.h"
#include "geometry-manager.h"
#include <glib/gi18n.h>

typedef struct _CommonDialogData CommonDialogData;
typedef struct _FolderDialogData FolderDialogData;
typedef struct _SubfolderDialogData SubfolderDialogData;

typedef gboolean (*CommonDialogFunc)(CommonDialogData * common_data);

#define BALSA_FOLDER_CONF_IMAP_KEY "balsa-folder-conf-imap"

struct _CommonDialogData {
    GtkDialog *dialog;
    GtkTreeStore *store;
    BalsaMailboxNode *mbnode;
    CommonDialogFunc ok;
};

struct _FolderDialogData {
    CommonDialogData common_data;

    LibBalsaServerCfg *server_cfg;
    LibBalsaServer *server;
    GtkWidget *subscribed, *list_inbox, *prefix;
    GtkWidget *connection_limit, *enable_persistent,
        *use_idle, *has_bugs, *use_status;
};

/* FIXME: identity_name will leak on cancelled folder edition */

struct _SubfolderDialogData {
    CommonDialogData common_data;

    BalsaMailboxConfView *mcv;
    GtkWidget *parent_folder, *folder_name, *host_label;
    const gchar *old_folder, *old_parent;
    BalsaMailboxNode *parent;   /* (new) parent of the mbnode.  */
    /* Used for renaming and creation */
};

/* Destroy notification */
static void
folder_conf_destroy_common_data(CommonDialogData * common_data)
{
    if (common_data->dialog != NULL) {
        /* The mailbox node was destroyed. Close the dialog, but don't
         * trigger further calls to folder_conf_destroy_common_data. */
        common_data->mbnode = NULL;
        gtk_dialog_response(common_data->dialog, GTK_RESPONSE_NONE);
    } else {
        g_free(common_data);
    }
}

static void
folder_conf_response(GtkDialog * dialog, gint response,
                     CommonDialogData * common_data)
{
    GError *err = NULL;

    /* If mbnode's parent gets rescanned, mbnode will be finalized,
     * which triggers folder_conf_destroy_common_data, and recursively calls
     * folder_conf_response, which results in common_data being freed before
     * we're done with it; we ref mbnode to avoid that. */
    if (common_data->mbnode)
	g_object_ref(common_data->mbnode);
    switch (response) {
    case GTK_RESPONSE_HELP:
        gtk_show_uri_on_window(GTK_WINDOW(dialog), "help:balsa/folder-config",
                               gtk_get_current_event_time(), &err);
        if (err) {
            balsa_information(LIBBALSA_INFORMATION_WARNING,
                              _("Error displaying config help: %s\n"),
                              err->message);
            g_error_free(err);
        }
        g_clear_object(&common_data->mbnode);
        return;
    case GTK_RESPONSE_OK:
        if (!common_data->ok(common_data))
            break;
        /* ...or fall over */
    default:
        gtk_widget_destroy(GTK_WIDGET(common_data->dialog));
        common_data->dialog = NULL;
        g_clear_object(&common_data->store);
        if (common_data->mbnode != NULL) {
            /* Clearing the data signifies that the dialog has been
             * destroyed. It also triggers a call to
             * folder_conf_destroy_common_data, which will free common_data, so we cache
             * common_data->mbnode. */
	    BalsaMailboxNode *mbnode = common_data->mbnode;
            g_object_set_data(G_OBJECT(mbnode),
                              BALSA_FOLDER_CONF_IMAP_KEY, NULL);
	    g_object_unref(mbnode);
	} else
            /* Cancelling, without creating a mailbox node. Nobody owns
             * the xDialogData, so we'll free it here. */
            g_free(common_data);
        break;
    }
}

/* folder_conf_imap_node:
   show configuration widget for given mailbox node, allow user to 
   modify it and update mailbox node accordingly.
   Creates the node when mn == NULL.
*/
static void 
validate_folder(GtkWidget G_GNUC_UNUSED *w, FolderDialogData *folder_data)
{
    gtk_dialog_set_response_sensitive(folder_data->common_data.dialog,
                                      GTK_RESPONSE_OK,
                                      libbalsa_server_cfg_valid(folder_data->server_cfg));
}

static gboolean
imap_apply_subscriptions(GtkTreeModel *model,
                         GtkTreePath  *path,
                         GtkTreeIter  *iter,
                         gpointer      data)
{
	gchar *mbox_path;
	gboolean new_state;
	gboolean old_state;
	GPtrArray **changed = (GPtrArray **) data;

	gtk_tree_model_get(model, iter,
			LB_SCANNER_IMAP_PATH, &mbox_path,
			LB_SCANNER_IMAP_SUBS_NEW, &new_state,
			LB_SCANNER_IMAP_SUBS_OLD, &old_state, -1);
	if (old_state != new_state) {
		if (new_state) {
			g_ptr_array_add(changed[0], mbox_path);
		} else {
			g_ptr_array_add(changed[1], mbox_path);
		}
	} else {
		g_free(mbox_path);
	}

	return FALSE;
}

static void
imap_update_subscriptions(FolderDialogData *folder_data)
{
	if (folder_data->common_data.store != NULL) {
		GPtrArray *changed[2];		/* 0 subscribe, 1 unsubscribe */
		GError *error = NULL;

		changed[0] = g_ptr_array_new_full(4U, g_free);		/* count is a wild guess... */
		changed[1] = g_ptr_array_new_full(4U, g_free);
		gtk_tree_model_foreach(GTK_TREE_MODEL(folder_data->common_data.store),
                                       imap_apply_subscriptions,
                                       changed);
		if ((changed[0]->len > 0U) || (changed[1]->len > 0U)) {
			if (!libbalsa_imap_server_subscriptions(LIBBALSA_IMAP_SERVER(folder_data->server),
                                                                changed[0],
                                                                changed[1],
                                                                &error)) {
				libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                                     _("Changing subscriptions failed: %s"),
                                                     error->message);
				g_clear_error(&error);
			}
		}
		g_ptr_array_unref(changed[0]);
		g_ptr_array_unref(changed[1]);
	}
}

static gboolean
folder_conf_clicked_ok(FolderDialogData * folder_data)
{
    gboolean insert = FALSE;
    LibBalsaImapServer *imap;
    BalsaMailboxNode *mbnode = folder_data->common_data.mbnode;

    if (mbnode == NULL) {
        g_signal_connect(folder_data->server, "get-password",
                         G_CALLBACK(ask_password), NULL);
    }

    libbalsa_server_cfg_assign_server(folder_data->server_cfg, folder_data->server);

    imap = LIBBALSA_IMAP_SERVER(folder_data->server);
    libbalsa_imap_server_set_max_connections
        (imap, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(folder_data->connection_limit)));
    libbalsa_imap_server_enable_persistent_cache
        (imap, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(folder_data->enable_persistent)));
    libbalsa_imap_server_set_use_idle
        (imap, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(folder_data->use_idle)));
    libbalsa_imap_server_set_bug
        (imap, ISBUG_FETCH, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(folder_data->has_bugs)));
    libbalsa_imap_server_set_use_status
        (imap, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(folder_data->use_status)));

    if (mbnode == NULL) {
    	folder_data->common_data.mbnode = mbnode =
            balsa_mailbox_node_new_imap_folder(folder_data->server, NULL);
    	g_object_ref(mbnode);
    	/* The mailbox node takes over ownership of the
    	 * FolderDialogData. */
    	g_object_set_data_full(G_OBJECT(mbnode),
    		BALSA_FOLDER_CONF_IMAP_KEY, folder_data,
			(GDestroyNotify) folder_conf_destroy_common_data);
        insert = TRUE;
    }

    balsa_mailbox_node_set_name(mbnode,
                                libbalsa_server_cfg_get_name(folder_data->server_cfg));
    balsa_mailbox_node_set_dir(mbnode,
                               gtk_entry_get_text(GTK_ENTRY(folder_data->prefix)));
    balsa_mailbox_node_set_subscribed(mbnode,
                                      gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(folder_data->subscribed)));
    balsa_mailbox_node_set_list_inbox(mbnode,
                                      gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(folder_data->list_inbox)));

    libbalsa_server_config_changed(folder_data->server); /* trigger config save */
    imap_update_subscriptions(folder_data);

    if (insert) {
    	balsa_mblist_mailbox_node_append(NULL, mbnode);
    	balsa_mailbox_node_append_subtree(mbnode);
    	config_folder_add(mbnode, NULL);
    	g_signal_connect_swapped(folder_data->server, "config-changed",
                                 G_CALLBACK(config_folder_update), mbnode);
    	update_mail_servers();
    } else {
    	balsa_mailbox_node_rescan(mbnode);
    	balsa_mblist_mailbox_node_redraw(mbnode);
    }

    return TRUE;
}

static void
on_subscription_toggled(GtkCellRendererToggle *cell_renderer,
						gchar                 *path,
						GtkTreeStore          *store)
{
	GtkTreeIter iter;

	if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(store), &iter, path)) {
		gboolean state;
		gboolean orig_state;

		gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
				LB_SCANNER_IMAP_SUBS_NEW, &state,
				LB_SCANNER_IMAP_SUBS_OLD, &orig_state, -1);
		state = !state;
		gtk_tree_store_set(store, &iter,
				LB_SCANNER_IMAP_SUBS_NEW, state,
				LB_SCANNER_IMAP_STYLE,
                                (state == orig_state) ? PANGO_STYLE_NORMAL : PANGO_STYLE_ITALIC,
                                -1);
	}
}

static GtkWidget *
create_imap_folder_dialog(LibBalsaServer  *server,
						  GtkWindow       *parent,
						  const gchar     *geometry_key,
						  gboolean         with_subsrciptions,
						  const gchar     *title,
						  const gchar     *message,
						  GtkTreeStore   **store,
						  GtkWidget      **treeview)
{
	GtkWidget *dialog;
	GtkWidget *content_area;
	GtkWidget *vbox;
	GtkWidget *label;
	GtkWidget *scrolled_wind;
	GtkTreeSelection *selection;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GError *error = NULL;

	/* get the folder tree from the server if required */
	if (*store == NULL) {
		*store = libbalsa_scanner_imap_tree(server, with_subsrciptions, &error);
		if (*store == NULL) {
			libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                             _("Cannot list IMAP folders: %s"),
                                             error->message);
			g_error_free(error);
			return NULL;
		} else {
			g_object_ref(*store);
		}
	}

	/* dialog */
	if (with_subsrciptions) {
		dialog = gtk_dialog_new_with_buttons(title,
                                                     parent,
                                                     GTK_DIALOG_MODAL | libbalsa_dialog_flags(),
                                                     _("_Close"), GTK_RESPONSE_CLOSE,
                                                     NULL);
	} else {
		dialog = gtk_dialog_new_with_buttons(title,
                                                     parent,
                                                     GTK_DIALOG_MODAL | libbalsa_dialog_flags(),
                                                     _("_Cancel"), GTK_RESPONSE_REJECT,
                                                     _("_OK"), GTK_RESPONSE_ACCEPT,
                                                     NULL);
	}
        content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	geometry_manager_attach(GTK_WINDOW(dialog), geometry_key);

	/* content: vbox, message label, scrolled window */
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2 * HIG_PADDING);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), HIG_PADDING);
	gtk_container_add(GTK_CONTAINER(content_area), vbox);
	gtk_widget_set_vexpand(vbox, TRUE);

	label = libbalsa_create_wrap_label(message, FALSE);
	gtk_container_add(GTK_CONTAINER(vbox), label);

	scrolled_wind = gtk_scrolled_window_new(NULL,NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled_wind),
                                            GTK_SHADOW_ETCHED_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_wind),
                                       GTK_POLICY_NEVER,
                                       GTK_POLICY_AUTOMATIC);

        gtk_widget_set_vexpand(scrolled_wind, TRUE);
        gtk_widget_set_valign(scrolled_wind, GTK_ALIGN_FILL);
	gtk_container_add(GTK_CONTAINER(vbox), scrolled_wind);

	/* folder tree */
	*treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(*store));
	gtk_container_add(GTK_CONTAINER(scrolled_wind), *treeview);
	gtk_tree_view_expand_all(GTK_TREE_VIEW(*treeview));

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(_("folder"), renderer,
			"text", LB_SCANNER_IMAP_FOLDER,
			"style", LB_SCANNER_IMAP_STYLE, NULL);
	gtk_tree_view_column_set_expand(column, TRUE);
	gtk_tree_view_column_set_sort_column_id(column, 0);
	gtk_tree_view_append_column (GTK_TREE_VIEW(*treeview), column);
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(*treeview));

	/* subscriptions if requested */
	if (with_subsrciptions) {
		renderer = gtk_cell_renderer_toggle_new();
		g_signal_connect(renderer, "toggled", G_CALLBACK(on_subscription_toggled), *store);
		g_object_set(renderer, "activatable", TRUE, NULL);
		column = gtk_tree_view_column_new_with_attributes(_("subscribed"), renderer,
				"active", LB_SCANNER_IMAP_SUBS_NEW, NULL);
		gtk_tree_view_column_set_expand(column, FALSE);
		gtk_tree_view_append_column (GTK_TREE_VIEW(*treeview), column);

		gtk_tree_selection_set_mode(selection, GTK_SELECTION_NONE);
	} else {
		gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
	}

	gtk_widget_show_all(vbox);

	return dialog;
}

static void
folder_conf_imap_subscriptions(GtkButton        *widget,
                               FolderDialogData *folder_data)
{
	GtkWidget *dialog;
	GtkWidget *treeview;
	gchar *label_str;

	label_str = g_strdup_printf(_("Manage folder subscriptions of IMAP server “%s”"),
			            (folder_data->common_data.mbnode != NULL) ?
                                     balsa_mailbox_node_get_name(folder_data->common_data.mbnode) :
                                     _("unknown"));
	dialog = create_imap_folder_dialog(folder_data->server, GTK_WINDOW(folder_data->common_data.dialog),
                                           "IMAPSubscriptions", TRUE,
                                           _("Manage subscriptions"),
                                           label_str, &folder_data->common_data.store, &treeview);
	g_free(label_str);

	if (dialog != NULL) {
		/* dialog is modal, so the UI is blocked until it is closed */
		g_signal_connect(dialog, "response",
				 G_CALLBACK(gtk_widget_destroy), NULL);
		gtk_widget_show_all(dialog);
	}
}

static void
folder_data_subscribed_toggled(GtkToggleButton *toggle,
					   GtkWidget       *button)
{
	gtk_widget_set_sensitive(button, gtk_toggle_button_get_active(toggle));
}

/* folder_conf_imap_node:
   show the IMAP Folder configuration dialog for given mailbox node.
   If mn is NULL, setup it with default values for folder creation.
*/
void
folder_conf_imap_node(BalsaMailboxNode *mn)
{
    FolderDialogData *folder_data;
    static FolderDialogData *folder_data_new;
    GtkWidget *box;
    GtkWidget *button;
    GtkWidget *content_area;

    /* Allow only one dialog per mailbox node, and one with mn == NULL
     * for creating a new folder. */
    folder_data = mn ? g_object_get_data(G_OBJECT(mn), BALSA_FOLDER_CONF_IMAP_KEY)
             : folder_data_new;
    if (folder_data) {
        gtk_window_present_with_time(GTK_WINDOW(folder_data->common_data.dialog),
                                     gtk_get_current_event_time());
        return;
    }

    folder_data = g_new0(FolderDialogData, 1);
    if (mn != NULL) {
    	folder_data->server = balsa_mailbox_node_get_server(mn);
    } else {
    	folder_data->server = g_object_new(LIBBALSA_TYPE_IMAP_SERVER, NULL);
    }

    folder_data->common_data.ok = (CommonDialogFunc) folder_conf_clicked_ok;
    folder_data->common_data.mbnode = mn;
    folder_data->common_data.dialog =
        GTK_DIALOG(gtk_dialog_new_with_buttons
                   (_("Remote IMAP account"),
                    GTK_WINDOW(balsa_app.main_window),
                    GTK_DIALOG_DESTROY_WITH_PARENT |
                    libbalsa_dialog_flags(),
                    (mn != NULL) ? _("_Apply") : _("_Add"), GTK_RESPONSE_OK,
                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                    _("_Help"), GTK_RESPONSE_HELP,
                    NULL));
    gtk_dialog_set_response_sensitive(folder_data->common_data.dialog, GTK_RESPONSE_OK, FALSE);
    g_object_add_weak_pointer(G_OBJECT(folder_data->common_data.dialog),
                              (gpointer *) &folder_data->common_data.dialog);
    gtk_window_set_role(GTK_WINDOW(folder_data->common_data.dialog), "folder_config_dialog");
    if (mn) {
        g_object_set_data_full(G_OBJECT(mn),
                               BALSA_FOLDER_CONF_IMAP_KEY, folder_data, 
                               (GDestroyNotify) folder_conf_destroy_common_data);
    } else {
        folder_data_new = folder_data;
        g_object_add_weak_pointer(G_OBJECT(folder_data->common_data.dialog),
                                  (gpointer *) &folder_data_new);
    }

    folder_data->server_cfg = libbalsa_server_cfg_new(folder_data->server, (mn != NULL) ? balsa_mailbox_node_get_name(mn) : NULL);
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(folder_data->common_data.dialog));
    gtk_container_add(GTK_CONTAINER(content_area), GTK_WIDGET(folder_data->server_cfg));
    g_signal_connect(folder_data->server_cfg, "changed", G_CALLBACK(validate_folder), folder_data);

    /* additional basic settings - subscription management */
    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2 * HIG_PADDING);

    folder_data->subscribed = gtk_check_button_new_with_mnemonic(_("Subscribed _folders only"));
    gtk_widget_set_hexpand(folder_data->subscribed, TRUE);
    gtk_widget_set_halign(folder_data->subscribed, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(box), folder_data->subscribed);

    button = gtk_button_new_with_label(_("Manage subscriptions…"));
    g_signal_connect(button, "clicked", G_CALLBACK(folder_conf_imap_subscriptions), folder_data);
    if (mn != NULL) {
        gboolean subscribed = balsa_mailbox_node_get_subscribed(mn);
    	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(folder_data->subscribed), subscribed);
    	gtk_widget_set_sensitive(button, subscribed);
    } else {
    	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(folder_data->subscribed), FALSE);
    	gtk_widget_set_sensitive(button, FALSE);
    }
    gtk_container_add(GTK_CONTAINER(box), button);
    g_signal_connect(folder_data->subscribed, "toggled", G_CALLBACK(folder_data_subscribed_toggled), button);
    libbalsa_server_cfg_add_row(folder_data->server_cfg, TRUE, box, NULL);

    folder_data->list_inbox = libbalsa_server_cfg_add_check(folder_data->server_cfg, TRUE, _("Always show _Inbox"),
    	(mn != NULL) ? balsa_mailbox_node_get_list_inbox(mn) : TRUE, NULL, NULL);
    folder_data->prefix = libbalsa_server_cfg_add_entry(folder_data->server_cfg, TRUE, _("Pr_efix:"),
    	(mn != NULL) ? balsa_mailbox_node_get_dir(mn) : NULL, NULL, NULL);

    /* additional advanced settings */
    folder_data->connection_limit = gtk_spin_button_new_with_range(1.0, 40.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(folder_data->connection_limit),
    	(gdouble) libbalsa_imap_server_get_max_connections(LIBBALSA_IMAP_SERVER(folder_data->server)));
    libbalsa_server_cfg_add_item(folder_data->server_cfg, FALSE, _("_Max number of connections:"), folder_data->connection_limit);
    folder_data->enable_persistent = libbalsa_server_cfg_add_check(folder_data->server_cfg, FALSE, _("Enable _persistent cache"),
    	libbalsa_imap_server_has_persistent_cache(LIBBALSA_IMAP_SERVER(folder_data->server)), NULL, NULL);
    folder_data->use_idle = libbalsa_server_cfg_add_check(folder_data->server_cfg, FALSE, _("Use IDLE command"),
    	libbalsa_imap_server_get_use_idle(LIBBALSA_IMAP_SERVER(folder_data->server)), NULL, NULL);
    folder_data->has_bugs = libbalsa_server_cfg_add_check(folder_data->server_cfg, FALSE, _("Enable _bug workarounds"),
    	libbalsa_imap_server_has_bug(LIBBALSA_IMAP_SERVER(folder_data->server), ISBUG_FETCH), NULL, NULL);
    folder_data->use_status = libbalsa_server_cfg_add_check(folder_data->server_cfg, FALSE, _("Use STATUS for mailbox checking"),
    	libbalsa_imap_server_get_use_status(LIBBALSA_IMAP_SERVER(folder_data->server)), NULL, NULL);

    gtk_widget_show_all(GTK_WIDGET(folder_data->common_data.dialog));

    gtk_dialog_set_default_response(folder_data->common_data.dialog, 
                                    mn ? GTK_RESPONSE_OK 
                                    : GTK_RESPONSE_CANCEL);

    g_signal_connect(folder_data->common_data.dialog, "response",
                     G_CALLBACK(folder_conf_response), folder_data);
}

/* folder_conf_imap_sub_node:
   Show name and path for an existing subfolder,
   or create a new one.
*/

static void
validate_sub_folder(GtkWidget * w, SubfolderDialogData * sub_folder_data)
{
    BalsaMailboxNode *mn = sub_folder_data->parent;
    /*
     * Allow typing in the parent_folder entry box only if we already
     * have the server information in mn:
     */
    gboolean have_server = (mn && LIBBALSA_IS_IMAP_SERVER(balsa_mailbox_node_get_server(mn)));
    gtk_editable_set_editable(GTK_EDITABLE(sub_folder_data->parent_folder), have_server);
    gtk_widget_set_can_focus(sub_folder_data->parent_folder, have_server);
    /*
     * We'll allow a null parent name, although some IMAP servers
     * will deny permission:
     */
    gtk_dialog_set_response_sensitive(sub_folder_data->common_data.dialog, GTK_RESPONSE_OK, 
                                      have_server &&
                                      *gtk_entry_get_text(GTK_ENTRY
                                                          (sub_folder_data->folder_name)));
}

static gboolean
select_parent_folder(GtkTreeModel *model,
					 GtkTreePath  *path,
					 GtkTreeIter  *iter,
					 gpointer      data)
{
	const gchar *find_folder = (const gchar *) data;
	gchar *foldername;
	gboolean found;

	gtk_tree_model_get(model, iter,
			LB_SCANNER_IMAP_PATH, &foldername, -1);
	if ((foldername != NULL) && (strcmp(foldername, find_folder) == 0)) {
		GtkTreeSelection *selection;

		selection = GTK_TREE_SELECTION(g_object_get_data(G_OBJECT(model), "selection"));
		gtk_tree_selection_select_iter(selection, iter);
		found = TRUE;
	} else {
		found = FALSE;
	}
	g_free(foldername);
	return found;
}

static void
on_parent_double_click(GtkTreeView G_GNUC_UNUSED       *treeview,
					   GtkTreePath G_GNUC_UNUSED       *path,
					   GtkTreeViewColumn G_GNUC_UNUSED *column,
                       GtkDialog                       *dialog)
{
    gtk_dialog_response(dialog, GTK_RESPONSE_ACCEPT);
}

typedef struct {
    SubfolderDialogData *sub_folder_data;
    GtkTreeSelection *selection;
    GtkTreeModel *model;
} browse_button_cb_data;

static void browse_button_cb_response(GtkDialog *self,
                                      gint       response_id,
                                      gpointer   user_data);

static void
browse_button_cb(GtkWidget           *widget,
				 SubfolderDialogData *sub_folder_data)
{
	GtkWidget *dialog;
	GtkWidget *treeview;
	gchar *label_str;

	label_str = g_strdup_printf(_("Select parent folder of “%s”"),
                                    libbalsa_mailbox_get_name(balsa_mailbox_node_get_mailbox(sub_folder_data->common_data.mbnode)));
	dialog = create_imap_folder_dialog(balsa_mailbox_node_get_server(sub_folder_data->common_data.mbnode), GTK_WINDOW(sub_folder_data->common_data.dialog), "IMAPSelectParent", FALSE,
			_("Select parent folder"), label_str, &sub_folder_data->common_data.store, &treeview);
	g_free(label_str);

	if (dialog != NULL) {
		GtkTreeModel *model;
		GtkTreeSelection *selection;
		const gchar *current_parent;
		browse_button_cb_data *data;

		/* select the parent item (if any) */
		model = gtk_tree_view_get_model(GTK_TREE_VIEW(treeview));
		selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
		current_parent = gtk_entry_get_text(GTK_ENTRY(sub_folder_data->parent_folder));
		if (current_parent[0] != '\0') {
			g_object_set_data(G_OBJECT(model), "selection", selection);		/* needed in the callback */
			gtk_tree_model_foreach(model, select_parent_folder, (gpointer) current_parent);
		} else {
			GtkTreeIter iter;

			/* no parent: select the first node */
			gtk_tree_model_get_iter_first(model, &iter);
			gtk_tree_selection_select_iter(selection, &iter);
		}
		g_signal_connect(treeview, "row-activated", G_CALLBACK(on_parent_double_click), dialog);

		data = g_new(browse_button_cb_data, 1);
		data->sub_folder_data = sub_folder_data;
		data->selection = selection;
		data->model = model;
		g_signal_connect(dialog, "response", G_CALLBACK(browse_button_cb_response), data);
		gtk_widget_show_all(dialog);
	}
}

static void
browse_button_cb_response(GtkDialog *self,
                          gint       response_id,
                          gpointer   user_data)
{
    browse_button_cb_data *data = user_data;

    if (response_id == GTK_RESPONSE_ACCEPT) {
        GtkTreeIter iter;

        if (gtk_tree_selection_get_selected(data->selection, NULL, &iter)) {
            gchar *selected_path;

            gtk_tree_model_get(data->model, &iter,
                               LB_SCANNER_IMAP_PATH, &selected_path, -1);
            gtk_entry_set_text(GTK_ENTRY(data->sub_folder_data->parent_folder),
                               selected_path);
            g_free(selected_path);
            validate_sub_folder(NULL, data->sub_folder_data);
        }
    }

    g_free(data);
    gtk_widget_destroy(GTK_WIDGET(self));
}

static gboolean
subfolder_conf_clicked_ok(SubfolderDialogData * sub_folder_data)
{
    gchar *parent, *folder;
    gboolean ret = TRUE;

    parent = gtk_editable_get_chars(GTK_EDITABLE(sub_folder_data->parent_folder), 0, -1);
    folder = gtk_editable_get_chars(GTK_EDITABLE(sub_folder_data->folder_name), 0, -1);
	g_debug("sub_folder_data->old_parent=%s; sub_folder_data->old_folder=%s",
		sub_folder_data->old_parent, sub_folder_data->old_folder);

    if (sub_folder_data->common_data.mbnode) {
        LibBalsaMailbox *mailbox = balsa_mailbox_node_get_mailbox(sub_folder_data->common_data.mbnode);

        /* Views stuff. */
        if (mailbox != NULL)
            mailbox_conf_view_check(sub_folder_data->mcv, mailbox);
        
        /* rename */
        if (g_strcmp0(parent, sub_folder_data->old_parent) != 0 ||
            g_strcmp0(folder, sub_folder_data->old_folder) != 0) {
            gint button = GTK_RESPONSE_OK;
            if (g_strcmp0(sub_folder_data->old_folder, "INBOX") == 0 &&
                (sub_folder_data->old_parent == NULL || sub_folder_data->old_parent[0] == '\0')) {
                gchar *msg =
                    g_strdup_printf(_
                                    ("Renaming Inbox is special!\n"
                                     "You will create a subfolder %s in %s\n"
                                     "containing the messages from Inbox.\n"
                                     "Inbox and its subfolders will remain.\n"
                                     "What would you like to do?"),
folder, parent);
                GtkWidget *ask =
                    gtk_dialog_new_with_buttons(_("Question"),
                                                GTK_WINDOW(sub_folder_data->common_data.dialog),
                                                GTK_DIALOG_MODAL |
                                                libbalsa_dialog_flags(),
                                                _("Rename Inbox"),
                                                GTK_RESPONSE_OK,
                                                _("Cancel"),
                                                GTK_RESPONSE_CANCEL,
                                                NULL);
                GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(ask));

                gtk_container_add(GTK_CONTAINER(content_area), gtk_label_new(msg));
                g_free(msg);
                button = gtk_dialog_run(GTK_DIALOG(ask));
                gtk_widget_destroy(ask);
            }
            if (button == GTK_RESPONSE_OK) {
                GError* err = NULL;
                LibBalsaServer *server;

                /* Close the mailbox before renaming,
                 * otherwise the rescan will try to close it
                 * under its old name.
                 */
                balsa_window_close_mbnode(balsa_app.main_window,
                                          sub_folder_data->common_data.mbnode);
                if (!libbalsa_imap_rename_subfolder
                   (LIBBALSA_MAILBOX_IMAP(balsa_mailbox_node_get_mailbox(sub_folder_data->common_data.mbnode)),
                    parent, folder, balsa_mailbox_node_get_subscribed(sub_folder_data->common_data.mbnode), &err)) {
                    balsa_information(LIBBALSA_INFORMATION_ERROR,
                                      _("Folder rename failed. Reason: %s"),
                                      err ? err->message : "unknown");
                    g_clear_error(&err);
                    ret = FALSE;
                    goto error;
                }
                balsa_mailbox_node_set_dir(sub_folder_data->common_data.mbnode, parent);

                /*  Rescan as little of the tree as possible. */
                server = balsa_mailbox_node_get_server(sub_folder_data->parent);
                if (sub_folder_data->old_parent != NULL
                    && g_str_has_prefix(sub_folder_data->old_parent, parent)) {
                    /* moved it up the tree */
		    BalsaMailboxNode *mbnode = balsa_find_dir(server, parent);
                    if (mbnode != NULL) {
                        balsa_mailbox_node_rescan(mbnode);
			g_object_unref(mbnode);
		    } else
                        g_debug("Parent not found!?");
                } else if (sub_folder_data->old_parent != NULL
                           && g_str_has_prefix(parent, sub_folder_data->old_parent)) {
                    /* moved it down the tree */
		    BalsaMailboxNode *mbnode = balsa_find_dir(server, sub_folder_data->old_parent);
                    if (mbnode != NULL) {
                        balsa_mailbox_node_rescan(mbnode);
			g_object_unref(mbnode);
		    }
                } else {
                    /* moved it sideways: a chain of folders might
                     * go away, so we'd better rescan the complete IMAP server
                     */
		    BalsaMailboxNode *mbnode = balsa_mailbox_node_get_parent(sub_folder_data->common_data.mbnode);
		    BalsaMailboxNode *parent_mbnode;

                    while ((balsa_mailbox_node_get_mailbox(mbnode) != NULL) &&
                           ((parent_mbnode = balsa_mailbox_node_get_parent(mbnode)) != NULL))
                        mbnode = parent_mbnode;
                    balsa_mailbox_node_rescan(mbnode);
                }
            }
        }
    } else {
        GError *err = NULL;
        /* create and subscribe, if parent was. */
        if (libbalsa_imap_new_subfolder(parent, folder,
                                        balsa_mailbox_node_get_subscribed(sub_folder_data->parent),
                                        balsa_mailbox_node_get_server(sub_folder_data->parent),
                                        &err)) {
            /* see it as server sees it: */
            balsa_mailbox_node_rescan(sub_folder_data->parent);
        } else {
            balsa_information(LIBBALSA_INFORMATION_ERROR,
                              _("Folder creation failed. Reason: %s"),
                              err ? err->message : "unknown");
            g_clear_error(&err);
            ret = FALSE;
        }
    }
 error:
    g_free(parent);
    g_free(folder);
    return ret;
}

/* folder_conf_imap_sub_node:
   show the IMAP Folder configuration dialog for given mailbox node
   representing a sub-folder.
   If mn is NULL, setup it with default values for folder creation.
*/
static void
set_ok_sensitive(GtkDialog * dialog)
{
    gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_OK, TRUE);
}

void
folder_conf_imap_sub_node(BalsaMailboxNode * mn)
{
    GtkWidget *content_area;
    GtkWidget *grid, *button, *label, *hbox;
    SubfolderDialogData *sub_folder_data;
    LibBalsaMailbox *mailbox;
    LibBalsaServer *server;
    static SubfolderDialogData *sub_folder_data_new = NULL;
    guint row;

    g_assert(mn != NULL);

    /* Allow only one dialog per mailbox node */
    sub_folder_data = g_object_get_data(G_OBJECT(mn), BALSA_FOLDER_CONF_IMAP_KEY);
    if (sub_folder_data) {
        gtk_window_present_with_time(GTK_WINDOW(sub_folder_data->common_data.dialog),
                                     gtk_get_current_event_time());
        return;
    }

    sub_folder_data = g_new0(SubfolderDialogData, 1);
    sub_folder_data->common_data.ok = (CommonDialogFunc) subfolder_conf_clicked_ok;
    sub_folder_data->common_data.mbnode = mn;

    mailbox = balsa_mailbox_node_get_mailbox(mn);
    /* update */
    if (mailbox == NULL) {
        balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("An IMAP folder that is not a mailbox\n"
                            "has no properties that can be changed."));
        g_free(sub_folder_data);

        return;
    }

    sub_folder_data->parent = balsa_mailbox_node_get_parent(mn);
    sub_folder_data->old_folder = libbalsa_mailbox_get_name(mailbox);
    sub_folder_data->old_parent = balsa_mailbox_node_get_dir(sub_folder_data->parent);

    sub_folder_data->common_data.dialog = 
        GTK_DIALOG(gtk_dialog_new_with_buttons
                   (_("Remote IMAP subfolder"), 
                    GTK_WINDOW(balsa_app.main_window),
                    GTK_DIALOG_DESTROY_WITH_PARENT | /* must NOT be modal */
                    libbalsa_dialog_flags(),
                    _("_Apply"), GTK_RESPONSE_OK,
                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                    _("_Help"), GTK_RESPONSE_HELP,
                    NULL));
    content_area = gtk_dialog_get_content_area(sub_folder_data->common_data.dialog);
    g_object_add_weak_pointer(G_OBJECT(sub_folder_data->common_data.dialog),
                              (gpointer *) &sub_folder_data->common_data.dialog);
    /* `Enter' key => Create: */
    gtk_dialog_set_default_response(GTK_DIALOG(sub_folder_data->common_data.dialog), GTK_RESPONSE_OK);
    gtk_window_set_role(GTK_WINDOW(sub_folder_data->common_data.dialog), "subfolder_config_dialog");

    if (sub_folder_data->common_data.mbnode) {
        g_object_set_data_full(G_OBJECT(sub_folder_data->common_data.mbnode),
                               BALSA_FOLDER_CONF_IMAP_KEY, sub_folder_data, 
                               (GDestroyNotify) folder_conf_destroy_common_data);
    } else {
        sub_folder_data_new = sub_folder_data;
        g_object_add_weak_pointer(G_OBJECT(sub_folder_data->common_data.dialog),
                                  (gpointer *) &sub_folder_data_new);
    }

    grid = libbalsa_create_grid();
    gtk_grid_set_row_spacing(GTK_GRID(grid), HIG_PADDING);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 2 * HIG_PADDING);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 2 * HIG_PADDING);
    gtk_widget_set_vexpand(grid, TRUE);
    gtk_widget_set_valign(grid, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(content_area), grid);
 
    row = 0;
    /* INPUT FIELD CREATION */
    label= libbalsa_create_grid_label(_("_Folder name:"), grid, row);
    sub_folder_data->folder_name =
        libbalsa_create_grid_entry(grid, G_CALLBACK(validate_sub_folder),
                                   sub_folder_data, row, sub_folder_data->old_folder, label);

    ++row;
    (void) libbalsa_create_grid_label(_("Host:"), grid, row);
    sub_folder_data->host_label =
        gtk_label_new((sub_folder_data->common_data.mbnode != NULL &&
                       (server = balsa_mailbox_node_get_server(sub_folder_data->common_data.mbnode)) != NULL)
                      ? libbalsa_server_get_host(server) : "");
    gtk_widget_set_halign(sub_folder_data->host_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(sub_folder_data->host_label, TRUE);
    gtk_grid_attach(GTK_GRID(grid), sub_folder_data->host_label, 1, row, 1, 1);

    ++row;
    (void) libbalsa_create_grid_label(_("Subfolder of:"), grid, row);
    sub_folder_data->parent_folder = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(sub_folder_data->parent_folder), FALSE);
    gtk_widget_set_can_focus(sub_folder_data->parent_folder, FALSE);
    gtk_entry_set_text(GTK_ENTRY(sub_folder_data->parent_folder), sub_folder_data->old_parent);

    button = gtk_button_new_with_mnemonic(_("_Browse…"));
    g_signal_connect(button, "clicked",
		     G_CALLBACK(browse_button_cb), (gpointer) sub_folder_data);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2 * HIG_PADDING);

    gtk_widget_set_hexpand(sub_folder_data->parent_folder, TRUE);
    gtk_widget_set_halign(sub_folder_data->parent_folder, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(hbox), sub_folder_data->parent_folder);
    gtk_container_add(GTK_CONTAINER(hbox), button);

    gtk_widget_set_hexpand(hbox, TRUE);
    gtk_grid_attach(GTK_GRID(grid), hbox, 1, row, 1, 1);

    {
        static const char *std_acls[] = {
            "lrs", N_("read-only"),
            "lrswipkxte", N_("read-write"),
            "lrswipkxtea", N_("admin"),
            "lrsp", N_("post"),
            "lrsip", N_("append"),
            "lrxte", N_("delete"),
            NULL, N_("special") };
        GString *rights_str;
        gchar * rights;
        gchar * quotas;
        gboolean readonly;

        ++row;
        (void) libbalsa_create_grid_label(_("Permissions:"), grid, row);

        /* mailbox closed: no detailed permissions available */
        readonly = libbalsa_mailbox_get_readonly(mailbox);
        if (!libbalsa_mailbox_imap_is_connected(LIBBALSA_MAILBOX_IMAP(mailbox))) {
            rights_str = g_string_new(std_acls[readonly ? 1 : 3]);
            rights_str =
                g_string_append(rights_str,
                                _("\ndetailed permissions are available only for open folders"));
        } else {
            rights = libbalsa_imap_get_rights(LIBBALSA_MAILBOX_IMAP(mailbox));
            if (!rights) {
                rights_str = g_string_new(std_acls[readonly ? 1 : 3]);
                rights_str =
                    g_string_append(rights_str,
                                    _("\nthe server does not support ACLs"));
            } else {
                gint n;
                gchar **acls;

                /* my rights */
                for (n = 0;
                     g_strcmp0(std_acls[n], rights) != 0;
                     n += 2);
                rights_str = g_string_new(_("mine: "));
                if (std_acls[n])
                    rights_str = g_string_append(rights_str, std_acls[n + 1]);
                else
                    g_string_append_printf(rights_str, "%s (%s)",
                                           std_acls[n + 1], rights);

                /* acl's - only available if I have admin privileges */
                if ((acls = libbalsa_imap_get_acls(LIBBALSA_MAILBOX_IMAP(mailbox)))) {
                    int uid;

                    for (uid = 0; acls[uid]; uid += 2) {
                        for (n = 0;
                             g_strcmp0(std_acls[n], acls[uid + 1]) != 0;
                             n += 2);
                        if (std_acls[n])
                            g_string_append_printf(rights_str,
                                                   "\nuid '%s': %s",
                                                   acls[uid], std_acls[n + 1]);
                        else
                            g_string_append_printf(rights_str,
                                                   "\nuid '%s': %s (%s)",
                                                   acls[uid], std_acls[n + 1],
                                                   acls[uid + 1]);
                    }
                    g_strfreev(acls);
                }
                g_free(rights);
            }
        }
        rights = g_string_free(rights_str, FALSE);
        label = gtk_label_new(rights);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid), label, 1, row, 1, 1);
        g_free(rights);

        ++row;
        (void) libbalsa_create_grid_label(_("Quota:"), grid, row);

        /* mailbox closed: no quota available */
        if (!libbalsa_mailbox_imap_is_connected(LIBBALSA_MAILBOX_IMAP(mailbox)))
            quotas = g_strdup(_("quota information available only for open folders"));
        else {
            gulong max, used;

            if (!libbalsa_imap_get_quota(LIBBALSA_MAILBOX_IMAP(mailbox), &max, &used))
                quotas = g_strdup(_("the server does not support quotas"));
            else if (max == 0 && used == 0)
                quotas = g_strdup(_("no limits"));
            else {
                gchar *use_str = libbalsa_size_to_gchar(used * G_GUINT64_CONSTANT(1024));
                gchar *max_str = libbalsa_size_to_gchar(max * G_GUINT64_CONSTANT(1024));

                quotas = g_strdup_printf(_("%s of %s (%.1f%%) used"), use_str, max_str,
                                         100.0 * (float) used / (float) max);
                g_free(use_str);
                g_free(max_str);
            }
        }
        label = gtk_label_new(quotas);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_widget_set_hexpand(label, TRUE);
        gtk_grid_attach(GTK_GRID(grid), label, 1, row, 1, 1);
        g_free(quotas);

        sub_folder_data->mcv = mailbox_conf_view_new(mailbox,
                                         GTK_WINDOW(sub_folder_data->common_data.dialog),
                                         grid, 5,
                                         G_CALLBACK(set_ok_sensitive));
    }

    gtk_widget_show_all(GTK_WIDGET(sub_folder_data->common_data.dialog));

    gtk_widget_grab_focus(sub_folder_data->folder_name);

    g_signal_connect(sub_folder_data->common_data.dialog, "response",
                     G_CALLBACK(folder_conf_response), sub_folder_data);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(sub_folder_data->common_data.dialog),
                                      GTK_RESPONSE_OK, FALSE);
    gtk_widget_show_all(GTK_WIDGET(sub_folder_data->common_data.dialog));
}

void
folder_conf_delete(BalsaMailboxNode* mbnode)
{
    GtkWidget* ask;
    gint response;

    if (balsa_mailbox_node_get_config_prefix(mbnode) == NULL) {
        balsa_information(LIBBALSA_INFORMATION_ERROR,
	                  _("This folder is not stored in configuration. "
	                    "I do not yet know how to remove it "
                            "from remote server."));
	return;
    }
	
    ask = gtk_message_dialog_new(GTK_WINDOW(balsa_app.main_window), 0,
                                 GTK_MESSAGE_QUESTION,
                                 GTK_BUTTONS_OK_CANCEL,
                                 _("This will remove the folder "
                                   "“%s” from the list.\n"
                                   "You may use “New IMAP Folder” "
                                   "later to add this folder again.\n"),
                                 balsa_mailbox_node_get_name(mbnode));
    gtk_window_set_title(GTK_WINDOW(ask), _("Confirm"));

    response = gtk_dialog_run(GTK_DIALOG(ask));
    gtk_widget_destroy(ask);
    if (response != GTK_RESPONSE_OK)
	return;

    /* Delete it from the config file and internal nodes */
    config_folder_delete(mbnode);

    /* Remove the node from balsa's mailbox list */
    balsa_mblist_mailbox_node_remove(mbnode);
    update_mail_servers();
}

void
folder_conf_add_imap_cb(GtkWidget * widget, gpointer data)
{
    folder_conf_imap_node(NULL);
}

void
folder_conf_add_imap_sub_cb(GtkWidget * widget, gpointer data)
{
	BalsaMailboxNode *mbnode = BALSA_MAILBOX_NODE(data);

	if (mbnode != NULL) {
		GtkWidget *dialog;
		GtkWidget *content_area;
		GtkWidget *grid;
		GtkWidget *plabel;
		GtkWidget *label;
		GtkWidget *name_entry;
		gint row;
		int result;

		dialog = gtk_dialog_new_with_buttons(_("Create IMAP subfolder"),
             GTK_WINDOW(balsa_app.main_window),
             GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags(),
             _("_Create"), GTK_RESPONSE_ACCEPT,
             _("_Cancel"), GTK_RESPONSE_REJECT,
             NULL);
		gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_REJECT);
                content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

	    grid = libbalsa_create_grid();
	    gtk_grid_set_row_spacing(GTK_GRID(grid), HIG_PADDING);
	    gtk_grid_set_column_spacing(GTK_GRID(grid), 2 * HIG_PADDING);
	    gtk_container_set_border_width(GTK_CONTAINER(grid), 2 * HIG_PADDING);

            gtk_widget_set_vexpand(grid, TRUE);
            gtk_widget_set_valign(grid, GTK_ALIGN_FILL);
            gtk_container_add(GTK_CONTAINER(content_area), grid);

	    row = 0;
	    (void) libbalsa_create_grid_label(_("Subfolder of:"), grid, row);
	    plabel = gtk_label_new(balsa_mailbox_node_get_mailbox(mbnode) != NULL ?
                                   balsa_mailbox_node_get_dir(mbnode) : _("server (top level)"));
	    gtk_widget_set_halign(plabel, GTK_ALIGN_START);
	    gtk_widget_set_hexpand(plabel, TRUE);
	    gtk_grid_attach(GTK_GRID(grid), plabel, 1, row++, 1, 1);
	    label = libbalsa_create_grid_label(_("_Folder name:"), grid, row);
	    name_entry = libbalsa_create_grid_entry(grid, NULL, NULL, row, NULL, label);
	    gtk_widget_show_all(grid);

	    result = gtk_dialog_run(GTK_DIALOG(dialog));
	    if (result == GTK_RESPONSE_ACCEPT) {
	    	const gchar *new_name;
                gint delim = balsa_mailbox_node_get_delim(mbnode);

	        new_name = gtk_entry_get_text(GTK_ENTRY(name_entry));
	        if ((delim != 0) && (strchr(new_name, delim) != NULL)) {
	        	balsa_information(LIBBALSA_INFORMATION_ERROR,
	        		_("The character “%c” is used as hierarchy separator by the server "
	        		  "and therefore not permitted in the folder name."),
                                delim);
	        } else {
	        	GError *err = NULL;

	        	if (libbalsa_imap_new_subfolder(balsa_mailbox_node_get_dir(mbnode),
                                                        new_name,
                                                        balsa_mailbox_node_get_subscribed(mbnode),
                                                        balsa_mailbox_node_get_server(mbnode),
                                                        &err)) {
	        		/* see it as server sees it: */
	        		balsa_mailbox_node_rescan(mbnode);
	        	} else {
	        		balsa_information(LIBBALSA_INFORMATION_ERROR,
	        			_("Folder creation failed. Reason: %s"),
						err ? err->message : "unknown");
	        		g_clear_error(&err);
	        	}
	        }
	    }
	    gtk_widget_destroy(dialog);
	}
}
