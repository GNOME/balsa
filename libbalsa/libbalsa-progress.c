/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * Flexible progress dialogue for Balsa
 * Copyright (C) 2017 Albrecht Dreß <albrecht.dress@arcor.de>
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "libbalsa-progress.h"
#include <math.h>
#include <glib/gi18n.h>
#include "libbalsa.h"


/* note: this value is forced as minimum and maximum width of the dialogue, which may be wrong for /very/ high-resolution screens */
#define PROGRESS_DIALOG_WIDTH		320


static void progress_dialog_response_cb(GtkWidget *dialog,
                            		    gint       response);
static void progress_dialog_destroy_cb(GtkWidget G_GNUC_UNUSED *widget,
						   	   	   	   gpointer                 user_data);
static GtkWidget *find_widget_by_name(GtkContainer *container,
									  const gchar  *id);
static GtkWidget *create_progress_widget(const gchar *progress_id,
	   	   	   	   	   	   	   	   	     const gchar *title);
static gboolean remove_progress_widget(gpointer user_data);
static void send_progress_data_free(LibbalsaProgressData *progress_data);


/* While libbalsa_progress_dialog_update() will always be called from the main context (typically from an idle callback),
 * libbalsa_progress_dialog_ensure /may/ also be called from a thread, so we must ensure the integrity of the progress dialogue
 * widget.  As both functions should be really fast, one big common progress dialogue mutex is sufficient. */
static GMutex progress_mutex;


void
libbalsa_progress_dialog_ensure(GtkWidget   **progress_dialog,
								const gchar  *dialog_title,
								GtkWindow    *parent,
								const gchar  *progress_id)
{
	GtkWidget *progress_widget;
	GtkWidget *content_box;

	g_return_if_fail((progress_dialog != NULL) && (dialog_title != NULL) && (progress_id != NULL));

	g_mutex_lock(&progress_mutex);

    if (*progress_dialog == NULL) {
    	GdkGeometry hints;

    	*progress_dialog = gtk_dialog_new_with_buttons(dialog_title, parent,
    		GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags(), _("_Hide"), GTK_RESPONSE_CLOSE, NULL);
    	gtk_window_set_role(GTK_WINDOW(*progress_dialog), "progress_dialog");
        hints.min_width = PROGRESS_DIALOG_WIDTH;
        hints.min_height = 1;
        hints.max_width = PROGRESS_DIALOG_WIDTH;
        hints.max_height = -1;
        gtk_window_set_geometry_hints(GTK_WINDOW(*progress_dialog), NULL, &hints, GDK_HINT_MIN_SIZE + GDK_HINT_MAX_SIZE);
        gtk_window_set_resizable(GTK_WINDOW(*progress_dialog), FALSE);
        g_signal_connect(G_OBJECT(*progress_dialog), "response", G_CALLBACK(progress_dialog_response_cb), NULL);
        g_signal_connect(G_OBJECT(*progress_dialog), "destroy", G_CALLBACK(progress_dialog_destroy_cb), progress_dialog);

    	content_box = gtk_dialog_get_content_area(GTK_DIALOG(*progress_dialog));
    	gtk_box_set_spacing(GTK_BOX(content_box), 6);

        gtk_widget_show_all(*progress_dialog);
    } else {
    	content_box = gtk_dialog_get_content_area(GTK_DIALOG(*progress_dialog));
    }

    progress_widget = find_widget_by_name(GTK_CONTAINER(content_box), progress_id);
    if (progress_widget != NULL) {
    	if (!gtk_revealer_get_child_revealed(GTK_REVEALER(progress_widget))) {
    		gtk_revealer_set_reveal_child(GTK_REVEALER(progress_widget), TRUE);
    	}
    } else {
    	progress_widget = create_progress_widget(progress_id, progress_id);
    	gtk_revealer_set_reveal_child(GTK_REVEALER(progress_widget), TRUE);
    	gtk_box_pack_start(GTK_BOX(content_box), progress_widget, FALSE, FALSE, 0);
    	gtk_widget_show_all(progress_widget);
    }

	g_mutex_unlock(&progress_mutex);
}

static void
revealer_destroy_notify(gpointer timer_id)
{
    g_source_remove(GPOINTER_TO_UINT(timer_id));
}

