/* -*- C -*-
 * filter-edit-callbacks.c
 *
 * Callbacks for the filter edit dialog
 */

#include "config.h"

#include <gnome.h>

#include "filter.h"
#include "filter-private.h"
#include "filter-funcs.h"
#include "filter-edit.h"


/*
 * fe_dialog_button_clicked()
 *
 * Handles the clicking of the main buttons at the 
 * bottom of the dialog.  wooo.
 */
void
fe_dialog_button_clicked (GtkWidget * widget,
			  gpointer data)
{
  switch (GPOINTER_TO_INT (data))
    {
    case 2:			/* Cancel button */
      break;

    case 1:			/* Help button */
      /* something here */
      break;

    case 3:			/* OK button */
      /* more of something here */

    default:
      /* we should NEVER get here */
    }

  /* destroy the dialog */
  gtk_widget_destroy (fe_dialog);
  /* more destruction is needed.  But that is for later */
}				/* end fe_dialog_button_clicked */


/*
 * fe_checkbutton_toggled()
 *
 * Handles toggling of the type checkbuttons.
 * When they are toggled, the notebook page must change
 */
void
fe_checkbutton_toggled (GtkWidget * widget,
			gpointer data)
{
  if (GTK_CHECK_MENU_ITEM (widget)->active)
    {
      gtk_notebook_set_page (GTK_NOTEBOOK (fe_type_notebook),
			     GPOINTER_TO_INT (data));
    }
}				/* end fe_checkbutton_toggled() */


/*
 * fe_action_selected()
 *
 * Callback for the "Action" option menu
 */
void 
fe_action_selected (GtkWidget * widget,
		    gpointer data)
{
  gtk_widget_set_sensitive (GTK_WIDGET (fe_action_entry),
			    TRUE);
  switch (GPOINTER_TO_INT (data))
    {
    case 0:			/* copy to folder */
    case 2:			/* print on printer */
    case 3:			/* run program */
      gtk_widget_set_sensitive (GTK_WIDGET (fe_disp_place),
				TRUE);
      break;

    case 4:			/* send to trash */
      gtk_widget_set_sensitive (GTK_WIDGET (fe_action_entry),
				FALSE);
      /* fall through */
    case 1:			/* move to folder */
      if (GTK_TOGGLE_BUTTON (fe_disp_place)->active)
	gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (fe_disp_continue),
				     TRUE);
      gtk_widget_set_sensitive (GTK_WIDGET (fe_disp_place),
				FALSE);
      break;

    default:
      break;
    }
}				/* end fe_action_selected() */


/*
 * fe_add_pressed()
 *
 * Callback for the "Add" button for the regex type
 */
void
fe_add_pressed (GtkWidget * widget,
		gpointer throwaway)
{
  gchar *text;
  GList *list;
  GtkWidget *list_item;

  list = NULL;

  text = gtk_entry_get_text (GTK_ENTRY (fe_type_regex_entry));

  if ((text == NULL) || (strlen (text) == 0))
    return;
  list_item = gtk_list_item_new_with_label (text);
  gtk_widget_show (list_item);
  list = g_list_append (list,
			list_item);
  gtk_list_append_items (GTK_LIST (fe_type_regex_list),
			 list);
  gtk_entry_set_text (GTK_ENTRY (fe_type_regex_entry), "");
}				/* end fe_add_pressed() */


/*
 * fe_remove_pressed()
 * 
 * Callback for the "remove" button of the regex type
 */
void
fe_remove_pressed (GtkWidget * widget,
		   gpointer throwaway)
{
  GList *selected;

  selected = (GList *) GTK_LIST (fe_type_regex_list)->selection;

  if (!selected)
    return;

  gtk_list_remove_items (GTK_LIST (fe_type_regex_list),
			 selected);
}				/* end fe_remove_pressed() */


/*
 * fe_type_simple_toggled()
 *
 * Callback for the checkbuttons in the "Simple" type
 */
