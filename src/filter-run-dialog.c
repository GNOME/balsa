/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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

#include "config.h"

#include <gnome.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "balsa-app.h"
#include "filter-run.h"
#include "mailbox-filter.h"
#include "save-restore.h"
#include "pixmaps/other_enabled.xpm"
#include "balsa-icons.h"

/* fe_already_open is TRUE when the filters dialog is opened, we use this to prevent incoherency if we
 * have both filters dialog and mailbox filters dialog boxes opened at the same time
 * FIXME : we can perhaps imagine a way to "refresh" the other dialog boxes when filters have been modified
 * but it'll be complex, and I'm not sure it is worth it
 *
 * Defined in filter-edit-dialog.c
 */

extern int fe_already_open;

/* BalsaFilterRunDialog signals */

enum {
    REFRESH,
    LAST_SIGNAL,
};

static gint balsa_filter_run_dialog_signals[LAST_SIGNAL];
static GnomeDialogClass *parent_class = NULL;

GList * fr_dialogs_opened=NULL;

/* BalsaFilterRunDialog methods */

static void balsa_filter_run_dialog_class_init(BalsaFilterRunDialogClass * klass);
static void balsa_filter_run_dialog_init(BalsaFilterRunDialog * p);

static void fr_refresh(BalsaFilterRunDialog * dialog,GSList * names_changing,gpointer throwaway);

static void 
populate_available_filters_list(GtkCList * clist,GSList * mailbox_filters)
{
    LibBalsaFilter* fil;
    GSList * source=balsa_app.filters,* lst;
    gint row;

    for (source=balsa_app.filters;source;source=g_slist_next(source)) {
	fil=(LibBalsaFilter *)source->data;
	/* We look for each filter in the mailbox list */
	for (lst=mailbox_filters;
             lst && fil!=((LibBalsaMailboxFilter*)lst->data)->actual_filter;
             lst=g_slist_next(lst));
	/* If it's not in mailbox list we can add it to available filters */
	if (!lst) {
	    row=gtk_clist_append(clist,&(fil->name));
	    gtk_clist_set_row_data(clist,row,fil);
	}
    }
}

/* Set the icon corresponding to the when type */

static void
set_icon(GtkCList * clist,gint row,gint when)
{
    GdkPixmap *pixmap;
    GdkBitmap *mask;
    gint i;

    balsa_icon_create(other_enabled_xpm, &pixmap, &mask);

    for (i=0;i<FILTER_WHEN_NB;i++)
	if (when & (1 << i))
	    gtk_clist_set_pixmap(clist, row, i+1, pixmap,mask);

    gdk_pixmap_unref(pixmap);
    gdk_bitmap_unref(mask);
}

static gint
populate_selected_filters_list(GtkCList * clist,GSList * filters_list)
{
    LibBalsaMailboxFilter * fil,* mf;
    gint row;
    gchar * col[FILTER_WHEN_NB+1];

    for (row=1;row<=FILTER_WHEN_NB;row++) col[row]=NULL;
    for(;filters_list;filters_list=g_slist_next(filters_list)) {
	mf=g_new(LibBalsaMailboxFilter,1);
	fil=(LibBalsaMailboxFilter*)filters_list->data;
        /* FIXME: line below looks suspicious to me */
	*mf=*fil;
	col[0]=fil->actual_filter->name;
	row=gtk_clist_append(clist,col);
	set_icon(clist,row,fil->when);
	gtk_clist_set_row_data(clist,row,mf);
    }
    return TRUE;
}

guint
balsa_filter_run_dialog_get_type()
{
    static guint balsa_filter_run_dialog_type = 0;

    if (!balsa_filter_run_dialog_type) {
	GtkTypeInfo balsa_filter_run_dialog_info = {
	    "BalsaFilterRunDialog",
	    sizeof(BalsaFilterRunDialog),
	    sizeof(BalsaFilterRunDialogClass),
	    (GtkClassInitFunc) balsa_filter_run_dialog_class_init,
	    (GtkObjectInitFunc) balsa_filter_run_dialog_init,
	    (gpointer) NULL,
	    (gpointer) NULL,
	    (GtkClassInitFunc) NULL
	};

	balsa_filter_run_dialog_type =
	    gtk_type_unique(gnome_dialog_get_type(), &balsa_filter_run_dialog_info);
    }

    return balsa_filter_run_dialog_type;
}


static void
balsa_filter_run_dialog_class_init(BalsaFilterRunDialogClass * klass)
{
    GtkObjectClass *object_class;

    object_class = GTK_OBJECT_CLASS(klass);

    balsa_filter_run_dialog_signals[REFRESH] =
	gtk_signal_new("refresh",
		       GTK_RUN_FIRST,
		       BALSA_TYPE_FILTER_RUN_DIALOG,
		       GTK_SIGNAL_OFFSET(BalsaFilterRunDialogClass, refresh),
		       gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, GTK_TYPE_POINTER);

    parent_class = gtk_type_class(gnome_dialog_get_type());

    klass->refresh = fr_refresh;
}

