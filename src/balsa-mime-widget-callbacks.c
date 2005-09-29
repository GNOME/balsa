/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2001 Stuart Parmenter and others,
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
#include <libgnomevfs/gnome-vfs-mime-info.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>
#include "config.h"
#include "balsa-app.h"
#include "i18n.h"
#include "balsa-message.h"
#include "balsa-mime-widget.h"
#include "balsa-mime-widget-callbacks.h"


void
balsa_mime_widget_ctx_menu_cb(GtkWidget * menu_item,
			      LibBalsaMessageBody * mime_body)
{
    gchar *content_type, *fpos;
    const gchar *cmd;
    gchar *key;
    GError *err = NULL;
    g_return_if_fail(mime_body != NULL);

    content_type = libbalsa_message_body_get_mime_type(mime_body);
    key = g_object_get_data(G_OBJECT(menu_item), "mime_action");

    if (key != NULL
	&& (cmd = gnome_vfs_mime_get_value(content_type, key)) != NULL) {
	if (!libbalsa_message_body_save_temporary(mime_body, &err)) {
	    balsa_information(LIBBALSA_INFORMATION_WARNING,
			      _("Could not create temporary file %s: "),
			      mime_body->temp_filename,
                              err ? err->message : "Unknown error");
            g_clear_error(&err);
	    g_free(content_type);
	    return;
	}

	if ((fpos = strstr(cmd, "%f")) != NULL) {
	    gchar *exe_str, *cps = g_strdup(cmd);
	    cps[fpos - cmd + 1] = 's';
	    exe_str = g_strdup_printf(cps, mime_body->temp_filename);
	    gnome_execute_shell(NULL, exe_str);
	    fprintf(stderr, "Executed: %s\n", exe_str);
	    g_free(cps);
	    g_free(exe_str);
	}
    } else
	fprintf(stderr, "view for %s returned NULL\n", content_type);

    g_free(content_type);
}


void
balsa_mime_widget_ctx_menu_vfs_cb(GtkWidget * menu_item,
				  LibBalsaMessageBody * mime_body)
{
    gchar *id;

    if ((id = g_object_get_data(G_OBJECT(menu_item), "mime_action"))) {
        GError *err = NULL;
#if HAVE_GNOME_VFS29
	GnomeVFSMimeApplication *app =
	    gnome_vfs_mime_application_new_from_desktop_id(id);
#else				/* HAVE_GNOME_VFS29 */
	GnomeVFSMimeApplication *app =
	    gnome_vfs_mime_application_new_from_id(id);
#endif				/* HAVE_GNOME_VFS29 */
	if (app) {
	    if (libbalsa_message_body_save_temporary(mime_body, &err)) {
#if HAVE_GNOME_VFS29
		gchar *uri =
		    g_strconcat("file://", mime_body->temp_filename,
				NULL);
		GList *uris = g_list_prepend(NULL, uri);
		gnome_vfs_mime_application_launch(app, uris);
		g_free(uri);
		g_list_free(uris);
#else				/* HAVE_GNOME_VFS29 */
		gboolean tmp =
		    (app->expects_uris ==
		     GNOME_VFS_MIME_APPLICATION_ARGUMENT_TYPE_URIS);
		gchar *exe_str =
		    g_strdup_printf("%s \"%s%s\"", app->command,
				    tmp ? "file://" : "",
				    mime_body->temp_filename);

		gnome_execute_shell(NULL, exe_str);
		fprintf(stderr, "Executed: %s\n", exe_str);
		g_free(exe_str);
#endif				/* HAVE_GNOME_VFS29 */
	    } else {
		balsa_information(LIBBALSA_INFORMATION_WARNING,
				  _("could not create temporary file %s: %s"),
				  mime_body->temp_filename,
                                  err ? err->message : "Unknown error");
	    }
	    gnome_vfs_mime_application_free(app);
	} else {
	    fprintf(stderr, "lookup for application %s returned NULL\n",
		    id);
	}
    }
}