void
fe_type_simple_toggled (GtkWidget * widget,
			gpointer data)
{
  if (GTK_TOGGLE_BUTTON (widget)->active)
    {
      switch (GPOINTER_TO_INT (data))
	{
	case 1:		/* ALL */
	  gtk_toggle_button_set_state (
				  GTK_TOGGLE_BUTTON (fe_type_simple_header),
					FALSE);
	  gtk_widget_set_sensitive (
				     GTK_WIDGET (fe_type_simple_header),
				     FALSE);
	  gtk_toggle_button_set_state (
				    GTK_TOGGLE_BUTTON (fe_type_simple_body),
					FALSE);
	  gtk_widget_set_sensitive (
				     GTK_WIDGET (fe_type_simple_body),
				     FALSE);

	case 2:		/* header */
	  gtk_toggle_button_set_state (
				      GTK_TOGGLE_BUTTON (fe_type_simple_to),
					FALSE);
	  gtk_widget_set_sensitive (
				     GTK_WIDGET (fe_type_simple_to),
				     FALSE);
	  gtk_toggle_button_set_state (
				    GTK_TOGGLE_BUTTON (fe_type_simple_from),
					FALSE);
	  gtk_widget_set_sensitive (
				     GTK_WIDGET (fe_type_simple_from),
				     FALSE);
	  gtk_toggle_button_set_state (
				 GTK_TOGGLE_BUTTON (fe_type_simple_subject),
					FALSE);
	  gtk_widget_set_sensitive (
				     GTK_WIDGET (fe_type_simple_subject),
				     FALSE);

	default:
	  break;
	}
    }
  else
    {
      switch (GPOINTER_TO_INT (data))
	{
	case 1:		/* ALL */
	  gtk_widget_set_sensitive (
				     GTK_WIDGET (fe_type_simple_header),
				     TRUE);
	  gtk_widget_set_sensitive (
				     GTK_WIDGET (fe_type_simple_body),
				     TRUE);

	case 2:		/* header */
	  gtk_widget_set_sensitive (
				     GTK_WIDGET (fe_type_simple_to),
				     TRUE);
	  gtk_widget_set_sensitive (
				     GTK_WIDGET (fe_type_simple_from),
				     TRUE);
	  gtk_widget_set_sensitive (
				     GTK_WIDGET (fe_type_simple_subject),
				     TRUE);

	default:
	  break;
	}
    }
}				/* end fe_type_simple_toggled() */

/*
 * browse_fileselect_clicked()
 *
 * Callback for the fileselection dialog buttons of the 
 * sound browse function.
 */
void
browse_fileselect_clicked (GtkWidget * widget,
			   gpointer data)
{
  gtk_entry_set_text (GTK_ENTRY (fe_sound_entry),
		      gtk_file_selection_get_filename (
						GTK_FILE_SELECTION (data)));
  gtk_widget_destroy (GTK_WIDGET (data));
}				/* end browse_fileselect_clicked */

/*
 * fe_sound_browse_clicked()
 *
 * Callback when the "Browse..." button is clicked
 */
void
fe_sound_browse_clicked (GtkWidget * widget,
			 gpointer throwaway)
{
  GtkWidget *filesel;
  gchar *current;

  filesel = gtk_file_selection_new ("Play sound...");

  current = gtk_entry_get_text (GTK_ENTRY (fe_sound_entry));
  if ((current != NULL) && (strlen (current) != 0))
    gtk_file_selection_set_filename (GTK_FILE_SELECTION (filesel),
				     current);

  gtk_signal_connect (GTK_OBJECT (GTK_FILE_SELECTION (filesel)->ok_button),
		      "clicked",
		      GTK_SIGNAL_FUNC (browse_fileselect_clicked),
		      GTK_OBJECT (filesel));
  gtk_signal_connect_object (
		   GTK_OBJECT (GTK_FILE_SELECTION (filesel)->cancel_button),
			      "clicked",
			      (GtkSignalFunc) gtk_widget_destroy,
			      GTK_OBJECT (filesel));
  gtk_widget_show (filesel);
}				/* end fe_sound_browse_clicked */

/*
 * fe_new_pressed()
 *
 * Callback for the "new" button
 */
