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
#include "balsa-mime-widget-callbacks.h"

#include <string.h>
#include "balsa-app.h"
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include "libbalsa-vfs.h"
#include "balsa-message.h"
#include "balsa-mime-widget.h"

#include <gdk/gdkkeysyms.h>


void
balsa_mime_widget_ctx_menu_cb(GtkWidget * menu_item,
			      LibBalsaMessageBody * mime_body)
{
    GError *err = NULL;
    gboolean result;

    g_return_if_fail(mime_body != NULL);
    result = libbalsa_vfs_launch_app_for_body(mime_body,
                                              G_OBJECT(menu_item),
                                              &err);
    if (!result)
        balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("Could not launch application: %s"),
                          err ? err->message : _("Unknown error"));
    g_clear_error(&err);
}


/** Pops up a "save part" dialog for a message part.

    @param parent_widget the widget located in the window that is to
    became a parent for the dialog box.

    @param mime_body message part to be saved.
*/
typedef struct {
    GtkWidget *parent_widget;
    LibBalsaMessageBody *mime_body;
} balsa_mime_widget_ctx_menu_save_data_t;

static void balsa_mime_widget_ctx_menu_save_response(GtkDialog *self,
                                                     gint       response_id,
                                                     gpointer   user_data);

void
balsa_mime_widget_ctx_menu_save(GtkWidget * parent_widget,
				LibBalsaMessageBody * mime_body)
{
    gchar *cont_type, *title;
    GtkWidget *save_dialog;
    GtkFileChooser *save_chooser;
    balsa_mime_widget_ctx_menu_save_data_t *data;

    g_return_if_fail(mime_body != NULL);

    cont_type = libbalsa_message_body_get_mime_type(mime_body);
    title = g_strdup_printf(_("Save %s MIME Part"), cont_type);

    save_dialog =
	gtk_file_chooser_dialog_new(title,
                                    balsa_get_parent_window(parent_widget),
				    GTK_FILE_CHOOSER_ACTION_SAVE,
                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                    _("_OK"),     GTK_RESPONSE_OK,
                                    NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(save_dialog),
				    GTK_RESPONSE_OK);
    g_free(title);
    g_free(cont_type);

    save_chooser = GTK_FILE_CHOOSER(save_dialog);
    gtk_file_chooser_set_do_overwrite_confirmation(save_chooser, TRUE);
    gtk_file_chooser_set_local_only(save_chooser, libbalsa_vfs_local_only());
    if (balsa_app.save_dir)
        gtk_file_chooser_set_current_folder_uri(save_chooser, balsa_app.save_dir);

    if (mime_body->filename) {
        /* Note that LibBalsaMessageBody:filename is in UTF-8 encoding,
         * even if the system’s encoding for filenames is different. */
        gchar * filename = g_path_get_basename(mime_body->filename);
	gtk_file_chooser_set_current_name(save_chooser, filename);
	g_free(filename);
    }

    data = g_new(balsa_mime_widget_ctx_menu_save_data_t, 1);
    data->parent_widget = parent_widget;
    data->mime_body = mime_body;
    gtk_window_set_modal(GTK_WINDOW(save_dialog), TRUE);
    g_signal_connect(save_dialog, "response",
                     G_CALLBACK(balsa_mime_widget_ctx_menu_save_response), data);
    gtk_widget_show_all(save_dialog);
}

static void
balsa_mime_widget_ctx_menu_save_response(GtkDialog *self,
                                         gint       response_id,
                                         gpointer   user_data)
{
    GtkWidget *save_dialog = (GtkWidget *) self;
    GtkFileChooser *save_chooser = (GtkFileChooser *) self;
    balsa_mime_widget_ctx_menu_save_data_t *data = user_data;
    GtkWidget *parent_widget = data->parent_widget;
    LibBalsaMessageBody *mime_body = data->mime_body;
    GFile *save_file;
    gchar *save_path;
    gchar *tmp_path;
    int handle;

    if (response_id != GTK_RESPONSE_OK) {
	gtk_widget_destroy(save_dialog);
	return;
    }

    /* get the file name */
    save_file = gtk_file_chooser_get_file(save_chooser);

    /* remember the folder uri */
    g_free(balsa_app.save_dir);
    balsa_app.save_dir = gtk_file_chooser_get_current_folder_uri(save_chooser);

    gtk_widget_destroy(save_dialog);

    /* save the file */
    save_path = g_file_get_path(save_file);
    tmp_path = g_strconcat(save_path, "XXXXXX", NULL);
    handle = g_mkstemp(tmp_path);

    if (handle < 0) {
        int errsv = errno;

        balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("Failed to create file “%s”: %s"),
                          tmp_path, g_strerror(errsv));
    } else {
        gboolean ok;
        GError *err = NULL;
        ssize_t bytes_written;
        gchar *save_uri;

        ok = libbalsa_message_body_save_fs(mime_body, handle,
                                           mime_body->body_type ==
                                           LIBBALSA_MESSAGE_BODY_TYPE_TEXT,
                                           &bytes_written, &err);
        close(handle);

        save_uri = g_file_get_uri(save_file);
        if (!ok) {
            balsa_information(LIBBALSA_INFORMATION_ERROR,
                              _("Could not save %s: %s"),
                              save_uri, err ? err->message : _("Unknown error"));
            g_clear_error(&err);
        } else if (bytes_written == 0) {
            balsa_information(LIBBALSA_INFORMATION_WARNING,
                              _("Empty part was not saved to %s"), save_uri);
            g_unlink(tmp_path);
        } else {
            g_rename(tmp_path, save_path);
            balsa_mime_widget_view_save_dir(parent_widget);
        }
        g_free(save_uri);
    }

    g_object_unref(save_file);
    g_free(save_path);
    g_free(tmp_path);
    g_free(data);
}

