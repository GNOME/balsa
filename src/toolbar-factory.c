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

struct toolbar_bdata {
    GtkWidget *widget;
    void (*callback)(GtkWidget *, gpointer);
    gpointer data;
    const char *id;
    int disabled;
    int position;
} toolbar_data[MAXTOOLBARS][MAXTOOLBARITEMS] = { { {0} } };

static struct toolbar_bmap
{
    GtkWidget *window;
    GtkWidget *toolbar;
    BalsaToolbarType type;
} toolbar_map[100];
static int toolbar_map_entries=0;

static char *toolbar0_legal[]={
    "",
    BALSA_PIXMAP_CLOSE_MBOX,
    BALSA_PIXMAP_CONTINUE,
    BALSA_PIXMAP_FORWARD,
    BALSA_PIXMAP_MARKED_ALL,
    BALSA_PIXMAP_MARKED_NEW,
    BALSA_PIXMAP_NEW,
    BALSA_PIXMAP_NEXT,
    BALSA_PIXMAP_NEXT_FLAGGED,
    BALSA_PIXMAP_NEXT_UNREAD,
    BALSA_PIXMAP_PREVIOUS,
    BALSA_PIXMAP_PRINT,
    BALSA_PIXMAP_RECEIVE,
    BALSA_PIXMAP_REPLY,
    BALSA_PIXMAP_REPLY_ALL,
    BALSA_PIXMAP_REPLY_GROUP,
    BALSA_PIXMAP_SEND_RECEIVE,
    BALSA_PIXMAP_SHOW_HEADERS,
    BALSA_PIXMAP_SHOW_PREVIEW,
    BALSA_PIXMAP_TRASH,
    BALSA_PIXMAP_TRASH_EMPTY,
    NULL
};

static char *toolbar1_legal[]={
    "",
    BALSA_PIXMAP_ATTACHMENT,
    BALSA_PIXMAP_IDENTITY,
    BALSA_PIXMAP_POSTPONE,
    BALSA_PIXMAP_PRINT,
    BALSA_PIXMAP_SAVE,
    BALSA_PIXMAP_SEND,
    GNOME_STOCK_PIXMAP_CLOSE,
    GNOME_STOCK_PIXMAP_SPELLCHECK,
    NULL
};

static char *toolbar2_legal[]={
    "",
    BALSA_PIXMAP_FORWARD,
    BALSA_PIXMAP_NEXT,
    BALSA_PIXMAP_NEXT_FLAGGED,
    BALSA_PIXMAP_NEXT_UNREAD,
    BALSA_PIXMAP_PREVIOUS,
    BALSA_PIXMAP_PRINT,
    BALSA_PIXMAP_REPLY,
    BALSA_PIXMAP_REPLY_ALL,
    BALSA_PIXMAP_REPLY_GROUP,
    BALSA_PIXMAP_SAVE,
    BALSA_PIXMAP_SHOW_HEADERS,
    BALSA_PIXMAP_TRASH,
    BALSA_PIXMAP_TRASH_EMPTY,
    GNOME_STOCK_PIXMAP_CLOSE,
    NULL
};

static char **toolbar_legal[]={toolbar0_legal, toolbar1_legal, toolbar2_legal};

