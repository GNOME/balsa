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

/*
 * Callbacks for the filter run dialog
 */

#include "config.h"

#include <gnome.h>

#include <string.h>
#include "mailbox-filter.h"
#include "filter-private.h"
#include "filter-funcs.h"
#include "filter-run.h"
#include "balsa-app.h"
#include "save-restore.h"
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "pixmaps/other_enabled.xpm"

/* Global vars */

extern GList * fr_dialogs_opened;

static void
get_pixmap_and_mask_from_xpm(char* xpm[],
                             GdkPixmap **pixmap, GdkBitmap **mask)
{
    GdkPixbuf *pb = gdk_pixbuf_new_from_xpm_data((const char**)xpm);
    gdk_pixbuf_render_pixmap_and_mask(pb, pixmap, mask, 0);
    gdk_pixbuf_unref(pb);
}

/* FIXME : we should single out invalid filters in the list (eg with colors) */
/* FIXME : implement error reporting */

void
fr_clean_associated_mailbox_filters(GtkCList * clist)
{
    gint row;

    for(row=0;row<clist->rows;row++)
	g_free((LibBalsaMailboxFilter*)gtk_clist_get_row_data(clist,row));
}

static GSList * build_selected_filters_list(GtkCList * clist,gboolean to_run)
{
    GSList * filters=NULL;
    gint row;

    /* Construct list of selected filters */
    for(row=0;row<clist->rows;row++)
	if (to_run)
	    filters=g_slist_prepend(filters,
				    ((LibBalsaMailboxFilter*)
                                     gtk_clist_get_row_data(clist,row))->actual_filter);
	else
	    filters=g_slist_prepend(filters,gtk_clist_get_row_data(clist,row));
    
    filters=g_slist_reverse(filters);

    return filters;
}

/*
 * Run the selected filters on the mailbox
 * Returns TRUE if OK, or FALSE if errors occured
 */
static gboolean
run_filters_on_mailbox(GtkCList * clist,LibBalsaMailbox *mbox)
{
    GSList * filters=build_selected_filters_list(clist,TRUE);

    if (!filters) return TRUE;
    if (!filters_prepare_to_run(filters))
	return FALSE;
    gtk_clist_freeze(GTK_CLIST(balsa_app.mblist));
    if (filters_run_on_messages(filters,mbox->message_list))
	enable_empty_trash(TRASH_FULL);
    gtk_clist_thaw(GTK_CLIST(balsa_app.mblist));
    g_slist_free(filters);
    return TRUE;
}

/* "destroy" callback */

void fr_destroy_window_cb(GtkWidget * widget,gpointer throwaway)
{
    /* We pull out the destructed dialog from the list of opened dialogs */
    fr_dialogs_opened=g_list_remove(fr_dialogs_opened,widget);
}

static
void save_filters(BalsaFilterRunDialog * p)
{
    if (p->filters_modified) {
	g_slist_free(p->mbox->filters);
	p->mbox->filters=build_selected_filters_list(p->selected_filters,FALSE);
	config_mailbox_filters_save(p->mbox);
	p->filters_modified=FALSE;
    }
}

/* Dialog box button callbacks */
void fr_dialog_button_clicked(GtkWidget * widget, gint button,
			      gpointer throwaway)
{
    BalsaFilterRunDialog * p;

    p=BALSA_FILTER_RUN_DIALOG(widget);
    switch (button) {
    case 0:			/* Apply button */
	if (!run_filters_on_mailbox(p->selected_filters,p->mbox))
	    balsa_information(LIBBALSA_INFORMATION_ERROR,_("Error when applying filters"));
	return;
    case 1:                     /* OK button */
	save_filters(p);
	break;

    case 2:			/* Cancel button */
	/* We free the mailbox_filter datas, they are useless now */
	fr_clean_associated_mailbox_filters(p->selected_filters);
	
	break;
    case 3:			/* Help button */
	/* more of something here */
	return;

    default:
	/* we should NEVER get here */
	break;
    }
    gnome_dialog_close(GNOME_DIALOG(p));
}

/* 
 *Callbacks for left/right buttons
 */

void
fr_add_pressed(GtkWidget * widget, gpointer data)
{
    LibBalsaFilter* fil;
    GList * lst;
    gint row,rows;
    gchar *col[FILTER_WHEN_NB+1];
    BalsaFilterRunDialog * p;

    p=BALSA_FILTER_RUN_DIALOG(data);

    /* We check possibility of recursion here: we do not allow a filter 
       to be applied on a mailbox if its action rule modify this mailbox
    */
    for (row=1;row<=FILTER_WHEN_NB;row++) col[row]=NULL;
    for(lst=p->available_filters->selection; lst; lst=g_list_next(lst)) {
	fil=(LibBalsaFilter*)
            gtk_clist_get_row_data(p->available_filters,
                                   GPOINTER_TO_INT(lst->data));
	if (fil->action==FILTER_RUN || fil->action==FILTER_TRASH || 
            strcmp(fil->action_string,p->mbox->name)!=0) {
	    /* Ok we can add the filter to this mailbox, there is no recursion problem */
	    LibBalsaMailboxFilter* mf = g_new(LibBalsaMailboxFilter,1);
	    mf->actual_filter=fil;
	    mf->when=FILTER_WHEN_NEVER;
	    col[0]=fil->name;
	    row =gtk_clist_append(p->selected_filters,col);
	    gtk_clist_set_row_data(p->selected_filters,row, mf);
	    /* Mark for later deletion */
	    gtk_clist_set_row_data(p->available_filters,
                                   GPOINTER_TO_INT(lst->data),NULL);
	}
	else
	    balsa_information(LIBBALSA_INFORMATION_ERROR,
			      _("The destination mailbox of the filter %s is %s\n"
			      "you can't associate it with the same mailbox (that causes recursion)"),
			      fil->name,p->mbox->name);
    }

    gtk_clist_freeze(p->available_filters);
    rows=p->available_filters->rows;
    for (row=0;row<p->available_filters->rows;) {
	if (!gtk_clist_get_row_data(p->available_filters,row))
	    gtk_clist_remove(p->available_filters,row);
	else row++;
    }
    p->filters_modified=p->available_filters->rows<rows;
    gtk_clist_thaw(p->available_filters);
}

