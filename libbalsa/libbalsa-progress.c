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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "libbalsa-progress.h"
#include <math.h>
#include <glib/gi18n.h>
#include "libbalsa.h"


/* note: this value is forced as minimum and maximum width of the dialogue, which may be wrong for /very/ high-resolution screens */
#define PROGRESS_DIALOG_WIDTH		320
#define FADER_DURATION				500U
#define ACTIVITY_DURATION			100U


typedef struct {
	ProgressDialog *dialog;
	const gchar    *title;
	GtkWindow      *parent;
	const gchar    *id;
	gboolean        done;
	GCond           cond;
} create_progress_dlg_t;


typedef struct {
	ProgressDialog *dialog;
	gchar          *id;
	gboolean        finished;
	gchar     	   *message;
	gdouble         fraction;
} update_progress_data_t;


typedef struct {
	GtkWidget *revealer;
	GtkWidget *progress;
	GtkWidget *label;
	guint      activity_id;
	guint      fadeout_id;
} progress_widget_data_t;


static void libbalsa_progress_dialog_ensure_real(ProgressDialog *progress_dialog,
								     	 	     const gchar    *dialog_title,
												 GtkWindow      *parent,
												 const gchar    *progress_id);
static gboolean libbalsa_progress_dialog_create_cb(create_progress_dlg_t *dlg_data);

static void progress_dialog_response_cb(GtkWidget *dialog,
                            		    gint       response,
                                        gpointer   user_data);
static void progress_dialog_destroy_cb(GtkWidget G_GNUC_UNUSED *widget,
									   ProgressDialog          *progress_dialog);
static progress_widget_data_t *find_progress_data_by_name(GtkContainer *container,
														  const gchar  *id);
static GtkWidget *create_progress_widget(const gchar *progress_id)
	G_GNUC_WARN_UNUSED_RESULT;
static gboolean remove_progress_widget(progress_widget_data_t *progress_data);
static void libbalsa_progress_dialog_update_real(ProgressDialog *progress_dialog,
									 	 	 	 const gchar    *progress_id,
												 gboolean        finished,
												 gdouble         fraction,
												 const gchar    *message);
static gboolean libbalsa_progress_dialog_update_cb(update_progress_data_t *upd_data);
static gboolean progress_activity(GtkProgressBar *progress);


void
libbalsa_progress_dialog_ensure(ProgressDialog *progress_dialog,
								const gchar    *dialog_title,
								GtkWindow      *parent,
								const gchar    *progress_id)
{
	g_return_if_fail((progress_dialog != NULL) && (dialog_title != NULL) && (progress_id != NULL));

	g_mutex_lock(&progress_dialog->mutex);

	if (libbalsa_am_i_subthread()) {
		create_progress_dlg_t dlgdata;

		/* shift the work to an idle callback, and return only after it has been run */
		dlgdata.dialog = progress_dialog;
		dlgdata.title = dialog_title;
		dlgdata.parent = parent;
		dlgdata.id = progress_id;
		dlgdata.done = FALSE;
		g_cond_init(&dlgdata.cond);
		gdk_threads_add_idle((GSourceFunc) libbalsa_progress_dialog_create_cb, &dlgdata);

		while (!dlgdata.done) {
			g_cond_wait(&dlgdata.cond, &progress_dialog->mutex);
		}
		g_cond_clear(&dlgdata.cond);
	} else {
		libbalsa_progress_dialog_ensure_real(progress_dialog, dialog_title, parent, progress_id);
	}

	g_mutex_unlock(&progress_dialog->mutex);
}


void
libbalsa_progress_dialog_update(ProgressDialog *progress_dialog,
								const gchar    *progress_id,
								gboolean        finished,
								gdouble         fraction,
								const gchar    *message,
								...)
{
	g_return_if_fail((progress_dialog != NULL) && (progress_id != NULL));

	g_mutex_lock(&progress_dialog->mutex);

	/* nothing to do if the progress dialogue has been closed */
	if (progress_dialog->dialog != NULL) {
		gchar *real_msg;

		if (message != NULL) {
			va_list args;

			va_start(args, message);
			real_msg = g_strdup_vprintf(message, args);
			va_end(args);
		} else {
			real_msg = NULL;
		}

		if (libbalsa_am_i_subthread()) {
			update_progress_data_t *update_data;

			update_data = g_malloc(sizeof(update_progress_data_t));
			update_data->dialog = progress_dialog;
			update_data->id = g_strdup(progress_id);
			update_data->finished = finished;
			update_data->fraction = fraction;
			update_data->message = real_msg;
			gdk_threads_add_idle((GSourceFunc) libbalsa_progress_dialog_update_cb, update_data);
		} else {
			libbalsa_progress_dialog_update_real(progress_dialog, progress_id, finished, fraction, real_msg);
			g_free(real_msg);
		}
	}

	g_mutex_unlock(&progress_dialog->mutex);
}