button_data toolbar_buttons[]={
    {"", N_("Separator"), "", 0},
    {BALSA_PIXMAP_RECEIVE, N_("Check"),
     N_("Check for new email"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_NEW, N_("Compose"),
     N_("Compose message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_CONTINUE, N_("Continue"),
     N_("Continue message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_REPLY, N_("Reply"),
     N_("Reply to the current message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_REPLY_ALL, N_("Reply\nto all"),
     N_("Reply to all recipients"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_REPLY_GROUP, N_("Reply\nto group"),
     N_("Reply to mailing list"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_FORWARD, N_("Forward"),
     N_("Forward the current message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_PREVIOUS, N_("Previous"),
     N_("Open previous"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_NEXT, N_("Next"),
     N_("Open next"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_NEXT_UNREAD, N_("Next\nunread"),
     N_("Open next unread message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_NEXT_FLAGGED, N_("Next\nflagged"),
     N_("Open next flagged message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_TRASH, N_("Trash /\nDelete"),
     N_("Move the current message to trash"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_POSTPONE, N_("Postpone"),
     N_("Postpone current message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_PRINT, N_("Print"),
     N_("Print current message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_SEND, N_("Send"),
     N_("Send this message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_SEND_RECEIVE, N_("Send /\nReceive"),
     N_("Send and Receive messages"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_ATTACHMENT, N_("Attach"),
     N_("Add attachments to this message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_SAVE, N_("Save"),
     N_("Save the current item"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_IDENTITY, N_("Select Identity"),
     N_("Set identity to use for this message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {GNOME_STOCK_PIXMAP_SPELLCHECK, N_("Spelling"),
     N_("Run a spell check"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {GNOME_STOCK_PIXMAP_CLOSE, N_("Cancel"), 
     N_("Cancel this message"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_MARKED_NEW, N_("Toggle\nnew"),
     N_("Toggle new message flag"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_MARKED_ALL, N_("Mark all"),
     N_("Mark all messages in current mailbox"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_SHOW_HEADERS, N_("All\nheaders"),
     N_("Show all headers"), TOOLBAR_BUTTON_TYPE_TOGGLE},
    {BALSA_PIXMAP_TRASH_EMPTY, N_("Empty Trash"),
     N_("Delete messages from the trash mailbox"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_CLOSE_MBOX, N_("Close"),
     N_("Close current mailbox"), TOOLBAR_BUTTON_TYPE_BUTTON},
    {BALSA_PIXMAP_SHOW_PREVIEW, N_("Preview\npane"),
     N_("Show preview pane"), TOOLBAR_BUTTON_TYPE_TOGGLE}
};

const int toolbar_button_count = sizeof(toolbar_buttons)
     /sizeof(button_data);

static void populate_stock_toolbar(int bar, BalsaToolbarType id);
static int get_toolbar_button_slot(BalsaToolbarType toolbar, const char *id);
static GtkToolbar *get_bar_instance(GtkWidget *window, 
				    BalsaToolbarType toolbar);
static int get_position_value(BalsaToolbarType toolbar, const char *id);

#ifdef NEW_GTK
#define balsa_toolbar_remove_all(bar,j) gtk_toolbar_remove_all(bar)
#else
/* this should go to GTK because it modifies its internal structures. */
void
balsa_toolbar_remove_all(GtkToolbar *toolbar)
{
    GList *children;
    
    g_return_if_fail (GTK_IS_TOOLBAR (toolbar));
    
    for (children = toolbar->children; children; children = children->next) {
	GtkToolbarChild *child = children->data;
	
	if (child->type != GTK_TOOLBAR_CHILD_SPACE) {
#ifdef REMOVE_TOOLBAR_ITEMS_VERY_CAUTIOUSLY
	    gtk_widget_ref (child->widget);
	    gtk_widget_unparent (child->widget);
	    gtk_widget_destroy (child->widget);
	    gtk_widget_unref (child->widget);
#else 
	    gtk_widget_unparent (child->widget);
#endif /* REMOVE_TOOLBAR_ITEMS_VERY_CAUTIOUSLY */
	}
	g_free (child);
    }
    g_list_free (toolbar->children);
    toolbar->children = NULL;
    gtk_widget_queue_resize (GTK_WIDGET (toolbar));
}
#endif

static int
get_position_value(BalsaToolbarType toolbar, const char *id)
{
    int i;

    for(i=0; i<MAXTOOLBARITEMS; i++)  {
	if(toolbar_data[toolbar][i].id &&
	   !strcmp(toolbar_data[toolbar][i].id, id))
	    break;
    }
    if(i == MAXTOOLBARITEMS)
	return -1;
    return toolbar_data[toolbar][i].position;
}

/* get_tool_widget:
   Get the GtkWidget * to a button in a specific toolbar.

   Parameters:
   		window		The window the toolbar is attached to
		toolbar		The type of the toolbar to search
		id			The ID string of the button pixmap

   Returns:
   		GtkWidget *, or NULL if error / not found
*/
GtkWidget *
get_tool_widget(GtkWidget *window, BalsaToolbarType toolbar, const char *id)
{
    GtkToolbar *bar;
    GList *children;
    int position;
    GtkWidget *child;

    bar=get_bar_instance(window, toolbar);
    if(!bar)
	return NULL;
    
    position=get_position_value(toolbar, id);
    if (position < 0)
	return NULL;
    
    children = gtk_container_children(GTK_CONTAINER(bar));
    child = GTK_WIDGET(g_list_nth_data(children, position));
    g_list_free(children);

    return child;
}

/* get_bar_instance:
   Get a pointer to the toolbar for a given window

   Parameters:
   	window		Window the toolbar is attached to
	toolbar		The type of the toolbar

   Returns:
   	GtkToolbar *, or NULL if error / not found

   Notes:
   	Uses the internal map tables, _not_ the GtkWidget "children" list
*/
static GtkToolbar*
get_bar_instance(GtkWidget *window, BalsaToolbarType toolbar)
{
    int i;

    for(i=0; i<toolbar_map_entries; i++)  {
	if(toolbar_map[i].window == window &&
	   toolbar_map[i].type == toolbar)
	    return GTK_TOOLBAR(toolbar_map[i].toolbar);
    }
    return NULL;
}

static const gchar* main_toolbar[] = {
    BALSA_PIXMAP_RECEIVE,
    "",
    BALSA_PIXMAP_TRASH,
    "",
    BALSA_PIXMAP_NEW,
    BALSA_PIXMAP_CONTINUE,
    BALSA_PIXMAP_REPLY,
    BALSA_PIXMAP_REPLY_ALL,
    BALSA_PIXMAP_FORWARD,
    "",
    BALSA_PIXMAP_PREVIOUS,
    BALSA_PIXMAP_NEXT,
    BALSA_PIXMAP_NEXT_UNREAD,
    "",
    BALSA_PIXMAP_PRINT,
    NULL
};

static const gchar* compose_toolbar[] = {
    BALSA_PIXMAP_SEND,
    "",
    BALSA_PIXMAP_ATTACHMENT,
    "",
    BALSA_PIXMAP_SAVE,
    "",
    BALSA_PIXMAP_IDENTITY,
    "",
    GNOME_STOCK_PIXMAP_SPELLCHECK,
    "",
    BALSA_PIXMAP_PRINT,
    "",
    GNOME_STOCK_PIXMAP_CLOSE,
    NULL
};

static const gchar* message_toolbar[] = {
    BALSA_PIXMAP_NEXT_UNREAD,
    "",
    BALSA_PIXMAP_REPLY,
    BALSA_PIXMAP_REPLY_ALL,
    BALSA_PIXMAP_REPLY_GROUP,
    BALSA_PIXMAP_FORWARD,
    "",
    BALSA_PIXMAP_PREVIOUS,
    BALSA_PIXMAP_NEXT,
    BALSA_PIXMAP_SAVE,
    "",
    BALSA_PIXMAP_PRINT,
    "",
    BALSA_PIXMAP_TRASH,
    NULL
};

static const gchar* null_toolbar[] = { NULL };

static void
populate_stock_toolbar(int index, BalsaToolbarType id)
{
    const gchar** toolbar;
    int i;

    switch(id) {
    case TOOLBAR_MAIN:     toolbar = main_toolbar;    break;
    case TOOLBAR_COMPOSE:  toolbar = compose_toolbar; break; 
    case TOOLBAR_MESSAGE:  toolbar = message_toolbar; break;
    default:               toolbar = null_toolbar;    break;
    }
    for(i=0; toolbar[i]; i++)
	balsa_app.toolbars[index][i] = g_strdup(toolbar[i]);
    balsa_app.toolbars[index][i]= NULL;
}

/* get_toolbar_index:
   Get the index of the given toolbar in the data read from config
   
   Toolbar numbering in the config file is dependent on the order in that
   each toolbar was customized. Therefore, the toolbar ID is not the same
   as the config index. This maps the toolbar id to the config index.

   Parameters:
   	id		ID of the desired toolbar

   Returns:
   	Toolbar index in config data, or -1 if error / not found
*/
int
get_toolbar_index(BalsaToolbarType id)
{
    int i;
    
    for(i=0; i<balsa_app.toolbar_count; i++)
	if(balsa_app.toolbar_ids[i] == id)
	    return i;
    
    return -1;
}

/* create_stock_toolbar:
   Create a stock toolbar (uncustomized version) from the tables in this file
   
   This function is called to create a toolbar template when a toolbar has
   never been customized.

   Parameters:
   	id			ID of the toolbar to create

   Returns:
	0 if OK, -1 if there are too many toolbars.
*/
int
create_stock_toolbar(BalsaToolbarType id)
{
    int newbar;
    
    if(get_toolbar_index(id) != -1)
	return 0;
    
    /* Create new toolbar */
    if(balsa_app.toolbar_count >= MAXTOOLBARS)
	return -1;
    
    newbar = balsa_app.toolbar_count++;
    
    balsa_app.toolbars[newbar]=
	(char **)g_malloc(sizeof(char *)*MAXTOOLBARITEMS);
    
    populate_stock_toolbar(newbar, id);
    balsa_app.toolbar_ids[newbar]=id;
    
    return 0;
}

/* get_toolbar:
   This is the main toolbar generating function

   It will check if there is a toolbar for the given window/type combination
   If one is found, it will empty and repopulate it, if none is found it
   will create one, enter it into the local tables and fill it with the
   buttons loaded from config or a default set of buttons.

   The first time this function is called for a given window, thr caller
   is responsible for attaching the newly created toolbar to it's window.
   On subsequent calls for the same window / id combination this _must_ not
   be done.
   Once the toolbar is attached to a window, it will be updated in place. It
   will never be destroyed for the lifetime of the window. Therefore, the
   results of calling any of the toolbar add functions on the second and
   further calls to this function will yield unsightly results and maybe
   cause crashes.

   Correct usage is:
   	-	Call get_toolbar _once_ during window construction. Attach the
		returned toolbar handle to the window.
	-	When a toolbar needs to be refreshed, call get_toolbar again, but
		_discard_ the return value
	-	In a "destroy" handler for a window, call release_toolbars
   
   Parameters:
	window		the GtkWindow for which a toolbar should be created
	toolbar		the ID of the toolbar to create

   Returns:
    a GtkToolbar *, or NULL if error
*/
GtkToolbar *
get_toolbar(GtkWidget *window, BalsaToolbarType toolbar)
{
    GtkToolbar *bar;
    int index;
    int i, j, button;
    int position;
    int type;
    char *tmp, *text;
    struct toolbar_bdata tmpdata[MAXTOOLBARITEMS];

    memset((char *)&tmpdata, 0, sizeof(tmpdata));

    for(i=0; i<MAXTOOLBARITEMS; i++) {
	tmpdata[i]=toolbar_data[toolbar][i];
	tmpdata[i].widget=NULL;
	tmpdata[i].position=-1;
    }
	
    for(i=0; i<toolbar_map_entries; i++) {
	if(toolbar_map[i].window == window && toolbar_map[i].type == toolbar)
	    break;
    }
    if(i == toolbar_map_entries) {
	if(i >= 100) /* FIXME: what is this magic number? */
	    return NULL;
	++toolbar_map_entries;
#if BALSA_MAJOR < 2
	bar=GTK_TOOLBAR(gtk_toolbar_new(GTK_ORIENTATION_HORIZONTAL,
					GTK_TOOLBAR_BOTH));
#else
        bar = GTK_TOOLBAR(gtk_toolbar_new());
        gtk_toolbar_set_orientation(bar, GTK_ORIENTATION_HORIZONTAL);
        gtk_toolbar_set_style(bar, GTK_TOOLBAR_BOTH);
#endif                          /* BALSA_MAJOR < 2 */
    } else {
	bar=GTK_TOOLBAR(toolbar_map[i].toolbar);
	/* remove all items from the existing bar. */
	balsa_toolbar_remove_all(bar);
    }

    toolbar_map[i].toolbar=GTK_WIDGET(bar);
    toolbar_map[i].window=window;
    toolbar_map[i].type=toolbar;
    
#if BALSA_MAJOR < 2
    gtk_toolbar_set_space_style(bar, GTK_TOOLBAR_SPACE_LINE);
#endif                          /* BALSA_MAJOR < 2 */
    
    if(create_stock_toolbar(toolbar) == -1)
	return NULL;

    index=get_toolbar_index(toolbar);
    if(index == -1)
	return NULL;

    gtk_toolbar_set_style(bar, GTK_TOOLBAR_BOTH);

    position=0;
    for(j=0; balsa_app.toolbars[index][j]; j++) {
	button=get_toolbar_button_index(balsa_app.toolbars[index][j]);
	
	if(button == -1) {
	    g_warning("button '%s' not found. ABORT!\n",
		      balsa_app.toolbars[index][j]);
	    continue;
	}

	if(!*(balsa_app.toolbars[index][j])) {
	    gtk_toolbar_append_space(bar);
	    continue;
	}
	for(i=0; i<MAXTOOLBARITEMS; i++) {
	    if(tmpdata[i].id &&
	       !strcmp(tmpdata[i].id, toolbar_buttons[button].pixmap_id))
		break;
	}

	if(i != MAXTOOLBARITEMS && tmpdata[i].widget == NULL) {
	    text=tmp=g_strdup(_(toolbar_buttons[button].button_text));
	    if(!balsa_app.toolbar_wrap_button_text)
		while(*tmp) {
		    if(*tmp == '\n')
			*tmp=' ';
		    ++tmp;
		}
	    switch(toolbar_buttons[button].type) {
	    case TOOLBAR_BUTTON_TYPE_RADIO:
		type=GTK_TOOLBAR_CHILD_RADIOBUTTON;
		break;
	    case TOOLBAR_BUTTON_TYPE_TOGGLE:
		type=GTK_TOOLBAR_CHILD_TOGGLEBUTTON;
		break;
	    case TOOLBAR_BUTTON_TYPE_BUTTON:
	    default:
		type=GTK_TOOLBAR_CHILD_BUTTON;
		break;
	    }
	    tmpdata[i].widget=
		gtk_toolbar_append_element(
		    bar, type, NULL, text, 
		    _(toolbar_buttons[button].help_text),
		    _(toolbar_buttons[button].help_text),
#if BALSA_MAJOR < 2
		    gnome_stock_pixmap_widget(
			window, toolbar_buttons[button].pixmap_id),
#else
                    gtk_image_new_from_stock(toolbar_buttons[button].pixmap_id,
                                             GTK_ICON_SIZE_BUTTON),
#endif                          /* BALSA_MAJOR < 2 */
		    GTK_SIGNAL_FUNC(tmpdata[i].callback),
		    tmpdata[i].data != NULL ? tmpdata[i].data : window);
	    g_free(text);
	    tmpdata[i].position=position++;
	    gtk_widget_set_sensitive(tmpdata[i].widget, !tmpdata[i].disabled);
	}
    }
    
    for(i=0; i<MAXTOOLBARITEMS; i++)
	toolbar_data[toolbar][i]=tmpdata[i];
    
    gtk_widget_show_all(GTK_WIDGET(bar));
    gtk_toolbar_set_style(bar, balsa_app.toolbar_style);
    return bar;
}

static int
get_toolbar_button_slot(BalsaToolbarType toolbar, const char *id)
{
    int i;
    
    for(i=0;i<MAXTOOLBARITEMS &&
	    (toolbar_data[toolbar][i].widget ||
	     toolbar_data[toolbar][i].id);i++) {
	if(toolbar_data[toolbar][i].id &&
	   !strcmp(id, toolbar_data[toolbar][i].id))
	    return i;
    }
    if(i == MAXTOOLBARITEMS)
	return -1;
	
    toolbar_data[toolbar][i].id=id;
    return i;
}

/* set_toolbar_button_callback:
   This _must_ be called for each toolbar button to set a handler
   _before_ get_toolbar is called. Failure to do so will keep the
   buttons without handlers from appearing on the toolbar

   Parameters:
   	toolbar		ID of the toolbar to set callbacks for
	id			Pixmap ID of the button to associate
	callback	The callback function
	data		User data to be passed to the callback

   Returns:
   	nothing

   Notes:
   	If data == NULL, the GtkWidget * of the toolbar's parent window will
	be passed to the callback

*/
void
set_toolbar_button_callback(BalsaToolbarType toolbar, const char *id, 
			    void (*callback)(GtkWidget *, gpointer), 
			    gpointer data)
{
    int slot;
    
    slot=get_toolbar_button_slot(toolbar, id);
    if(slot == -1)
	return;
	
    toolbar_data[toolbar][slot].callback=callback;
    toolbar_data[toolbar][slot].data=data;
}

/* set_toolbar_button_sensitive:
   Sensitize or desensitize a toolbar button

   This should be used in preference to gtk_widget_set_sensitive because it
   also sets internal flags. This way sensitivity will be preserved across
   toolbar reconfiguration.

   Parameters:
   	window		Window * of the toolbar's parent window
	toolbar		Type if the toolbar
	id			Pixmap ID of the button to set
	sensitive	1 sets sensitive, 0 desensitizes

   Notes:
   	This function may be called before the toolbar is instantiated using
	get_toolbar.
*/
void
set_toolbar_button_sensitive(GtkWidget *window, BalsaToolbarType toolbar, 
			     const char *id, int sensitive)
{
    int slot;
    GtkWidget *widget;

    slot=get_toolbar_button_slot(toolbar, id);
    if(slot == -1)
	return;
	
    toolbar_data[toolbar][slot].disabled=!sensitive;

    widget=get_tool_widget(window, toolbar, id);
    if(widget)
	gtk_widget_set_sensitive(widget, sensitive);
}

/* release_toolbars:
   Another mainstay of the toolbar system. This function will release the
   toolbar from the module internal data tables. These tables have a finite
   (100) size and may overflow if toolbars are not released. The proper way
   to do this is to call release_toolbars from a "destroy" handler for the
   window using the toolbar.

   Parameters:
   	window		The window that is being destroyed

   Returns:
   	nothing
*/
void
release_toolbars(GtkWidget *window)
{
    int i;

    for(i=0; i<toolbar_map_entries; i++) {
	if(toolbar_map[i].window == window) {
	    if(i < toolbar_map_entries-1)
		toolbar_map[i]=toolbar_map[toolbar_map_entries-1];

	    toolbar_map_entries--;
	    i--;
	}
    }
}

/* update_all_toolbars:
   Update all toolbars in all windows displaying a toolbar

   Called from toolbar-prefs.c after a change has been mada to a toolbar
   layout.
*/
void
update_all_toolbars(void)
{
    int i;

    for(i=0; i<toolbar_map_entries; i++)
	get_toolbar(toolbar_map[i].window, toolbar_map[i].type);
}

/* get_legal_toolbar_buttons:
   Returns a pointer to an array of char * listing the buttons that can
   be placed on the given toolbar. A pointer to an empty string, if present,
   _must_ be the first item and means that separators are legal to insert
   in the given toolbar. If the first item is not a separator, the behavior
   of the preferences dialog is undefined.
*/
char**
get_legal_toolbar_buttons(int toolbar)
{
    return(toolbar_legal[toolbar]);
}

