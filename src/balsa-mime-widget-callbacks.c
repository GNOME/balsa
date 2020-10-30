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
#include <glib.h>
#include "libbalsa-vfs.h"
#include "balsa-message.h"
#include "balsa-mime-widget.h"

#include <gdk/gdkkeysyms.h>

#if HAVE_MACOSX_DESKTOP
#  include "macosx-helpers.h"
#endif


void
balsa_mime_widget_ctx_menu_launch_app(const gchar         *app_name,
                                      LibBalsaMessageBody *mime_body)
{
    GError *err = NULL;
    gboolean result;

    g_return_if_fail(mime_body != NULL);

    result = libbalsa_vfs_launch_app_for_body(mime_body, app_name, &err);
    if (!result)
        balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("Could not launch application: %s"),
                          err ? err->message : _("Unknown error"));
    g_clear_error(&err);
}

void
balsa_mime_widget_ctx_menu_cb(GtkWidget *button,
                              gpointer   user_data)
{
    LibBalsaMessageBody *mime_body = user_data;
    const gchar *app_name;

    app_name = g_object_get_data(G_OBJECT(button), LIBBALSA_VFS_MIME_ACTION);
    balsa_mime_widget_ctx_menu_launch_app(app_name, mime_body);
}

/** Pops up a "save part" dialog for a message part.

    @param parent_widget the widget located in the window that is to
    became a parent for the dialog box.

    @param mime_body message part to be saved.
*/
typedef struct {
    GMutex lock;
    GCond cond;
    LibBalsaMessageBody *mime_body;
    LibbalsaVfs *save_file;
    char *file_uri;
    int response_id;
} ctx_menu_data;

static void
ctx_menu_confirm(GtkDialog *confirm,
                 int        response_id,
                 gpointer   user_data)
{
    ctx_menu_data *data = user_data;

    g_mutex_lock(&data->lock);
    data->response_id = response_id;
    g_cond_signal(&data->cond);
    g_mutex_unlock(&data->lock);

    gtk_window_destroy(GTK_WINDOW(confirm));
}

static gboolean
ctx_menu_idle(gpointer user_data)
{
    ctx_menu_data *data = user_data;
    GtkWidget *confirm;

    /* File exists. check if they really want to overwrite */
    confirm = gtk_message_dialog_new(GTK_WINDOW(balsa_app.main_window),
                                     GTK_DIALOG_MODAL,
                                     GTK_MESSAGE_QUESTION,
                                     GTK_BUTTONS_YES_NO, _("File already exists. Overwrite?"));
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(confirm, GTK_WINDOW(balsa_app.main_window));
#endif

    g_signal_connect(confirm, "response", G_CALLBACK(ctx_menu_confirm), data);
    gtk_widget_show(confirm);

    return G_SOURCE_REMOVE;
}

static gpointer
ctx_menu_thread(gpointer user_data)
{
    ctx_menu_data *data = user_data;
    gboolean do_save;

    /* get a confirmation to overwrite if the file exists */
    if (libbalsa_vfs_file_exists(data->save_file)) {
        g_mutex_init(&data->lock);
        g_cond_init(&data->cond);

        g_mutex_lock(&data->lock);
        data->response_id = 0;
        g_idle_add(ctx_menu_idle, data);
        while (data->response_id == 0)
            g_cond_wait(&data->cond, &data->lock);
        g_mutex_unlock(&data->lock);

        g_mutex_clear(&data->lock);
        g_cond_clear(&data->cond);

	do_save = data->response_id == GTK_RESPONSE_YES;
	if (do_save) {
            GError *err = NULL;

	    if (libbalsa_vfs_file_unlink(data->save_file, &err) != 0) {
                balsa_information(LIBBALSA_INFORMATION_ERROR,
                                  _("Unlink %s: %s"),
                                  data->file_uri, err != NULL ? err->message : _("Unknown error"));
                g_clear_error(&err);
            }
        }
    } else
	do_save = TRUE;

    /* save the file */
    if (do_save) {
        GError *err = NULL;

	if (!libbalsa_message_body_save_vfs(data->mime_body, data->save_file,
                                            LIBBALSA_MESSAGE_BODY_UNSAFE,
                                            data->mime_body->body_type ==
                                            LIBBALSA_MESSAGE_BODY_TYPE_TEXT,
                                            &err)) {
	    balsa_information(LIBBALSA_INFORMATION_ERROR,
			      _("Could not save %s: %s"),
			      data->file_uri, err != NULL ? err->message : _("Unknown error"));
            g_clear_error(&err);
        }
    }

    g_object_unref(data->save_file);
    g_free(data->file_uri);
    g_free(data);

    return NULL;
}