void
balsa_mime_widget_view_save_dir(GtkWidget *widget)
{
	GAppInfo *app_info;

	app_info = (GAppInfo *) g_object_get_data(G_OBJECT(widget), BALSA_MIME_WIDGET_CB_APPINFO);
	if (app_info != NULL) {
		GList *list;
		GError *error = NULL;

		list = g_list_prepend(NULL, balsa_app.save_dir);
		if (!g_app_info_launch_uris(app_info, list, NULL, &error)) {
			balsa_information(LIBBALSA_INFORMATION_ERROR, _("Could not open folder %s: %s"),
				balsa_app.save_dir, error ? error->message : _("Unknown error"));
			g_clear_error(&error);
		}
		g_list_free(list);
	}
}

static void
scroll_change(GtkAdjustment * adj, gint diff, BalsaMessage * bm)
{
    gdouble upper =
        gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj);
    gdouble value;

    if (bm && gtk_adjustment_get_value(adj) >= upper && diff > 0) {
        if (balsa_window_next_unread(balsa_app.main_window))
            /* We're changing mailboxes, and GtkNotebook will grab the
             * focus, so we want to grab it back the next time we lose
             * it. */
            balsa_message_set_focus_state(bm, BALSA_MESSAGE_FOCUS_STATE_HOLD);
        return;
    }

    value = gtk_adjustment_get_value(adj);
    value += (gdouble) diff;

    gtk_adjustment_set_value(adj, MIN(value, upper));
}

gboolean
balsa_mime_widget_key_pressed(GtkEventControllerKey *controller,
                              guint                  keyval,
                              guint                  keycode,
                              GdkModifierType        state,
                              gpointer               user_data)
{
    BalsaMessage *bm = user_data;
    GtkAdjustment *adj;
    int page_adjust;

    adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW
                                              (balsa_message_get_scroll(bm)));

    page_adjust = balsa_app.pgdownmod ?
        (gtk_adjustment_get_page_size(adj) * balsa_app.pgdown_percent) /
        100 : gtk_adjustment_get_page_increment(adj);

    switch (keyval) {
    case GDK_KEY_Up:
        scroll_change(adj, -gtk_adjustment_get_step_increment(adj), NULL);
        break;
    case GDK_KEY_Down:
        scroll_change(adj, gtk_adjustment_get_step_increment(adj), NULL);
        break;
    case GDK_KEY_Page_Up:
        scroll_change(adj, -page_adjust, NULL);
        break;
    case GDK_KEY_Page_Down:
        scroll_change(adj, page_adjust, NULL);
        break;
    case GDK_KEY_Home:
        if (state & GDK_CONTROL_MASK)
            scroll_change(adj, -gtk_adjustment_get_value(adj), NULL);
        else
            return FALSE;
        break;
    case GDK_KEY_End:
        if (state & GDK_CONTROL_MASK)
            scroll_change(adj, gtk_adjustment_get_upper(adj), NULL);
        else
            return FALSE;
        break;
    case GDK_KEY_F10:
        if (state & GDK_SHIFT_MASK) {
	    GtkWidget *current_widget = balsa_message_current_part_widget(bm);

	    if (current_widget) {
                gboolean retval;

                g_signal_emit_by_name(current_widget, "popup-menu", &retval);

                return retval;
            } else
		return FALSE;
        } else
            return FALSE;
        break;
    case GDK_KEY_space:
        scroll_change(adj, page_adjust, bm);
        break;

    default:
        return FALSE;
    }
    return TRUE;
}

/*
 * Focus callbacks
 */

static void
bmw_set_can_focus(GtkWidget *widget,
                  gpointer   data)
{
    if (GTK_IS_CONTAINER(widget))
        gtk_container_foreach(GTK_CONTAINER(widget), bmw_set_can_focus, data);
    gtk_widget_set_can_focus(widget, GPOINTER_TO_INT(data));
}


void
balsa_mime_widget_limit_focus(GtkEventControllerKey *key_controller,
                              gpointer               user_data)
{
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(key_controller));
    BalsaMessage *bm = user_data;
    GtkWidget *container = balsa_mime_widget_get_container(balsa_message_get_bm_widget(bm));

    /* Disable can_focus on other message parts so that TAB does not
     * attempt to move the focus on them. */
    bmw_set_can_focus(container, GINT_TO_POINTER(FALSE));
    gtk_widget_set_can_focus(widget, TRUE);

    if (balsa_message_get_focus_state(bm) == BALSA_MESSAGE_FOCUS_STATE_NO)
        balsa_message_set_focus_state(bm, BALSA_MESSAGE_FOCUS_STATE_YES);
}


void
balsa_mime_widget_unlimit_focus(GtkEventControllerKey *key_controller,
                                gpointer               user_data)
{
    BalsaMessage *bm = user_data;
    GtkWidget *container = balsa_mime_widget_get_container(balsa_message_get_bm_widget(bm));

    bmw_set_can_focus(container, GINT_TO_POINTER(TRUE));

    if (balsa_message_get_message(bm) != NULL) {
        BalsaMessageFocusState focus_state = balsa_message_get_focus_state(bm);

        if (focus_state == BALSA_MESSAGE_FOCUS_STATE_HOLD) {
            balsa_message_grab_focus(bm);
            balsa_message_set_focus_state(bm, BALSA_MESSAGE_FOCUS_STATE_YES);
        } else
            balsa_message_set_focus_state(bm, BALSA_MESSAGE_FOCUS_STATE_NO);
    }
}
