/* -*- C -*-
 * filter-edit-callbacks.c
 *
 * Callbacks for the filter edit dialog
 */

#include "config.h"

#include <gnome.h>

#include <string.h>
#include "filter.h"
#include "filter-private.h"
#include "filter-funcs.h"
#include "filter-edit.h"


/*
 * unique_filter_name()
 *
 * Checks the name of a filter being added to see if it is unique.
 * It returns a unique name, freshly allocated if necessary.
 *
 * Arguments:
 *    gchar *name - the preferred choice for a name
 * Returns:
 *    gchar* - the unique name
 */
static gint unique_filter_name(GtkWidget *clist, gchar *name)
{
    gchar *row_text;
    gint len, row = 0;
   
  g_return_val_if_fail(clist != NULL, 0);
  g_return_val_if_fail(GTK_IS_CLIST(clist), 0);

  if ((!name) || (name[0] == '\0'))
        return(0);

    len = strlen(name);

    while (gtk_clist_get_text(GTK_CLIST(clist),
                              row,
                              1,
                              &row_text))
    {
        if ((len == strlen(row_text)) ||
            (strncmp(name, row_text, len) == 0))
            return(0);
    } /* end while(gtk_clist_get_text()) */

    return(1);
} /* end unique_filter_name() */

#if 0
/* FIXME FIXME FIXME */

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
    case 2:                        /* Cancel button */
      break;

    case 1:                        /* Help button */
      /* something here */
      break;

    case 3:                        /* OK button */
      /* more of something here */

    default:
      /* we should NEVER get here */
    }

  /* destroy the dialog */
  gtk_widget_destroy (fe_dialog);
  /* more destruction is needed.  But that is for later */
}                                /* end fe_dialog_button_clicked */
#endif

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
  if (GTK_CHECK_MENU_ITEM(widget)->active)
    {
      gtk_notebook_set_page (GTK_NOTEBOOK(fe_type_notebook),
                             GPOINTER_TO_INT (data) - 1);
    }
}                                /* end fe_checkbutton_toggled() */


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
    case 1:                        /* copy to folder */
    case 3:                        /* print on printer */
    case 4:                        /* run program */
      gtk_widget_set_sensitive (GTK_WIDGET (fe_disp_place),
                                TRUE);
      break;

    case 5:                        /* send to trash */
      gtk_widget_set_sensitive (GTK_WIDGET (fe_action_entry),
                                FALSE);
      /* fall through */
    case 2:                        /* move to folder */
      if (GTK_TOGGLE_BUTTON (fe_disp_place)->active)
        gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (fe_disp_continue),
                                     TRUE);
      gtk_widget_set_sensitive (GTK_WIDGET (fe_disp_place),
                                FALSE);
      break;

    default:
      break;
    }
}                                /* end fe_action_selected() */


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
}                                /* end fe_add_pressed() */


/*
 * fe_remove_pressed()
 * 
 * Callback for the "remove" button of the regex type
 */
void
fe_remove_pressed (GtkWidget *widget,
                   gpointer throwaway)
{
  GList *selected;

  selected = (GList *)(GTK_LIST(fe_type_regex_list))->selection;

  if (!selected)
    return;

  gtk_list_remove_items (GTK_LIST(fe_type_regex_list),
                         selected);
}                                /* end fe_remove_pressed() */


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
        case 1:                /* ALL */
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

        case 2:                /* header */
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
        case 1:                /* ALL */
          gtk_widget_set_sensitive (
                                     GTK_WIDGET (fe_type_simple_header),
                                     TRUE);
          gtk_widget_set_sensitive (
                                     GTK_WIDGET (fe_type_simple_body),
                                     TRUE);

        case 2:                /* header */
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
}                                /* end fe_type_simple_toggled() */

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
}                                /* end browse_fileselect_clicked */

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
}                                /* end fe_sound_browse_clicked */

/*
 * fe_new_pressed()
 *
 * Callback for the "new" button
 */
void
fe_new_pressed (GtkWidget * widget,
                gpointer data)
{
  GtkWidget *clist;
  gint new_row;
  GdkImlibImage *im;
  GdkPixmap *pixmap;
  GdkBitmap *mask;
  gchar *new_item[] =
  {
    "", "New filter"
  };

  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_CLIST(data));
  clist = GTK_WIDGET(data);

  new_row = gtk_clist_append (GTK_CLIST (clist),
                              new_item);

  /* now for the pixmap from gdk */
  im = gdk_imlib_create_image_from_xpm_data (enabled_xpm);
  gdk_imlib_render (im, im->rgb_width, im->rgb_height);
  pixmap = gdk_imlib_copy_image (im);
  mask = gdk_imlib_copy_mask (im);
  gdk_imlib_destroy_image (im);

  gtk_clist_set_pixmap (GTK_CLIST (clist),
                        new_row,
                        0,
                        pixmap,
                        mask);

  gdk_pixmap_unref (pixmap);
  gdk_bitmap_unref (mask);

  gtk_clist_select_row (GTK_CLIST (clist),
                        new_row, 1);
}                                /* end fe_new_pressed() */


/*
 * fe_delete_pressed()
 *
 * Callback for the "Delete" button
 */
void 
fe_delete_pressed (GtkWidget * widget,
                   gpointer data)
{
  GtkWidget *clist;
  gint row;
  filter *fil;

  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_CLIST(data));
  clist = GTK_WIDGET(data);

  if (!(GTK_CLIST (clist)->selection))
    return;

  row = GPOINTER_TO_INT ((GTK_CLIST (clist)->selection)->data);

  fil = (filter *) gtk_clist_get_row_data (GTK_CLIST (clist),
                                           row);

  if (fil)
    filter_free (fil, NULL);

  gtk_clist_remove (GTK_CLIST (clist),
                    row);
}                                /* end fe_delete_pressed() */


