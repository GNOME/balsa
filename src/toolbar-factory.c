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
#include <gdk/gdkx.h>

#ifdef USE_PIXBUF
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif

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
#include "main.h"
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
    char *id;
    int disabled;
    int position;
} toolbar_data[MAXTOOLBARS][MAXTOOLBARITEMS];

/* Rather than getting into the init stuff, this makes it self_contained */
static int init=1;

static struct toolbar_bmap
{
    GtkWidget *window;
    GtkWidget *toolbar;
    int type;
} toolbar_map[100];
static int toolbar_map_entries=0;

static char *toolbar0_legal[]={
    "",
    GNOME_STOCK_PIXMAP_MAIL_RCV,
    GNOME_STOCK_PIXMAP_TRASH,
    GNOME_STOCK_PIXMAP_MAIL_NEW,
    GNOME_STOCK_PIXMAP_MAIL,
    GNOME_STOCK_PIXMAP_MAIL_RPL,
    BALSA_PIXMAP_MAIL_RPL_ALL,
    GNOME_STOCK_PIXMAP_MAIL_FWD,
    GNOME_STOCK_PIXMAP_BACK,
    GNOME_STOCK_PIXMAP_FORWARD,
    BALSA_PIXMAP_NEXT_UNREAD,
    GNOME_STOCK_PIXMAP_PRINT,
    BALSA_PIXMAP_FLAG_UNREAD,
    BALSA_PIXMAP_MARK_ALL_MSGS,
    BALSA_PIXMAP_SHOW_ALL_HEADERS,
    NULL
};

static char *toolbar1_legal[]={
    "",
    GNOME_STOCK_PIXMAP_MAIL_SND,
    GNOME_STOCK_PIXMAP_ATTACH,
    GNOME_STOCK_PIXMAP_SAVE,
    BALSA_PIXMAP_IDENTITY,
    GNOME_STOCK_PIXMAP_SPELLCHECK,
    GNOME_STOCK_PIXMAP_PRINT,
    GNOME_STOCK_PIXMAP_CLOSE,
    NULL
};

static char *toolbar2_legal[]={
    "",
    GNOME_STOCK_PIXMAP_MAIL_RPL,
    BALSA_PIXMAP_MAIL_RPL_ALL,
    GNOME_STOCK_PIXMAP_MAIL_FWD,
    GNOME_STOCK_PIXMAP_BACK,
    GNOME_STOCK_PIXMAP_FORWARD,
    BALSA_PIXMAP_NEXT_UNREAD,
    GNOME_STOCK_PIXMAP_TRASH,
    GNOME_STOCK_PIXMAP_PRINT,
    GNOME_STOCK_PIXMAP_SAVE,
    GNOME_STOCK_PIXMAP_CLOSE,
    BALSA_PIXMAP_SHOW_ALL_HEADERS,
    NULL
};

static char **toolbar_legal[]={toolbar0_legal, toolbar1_legal, toolbar2_legal};

static void populate_stock_toolbar(int bar, int id);
static int get_toolbar_button_slot(int toolbar, char *id);
static GtkToolbar *get_bar_instance(GtkWidget *window, int toolbar);
static int get_position_value(int toolbar, char *id);

static int
get_position_value(int toolbar, char *id)
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
   FIXME: comment needed.
*/
GtkWidget *
get_tool_widget(GtkWidget *window, int toolbar, char *id)
{
    GtkToolbar *bar;
    GList *lp;
    int position;
    GtkToolbarChild *child;

    bar=get_bar_instance(window, toolbar);
    if(!bar)
	return NULL;
    
    position=get_position_value(toolbar, id);
    if(position == -1)
	return NULL;
    
    lp=g_list_first(bar->children);
    while(position--)
	lp=g_list_next(lp);

    if(!lp)
	return NULL;
	
    child=(GtkToolbarChild *)(lp->data);
    if(!child)
	return NULL;
	
    return child->widget;
}