gboolean
libbalsa_progress_dialog_update(gpointer user_data)
{
	LibbalsaProgressData *ctrl_data = (LibbalsaProgressData *) user_data;

	g_return_val_if_fail(ctrl_data != NULL, FALSE);

	g_mutex_lock(&progress_mutex);

	if (ctrl_data->progress_dialog == NULL) {
		if (ctrl_data->finished) {
			libbalsa_information(LIBBALSA_INFORMATION_MESSAGE, "%s:\n%s", ctrl_data->progress_id, ctrl_data->message);
		}
	} else {
		GtkWidget *progress_widget;
		GtkWidget *content_box;

		content_box = gtk_dialog_get_content_area(GTK_DIALOG(ctrl_data->progress_dialog));
		progress_widget = find_widget_by_name(GTK_CONTAINER(content_box), ctrl_data->progress_id);
		if (progress_widget != NULL) {
			if (ctrl_data->message != NULL) {
				GtkLabel *label;

				label = GTK_LABEL(g_object_get_data(G_OBJECT(progress_widget), "label"));
				gtk_label_set_text(label, ctrl_data->message);
			}
			if (isnan(ctrl_data->fraction) != 1) {
				GtkProgressBar *progress;

				progress = GTK_PROGRESS_BAR(g_object_get_data(G_OBJECT(progress_widget), "progress"));
				gtk_progress_bar_set_fraction(progress, ctrl_data->fraction);
			}
			if (ctrl_data->finished) {
				guint timer_id;

				gtk_revealer_set_reveal_child(GTK_REVEALER(progress_widget), FALSE);

				/* set a timer and remember it's id so we can remove it properly if the user destroys the whole dialogue */
                                timer_id = g_timeout_add(500, remove_progress_widget, progress_widget);
                                g_object_set_data_full(G_OBJECT(progress_widget), "timer", GUINT_TO_POINTER(timer_id),
                                                       revealer_destroy_notify);
			}
		}
	}

	g_mutex_unlock(&progress_mutex);
	send_progress_data_free(ctrl_data);

	return FALSE;
}


static void
progress_dialog_response_cb(GtkWidget *dialog,
                            gint       response)
{
    if (response == GTK_RESPONSE_CLOSE) {
        gtk_widget_destroy(dialog);
    }
}


static void
progress_dialog_destroy_cb(GtkWidget G_GNUC_UNUSED *widget,
						   gpointer                 user_data)
{
	g_mutex_lock(&progress_mutex);
	*((gpointer *) user_data) = NULL;
	g_mutex_unlock(&progress_mutex);
}


static GtkWidget *
find_widget_by_name(GtkContainer *container,
					const gchar  *id)
{
	GList *children;
	GList *this_child;
	GtkWidget *widget;

	children = gtk_container_get_children(container);
	widget = NULL;
	this_child = children;
	while ((widget == NULL) && (this_child != NULL)) {
		if (strcmp(gtk_widget_get_name(GTK_WIDGET(this_child->data)), id) == 0) {
			widget = GTK_WIDGET(this_child->data);
		} else {
			this_child = this_child->next;
		}
	}
	g_list_free(children);

	return widget;
}


static GtkWidget *
create_progress_widget(const gchar *progress_id,
					   const gchar *title)
{
	GtkWidget *result;
	GtkWidget *box;
	GtkWidget *label;
	GtkWidget *progress;

	result = gtk_revealer_new();
	gtk_revealer_set_transition_type(GTK_REVEALER(result), GTK_REVEALER_TRANSITION_TYPE_CROSSFADE);
	gtk_revealer_set_transition_duration(GTK_REVEALER(result), 500);
	gtk_revealer_set_reveal_child(GTK_REVEALER(result), FALSE);
	gtk_widget_set_name(result, progress_id);

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	gtk_container_add(GTK_CONTAINER(result), box);

	label = gtk_label_new(title);
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

	label = gtk_label_new(" ");
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	g_object_set_data(G_OBJECT(result), "label", label);
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

	progress = gtk_progress_bar_new();
	g_object_set_data(G_OBJECT(result), "progress", progress);
	gtk_box_pack_start(GTK_BOX(box), progress, FALSE, FALSE, 0);

	return result;
}


static void
count_revealers(GtkWidget *widget,
                gpointer   data)
{
	guint *count = (guint *) data;

	if (GTK_IS_REVEALER(widget)) {
		*count += 1U;
	}
}


static gboolean
remove_progress_widget(gpointer user_data)
{
	GtkWidget *progress = GTK_WIDGET(user_data);
	GtkWidget *parent_dialog;
	GtkWidget *content_box;
	guint rev_children = 0U;

        (void) g_object_steal_data(G_OBJECT(progress), "timer");

	parent_dialog = gtk_widget_get_toplevel(progress);
	gtk_widget_destroy(progress);

	/* count the GtkRevealer children left, so we can just destroy the dialogue if there is none */
	content_box = gtk_dialog_get_content_area(GTK_DIALOG(parent_dialog));
	gtk_container_foreach(GTK_CONTAINER(content_box), count_revealers, &rev_children);
	if (rev_children == 0U) {
		gtk_widget_destroy(parent_dialog);
	} else {
		gtk_window_resize(GTK_WINDOW(parent_dialog), PROGRESS_DIALOG_WIDTH, 1);
	}
	return FALSE;
}


static void
send_progress_data_free(LibbalsaProgressData *progress_data)
{
	g_free(progress_data->progress_id);
	g_free(progress_data->message);
	g_free(progress_data);
}