/* --- local functions --- */

/* note: the mutex ProgressDialog::mutex is always locked when this function is called */
static void
libbalsa_progress_dialog_ensure_real(ProgressDialog *progress_dialog,
								     const gchar    *dialog_title,
								     GtkWindow      *parent,
								     const gchar    *progress_id)
{
	GtkWidget *content_box;
	const progress_widget_data_t *progress_data;

    if (progress_dialog->dialog == NULL) {
    	GdkGeometry hints;

    	progress_dialog->dialog = gtk_dialog_new_with_buttons(dialog_title, parent,
    		GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags(), _("_Hide"), GTK_RESPONSE_CLOSE, NULL);
    	gtk_window_set_role(GTK_WINDOW(progress_dialog->dialog), "progress_dialog");
        hints.min_width = PROGRESS_DIALOG_WIDTH;
        hints.min_height = 1;
        hints.max_width = PROGRESS_DIALOG_WIDTH;
        hints.max_height = -1;
        gtk_window_set_geometry_hints(GTK_WINDOW(progress_dialog->dialog), NULL, &hints, GDK_HINT_MIN_SIZE + GDK_HINT_MAX_SIZE);
        gtk_window_set_resizable(GTK_WINDOW(progress_dialog->dialog), FALSE);
        g_signal_connect(progress_dialog->dialog, "response", G_CALLBACK(progress_dialog_response_cb), NULL);
        g_signal_connect(progress_dialog->dialog, "destroy", G_CALLBACK(progress_dialog_destroy_cb), progress_dialog);

    	content_box = gtk_dialog_get_content_area(GTK_DIALOG(progress_dialog->dialog));
    	gtk_box_set_spacing(GTK_BOX(content_box), 6);

        gtk_widget_show_all(progress_dialog->dialog);
    } else {
    	content_box = gtk_dialog_get_content_area(GTK_DIALOG(progress_dialog->dialog));
    }

    progress_data = find_progress_data_by_name(GTK_CONTAINER(content_box), progress_id);
    if (progress_data != NULL) {
    	if (!gtk_revealer_get_child_revealed(GTK_REVEALER(progress_data->revealer))) {
    		gtk_revealer_set_reveal_child(GTK_REVEALER(progress_data->revealer), TRUE);
    	}
    } else {
    	GtkWidget *progress_widget;

    	progress_widget = create_progress_widget(progress_id);
    	gtk_revealer_set_reveal_child(GTK_REVEALER(progress_widget), TRUE);
    	gtk_container_add(GTK_CONTAINER(content_box), progress_widget);
    	gtk_widget_show_all(progress_widget);
    }
}


/* note: the mutex ProgressDialog::mutex is never locked when this function is called */
static gboolean
libbalsa_progress_dialog_create_cb(create_progress_dlg_t *dlg_data)
{
	g_mutex_lock(&dlg_data->dialog->mutex);
	libbalsa_progress_dialog_ensure_real(dlg_data->dialog, dlg_data->title, dlg_data->parent, dlg_data->id);
	dlg_data->done = TRUE;
	g_cond_signal(&dlg_data->cond);
	g_mutex_unlock(&dlg_data->dialog->mutex);

	return FALSE;
}


static void
progress_dialog_response_cb(GtkWidget *dialog,
                            gint       response,
                            gpointer   user_data)
{
    if (response == GTK_RESPONSE_CLOSE) {
        gtk_widget_destroy(dialog);
    }
}


static void
progress_dialog_destroy_cb(GtkWidget G_GNUC_UNUSED *widget,
						   ProgressDialog          *progress_dialog)
{
	g_mutex_lock(&progress_dialog->mutex);
	progress_dialog->dialog = NULL;
	g_mutex_unlock(&progress_dialog->mutex);
}


static void
progress_data_destroy_cb(GtkWidget G_GNUC_UNUSED *widget,
						 progress_widget_data_t  *progress_data)
{
	if (progress_data->activity_id != 0U) {
		g_source_remove(progress_data->activity_id);
		progress_data->activity_id = 0U;
	}
	if (progress_data->fadeout_id != 0U) {
		g_source_remove(progress_data->fadeout_id);
		progress_data->fadeout_id = 0U;
	}
}


static progress_widget_data_t *
find_progress_data_by_name(GtkContainer *container,
						   const gchar  *id)
{
	GList *children;
	GList *this_child;
	progress_widget_data_t *data;

	children = gtk_container_get_children(container);
	data = NULL;
	this_child = children;
	while ((data == NULL) && (this_child != NULL)) {
		if (strcmp(gtk_widget_get_name(GTK_WIDGET(this_child->data)), id) == 0) {
			data = g_object_get_data(G_OBJECT(this_child->data), "data");
		} else {
			this_child = this_child->next;
		}
	}
	g_list_free(children);

	return data;
}


