/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2013 Stuart Parmenter and others,
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
#include "balsa-mime-widget-callbacks.h"

#include <string.h>
#include "balsa-app.h"
#include <glib/gi18n.h>
#include "libbalsa-vfs.h"
#include "balsa-message.h"
#include "balsa-mime-widget.h"

#include <gdk/gdkkeysyms.h>

#if HAVE_MACOSX_DESKTOP
#  include "macosx-helpers.h"
#endif


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
                          err ? err->message : "Unknown error");
    g_clear_error(&err);
}


/** Pops up a "save part" dialog for a message part.

    @param parent_widget the widget located in the window that is to
    became a parent for the dialog box.

    @param mime_body message part to be saved.
*/
void
balsa_mime_widget_ctx_menu_save(GtkWidget * parent_widget,
				LibBalsaMessageBody * mime_body)
{
    gchar *cont_type, *title;
    GtkWidget *save_dialog;
    gchar *file_uri;
    LibbalsaVfs *save_file;
    gboolean do_save;
    GError *err = NULL;

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
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(save_dialog, balsa_get_parent_window(parent_widget));
#endif
    gtk_dialog_set_default_response(GTK_DIALOG(save_dialog),
				    GTK_RESPONSE_OK);
    g_free(title);
    g_free(cont_type);

    gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(save_dialog),
                                    libbalsa_vfs_local_only());
    if (balsa_app.save_dir)
        gtk_file_chooser_set_current_folder_uri(GTK_FILE_CHOOSER(save_dialog),
                                                balsa_app.save_dir);

    if (mime_body->filename) {
        gchar * filename = g_strdup(mime_body->filename);
	libbalsa_utf8_sanitize(&filename, balsa_app.convert_unknown_8bit,
			       NULL);
	gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(save_dialog),
					  filename);
	g_free(filename);
    }

    gtk_window_set_modal(GTK_WINDOW(save_dialog), TRUE);
    if (gtk_dialog_run(GTK_DIALOG(save_dialog)) != GTK_RESPONSE_OK) {
	gtk_widget_destroy(save_dialog);
	return;
    }

    /* get the file name */
    file_uri = gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(save_dialog));
    gtk_widget_destroy(save_dialog);
    if (!(save_file = libbalsa_vfs_new_from_uri(file_uri))) {
        balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("Could not construct URI from %s"),
                          file_uri);
        g_free(file_uri);
	return;
    }

    /* remember the folder uri */
    g_free(balsa_app.save_dir);
    balsa_app.save_dir = g_strdup(libbalsa_vfs_get_folder(save_file));

    /* get a confirmation to overwrite if the file exists */
    if (libbalsa_vfs_file_exists(save_file)) {
	GtkWidget *confirm;

	/* File exists. check if they really want to overwrite */
	confirm = gtk_message_dialog_new(GTK_WINDOW(balsa_app.main_window),
					 GTK_DIALOG_MODAL |
                                         GTK_DIALOG_USE_HEADER_BAR,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_YES_NO,
					 _("File already exists. Overwrite?"));
#if HAVE_MACOSX_DESKTOP
	libbalsa_macosx_menu_for_parent(confirm, GTK_WINDOW(balsa_app.main_window));
#endif
	do_save =
	    (gtk_dialog_run(GTK_DIALOG(confirm)) == GTK_RESPONSE_YES);
	gtk_widget_destroy(confirm);
	if (do_save)
	    if (libbalsa_vfs_file_unlink(save_file, &err) != 0)
                balsa_information(LIBBALSA_INFORMATION_ERROR,
                                  _("Unlink %s: %s"),
                                  file_uri, err ? err->message : "Unknown error");
    } else
	do_save = TRUE;

    /* save the file */
    if (do_save) {
	if (!libbalsa_message_body_save_vfs(mime_body, save_file,
                                            LIBBALSA_MESSAGE_BODY_UNSAFE,
                                            mime_body->body_type ==
                                            LIBBALSA_MESSAGE_BODY_TYPE_TEXT,
                                            &err)) {
	    balsa_information(LIBBALSA_INFORMATION_ERROR,
			      _("Could not save %s: %s"),
			      file_uri, err ? err->message : "Unknown error");
            g_clear_error(&err);
        }
    }

    g_object_unref(save_file);
    g_free(file_uri);
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
            bm->focus_state = BALSA_MESSAGE_FOCUS_STATE_HOLD;
        return;
    }

    value = gtk_adjustment_get_value(adj);
    value += (gdouble) diff;

    gtk_adjustment_set_value(adj, MIN(value, upper));
}

gint
balsa_mime_widget_key_press_event(GtkWidget * widget, GdkEventKey * event,
				  BalsaMessage * bm)
{
    GtkAdjustment *adj;
    int page_adjust;

    adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW
                                              (bm->scroll));

    page_adjust = balsa_app.pgdownmod ?
        (gtk_adjustment_get_page_size(adj) * balsa_app.pgdown_percent) /
        100 : gtk_adjustment_get_page_increment(adj);

    switch (event->keyval) {
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
        if (event->state & GDK_CONTROL_MASK)
            scroll_change(adj, -gtk_adjustment_get_value(adj), NULL);
        else
            return FALSE;
        break;
    case GDK_KEY_End:
        if (event->state & GDK_CONTROL_MASK)
            scroll_change(adj, gtk_adjustment_get_upper(adj), NULL);
        else
            return FALSE;
        break;
    case GDK_KEY_F10:
        if (event->state & GDK_SHIFT_MASK) {
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


gint
balsa_mime_widget_limit_focus(GtkWidget * widget, GdkEventFocus * event, BalsaMessage * bm)
{
    /* Disable can_focus on other message parts so that TAB does not
     * attempt to move the focus on them. */
    GList *list = g_list_append(NULL, widget);

    gtk_container_set_focus_chain(GTK_CONTAINER(bm->bm_widget->container), list);
    g_list_free(list);
    if (bm->focus_state == BALSA_MESSAGE_FOCUS_STATE_NO)
        bm->focus_state = BALSA_MESSAGE_FOCUS_STATE_YES;
    return FALSE;
}


gint
balsa_mime_widget_unlimit_focus(GtkWidget * widget, GdkEventFocus * event, BalsaMessage * bm)
{
    gtk_container_unset_focus_chain(GTK_CONTAINER(bm->bm_widget->container));
    if (bm->message) {
        BalsaMessageFocusState focus_state = bm->focus_state;
        if (focus_state == BALSA_MESSAGE_FOCUS_STATE_HOLD) {
            balsa_message_grab_focus(bm);
            bm->focus_state = BALSA_MESSAGE_FOCUS_STATE_YES;
        } else
            bm->focus_state = BALSA_MESSAGE_FOCUS_STATE_NO;
    }
    return FALSE;
}