GtkWidget *
balsa_filter_run_dialog_new(LibBalsaMailbox * mbox)
{
    BalsaFilterRunDialog *p;
    gchar * dialog_title;
    guint width,i;

    p = gtk_type_new(balsa_filter_run_dialog_get_type());
    g_return_val_if_fail(p,NULL);

    /* We set the dialog title */
    p->mbox=mbox;
    dialog_title=g_strconcat(_("Balsa Filters of mailbox : "),p->mbox->name,NULL);
    gtk_window_set_title(GTK_WINDOW(p),dialog_title);
    g_free(dialog_title);

    /* Open the mailbox if needed */
    if (mbox->open_ref==0)
	libbalsa_mailbox_open(p->mbox);
    /* Load associated filters if needed */
    if (!p->mbox->filters)
	config_mailbox_filters_load(p->mbox);

    /* Populate the clists */
    populate_available_filters_list(p->available_filters,mbox->filters);

    /* Populate list of selected filters */
    if (!populate_selected_filters_list(p->selected_filters,mbox->filters)) {
	fr_clean_associated_mailbox_filters(p->selected_filters);
	balsa_information(LIBBALSA_INFORMATION_ERROR,_("Memory allocation error"));
	gnome_dialog_close(GNOME_DIALOG(p));
    }

    /* FIXME : The only way I found to have decent sizes */
    width=0;
    for (i=0;i<FILTER_WHEN_NB;i++) width+=gtk_clist_optimal_column_width(p->available_filters,i);
    width=MIN(width,300);
    gtk_widget_set_usize(GTK_WIDGET(p->available_filters),width,200);
    gtk_widget_set_usize(GTK_WIDGET(p->selected_filters),width,200);

    return GTK_WIDGET(p);
}

static 
void balsa_filter_run_dialog_init(BalsaFilterRunDialog * p)
{
    GtkWidget * bbox, * hbox,* vbox;
    GtkWidget *button;
    GtkWidget *sw;
    static gchar *titles_available[] = { N_("Name") };
    static gchar *titles_selected[] = { N_("Name"),N_("On reception"),N_("On exit") };
    guint i;


/*
 * Builds the two lists of filters (available, and selected ones) of the dialog
 */


    /*
       /-----------------\
       | /---\  | /---\  |
       | |   | -> |   |  |
       | |   |  | |   |  |
       | |   | <- |   |  |
       | \---/  | \---/  |
       \-----------------/
     */

    gnome_dialog_append_buttons(GNOME_DIALOG(p),
				GNOME_STOCK_BUTTON_APPLY,
				GNOME_STOCK_BUTTON_OK,
				GNOME_STOCK_BUTTON_CANCEL,
				GNOME_STOCK_BUTTON_HELP, NULL);

    gtk_signal_connect(GTK_OBJECT(p), "clicked",
                       GTK_SIGNAL_FUNC(fr_dialog_button_clicked), NULL);
    gtk_signal_connect(GTK_OBJECT(p), "destroy",
                       GTK_SIGNAL_FUNC(fr_destroy_window_cb), NULL);

    hbox = gtk_hbox_new(FALSE,2);

    gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(p)->vbox),
		       hbox, TRUE, TRUE, 0);

    gtk_widget_push_visual(gdk_rgb_get_visual());
    gtk_widget_push_colormap(gdk_rgb_get_cmap());

#ifdef ENABLE_NLS
    titles_available[0] = _(titles_available[0]);
    titles_selected[0] = _(titles_selected[0]);
    titles_selected[1] = _(titles_selected[1]);
    titles_selected[2] = _(titles_selected[2]);
