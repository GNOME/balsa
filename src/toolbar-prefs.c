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

#include "config.h"

#include <string.h>
#include <gnome.h>

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include "libbalsa.h"

#include "address-book.h"
#include "balsa-app.h"
#include "balsa-icons.h"
#include "balsa-index.h"
#include "balsa-mblist.h"
#include "balsa-message.h"
#include "folder-conf.h"
#include "mailbox-conf.h"
#include "main-window.h"
#include "message-window.h"
#include "pref-manager.h"
#include "print.h"
#include "sendmsg-window.h"
#include "store-address.h"
#include "save-restore.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"
#endif

#include "libinit_balsa/init_balsa.h"

#include "toolbar-prefs.h"
#include "toolbar-factory.h"

#ifndef MAX
#define MAX(a, b) (a > b ? a : b)
#endif

#define OLD_BALSA_COMPATIBILITY_TRANSLATION

static int customize_open=0;
GtkWidget *customize_widget;
static int word_wrap;


struct toolbar_item {
    GtkWidget *widget;
    int id;
};

struct toolbar_page {
    GtkWidget *list;
    GtkWidget *destination;
    GtkWidget *preview;
    GtkWidget *add_button;
    GtkWidget *remove_button;
    GtkWidget *back_button;
    GtkWidget *forward_button;
    int selected_source;
    int selected_destination;
};

static struct toolbar_page toolbar_pages[STOCK_TOOLBAR_COUNT];
static struct toolbar_item *toolbar_items[STOCK_TOOLBAR_COUNT];
int toolbar_item_count[STOCK_TOOLBAR_COUNT];

static GtkWidget *create_toolbar_page(BalsaToolbarType);
static void page_destroy_cb(GtkWidget *, gpointer);
static void populate_list(GtkWidget *, int);
static void add_button_cb(GtkWidget *, gpointer);
static void remove_button_cb(GtkWidget *, gpointer);
static void back_button_cb(GtkWidget *, gpointer);
static void forward_button_cb(GtkWidget *, gpointer);
static void source_selected_cb(GtkCList *clist, gint row, gint column,
                               GdkEventButton *event, gpointer user_data);
static void source_unselected_cb(GtkCList *clist, gint row, gint column,
                                 GdkEventButton *event, gpointer user_data);
static void dest_selecteded_cb(GtkCList *clist, gint row, gint column,
                               GdkEventButton *event, gpointer user_data);
static void dest_unselected_cb(GtkCList *clist, gint row, gint column,
                               GdkEventButton *event, gpointer user_data);
static void recreate_preview(BalsaToolbarType toolbar, gboolean preview_only);
static void remove_toolbar_item(BalsaToolbarType toolbar, int item);
static void get_toolbar_data(BalsaToolbarType toolbar);
static void apply_toolbar_prefs(GtkWidget *widget, gpointer data);
static void wrap_toggled_cb(GtkWidget *widget, gpointer data);
static void page_active_cb(GtkWidget *widget, GdkEvent *event, gpointer data);

static void
page_active_cb(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    GtkToggleButton *btn;
    
    btn=(GtkToggleButton *)data;
    gtk_toggle_button_set_active(btn, word_wrap);
}

static void
wrap_toggled_cb(GtkWidget *widget, gpointer data)
{
    BalsaToolbarType toolbar = GPOINTER_TO_INT(data);
    
    word_wrap=GTK_TOGGLE_BUTTON(widget)->active;
    
    gnome_property_box_changed(GNOME_PROPERTY_BOX(customize_widget));
    recreate_preview(toolbar, FALSE);
}

static void
apply_toolbar_prefs(GtkWidget *widget, gpointer data)
{
    int i, j;
    int index;
    
    balsa_app.toolbar_wrap_button_text=word_wrap;
    
    for(i=0; i<STOCK_TOOLBAR_COUNT; i++) {
	index=get_toolbar_index(i);
	if(index == -1)
	    continue;
	
        for(j=0; balsa_app.toolbars[index][j]; j++) {
            g_free(balsa_app.toolbars[index][j]);
            balsa_app.toolbars[index][j]=NULL;
        }
        
        for(j=0; j<toolbar_item_count[i]; j++) {
            balsa_app.toolbars[index][j]=
                g_strdup(toolbar_buttons[toolbar_items[i][j].id].pixmap_id);
        }
        balsa_app.toolbars[index][j]=NULL;
    }
    
    update_all_toolbars();
}