/* get_bar_instance:
   FIXME: comment needed.
*/
static GtkToolbar*
get_bar_instance(GtkWidget *window, int toolbar)
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
    GNOME_STOCK_PIXMAP_MAIL_RCV, "", 
    GNOME_STOCK_PIXMAP_TRASH   , "",
    GNOME_STOCK_PIXMAP_MAIL_NEW, GNOME_STOCK_PIXMAP_MAIL,
    GNOME_STOCK_PIXMAP_MAIL_RPL, BALSA_PIXMAP_MAIL_RPL_ALL,
    GNOME_STOCK_PIXMAP_MAIL_FWD, "", GNOME_STOCK_PIXMAP_BACK,
    GNOME_STOCK_PIXMAP_FORWARD,  BALSA_PIXMAP_NEXT_UNREAD,    
    "",                          GNOME_STOCK_PIXMAP_PRINT,
    NULL
};

static const gchar* compose_toolbar[] = {
    GNOME_STOCK_PIXMAP_MAIL_SND,   "", 
    GNOME_STOCK_PIXMAP_ATTACH,     "",
    GNOME_STOCK_PIXMAP_SAVE,       "",
    BALSA_PIXMAP_IDENTITY,         "",
    GNOME_STOCK_PIXMAP_SPELLCHECK, "",
    GNOME_STOCK_PIXMAP_PRINT,      "",
    GNOME_STOCK_PIXMAP_CLOSE, NULL
};

static const gchar* message_toolbar[] = {
    BALSA_PIXMAP_NEXT_UNREAD,    "",
    GNOME_STOCK_PIXMAP_MAIL_RPL, BALSA_PIXMAP_MAIL_RPL_ALL,
    GNOME_STOCK_PIXMAP_MAIL_FWD, "",
    GNOME_STOCK_PIXMAP_BACK,     GNOME_STOCK_PIXMAP_FORWARD,
    GNOME_STOCK_PIXMAP_SAVE,     "",
    GNOME_STOCK_PIXMAP_PRINT,    "",
    GNOME_STOCK_PIXMAP_TRASH,    NULL
};

static const gchar* null_toolbar[] = { NULL };

static void
populate_stock_toolbar(int bar, int id)
{
    const gchar** toolbar;
    int i;

    switch(bar) {
    case 0:  toolbar = main_toolbar;    break;
    case 1:  toolbar = compose_toolbar; break; 
    case 2:  toolbar = message_toolbar; break;
    default: toolbar = null_toolbar;	break;
    }
    for(i=0; toolbar[i]; i++)
	balsa_app.toolbars[bar][i] = g_strdup(toolbar[i]);
    balsa_app.toolbars[bar][i]= NULL;
}

/* get_toolbar_index:
   FIXME: comment needed.
*/
int
get_toolbar_index(int id)
{
    int i;
    
    for(i=0; i<balsa_app.toolbar_count; i++)
	if(balsa_app.toolbar_ids[i] == id)
	    return i;
    
    return -1;
}

/* create_stock_toolbar:
   FIXME: comment needed.
*/
int
create_stock_toolbar(int id)
{
    int newbar;
    
    if(get_toolbar_index(id) != -1)
	return 0;
    
    /* Create new toolbar */
    if(balsa_app.toolbar_count >= MAXTOOLBARS)
	return -1;
    
    newbar=balsa_app.toolbar_count;
    ++balsa_app.toolbar_count;
    
    balsa_app.toolbars[newbar]=
	(char **)g_malloc(sizeof(char *)*MAXTOOLBARITEMS);
    
    balsa_app.toolbars[newbar][0]=NULL;
    populate_stock_toolbar(newbar, id);
    balsa_app.toolbar_ids[newbar]=id;
    
    return 0;
}