static void
ctx_menu_response(GtkDialog *save_dialog,
                  int        response_id,
                  gpointer   user_data)
{
    ctx_menu_data *data = user_data;
    GFile *file;

    if (response_id != GTK_RESPONSE_OK) {
	gtk_window_destroy(GTK_WINDOW(save_dialog));
        g_free(data);

	return;
    }

    /* get the file name */
    file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(save_dialog));
    gtk_window_destroy(GTK_WINDOW(save_dialog));

    data->file_uri = g_file_get_uri(file);
    g_object_unref(file);

    if ((data->save_file = libbalsa_vfs_new_from_uri(data->file_uri)) == NULL) {
        balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("Could not construct URI from %s"),
                          data->file_uri);
        g_free(data->file_uri);
        g_free(data);

	return;
    }

    /* remember the folder uri */
    g_free(balsa_app.save_dir);
    balsa_app.save_dir = g_strdup(libbalsa_vfs_get_folder(data->save_file));

    /* do the rest in a thread, as it may block waiting for user
     * confirmation */
    g_thread_unref(g_thread_new("ctx-menu-thread", ctx_menu_thread, data));
}

void
balsa_mime_widget_ctx_menu_save(GtkWidget * parent_widget,
				LibBalsaMessageBody * mime_body)
{
    ctx_menu_data *data;
    char *cont_type, *title;
    GtkWidget *save_dialog;

    g_return_if_fail(mime_body != NULL);

    cont_type = libbalsa_message_body_get_mime_type(mime_body);
    title = g_strdup_printf(_("Save %s MIME Part"), cont_type);
    g_free(cont_type);

    save_dialog =
	gtk_file_chooser_dialog_new(title,
                                    balsa_get_parent_window(parent_widget),
				    GTK_FILE_CHOOSER_ACTION_SAVE,
                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                    _("_OK"),     GTK_RESPONSE_OK,
                                    NULL);
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(save_dialog, balsa_get_parent_window(parent_widget));
#endif
    gtk_dialog_set_default_response(GTK_DIALOG(save_dialog), GTK_RESPONSE_OK);
    g_free(title);

    if (balsa_app.save_dir != NULL) {
        GFile *file;

        file = g_file_new_for_uri(balsa_app.save_dir);
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(save_dialog), file, NULL);
        g_object_unref(file);
    }

    if (mime_body->filename) {
        char *filename = g_path_get_basename(mime_body->filename);
	libbalsa_utf8_sanitize(&filename, balsa_app.convert_unknown_8bit, NULL);
	gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(save_dialog), filename);
	g_free(filename);
    }

    data = g_new(ctx_menu_data, 1);
    data->mime_body = mime_body;
    g_signal_connect(save_dialog, "response", G_CALLBACK(ctx_menu_response), data);

    gtk_window_set_modal(GTK_WINDOW(save_dialog), TRUE);
    gtk_widget_show(save_dialog);
}

static void
scroll_change(GtkAdjustment * adj, gint diff, BalsaMessage * bm)
{
    gdouble upper =
        gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj);
    gdouble value;

    if (bm != NULL && gtk_adjustment_get_value(adj) >= upper && diff > 0) {
        /* Scrolling below the bottom of a message means select the next
         * unread message: */
        balsa_window_next_unread(balsa_app.main_window, bm);

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
                  gboolean   can_focus)
{
    GtkWidget *child;

    for (child = gtk_widget_get_first_child(widget);
         child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        bmw_set_can_focus(child, can_focus);
    }

    gtk_widget_set_can_focus(widget, can_focus);
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
    bmw_set_can_focus(container, FALSE);
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

    bmw_set_can_focus(container, TRUE);

    if (balsa_message_get_message(bm) != NULL) {
        BalsaMessageFocusState focus_state = balsa_message_get_focus_state(bm);

        if (focus_state == BALSA_MESSAGE_FOCUS_STATE_HOLD) {
            balsa_message_grab_focus(bm);
            balsa_message_set_focus_state(bm, BALSA_MESSAGE_FOCUS_STATE_YES);
        } else
            balsa_message_set_focus_state(bm, BALSA_MESSAGE_FOCUS_STATE_NO);
    }
}