/* get_toolbar_button_index:
   id - button id
   returns -1 on failure.
*/
int
get_toolbar_button_index(const char *id)
{
#ifdef OLD_BALSA_COMPATIBILITY_TRANSLATION
    static const struct {
        gchar *new;
        gchar *old;
    } button_converter[] = {
        { BALSA_PIXMAP_ATTACHMENT,   GNOME_STOCK_PIXMAP_ATTACH },
        { BALSA_PIXMAP_NEW,          GNOME_STOCK_PIXMAP_MAIL_NEW },
        { BALSA_PIXMAP_CONTINUE,     GNOME_STOCK_PIXMAP_MAIL },
        { BALSA_PIXMAP_RECEIVE,      GNOME_STOCK_PIXMAP_MAIL_RCV },
        { BALSA_PIXMAP_REPLY,        GNOME_STOCK_PIXMAP_MAIL_RPL },
        { BALSA_PIXMAP_REPLY_ALL,    "reply_to_all" },
        { BALSA_PIXMAP_REPLY_GROUP,  "reply_to_group" },
        { BALSA_PIXMAP_FORWARD,      GNOME_STOCK_PIXMAP_MAIL_FWD },
        { BALSA_PIXMAP_NEXT,         GNOME_STOCK_PIXMAP_FORWARD },
        { BALSA_PIXMAP_PREVIOUS,     GNOME_STOCK_PIXMAP_BACK },
        { BALSA_PIXMAP_PRINT,        GNOME_STOCK_PIXMAP_PRINT },
        { BALSA_PIXMAP_SAVE,         GNOME_STOCK_PIXMAP_SAVE },
        { BALSA_PIXMAP_SEND,         GNOME_STOCK_PIXMAP_MAIL_SND },
        { BALSA_PIXMAP_TRASH,        GNOME_STOCK_PIXMAP_TRASH },
        { BALSA_PIXMAP_TRASH_EMPTY,  "empty_trash" },
        { BALSA_PIXMAP_NEXT_UNREAD,  "next_unread" },
        { BALSA_PIXMAP_NEXT_FLAGGED, "next_flagged" },
        { BALSA_PIXMAP_SHOW_HEADERS, "show_all_headers" },
        { BALSA_PIXMAP_SHOW_PREVIEW, "show_preview" },
        { BALSA_PIXMAP_MARKED_NEW,   "flag_unread" },
        { BALSA_PIXMAP_MARKED_ALL,   "mark_all" },
        { BALSA_PIXMAP_IDENTITY,     "identity" },
        { BALSA_PIXMAP_CLOSE_MBOX,   GNOME_STOCK_PIXMAP_CLOSE },
        { BALSA_PIXMAP_OTHER_CLOSE,  "close_mbox" },
        { NULL, NULL }
    };
#endif
    int i;

    g_return_val_if_fail(id, -1);

    for(i=0; i<toolbar_button_count; i++) {
	if(!strcmp(id, toolbar_buttons[i].pixmap_id))
	    return i;
    }
#ifdef OLD_BALSA_COMPATIBILITY_TRANSLATION
    /* you have got a second chance.... */
    
    for(i=0; button_converter[i].new; i++) {
        if(!strcmp(id, button_converter[i].old)) {
            int j;
            for(j=0; j<toolbar_button_count; j++) {
                if(!strcmp(button_converter[i].new,
                           toolbar_buttons[j].pixmap_id))
                    return j;
            }
            return -1;
        }
    }
#endif
    return -1;
}

static void
get_toolbar_data(BalsaToolbarType toolbar)
{
    int index;
    int button;
    int i;
    
    index=get_toolbar_index(toolbar);
    g_return_if_fail(index>=0);
    toolbar_item_count[toolbar]=0;
    
    for(i=0; balsa_app.toolbars[index][i]; i++) {
	button=get_toolbar_button_index(balsa_app.toolbars[index][i]);
	if(button == -1) {
	    printf("Warning: unknown button %s\n",
		   balsa_app.toolbars[index][i]);
	    continue;
	}
	toolbar_items[toolbar][toolbar_item_count[toolbar]].id=button;
	toolbar_items[toolbar][toolbar_item_count[toolbar]++].widget=NULL;
    }
}

