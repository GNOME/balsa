/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2020 Peter Bloomfield
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

#include "file-chooser-button.h"
#include <glib/gi18n.h>

/*
 * libbalsa_file_chooser_button
 *
 * Replacement in Gtk4 for GtkFileChooserButton
 */

#define LIBBALSA_FILE_CHOOSER_BUTTON_KEY "libbalsa-file-chooser-button"

typedef struct {
    GtkWidget           *button;
    char                *title;
    GtkFileChooserAction action;
    GFile               *file;
    GCallback            response_cb;
    gpointer             response_data;
    GtkWidget           *dialog;
} libbalsa_file_chooser_button_data;

static void
libbalsa_file_chooser_button_free(gpointer user_data)
{
    libbalsa_file_chooser_button_data *data = user_data;

    g_free(data->title);
    g_object_unref(data->file);
    g_free(data);
}

static void
libbalsa_file_chooser_button_response(GtkDialog *dialog,
                                      int        response_id,
                                      gpointer   user_data)
{
    libbalsa_file_chooser_button_data *data = user_data;

    gtk_widget_hide(GTK_WIDGET(dialog));

    if (response_id == GTK_RESPONSE_ACCEPT) {
        GFile *file;
        char *basename;

        file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        g_set_object(&data->file, file);
        basename = g_file_get_basename(file);
        g_object_unref(file);

        gtk_button_set_label(GTK_BUTTON(data->button), basename);
        g_free(basename);

        gtk_label_set_xalign(GTK_LABEL(gtk_button_get_child(GTK_BUTTON(data->button))), 0.0);

        if (data->response_cb != NULL)
            ((GFunc) data->response_cb)(data->button, data->response_data);
    }

    gtk_window_destroy(GTK_WINDOW(dialog));
    data->dialog = NULL;
}

static void
libbalsa_file_chooser_button_clicked(GtkButton *button,
                                     gpointer   user_data)
{
    libbalsa_file_chooser_button_data *data = user_data;

    if (data->dialog == NULL) {
        data->dialog =
            gtk_file_chooser_dialog_new(data->title,
                                        GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(button))),
                                        data->action,
                                        _("_Cancel"), GTK_RESPONSE_CANCEL,
                                        _("_Select"), GTK_RESPONSE_ACCEPT,
                                        NULL);
        g_signal_connect(data->dialog, "response",
                         G_CALLBACK(libbalsa_file_chooser_button_response), data);
    }

    gtk_file_chooser_set_file(GTK_FILE_CHOOSER(data->dialog), data->file, NULL);

    gtk_widget_show(data->dialog);
}

static GtkWidget *
libbalsa_file_chooser_button_from_data(libbalsa_file_chooser_button_data *data)
{
    data->file = g_file_new_for_path("");

    data->button = gtk_button_new();
    g_object_set_data_full(G_OBJECT(data->button), LIBBALSA_FILE_CHOOSER_BUTTON_KEY, data,
                           libbalsa_file_chooser_button_free);
    g_signal_connect(data->button, "clicked", G_CALLBACK(libbalsa_file_chooser_button_clicked), data);

    return data->button;
}

GtkWidget *
libbalsa_file_chooser_button_new(const char          *title,
                                 GtkFileChooserAction action,
                                 GCallback            response_cb,
                                 gpointer             response_data)
{
    libbalsa_file_chooser_button_data *data;

    data = g_new0(libbalsa_file_chooser_button_data, 1);
    data->title = g_strdup(title);
    data->action = action;
    data->response_cb = response_cb;
    data->response_data = response_data;

    return libbalsa_file_chooser_button_from_data(data);
}

GtkWidget *
libbalsa_file_chooser_button_new_with_dialog(GtkWidget *dialog)
{
    libbalsa_file_chooser_button_data *data;

    data = g_new0(libbalsa_file_chooser_button_data, 1);
    data->dialog = dialog;

    return libbalsa_file_chooser_button_from_data(data);
}

void
libbalsa_file_chooser_button_set_file(GtkWidget *button, GFile *file)
{
    libbalsa_file_chooser_button_data *data;
    char *basename;

    g_return_if_fail(GTK_IS_BUTTON(button));
    g_return_if_fail(G_IS_FILE(file));

    data = g_object_get_data(G_OBJECT(button), LIBBALSA_FILE_CHOOSER_BUTTON_KEY);

    g_return_if_fail(data != NULL);

    basename = g_file_get_basename(file);
    gtk_button_set_label(GTK_BUTTON(data->button), basename);
    g_free(basename);

    gtk_label_set_xalign(GTK_LABEL(gtk_button_get_child(GTK_BUTTON(data->button))), 0.0);

    g_set_object(&data->file, file);
}

GFile *
libbalsa_file_chooser_button_get_file(GtkWidget *button)
{
    libbalsa_file_chooser_button_data *data;

    g_return_val_if_fail(GTK_IS_BUTTON(button), NULL);

    data = g_object_get_data(G_OBJECT(button), LIBBALSA_FILE_CHOOSER_BUTTON_KEY);

    g_return_val_if_fail(data != NULL, NULL);

    return g_object_ref(data->file);
}