void
fr_remove_pressed(GtkWidget * widget, gpointer data)
{
    LibBalsaMailboxFilter* fil;
    gint new_row;
    BalsaFilterRunDialog * p;

    p=BALSA_FILTER_RUN_DIALOG(data);

    if (p->selected_filters->selection) {
	p->filters_modified=TRUE;
	while (p->selected_filters->selection) {
	    fil=(LibBalsaMailboxFilter*)
                gtk_clist_get_row_data(p->selected_filters,
                                       GPOINTER_TO_INT(p->selected_filters->selection->data));
	    new_row=gtk_clist_append(p->available_filters,
                                     &(fil->actual_filter->name));
	    gtk_clist_set_row_data(p->available_filters,new_row,
                                   fil->actual_filter);
	    gtk_clist_remove(p->selected_filters,
                             GPOINTER_TO_INT(p->selected_filters->selection->data));
	    g_free(fil);
	}
    }
}

/* Double click handling for clists */
void available_list_select_row_cb(GtkWidget *widget, gint row, gint column,
				  GdkEventButton *event, gpointer data)
{
    BalsaFilterRunDialog * p;

    if ( event == NULL )
	return;

    p=BALSA_FILTER_RUN_DIALOG(data);
    if (event->type == GDK_2BUTTON_PRESS)
	fr_add_pressed(NULL,data);
}

void selected_list_select_row_event_cb(GtkWidget *widget,
				       GdkEventButton *bevent, gpointer data)
{
    GtkCellType type;
    gint res,row,column;
    GdkPixmap *pixmap;
    GdkBitmap *mask;
    BalsaFilterRunDialog * p;

    if ( bevent == NULL )
	return;

    p=BALSA_FILTER_RUN_DIALOG(data);
    res = gtk_clist_get_selection_info(p->selected_filters,
                                       bevent->x,
                                       bevent->y, &row, &column);
    if ((bevent->button != 1) || !res || (column == 0))
        return;
    
    type = gtk_clist_get_cell_type(p->selected_filters, row, column);
    
    if (type == GTK_CELL_PIXMAP) {
	gtk_clist_set_text(p->selected_filters, row, column, NULL);
	FILTER_WHEN_CLRFLAG((LibBalsaMailboxFilter*)
                            gtk_clist_get_row_data(p->selected_filters,row),
                            1 << (column-1));
    } else {
	/* now for the pixmap from gdk */
	get_pixmap_and_mask_from_xpm(other_enabled_xpm, &pixmap, &mask);
	gtk_clist_set_pixmap(p->selected_filters, row, column, pixmap,mask);
	gdk_pixmap_unref(pixmap);
	gdk_bitmap_unref(mask);
	FILTER_WHEN_SETFLAG((LibBalsaMailboxFilter*)
                            gtk_clist_get_row_data(p->selected_filters,row),
                            1 << (column-1));
    }
    gtk_signal_emit_stop_by_name(GTK_OBJECT(p->selected_filters),
				 "button_press_event");
    p->filters_modified=TRUE;
}

void selected_list_select_row_cb(GtkWidget *widget,gint row,gint column,
				 GdkEventButton *event, gpointer data)
{
    if ( event == NULL )
	return;

    if (event->type == GDK_2BUTTON_PRESS)
	fr_remove_pressed(NULL,data);
}
/* 
 *Callbacks for up/down buttons
 */

/*
 * fe_down_pressed()
 *
 * Callback for the "Down" button
 */ 
void
fr_down_pressed(GtkWidget * widget, gpointer data)
{
    BalsaFilterRunDialog * p;
    gint row;

    p=BALSA_FILTER_RUN_DIALOG(data);

    if (!p->selected_filters->selection)
	return;

    p->filters_modified=TRUE;   
    row = GPOINTER_TO_INT(p->selected_filters->selection->data);
    gtk_clist_swap_rows(p->selected_filters, row, row + 1);
}			/* end fe_down_pressed */

/*
 * fe_up_pressed()
 *
 * Callback for the "Up" button
 */
void
fr_up_pressed(GtkWidget * widget, gpointer data)
{
    BalsaFilterRunDialog * p;
    gint row;

    p=BALSA_FILTER_RUN_DIALOG(data);

    if (!p->selected_filters->selection)
	return;

    p->filters_modified=TRUE;       
    row = GPOINTER_TO_INT(p->selected_filters->selection->data);
    gtk_clist_swap_rows(p->selected_filters, row - 1, row);
}			/* end fe_up_pressed */