static void
replace_nl_with_space(char* str)
{
    while(*str) {
	if(*str == '\n')
	    *str=' ';
	str++;
    }
}
static void
recreate_preview(BalsaToolbarType toolbar, gboolean preview_only)
{
    GtkToolbar *bar;
    GtkWidget *btn;
    int index;
    int i;
    char *text, *wrap;
    char *list_data[2];
    int row;
    GdkPixmap *pixmap, *mask;
    
    list_data[0] = list_data[1] = NULL;
    bar=GTK_TOOLBAR(toolbar_pages[toolbar].preview);
    
    balsa_toolbar_remove_all(bar);
    if(!preview_only) {
	gtk_clist_clear(
	    GTK_CLIST(toolbar_pages[toolbar].destination));
	gtk_clist_set_row_height(
	    GTK_CLIST(toolbar_pages[toolbar].destination), 24);
	gtk_clist_set_column_width(
	    GTK_CLIST(toolbar_pages[toolbar].destination), 0, 24);
    }
    
    for(i=0; i<toolbar_item_count[toolbar]; i++) {
        if(toolbar_items[toolbar][i].id) {
	    index=toolbar_items[toolbar][i].id;
	    if(index == -1)
		continue;
	    text=wrap=g_strdup(_(toolbar_buttons[index].button_text));
	    if(!word_wrap)
		replace_nl_with_space(wrap);

	    btn=gtk_toolbar_append_item(
		bar, text,
		_(toolbar_buttons[index].help_text),
		_(toolbar_buttons[index].help_text),
		gnome_stock_pixmap_widget(GTK_WIDGET(balsa_app.main_window),
					  toolbar_buttons[index].pixmap_id),
		NULL,
		NULL);
	    if(word_wrap) 
		replace_nl_with_space(wrap);

	    toolbar_items[toolbar][i].widget=btn;
	    if(!preview_only) {
		row=gtk_clist_append(
		    GTK_CLIST(toolbar_pages[toolbar].destination),
		    list_data);
		gtk_clist_set_text(
		    GTK_CLIST(toolbar_pages[toolbar].destination),
		    row, 1, text);
		gnome_stock_pixmap_gdk(toolbar_buttons[index].pixmap_id,
				       GNOME_STOCK_PIXMAP_REGULAR,
				       &pixmap, &mask);
		gtk_clist_set_pixmap(
		    GTK_CLIST(toolbar_pages[toolbar].destination),
		    row, 0, pixmap, mask);
	    }
	    g_free(text);
        } else {
	    gtk_toolbar_append_space(bar);
	    toolbar_items[toolbar][i].widget=NULL;
	    if(!preview_only) {
		row=gtk_clist_append(
		    GTK_CLIST(toolbar_pages[toolbar].destination),
		    list_data);
		gtk_clist_set_text(
		    GTK_CLIST(toolbar_pages[toolbar].destination),
		    row, 1, _("Separator"));
	    }
	}
    }
    gtk_clist_set_column_width(GTK_CLIST(toolbar_pages[toolbar].destination),
			       1, 
			       gtk_clist_optimal_column_width(
				   GTK_CLIST(toolbar_pages[toolbar].destination), 1));
    
    gtk_widget_show_all(toolbar_pages[toolbar].preview);
    
}