/*
 * fe_down_pressed()
 *
 * Callback for the "Down" button
 */
void 
fe_down_pressed (GtkWidget * widget,
                 gpointer data)
{
  GtkWidget *clist;
  gint row;

  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_CLIST(data));
  clist = GTK_WIDGET(data);

  if (!(GTK_CLIST (clist)->selection))
    return;

  row = GPOINTER_TO_INT ((GTK_CLIST (clist)->selection)->data);
  gtk_clist_swap_rows (GTK_CLIST (clist), row, row + 1);
}                                /* end fe_down_pressed */


/*
 * fe_up_pressed()
 *
 * Callback for the "Up" button
 */
void 
fe_up_pressed (GtkWidget * widget,
               gpointer data)
{
  GtkWidget *clist;
  gint row;

  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_CLIST(data));
  clist = GTK_WIDGET(data);

  if (!(GTK_CLIST (clist)->selection))
    return;

  row = GPOINTER_TO_INT ((GTK_CLIST (clist)->selection)->data);
  gtk_clist_swap_rows (GTK_CLIST (clist), row - 1, row);
}                                /* end fe_up_pressed */


/*
 * clist_button_event_press()
 *
 * Callback for when button is pressed.
 */
void
clist_button_event_press (GtkWidget * clist,
                             GdkEventButton * bevent,
                             gpointer data)
{
  gint row, column, res;
  GdkImlibImage *im;
  GdkPixmap *pixmap;
  GdkBitmap *mask;
  GtkCellType type;

  res = gtk_clist_get_selection_info (GTK_CLIST (clist),
                                      bevent->x,
                                      bevent->y,
                                      &row,
                                      &column);

  if ((bevent->button != 1) || (column != 0))
    return;

  type = gtk_clist_get_cell_type (GTK_CLIST (clist),
                                  row,
                                  column);

  if (type == GTK_CELL_PIXMAP)
    {
      gtk_clist_set_text (GTK_CLIST (clist),
                          row,
                          column,
                          NULL);

      gtk_clist_select_row (GTK_CLIST (clist), row, -1);
    }
  else
    {
      /* now for the pixmap from gdk */
      im = gdk_imlib_create_image_from_xpm_data (enabled_xpm);
      gdk_imlib_render (im, im->rgb_width, im->rgb_height);
      pixmap = gdk_imlib_copy_image (im);
      mask = gdk_imlib_copy_mask (im);
      gdk_imlib_destroy_image (im);

      gtk_clist_set_pixmap (GTK_CLIST (clist),
                            row,
                            0,
                            pixmap,
                            mask);

      gtk_clist_select_row (GTK_CLIST (clist), row, -1);

      gdk_pixmap_unref (pixmap);
      gdk_bitmap_unref (mask);
    }

  gtk_signal_emit_stop_by_name (GTK_OBJECT (clist),
                                "button_press_event");


} /* end clist_button_event_press() */


/*
 * fe_apply_pressed()
 *
 * Builds a new filter from the data provided, and sticks it where
 * the selection is in the clist
 */
void fe_apply_pressed(GtkWidget *widget,
                      gpointer data)
{
    filter *fil;
    gchar *temp;

    /* quick check before we malloc */
    temp = gtk_entry_get_text(GTK_ENTRY(fe_name_entry));
    if ((!temp) || (temp[0] == '\0') || (!unique_filter_name(data, temp)))
    {
        /* error_popup("Invalid filter name"); */
        return;
    }

    fil = filter_new();
    fil->name = g_strdup(temp);
    if (GTK_TOGGLE_BUTTON(fe_popup_button)->active)
    {
        static gchar defstring[19] = "Filter has matched";
        gchar *tmpstr;

        FILTER_SETFLAG(fil, FILTER_POPUP);
        tmpstr = gtk_entry_get_text(GTK_ENTRY(fe_popup_entry));
        
        strncpy(fil->popup_text,
                ((!tmpstr) || (tmpstr[0] == '\0')) ? defstring : tmpstr,
                256);
    }
#ifdef HAVE_LIBESD
    if (GTK_TOGGLE_BUTTON(fe_sound_button)->active)
    {
        gchar *tmpstr;

        FILTER_SETFLAG(fil, FILTER_SOUND);
        tmpstr = gtk_entry_get_text(GTK_ENTRY(fe_sound_entry));
        if ((!tmpstr) || (tmpstr[0] == '\0'))
        {
            filter_free(fil, NULL);
            /* error_dialog("You must provide a sound to play") */
            return;
        }
        strncpy(fil->popup_entry, tmpstr, PATH_MAX);
    }
#endif
} /* end fe_apply_pressed */


/*
 * fe_revert_pressed()
 *
 * Reverts the filter values to the ones stored.
 * It really just select()s the row, letting the callback handle
 * things
 */
void fe_revert_pressed(GtkWidget *widget,
                       gpointer data)
{
  GtkWidget *clist;

  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_CLIST(data));
  clist = GTK_WIDGET(data);

    gtk_clist_select_row(GTK_CLIST(clist),
                         GPOINTER_TO_INT(
                             ((GList *)(GTK_CLIST(clist)->selection))->data),
                         -1);
} /* end fe_revert_pressed() */