#endif
    p->available_filters = GTK_CLIST(gtk_clist_new_with_titles(1, titles_available));
    p->selected_filters = GTK_CLIST(gtk_clist_new_with_titles(FILTER_WHEN_NB+1, titles_selected));
    gtk_signal_connect(GTK_OBJECT(p->available_filters),"select_row",
		       GTK_SIGNAL_FUNC(available_list_select_row_cb),p);
    gtk_signal_connect(GTK_OBJECT(p->selected_filters), "select_row",
		       GTK_SIGNAL_FUNC(selected_list_select_row_cb),p);
    gtk_signal_connect(GTK_OBJECT(p->selected_filters), "button_press_event",
		       GTK_SIGNAL_FUNC(selected_list_select_row_event_cb),p);

    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);

    gtk_clist_set_selection_mode(p->available_filters, GTK_SELECTION_MULTIPLE);
    gtk_clist_set_row_height(p->available_filters, 0);
    gtk_clist_set_reorderable(p->available_filters, FALSE);
    gtk_clist_set_use_drag_icons(p->available_filters, FALSE);
    gtk_clist_column_titles_passive(p->available_filters);
    gtk_clist_set_column_auto_resize(p->available_filters,0,TRUE);
    gtk_clist_set_sort_column(p->available_filters,0);
    gtk_clist_set_sort_type(p->available_filters,GTK_SORT_ASCENDING);
    gtk_clist_set_auto_sort(p->available_filters,TRUE);

    gtk_container_add(GTK_CONTAINER(sw), GTK_WIDGET(p->available_filters));
    gtk_box_pack_start(GTK_BOX(hbox),sw, TRUE, TRUE, 0);
 
    /* Buttons between the 2 lists */
    bbox = gtk_vbutton_box_new();
    gtk_button_box_set_spacing(GTK_BUTTON_BOX(bbox), 2);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_SPREAD);
    gtk_button_box_set_child_size(GTK_BUTTON_BOX(bbox),
				  BALSA_BUTTON_WIDTH / 2,
				  BALSA_BUTTON_HEIGHT / 2);

    /* FIXME : I saw a lot of different ways to create button with pixmaps, hope this one is correct*/
    /* Right/Add button */
    button = balsa_stock_button_with_label(GNOME_STOCK_MENU_FORWARD, "");
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
		       GTK_SIGNAL_FUNC(fr_add_pressed), p);
    gtk_container_add(GTK_CONTAINER(bbox), button);
    /* Left/Remove button */
    button = balsa_stock_button_with_label(GNOME_STOCK_MENU_BACK, "");
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
		       GTK_SIGNAL_FUNC(fr_remove_pressed), p);
    gtk_container_add(GTK_CONTAINER(bbox), button);

    gtk_box_pack_start(GTK_BOX(hbox),bbox, TRUE, TRUE, 0);

    vbox=gtk_vbox_new(FALSE,2);

    gtk_box_pack_start(GTK_BOX(hbox),vbox, TRUE, TRUE, 0);

    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);

    gtk_clist_set_selection_mode(p->selected_filters, GTK_SELECTION_MULTIPLE);
    gtk_clist_set_row_height(p->selected_filters, 0);
    gtk_clist_set_reorderable(p->selected_filters, TRUE);
    gtk_clist_set_use_drag_icons(p->selected_filters, FALSE);
    gtk_clist_column_titles_passive(p->selected_filters);
    gtk_clist_set_column_auto_resize(p->selected_filters,0,TRUE);
    for (i=1;i<=FILTER_WHEN_NB;i++) {
	gtk_clist_set_column_auto_resize(p->selected_filters,i,TRUE);
	gtk_clist_set_column_justification(p->selected_filters,i,GTK_JUSTIFY_CENTER);
    }

    gtk_container_add(GTK_CONTAINER(sw), GTK_WIDGET(p->selected_filters));

    gtk_box_pack_start(GTK_BOX(vbox),sw, TRUE, TRUE, 0);

    /* up down arrow buttons */
    bbox = gtk_hbutton_box_new();
    gtk_button_box_set_spacing(GTK_BUTTON_BOX(bbox), 2);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_SPREAD);
    gtk_button_box_set_child_size(GTK_BUTTON_BOX(bbox),
				  BALSA_BUTTON_WIDTH / 2,
				  BALSA_BUTTON_HEIGHT / 2);

    gtk_box_pack_start(GTK_BOX(vbox), bbox, FALSE, FALSE, 2);

    /* up button */
    button = balsa_stock_button_with_label(GNOME_STOCK_MENU_UP, _("Up"));
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
		       GTK_SIGNAL_FUNC(fr_up_pressed), p);
    gtk_container_add(GTK_CONTAINER(bbox), button);
    /* down button */
    button = balsa_stock_button_with_label(GNOME_STOCK_MENU_DOWN, _("Down"));
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
		       GTK_SIGNAL_FUNC(fr_down_pressed), p);
    gtk_container_add(GTK_CONTAINER(bbox), button);

    gtk_widget_pop_colormap();
    gtk_widget_pop_visual();

    p->filters_modified=FALSE;
}

static void fr_refresh(BalsaFilterRunDialog * fr_dialog,GSList * names_changing,gpointer throwaway)
{
    /* FIXME : this is the future implementation of a signal that will be able to tell each filter-run dialog box
     * that filters have changed and that they have to refresh their content
     * We'll see that later if it's worth the pain
     */
}

/* filter_run_dialog(LibBalsaMailbox *mbox)
 * params:
 *   mbox - the mailbox concerned by edition/running filters
 */

void
filters_run_dialog(LibBalsaMailbox *mbox)
{
    GList * lst;
    GtkWidget * p;

    if (fe_already_open) {
	balsa_information(LIBBALSA_INFORMATION_ERROR,_("The filters dialog is opened, close it before you can run filters on any mailbox"));
	return;
    }
    /* We look for an existing dialog box for this mailbox */
    for (lst=fr_dialogs_opened;lst && strcmp(BALSA_FILTER_RUN_DIALOG(lst->data)->mbox->url,mbox->url)!=0;lst=g_list_next(lst));
    if (lst) {
	/* If there was yet a dialog box for this mailbox, we raise it */
	gdk_window_raise(GTK_WIDGET(lst->data)->window);
	return;
    }

    p=balsa_filter_run_dialog_new(mbox);
    if (!p) return;

    fr_dialogs_opened=g_list_prepend(fr_dialogs_opened,p);

    gtk_widget_show_all(p);
}