static void
back_button_cb(GtkWidget *widget, gpointer data)
{
    int toolbar;
    struct toolbar_item tmp;
    int row;
    
    toolbar=GPOINTER_TO_INT(data);
    
    if(toolbar_pages[toolbar].selected_destination == -1)
        return;
    
    if(toolbar_pages[toolbar].selected_destination == 0)
        return;
    
    row=toolbar_pages[toolbar].selected_destination;
    
    tmp=toolbar_items[toolbar][toolbar_pages[toolbar].selected_destination];
    toolbar_items[toolbar][toolbar_pages[toolbar].selected_destination]=
	toolbar_items[toolbar][toolbar_pages[toolbar].selected_destination-1];
    toolbar_items[toolbar][toolbar_pages[toolbar].selected_destination-1]=tmp;
    --toolbar_pages[toolbar].selected_destination;
    
    recreate_preview(toolbar, TRUE);
    gtk_clist_swap_rows(GTK_CLIST(toolbar_pages[toolbar].destination),
			row, row-1);
    gtk_clist_moveto(GTK_CLIST(toolbar_pages[toolbar].destination),
		     row-1, 0, 0, 0);
    gnome_property_box_changed(GNOME_PROPERTY_BOX(customize_widget));
    gtk_widget_set_sensitive(toolbar_pages[toolbar].back_button, 
			     toolbar_pages[toolbar].selected_destination > 0);
    gtk_widget_set_sensitive(toolbar_pages[toolbar].forward_button, 
			     TRUE);
}

static void
forward_button_cb(GtkWidget *widget, gpointer data)
{
    int toolbar;
    struct toolbar_item tmp;
    int row;
    
    toolbar = GPOINTER_TO_INT(data);
    if(toolbar_pages[toolbar].selected_destination == -1)
        return;
    
    if(toolbar_pages[toolbar].selected_destination >=
       toolbar_item_count[toolbar]-1)
        return;
    
    row=toolbar_pages[toolbar].selected_destination;
    
    tmp=toolbar_items[toolbar][toolbar_pages[toolbar].selected_destination];
    toolbar_items[toolbar][toolbar_pages[toolbar].selected_destination]=
	toolbar_items[toolbar][toolbar_pages[toolbar].selected_destination+1];
    toolbar_items[toolbar][toolbar_pages[toolbar].selected_destination+1]=tmp;
    ++toolbar_pages[toolbar].selected_destination;
    
    recreate_preview(toolbar, TRUE);
    gtk_clist_swap_rows(GTK_CLIST(toolbar_pages[toolbar].destination),
			row, row+1);
    gtk_clist_moveto(GTK_CLIST(toolbar_pages[toolbar].destination),
		     row-1, 0, 0, 0);
    gnome_property_box_changed(GNOME_PROPERTY_BOX(customize_widget));
    gtk_widget_set_sensitive(toolbar_pages[toolbar].back_button, 
			     TRUE);
    gtk_widget_set_sensitive(toolbar_pages[toolbar].forward_button, 
			     toolbar_pages[toolbar].selected_destination <
			     toolbar_item_count[toolbar]-1);
}

static void
remove_toolbar_item(BalsaToolbarType toolbar, int item)
{
    int i;
    
    for(i=item+1;i<toolbar_item_count[toolbar];i++)
	toolbar_items[toolbar][i-1]=toolbar_items[toolbar][i];
    
    toolbar_item_count[toolbar]--;
}

static void
remove_button_cb(GtkWidget *widget, gpointer data)
{
    int toolbar;
    GtkCList *list;
    int pos;
    
    toolbar=(int)data;
    list=GTK_CLIST(toolbar_pages[toolbar].destination);
    
    pos=toolbar_pages[toolbar].selected_destination;
    remove_toolbar_item(toolbar, pos);
    populate_list(toolbar_pages[toolbar].list, toolbar);
    gtk_clist_remove(list, toolbar_pages[toolbar].selected_destination);
    gnome_property_box_changed(GNOME_PROPERTY_BOX(customize_widget));
    recreate_preview(toolbar, TRUE);
}

static void
source_selected_cb(GtkCList *clist, gint row, gint column,
                   GdkEventButton *event, gpointer user_data)
{
    int toolbar = GPOINTER_TO_INT(user_data);
    toolbar_pages[toolbar].selected_source=row;
    gtk_widget_set_sensitive(toolbar_pages[toolbar].add_button, TRUE);
}
static void
source_unselected_cb(GtkCList *clist, gint row, gint column,
                     GdkEventButton *event, gpointer user_data)
{
    int toolbar = GPOINTER_TO_INT(user_data);
    toolbar_pages[toolbar].selected_source=-1;
    gtk_widget_set_sensitive(toolbar_pages[toolbar].add_button, FALSE);
}