void
fe_new_pressed (GtkWidget * widget,
		gpointer throwaway)
{
  gint new_row;
  GdkImlibImage *im;
  GdkPixmap *pixmap;
  GdkBitmap *mask;
  gchar *new_item[] =
  {
    "", "New filter"
  };

  new_row = gtk_clist_append (GTK_CLIST (fe_clist),
			      new_item);

  /* now for the pixmap from gdk */
  im = gdk_imlib_create_image_from_xpm_data (enabled_xpm);
  gdk_imlib_render (im, im->rgb_width, im->rgb_height);
  pixmap = gdk_imlib_copy_image (im);
  mask = gdk_imlib_copy_mask (im);
  gdk_imlib_destroy_image (im);

  gtk_clist_set_pixmap (GTK_CLIST (fe_clist),
			new_row,
			0,
			pixmap,
			mask);

  gdk_pixmap_unref (pixmap);
  gdk_bitmap_unref (mask);

  gtk_clist_select_row (GTK_CLIST (fe_clist),
			new_row, 1);
}				/* end fe_new_pressed() */


/*
 * fe_delete_pressed()
 *
 * Callback for the "Delete" button
 */
void 
fe_delete_pressed (GtkWidget * widget,
		   gpointer throwaway)
{
  gint row;
  filter *fil;

  if (!(GTK_CLIST (fe_clist)->selection))
    return;

  row = GPOINTER_TO_INT ((GTK_CLIST (fe_clist)->selection)->data);

  fil = (filter *) gtk_clist_get_row_data (GTK_CLIST (fe_clist),
					   row);

  if (fil)
    filter_free (fil, NULL);

  gtk_clist_remove (GTK_CLIST (fe_clist),
		    row);
}				/* end fe_delete_pressed() */


/*
 * fe_down_pressed()
 *
 * Callback for the "Down" button
 */
void 
fe_down_pressed (GtkWidget * widget,
		 gpointer throwaway)
{
  gint row;

  if (!(GTK_CLIST (fe_clist)->selection))
    return;

  row = GPOINTER_TO_INT ((GTK_CLIST (fe_clist)->selection)->data);
  gtk_clist_swap_rows (GTK_CLIST (fe_clist), row, row + 1);
}				/* end fe_down_pressed */


/*
 * fe_up_pressed()
 *
 * Callback for the "Up" button
 */
void 
fe_up_pressed (GtkWidget * widget,
	       gpointer throwaway)
{
  gint row;

  if (!(GTK_CLIST (fe_clist)->selection))
    return;

  row = GPOINTER_TO_INT ((GTK_CLIST (fe_clist)->selection)->data);
  gtk_clist_swap_rows (GTK_CLIST (fe_clist), row - 1, row);
}				/* end fe_up_pressed */


/*
 * fe_clist_button_event_press()
 *
 * Callback for when button is pressed.
 */
void
fe_clist_button_event_press (GtkWidget * widget,
			     GdkEventButton * bevent,
			     gpointer data)
{
  gint row, column, res;
  GdkImlibImage *im;
  GdkPixmap *pixmap;
  GdkBitmap *mask;
  GtkCellType type;


  res = gtk_clist_get_selection_info (GTK_CLIST (fe_clist),
				      bevent->x,
				      bevent->y,
				      &row,
				      &column);

  if ((bevent->button != 1) || (column != 0))
    return;

  type = gtk_clist_get_cell_type (GTK_CLIST (fe_clist),
				  row,
				  column);

  if (type == GTK_CELL_PIXMAP)
    {
      gtk_clist_set_text (GTK_CLIST (fe_clist),
			  row,
			  column,
			  NULL);

      gtk_clist_select_row (GTK_CLIST (fe_clist), row, -1);
    }
  else
    {
      /* now for the pixmap from gdk */
      im = gdk_imlib_create_image_from_xpm_data (enabled_xpm);
      gdk_imlib_render (im, im->rgb_width, im->rgb_height);
      pixmap = gdk_imlib_copy_image (im);
      mask = gdk_imlib_copy_mask (im);
      gdk_imlib_destroy_image (im);

      gtk_clist_set_pixmap (GTK_CLIST (fe_clist),
			    row,
			    0,
			    pixmap,
			    mask);

      gtk_clist_select_row (GTK_CLIST (fe_clist), row, -1);

      gdk_pixmap_unref (pixmap);
      gdk_bitmap_unref (mask);
    }

  gtk_signal_emit_stop_by_name (GTK_OBJECT (fe_clist),
				"button_press_event");


}				/* end fe_clist_button_event_press() */
