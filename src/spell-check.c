/* spell.c - Spell plugin.
 *
 * Copyright (C) 1998-1999 Martin Wahlen.
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
/*
 * Ugly code: but hey, I am new at this:-)
 *
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gtk/gtk.h>
#include <gnome.h>
#include <ctype.h>
#include "sendmsg-window.h"

/* Should be broken down into more structs one for doc and one for GUI stuff*/
typedef struct _SpellWindow SpellWindow;

struct _SpellWindow {
	GtkWidget *window;
	GtkWidget *hbox;
	GtkWidget *done;
	GtkWidget *spell;
	gint context;
	gint docid;
	guint index;
	guint oldindex;
	guint offs;
	gint handling;
	char *buffer;
	char *result;

        /* drift is the difference between the position
	   of a point in the original document and the
	   position of that point in the partially spell
	   checked document -- words can change length when
	   corrected */

        int drift;
};

SpellWindow *plugin;

/*Destroy handler: free the result buffer and tell gEdit to close the pipe*/ 
void spell_destroy(GtkWidget *widget, gpointer data)
{
	g_free(plugin->result);
	client_finish(plugin->context);
	gtk_exit(0);
}

void spell_exit(GtkWidget *widget, gpointer data)
{
	/*
	gint newdoc, i;

  	if(plugin->oldindex < strlen(plugin->buffer)) {
		for(i=plugin->oldindex; i<strlen(plugin->buffer); i++)
        		plugin->result[i+(plugin->offs)]=plugin->buffer[i];
  	}
  	newdoc = client_document_new(plugin->context, "Spell Checked");
  	client_text_append(newdoc,plugin->result, strlen(plugin->result));
  	client_document_show(newdoc); 
	*/

  	g_free(plugin->result);
  	client_finish(plugin->context);
  	gtk_exit(0);
}

static gchar*
parse_text (gchar* text, guint *old_index ) {
        gchar c;
        gchar buf[1024];
        guint i=0, len;
        guint index,text_start;

        index = *old_index;
        len = strlen(text);

        /* skip non isalnum chars */
        for ( ; index < len; ++index ) {
                c = text[index];
                if ( isalnum(c) || c == '\'' ) break;
        }

        if ( index == len ) {
		spell_exit(NULL, NULL);
		exit(0);
        }

        buf[i]= c;
        text_start = index;
        ++index;
        for ( ; index < len; ++index ) {
                c = text[index];
                if ( isalnum(c) || c == '\'' ) {
                        buf[++i] = c;
                } else
                        break;
        }
        buf[i+1] = 0;

        *old_index = index;
        return g_strdup(buf);
}

/*
 *Drawing magic! 
*/
static SpellWindow *
spell_dialog_new()
{
	SpellWindow *win = (SpellWindow *) g_malloc0(sizeof(SpellWindow));

	win->spell = gnome_spell_new();
	win->hbox = gtk_hbox_new(TRUE,0);
	win->done = gnome_stock_button (GNOME_STOCK_BUTTON_CLOSE);

	win->window = gnome_dialog_new();
	gtk_window_set_title (GTK_WINDOW(win->window), _("Spell Check"));
	gtk_box_pack_start (GTK_BOX (win->hbox), win->done, FALSE,
				FALSE, 3);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (win->window)->vbox),
				win->spell, FALSE, FALSE, 3);

        gtk_box_pack_start (GTK_BOX (GTK_DIALOG (win->window)->vbox),
				win->hbox, FALSE, FALSE, 3);

	gtk_signal_connect (GTK_OBJECT(win->window), "destroy",
			 	(GtkSignalFunc) spell_destroy, NULL);	

	gtk_signal_connect (GTK_OBJECT(win->done), "clicked",
                                (GtkSignalFunc) spell_exit, NULL);

	gtk_widget_show(win->done);
	gtk_widget_show(win->hbox);
	gtk_widget_show(win->spell);
	gtk_widget_show(win->window);	

	return win; 
}

void spell_start_check()
{
        char *word;
	int result;

        if(!GNOME_IS_SPELL(plugin->spell))
                return;
	
        while(plugin->handling &&
	      ((word=parse_text(plugin->buffer, &(plugin->index))) != NULL))
        {
	  selection_range sr;
	  sr.start = plugin->index - strlen (word);
	  sr.end = plugin->index;
	  sr.start -= plugin->drift;
	  sr.end -= plugin->drift;

	  printf ("    word='%s', index=%d, drift=%d\n",
		  word, plugin->index, plugin->drift);

	  client_document_set_selection_range (plugin->context, sr);

	  result = gnome_spell_check(GNOME_SPELL(plugin->spell), (gchar *) word);
	  plugin->handling = !result;
        }
}

void handled_word_callback(GtkWidget *spell, gpointer data)
{
        guint start, len;
	GnomeSpellInfo *si = (GnomeSpellInfo *) GNOME_SPELL(spell)->spellinfo->data;

	g_return_if_fail(GNOME_IS_SPELL(spell));

	len = strlen (si->word);
        start = plugin->index - len;

	if(si->replacement)
	  {
	    gchar *r = si->replacement;
	    plugin->drift += (len - strlen (r));
	    client_text_set_selection_text (plugin->context, r, strlen (r));
	  }

	/*
	for(i=plugin->oldindex; i<start; i++)
		plugin->result[i+(plugin->offs)]=plugin->buffer[i];
	strcpy(plugin->offs + i + plugin->result,r);
	plugin->oldindex = plugin->index;
	if(si->replacement)
		plugin->offs += strlen(si->replacement) - strlen(si->word);
	g_print("%s\n",plugin->result);	
	*/

	plugin->handling = 1;
	gtk_idle_add((GtkFunction) spell_start_check, NULL);
}

void
spell_check_menu_cb (GtkWidget * widget, gpointer data)
{
  SpellWindow *win;
  BalsaSendmsg *msg;

  g_assert(widget != NULL);
  g_assert(data != NULL);

  msg = (BalsaSendmsg *) data;

  win = spell_dialog_new();
  g_assert(win != NULL);

  gtk_window_set_modal(win->window, TRUE);
}