static void
dest_selected_cb(GtkCList *clist, gint row, gint column,
                 GdkEventButton *event, gpointer user_data)
{
    int toolbar = GPOINTER_TO_INT(user_data);
    
    toolbar_pages[toolbar].selected_destination=row;
    gtk_widget_set_sensitive(toolbar_pages[toolbar].remove_button,  TRUE);
    gtk_widget_set_sensitive(toolbar_pages[toolbar].back_button, 
			     toolbar_pages[toolbar].selected_destination > 0);
    gtk_widget_set_sensitive(toolbar_pages[toolbar].forward_button, 
			     toolbar_pages[toolbar].selected_destination <
			     toolbar_item_count[toolbar]-1);
}

static void
dest_unselected_cb(GtkCList *clist, gint row, gint column,
                   GdkEventButton *event, gpointer user_data)
{
    int toolbar = GPOINTER_TO_INT(user_data);
    
    toolbar_pages[toolbar].selected_destination=-1;
    gtk_widget_set_sensitive(toolbar_pages[toolbar].remove_button,  FALSE);
    gtk_widget_set_sensitive(toolbar_pages[toolbar].back_button,    FALSE);
    gtk_widget_set_sensitive(toolbar_pages[toolbar].forward_button, FALSE);
}

static void
add_button_cb(GtkWidget *widget, gpointer data)
{
    BalsaToolbarType toolbar;
    int add_item;
    GtkCList *list;
    int row;
    char *list_data[2];
    GdkPixmap *pixmap, *mask;
    char *text, *wrap;
    int i;

    toolbar=GPOINTER_TO_INT(data);
    list_data[0]=list_data[1]=NULL;

    list=GTK_CLIST(toolbar_pages[toolbar].destination);
    if(toolbar_pages[toolbar].selected_source == -1)
	return;

    if(toolbar_item_count[toolbar] >= MAXTOOLBARITEMS)
	return;

    if(!toolbar_item_count[toolbar] && !toolbar_pages[toolbar].selected_source)
	return;

    add_item=GPOINTER_TO_INT(
	gtk_clist_get_row_data(GTK_CLIST(toolbar_pages[toolbar].list),
			       toolbar_pages[toolbar].selected_source));

    if(add_item) {
	gtk_clist_get_pixmap(
	    GTK_CLIST(toolbar_pages[toolbar].list),
	    toolbar_pages[toolbar].selected_source, 0, &pixmap, &mask);
	
	gtk_clist_remove(GTK_CLIST(toolbar_pages[toolbar].list),
			 toolbar_pages[toolbar].selected_source);
    }
    
    row=toolbar_pages[toolbar].selected_destination;

    if(row == -1) {
	row=gtk_clist_append(list, list_data);
	toolbar_items[toolbar][toolbar_item_count[toolbar]++].id=
	    add_item;
    } else {
	row=gtk_clist_insert(list, row+1, list_data);
	for(i=toolbar_item_count[toolbar]-1;i >= row;i--)
	    toolbar_items[toolbar][i+1]=toolbar_items[toolbar][i];
	++toolbar_item_count[toolbar];
	toolbar_items[toolbar][row].id=add_item;
    }

    text=wrap=g_strdup(_(toolbar_buttons[add_item].button_text));
    while(*wrap) {
	if(*wrap == '\n')
	    *wrap=' ';
	++wrap;
    }

    gtk_clist_set_text(list, row, 1, text);
    g_free(text);
    if(add_item) {
	gtk_clist_set_pixmap(list, row, 0, pixmap, mask);
    }
	
    gtk_clist_select_row(list, row, 0);
    gtk_clist_moveto(list, row, 0, 0, 0);

    gtk_widget_show_all(toolbar_pages[toolbar].preview);
    gnome_property_box_changed(GNOME_PROPERTY_BOX(customize_widget));
    recreate_preview(toolbar, TRUE);
}