void
balsa_mime_widget_ctx_menu_save(GtkWidget * menu_item,
				LibBalsaMessageBody * mime_body)
{
    gchar *cont_type, *title;
    GtkWidget *save_dialog;
    gchar *filename;
    gboolean do_save;
    GError *err = NULL;

    g_return_if_fail(mime_body != NULL);

    cont_type = libbalsa_message_body_get_mime_type(mime_body);
    title = g_strdup_printf(_("Save %s MIME Part"), cont_type);
    save_dialog =
	gtk_file_chooser_dialog_new(title,
				    GTK_WINDOW(balsa_app.main_window),
				    GTK_FILE_CHOOSER_ACTION_SAVE,
				    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				    GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(save_dialog),
				    GTK_RESPONSE_OK);
    g_free(title);
    g_free(cont_type);

    if (balsa_app.save_dir)
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(save_dialog),
					    balsa_app.save_dir);

    if (mime_body->filename) {
	gchar *filename = g_strdup(mime_body->filename);
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

    /* attempt to save the file */
    filename
	= gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(save_dialog));
    gtk_widget_destroy(save_dialog);

    g_free(balsa_app.save_dir);
    balsa_app.save_dir = g_path_get_dirname(filename);

    if (access(filename, F_OK) == 0) {
	GtkWidget *confirm;

	/* File exists. check if they really want to overwrite */
	confirm = gtk_message_dialog_new(GTK_WINDOW(balsa_app.main_window),
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_YES_NO,
					 _("File already exists. Overwrite?"));
	do_save =
	    (gtk_dialog_run(GTK_DIALOG(confirm)) == GTK_RESPONSE_YES);
	gtk_widget_destroy(confirm);
	if (do_save)
	    unlink(filename);
    } else
	do_save = TRUE;

    if (do_save)
	if (!libbalsa_message_body_save(mime_body, filename,
                                        LIBBALSA_MESSAGE_BODY_UNSAFE,
					mime_body->body_type ==
					LIBBALSA_MESSAGE_BODY_TYPE_TEXT, &err))
	    balsa_information(LIBBALSA_INFORMATION_ERROR,
			      _("Could not save %s: %s"),
			      filename, err ? err->message : "Unknown error");
    g_free(filename);
}

static void
scroll_change(GtkAdjustment * adj, gint diff)
{
    gfloat upper;

    adj->value += diff;

    upper = adj->upper - adj->page_size;
    adj->value = MIN(adj->value, upper);
    adj->value = MAX(adj->value, 0.0);

    g_signal_emit_by_name(G_OBJECT(adj), "value_changed", 0);
}

gint
balsa_mime_widget_key_press_event(GtkWidget * widget, GdkEventKey * event,
				  BalsaMessage * bm)
{
    GtkViewport *viewport;
    int page_adjust;

    viewport = GTK_VIEWPORT(bm->cont_viewport);

    if (balsa_app.pgdownmod) {
            page_adjust = (viewport->vadjustment->page_size *
                 balsa_app.pgdown_percent) / 100;
    } else {
            page_adjust = viewport->vadjustment->page_increment;
    }

    switch (event->keyval) {
    case GDK_Up:
        scroll_change(viewport->vadjustment,
                      -viewport->vadjustment->step_increment);
        break;
    case GDK_Down:
        scroll_change(viewport->vadjustment,
                      viewport->vadjustment->step_increment);
        break;
    case GDK_Page_Up:
        scroll_change(viewport->vadjustment,
                      -page_adjust);
        break;
    case GDK_Page_Down:
        scroll_change(viewport->vadjustment,
                      page_adjust);
        break;
    case GDK_Home:
        if (event->state & GDK_CONTROL_MASK)
            scroll_change(viewport->vadjustment,
                          -viewport->vadjustment->value);
        else
            return FALSE;
        break;
    case GDK_End:
        if (event->state & GDK_CONTROL_MASK)
            scroll_change(viewport->vadjustment,
                          viewport->vadjustment->upper);
        else
            return FALSE;
        break;
    case GDK_F10:
        if (event->state & GDK_SHIFT_MASK) {
	    GtkWidget *current_widget = balsa_message_current_part_widget(bm);

	    if (current_widget)
		g_signal_emit_by_name(current_widget, "popup-menu");
	    else
		return FALSE;
        } else
            return FALSE;
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
    return FALSE;
}


gint
balsa_mime_widget_unlimit_focus(GtkWidget * widget, GdkEventFocus * event, BalsaMessage * bm)
{
    gtk_container_unset_focus_chain(GTK_CONTAINER(bm->bm_widget->container));
    return FALSE;
}