static GtkWidget *
create_progress_widget(const gchar *progress_id)
{
	GtkWidget *box;
	GtkWidget *label;
	progress_widget_data_t *widget_data;

	widget_data = g_malloc0(sizeof(progress_widget_data_t));
	widget_data->revealer = gtk_revealer_new();
	gtk_revealer_set_transition_type(GTK_REVEALER(widget_data->revealer), GTK_REVEALER_TRANSITION_TYPE_CROSSFADE);
	gtk_revealer_set_transition_duration(GTK_REVEALER(widget_data->revealer), FADER_DURATION);
	gtk_revealer_set_reveal_child(GTK_REVEALER(widget_data->revealer), FALSE);
	gtk_widget_set_name(widget_data->revealer, progress_id);
	g_object_set_data_full(G_OBJECT(widget_data->revealer), "data", widget_data, g_free);
    g_signal_connect(widget_data->revealer, "destroy", G_CALLBACK(progress_data_destroy_cb), widget_data);

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	gtk_container_add(GTK_CONTAINER(widget_data->revealer), box);

	label = gtk_label_new(progress_id);
	gtk_container_add(GTK_CONTAINER(box), label);

	widget_data->label = gtk_label_new(" ");
	gtk_label_set_line_wrap(GTK_LABEL(widget_data->label), TRUE);
	gtk_container_add(GTK_CONTAINER(box), widget_data->label);

	widget_data->progress = gtk_progress_bar_new();
	gtk_container_add(GTK_CONTAINER(box), widget_data->progress);

	return widget_data->revealer;
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
remove_progress_widget(progress_widget_data_t *progress_data)
{
	GtkWidget *parent_dialog;
	GtkWidget *content_box;
	guint rev_children = 0U;

	progress_data->fadeout_id = 0U;
	parent_dialog = gtk_widget_get_toplevel(progress_data->revealer);
	gtk_widget_destroy(progress_data->revealer);

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


/* note: the mutex ProgressDialog::mutex is never locked when this function is called */
static gboolean
libbalsa_progress_dialog_update_cb(update_progress_data_t *upd_data)
{
	g_mutex_lock(&upd_data->dialog->mutex);
	libbalsa_progress_dialog_update_real(upd_data->dialog, upd_data->id, upd_data->finished, upd_data->fraction, upd_data->message);
	g_mutex_unlock(&upd_data->dialog->mutex);
	g_free(upd_data->id);
	g_free(upd_data->message);
	g_free(upd_data);
	return FALSE;
}


/* note: the mutex ProgressDialog::mutex is always locked when this function is called */
static void
libbalsa_progress_dialog_update_real(ProgressDialog *progress_dialog,
									 const gchar    *progress_id,
									 gboolean        finished,
									 gdouble         fraction,
									 const gchar    *message)
{
	if (progress_dialog->dialog == NULL) {
		if (finished) {
			libbalsa_information(LIBBALSA_INFORMATION_MESSAGE, "%s:\n%s", progress_id, message);
		}
	} else {
		GtkWidget *content_box;
		progress_widget_data_t *progress_data;

		content_box = gtk_dialog_get_content_area(GTK_DIALOG(progress_dialog->dialog));
		progress_data = find_progress_data_by_name(GTK_CONTAINER(content_box), progress_id);
		if (progress_data != NULL) {
			if (message != NULL) {
				gtk_label_set_text(GTK_LABEL(progress_data->label), message);
			}
			if (isnan(fraction) != 1) {
				if (isinf(fraction) != 0) {
					if (progress_data->activity_id == 0U) {
						gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress_data->progress));
						progress_data->activity_id =
							g_timeout_add(ACTIVITY_DURATION, (GSourceFunc) progress_activity, progress_data->progress);
					}
				} else {
					if (progress_data->activity_id != 0U) {
						g_source_remove(progress_data->activity_id);
						progress_data->activity_id = 0U;
					}
					gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_data->progress), fraction);
				}
			}
			if (finished) {
				gtk_revealer_set_reveal_child(GTK_REVEALER(progress_data->revealer), FALSE);
				gtk_widget_set_name(progress_data->revealer, "_none_");
				progress_data->fadeout_id = g_timeout_add(FADER_DURATION, (GSourceFunc) remove_progress_widget, progress_data);
			}
		}
	}
}


static gboolean
progress_activity(GtkProgressBar *progress)
{
	gtk_progress_bar_pulse(progress);
	return TRUE;
}