static void
populate_list(GtkWidget *list, int toolbar)
{
    int i, j, row;
    GdkPixmap *pixmap;
    GdkPixmap *mask;
    char *tmp[2];
    char *text, *wrap;
    char **legal;

    legal=get_legal_toolbar_buttons(toolbar);

    /* FIXME: clear does not emit unselect-row signal for selected rows? */
    gtk_clist_unselect_all(GTK_CLIST(list)); 
    gtk_clist_clear(GTK_CLIST(list));

    gtk_clist_set_row_height(GTK_CLIST(list), 24);
    gtk_clist_set_column_width(GTK_CLIST(list), 0, 24);
    for(i=0; i<toolbar_button_count; i++) {
	for(j=0; legal[j]; j++)
	    if(!strcmp(legal[j], toolbar_buttons[i].pixmap_id))
		break;
	
	if(!legal[j])
	    continue;

	for(j=0; j<toolbar_item_count[toolbar]; j++) {
	    if(i == toolbar_items[toolbar][j].id)
		break;
	}
	if(i && j != toolbar_item_count[toolbar])
	    continue;
	
	tmp[0]=NULL;
	tmp[1]=NULL;
	row=gtk_clist_append(GTK_CLIST(list), tmp);
	if(*(toolbar_buttons[i].pixmap_id)) {
	    gnome_stock_pixmap_gdk(toolbar_buttons[i].pixmap_id,
				   GNOME_STOCK_PIXMAP_REGULAR,
				   &pixmap, &mask);
	    gtk_clist_set_pixmap(GTK_CLIST(list), row, 0, pixmap, mask);
	}
	text = wrap = g_strdup(_(toolbar_buttons[i].button_text));
	while(*wrap) {
	    if(*wrap == '\n')
		*wrap=' ';
	    ++wrap;
	}
	gtk_clist_set_text(GTK_CLIST(list), row, 1, text);
	gtk_clist_set_row_data(GTK_CLIST(list), row,
			       (gpointer)i);
	g_free(text);
    }
    gtk_clist_set_column_width(GTK_CLIST(list), 1,
			       gtk_clist_optimal_column_width(GTK_CLIST(list),
							      1));
}

static void
page_destroy_cb(GtkWidget *widget, gpointer data)
{
    int toolbar;

    toolbar=GPOINTER_TO_INT(data);
    g_free(toolbar_items[toolbar]);
    toolbar_item_count[toolbar]=0;
    customize_open=0;
}

/* customize_dialog_cb:
   FIXME: comment needed.
*/
void
customize_dialog_cb(GtkWidget *widget, gpointer data)
{
    GnomeApp *active_window = GNOME_APP(data);

    /* There can only be one */
    if(customize_open) {
	gdk_window_raise(customize_widget->window);
	return;
    }
    
    customize_open=1;
    
    customize_widget=gnome_property_box_new();
    
    gtk_window_set_title(GTK_WINDOW(customize_widget), _("Customize"));
    gtk_window_set_policy(GTK_WINDOW(customize_widget), TRUE, TRUE, TRUE);
    gtk_window_set_wmclass(GTK_WINDOW(customize_widget), "customize", "Balsa");
    gtk_window_set_default_size(GTK_WINDOW(customize_widget), 600, 440);

    gnome_dialog_set_parent(GNOME_DIALOG(customize_widget),
			    GTK_WINDOW(active_window));
    gtk_object_set_data(GTK_OBJECT(customize_widget), "balsawindow",
			(gpointer)active_window);
    
    word_wrap=balsa_app.toolbar_wrap_button_text;
    
    gnome_property_box_append_page(GNOME_PROPERTY_BOX(customize_widget),
                                   create_toolbar_page(TOOLBAR_MAIN),
				   gtk_label_new(_("Main window")));
    
    gnome_property_box_append_page(GNOME_PROPERTY_BOX(customize_widget),
                                   create_toolbar_page(TOOLBAR_COMPOSE),
				   gtk_label_new(_("Compose window")));
    
    gnome_property_box_append_page(GNOME_PROPERTY_BOX(customize_widget),
                                   create_toolbar_page(TOOLBAR_MESSAGE),
				   gtk_label_new(_("Message window")));
    
    gtk_widget_show_all(customize_widget);
}