/* get_toolbar:
   FIXME: comment needed.
*/
GtkToolbar *
get_toolbar(GtkWidget *window, int toolbar)
{
    GtkToolbar *bar;
    GtkToolbarChild *child;
    GList *lp;
    int index;
    int i, j, button;
    int position;
    int type;
    char *tmp, *text;
    struct toolbar_bdata tmpdata[MAXTOOLBARITEMS];

    if(init) {
	memset((char *)&toolbar_data, 0, sizeof(toolbar_data));
	init=0;
    }

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
	if(i >= 100)
	    return NULL;
	++toolbar_map_entries;
	bar=GTK_TOOLBAR(gtk_toolbar_new(GTK_ORIENTATION_HORIZONTAL,
					GTK_TOOLBAR_BOTH));
    } else  {
	bar=GTK_TOOLBAR(toolbar_map[i].toolbar);
	
	lp=bar->children;
	if(lp) {
	    do {
		--bar->num_children;
		bar->children=g_list_remove_link(bar->children, lp);

		child=(GtkToolbarChild *)lp->data;
		if(child->label)
		    gtk_widget_destroy(GTK_WIDGET(child->label));
		if(child->icon)
		    gtk_widget_destroy(GTK_WIDGET(child->icon));
		if(child->widget)
		    gtk_widget_destroy(GTK_WIDGET(child->widget));
		g_free(child); 
		g_list_free(lp);
		if(!bar->children)
		    break;
		lp=g_list_first(bar->children);
	    } while(lp);
	} else /* !lp */ {
	    for(i=0; i<MAXTOOLBARITEMS; i++)
		tmpdata[i].disabled=0;
	}
				
	if(bar->children)
	    g_list_free(bar->children);
	bar->children=NULL;
    }
    toolbar_map[i].toolbar=GTK_WIDGET(bar);
    toolbar_map[i].window=window;
    toolbar_map[i].type=toolbar;
    
    gtk_toolbar_set_space_style(bar, GTK_TOOLBAR_SPACE_LINE);
    
    if(create_stock_toolbar(toolbar) == -1)
	return NULL;

    index=get_toolbar_index(toolbar);
    if(index == -1)
	return NULL;

    gtk_toolbar_set_style(bar, GTK_TOOLBAR_BOTH);

    position=0;
    for(j=0; balsa_app.toolbars[index][j]; j++) {
	button=get_toolbar_button_index(balsa_app.toolbars[index][j]);
	
	if(button == -1)
	    continue;

	if(!*(balsa_app.toolbars[index][j])) {
	    gtk_toolbar_append_space(bar);
	    ++position;
	    continue;
	}
	for(i=0; i<MAXTOOLBARITEMS; i++) {
	    if(tmpdata[i].id &&
	       !strcmp(tmpdata[i].id, toolbar_buttons[button].pixmap_id))
		break;
	}

	if(i != MAXTOOLBARITEMS && tmpdata[i].widget == NULL) {
	    text=tmp=g_strdup(toolbar_buttons[button].button_text);
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
		    toolbar_buttons[button].help_text,
		    toolbar_buttons[button].help_text,
		    gnome_stock_pixmap_widget(
			window, toolbar_buttons[button].pixmap_id),
		    tmpdata[i].callback,
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
get_toolbar_button_slot(int toolbar, char *id)
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
   FIXME: comment needed.
*/
void
set_toolbar_button_callback(int toolbar, char *id, 
			    void (*callback)(GtkWidget *, gpointer), 
			    gpointer data)
{
    int slot;
    
    if(init) {
	memset((char *)&toolbar_data, 0, sizeof(toolbar_data));
	init=0;
    }

    slot=get_toolbar_button_slot(toolbar, id);
    if(slot == -1)
	return;
	
    toolbar_data[toolbar][slot].callback=callback;
    toolbar_data[toolbar][slot].data=data;
}

/* set_toolbar_button_sensitive:
   FIXME: comment needed.
*/
void
set_toolbar_button_sensitive(GtkWidget *window, int toolbar, char *id, 
			     int sensitive)
{
    int slot;
    GtkWidget *widget;

    if(init) {
	memset((char *)&toolbar_data, 0, sizeof(toolbar_data));
	init=0;
    }

    slot=get_toolbar_button_slot(toolbar, id);
    if(slot == -1)
	return;
	
    toolbar_data[toolbar][slot].disabled=!sensitive;

    widget=get_tool_widget(window, toolbar, id);
    if(widget)
	gtk_widget_set_sensitive(widget, sensitive);
}

/* release_toolbars:
   FIXME: comment needed.
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
   FIXME: comment needed.
*/
void
update_all_toolbars(void)
{
    int i;

    for(i=0; i<toolbar_map_entries; i++)
	get_toolbar(toolbar_map[i].window, toolbar_map[i].type);
}

/* get_legal_toolbar_buttons:
   FIXME: comment needed.
*/
char**
get_legal_toolbar_buttons(int toolbar)
{
    return(toolbar_legal[toolbar]);
}