static GtkWidget*
create_toolbar_page(BalsaToolbarType toolbar)
{
    GtkWidget *outer_box;
    GtkWidget *preview_frame, *preview_scroll, *preview_box;
    GtkWidget *preview_ctlbox;
    GtkWidget *lower_ctlbox, *button_box, *move_button_box, *center_button_box;
    GtkWidget *list_frame, *list_scroll, *list;
    GtkWidget *destination_frame, *destination_scroll, *destination;
    GtkWidget *add_button, *remove_button;
    GtkWidget *back_button, *forward_button;
    GtkWidget *wrap_button;

    /* Will only create if not loaded from config */
    create_stock_toolbar(toolbar);

    toolbar_pages[toolbar].selected_source=-1;
    toolbar_pages[toolbar].selected_destination=-1;

    /* The "window itself" */
    outer_box=gtk_vbox_new(FALSE, 0);

    /* Initialize dialog data */
    toolbar_items[toolbar]=(struct toolbar_item *)g_malloc(
	sizeof(struct toolbar_item)*MAXTOOLBARITEMS);

    toolbar_item_count[toolbar]=0;

    get_toolbar_data(toolbar);

    /* Preview display */
    preview_frame=gtk_frame_new(_("Preview"));
    gtk_box_pack_start(GTK_BOX(outer_box), preview_frame, FALSE, FALSE, 0);

    preview_ctlbox=gtk_vbox_new(FALSE, 5);
    gtk_container_add(GTK_CONTAINER(preview_frame), preview_ctlbox);
    gtk_container_set_border_width(GTK_CONTAINER(preview_ctlbox), 5);

    /* The preview is an actual, fully functional toolbar */
    preview_box=GTK_WIDGET(gtk_toolbar_new(GTK_ORIENTATION_HORIZONTAL,
					   GTK_TOOLBAR_BOTH));
    gtk_toolbar_set_space_style(GTK_TOOLBAR(preview_box),
				GTK_TOOLBAR_SPACE_LINE);
    gtk_toolbar_set_button_relief(GTK_TOOLBAR(preview_box),
				  GTK_RELIEF_NONE);
    gtk_toolbar_set_space_size(GTK_TOOLBAR(preview_box),
			       18);
    toolbar_pages[toolbar].preview=preview_box;

    /* embedded in a scrolled_window */
    preview_scroll=gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(preview_scroll),
				   GTK_POLICY_ALWAYS, GTK_POLICY_NEVER);

    gtk_box_pack_start(GTK_BOX(preview_ctlbox), preview_scroll, TRUE, 
		       TRUE, 0);

    /* can't avoid the usize */
    /* gtk_widget_set_usize(preview_scroll, 600, 86); */
    gtk_scrolled_window_add_with_viewport(
	GTK_SCROLLED_WINDOW(preview_scroll), preview_box);

    /* Wrap button */
    wrap_button=gtk_check_button_new_with_label(_("Wrap button labels"));

    gtk_box_pack_start(GTK_BOX(preview_ctlbox), wrap_button,
		       FALSE, FALSE, 0);

    /* Done with preview */
	
    /* Box for lower half of window */
    lower_ctlbox=gtk_hbox_new(FALSE, 5);
    gtk_container_set_border_width(GTK_CONTAINER(lower_ctlbox), 5);

    gtk_box_pack_start(GTK_BOX(outer_box), lower_ctlbox, TRUE, TRUE, 0);

    /* A CList to show the available items */
    list_scroll=gtk_scrolled_window_new(NULL, NULL);

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_scroll),
				   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    list_frame=gtk_frame_new(_("Available buttons"));
    list=gtk_clist_new(2);
    toolbar_pages[toolbar].list=list;

    gtk_box_pack_start(GTK_BOX(lower_ctlbox), list_frame,
		       TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(list_frame), list_scroll);
    gtk_container_add(GTK_CONTAINER(list_scroll), list);

    /* Done with source list */

    /* Another CList to show the current tools */
    destination_scroll=gtk_scrolled_window_new(NULL, NULL);
    /* gtk_widget_set_usize(destination_scroll, 200, 200); */

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(destination_scroll),
				   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	
    destination_frame=gtk_frame_new(_("Current toolbar"));
    destination=gtk_clist_new(2);
    toolbar_pages[toolbar].destination=destination;

    /* Done with destination list */

    /* Button box */
    center_button_box=gtk_vbox_new(TRUE, 0);

    button_box=gtk_vbox_new(FALSE, 0);

    gtk_box_pack_start(GTK_BOX(lower_ctlbox), center_button_box,
		       FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(center_button_box), button_box,
		       FALSE, FALSE, 0);

    back_button=gnome_pixmap_button(
	gnome_stock_pixmap_widget(outer_box, GNOME_STOCK_PIXMAP_UP),
	_("Up"));
    toolbar_pages[toolbar].back_button=back_button;

    gtk_box_pack_start(GTK_BOX(button_box), back_button, FALSE, FALSE, 0);

    move_button_box=gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), move_button_box, FALSE, FALSE, 0);

    remove_button=gnome_pixmap_button(
	gnome_stock_pixmap_widget(outer_box, GNOME_STOCK_PIXMAP_BACK),
	_("-"));
    toolbar_pages[toolbar].remove_button=remove_button;

    gtk_box_pack_start(GTK_BOX(move_button_box),
		       remove_button, FALSE, FALSE, 0);

    add_button=gnome_pixmap_button(
	gnome_stock_pixmap_widget(outer_box, GNOME_STOCK_PIXMAP_FORWARD),
	_("+"));
    toolbar_pages[toolbar].add_button=add_button;

    gtk_box_pack_start(GTK_BOX(move_button_box),
		       add_button, FALSE, FALSE, 0);

    forward_button=gnome_pixmap_button(
	gnome_stock_pixmap_widget(outer_box, GNOME_STOCK_PIXMAP_DOWN),
	_("Down"));
    toolbar_pages[toolbar].forward_button=forward_button;

    gtk_box_pack_start(GTK_BOX(button_box), forward_button, FALSE, FALSE, 0);

    /* Pack destination list */
    gtk_box_pack_start(GTK_BOX(lower_ctlbox), destination_frame,
		       TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(destination_frame), destination_scroll);
    gtk_container_add(GTK_CONTAINER(destination_scroll), destination);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wrap_button), word_wrap);

    /* Frame signals */
    gtk_signal_connect(GTK_OBJECT(outer_box), "destroy",
		       page_destroy_cb, (gpointer)toolbar);

    gtk_signal_connect(GTK_OBJECT(customize_widget), "apply",
		       GTK_SIGNAL_FUNC(apply_toolbar_prefs), (gpointer)toolbar);

    /* UI signals */
    gtk_signal_connect(GTK_OBJECT(list), "select-row",
		       source_selected_cb, (gpointer)toolbar);
    gtk_signal_connect(GTK_OBJECT(list), "unselect-row",
		       source_unselected_cb, (gpointer)toolbar);

    gtk_signal_connect(GTK_OBJECT(destination), "select-row",
		       dest_selected_cb, (gpointer)toolbar);
    gtk_signal_connect(GTK_OBJECT(destination), "unselect-row",
		       dest_unselected_cb, (gpointer)toolbar);

    gtk_signal_connect(GTK_OBJECT(add_button), "clicked",
		       add_button_cb, (gpointer)toolbar);

    gtk_signal_connect(GTK_OBJECT(remove_button), "clicked",
		       remove_button_cb, (gpointer)toolbar);

    gtk_signal_connect(GTK_OBJECT(forward_button), "clicked",
		       forward_button_cb, (gpointer)toolbar);

    gtk_signal_connect(GTK_OBJECT(back_button), "clicked",
		       back_button_cb, (gpointer)toolbar);

    gtk_signal_connect(GTK_OBJECT(wrap_button), "toggled",
		       wrap_toggled_cb, (gpointer)toolbar);

    gtk_signal_connect(GTK_OBJECT(outer_box), "expose-event",
		       page_active_cb, (gpointer)wrap_button);

    gtk_widget_set_sensitive(add_button, FALSE);
    gtk_widget_set_sensitive(remove_button, FALSE);
    gtk_widget_set_sensitive(back_button, FALSE);
    gtk_widget_set_sensitive(forward_button, FALSE);

    populate_list(list, toolbar);

    recreate_preview(toolbar, FALSE);

    return outer_box;
}
