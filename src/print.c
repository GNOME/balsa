/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Jay Painter and Stuart Parmenter
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <config.h>
#include <gnome.h>
#include <gtk/gtk.h>
#include <time.h>

#include "libbalsa.h"
#include "mime.h"

#include "balsa-app.h"
#include "balsa-icons.h"
#include "balsa-index.h"
#include "balsa-mblist.h"
#include "balsa-message.h"
#include "filter.h"
#include "balsa-index-page.h"
#include "misc.h"
#include "main.h"
#include "main-window.h"
#include "message-window.h"
#include "pref-manager.h"
#include "sendmsg-window.h"
#include "mailbox-conf.h"
#include "mblist-window.h"

void file_print_cb(GtkWidget *widget, gpointer cbdata);

static void print_destroy(GtkWidget *widget, gpointer data);
static void file_print_execute(); //GtkWidget *w, gpointer cbdata);
static void print_message(GtkWidget *widget, gpointer data);

static GtkWidget *print_dialog = NULL;
static GtkWidget *print_cmd_entry = NULL;


/*
 * PUBLIC: file_print_cb
 *
 * creates print dialog box.  this should be the only routine global to
 * the world.
 */
void
file_print_cb(GtkWidget *widget, gpointer cbdata)
{

	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *tmp;
	GtkWidget *data = (GtkWidget *)cbdata;

	g_assert(data != NULL);

	if (print_dialog)
	  return;

	print_dialog = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	gtk_window_set_title(GTK_WINDOW(print_dialog), _("Print"));
	gtk_signal_connect(GTK_OBJECT(print_dialog), "destroy",
		GTK_SIGNAL_FUNC(print_destroy), NULL);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(print_dialog), vbox);
	gtk_container_border_width(GTK_CONTAINER(print_dialog), 6);
	gtk_widget_show(vbox);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 10);
	gtk_widget_show(hbox);

	tmp = gtk_label_new(_("Enter print command below\nRemember to include '%s'"));
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, TRUE, 5);
	gtk_widget_show(tmp);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 5);
	gtk_widget_show(hbox);

	tmp = gtk_label_new(_("Print Command:"));
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, TRUE, 5);
	gtk_widget_show(tmp);

	print_cmd_entry = gtk_entry_new_with_max_length(255);
	
	if (balsa_app.PrintCommand.PrintCommand)
	  gtk_entry_set_text(GTK_ENTRY(print_cmd_entry), balsa_app.PrintCommand.PrintCommand);
	  
	gtk_box_pack_start(GTK_BOX(hbox), print_cmd_entry, TRUE, TRUE, 10);
	gtk_widget_show(print_cmd_entry);

	tmp = gtk_hseparator_new();
	gtk_box_pack_start(GTK_BOX(vbox), tmp, FALSE, TRUE, 10);
	gtk_widget_show(tmp);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 5);
	gtk_widget_show(hbox);

	tmp = gnome_stock_button(GNOME_STOCK_BUTTON_OK);
	gtk_box_pack_start(GTK_BOX(hbox), tmp, TRUE, TRUE, 15);
	gtk_signal_connect(GTK_OBJECT(tmp), "clicked",
		GTK_SIGNAL_FUNC(print_message), data);
	gtk_widget_show(tmp);

	tmp = gnome_stock_button(GNOME_STOCK_BUTTON_CANCEL);
	gtk_box_pack_start(GTK_BOX(hbox), tmp, TRUE, TRUE, 15);
	gtk_signal_connect(GTK_OBJECT(tmp), "clicked",
		GTK_SIGNAL_FUNC(print_destroy), NULL);
	gtk_widget_show(tmp);

	gtk_widget_show(print_dialog);
	
} /* file_print_cb */


/*
 * PRIVATE: print_destroy
 *
 * destroy the print dialog box
 */
static void
print_destroy(GtkWidget *widget, gpointer data)
{

	if (print_dialog) {
	
	  gtk_widget_destroy(print_dialog);
	  print_dialog = NULL;
	
	}

} /* print_destroy */


/*
 * PRIVATE: file_print_execute
 *
 * actually execute the print command
 */
static void
file_print_execute()
{

	gchar *scmd, *pcmd, *tmp, *fname;

	if (!balsa_app.PrintCommand.PrintCommand)
	   balsa_app.PrintCommand.PrintCommand  = g_strdup( gtk_entry_get_text(GTK_ENTRY(print_cmd_entry)));

	/* print using specified command */
	if ((pcmd = gtk_entry_get_text(GTK_ENTRY(print_cmd_entry))) == NULL)
	  return;

	/* look for "file variable" and place marker */
	if ((tmp = strstr (pcmd, "%s")) == NULL)
	  return;

	*tmp = '\0';
	tmp += 2;

	fname = g_strdup_printf("%s/.balsa-print", g_get_home_dir());

	/* build command and execute; g_malloc handles memory alloc errors */
	scmd = g_malloc (strlen(pcmd) + strlen(fname) + 1);
	sprintf (scmd, "%s%s%s", pcmd, fname, tmp);

	g_print("%s\n", scmd);

	if (system (scmd) == -1)
	  perror ("file_print_execute: system() error");

	g_free (scmd);

	if (unlink (fname))
	    perror ("file_print_execute: unlink() error");

	g_free (fname);
	
} /* file_print_execute */


void print_message(GtkWidget *widget, gpointer data)
{
    GtkWidget *index;
    GtkCList *clist;
    GList *list;
    Message *message;
    Address *addr = NULL;
    gchar *tmp;
    GString *printtext = g_string_new ("\n\n");
    FILE * fp;
    gchar tmp_file_name[PATH_MAX + 1];

    g_return_if_fail (widget != NULL);

    index = balsa_window_find_current_index(BALSA_WINDOW(data));

    if (!index)
	return;

    clist = GTK_CLIST (index);
    list = clist->selection;

    while (list) {
	sprintf(tmp_file_name,"%s/.balsa-print", g_get_home_dir());
	fp = fopen(tmp_file_name,"wra+");
	message = gtk_clist_get_row_data (clist, GPOINTER_TO_INT (list->data));
	addr = message->from    ;
	tmp = address_to_gchar (addr);
	fprintf(fp, "\n\n");
	fprintf(fp, "From:    \t%s\n", tmp);
	fprintf(fp, "Sent:    \t%s\n", message->date);
	tmp = make_string_from_list (message->to_list);
	fprintf(fp, "To:      \t%s\n", tmp);
	tmp = make_string_from_list (message->cc_list);
	fprintf(fp, "Cc:      \t%s\n", tmp);
	fprintf(fp, "Subject: \t%s\n", message->subject);
	fprintf(fp, "\n");
	message_body_ref (message);
	printtext = content2reply (message, NULL);

	if (balsa_app.PrintCommand.breakline == TRUE) {
	    int i = 0 , j = 0;
	    while (i <= balsa_app.PrintCommand.linesize && printtext->str[j++] != '\0') {
		if ( printtext->str[j] == '\n') {
		    printf("\n");
		    fprintf(fp, "\n");
		    i = 0;
		    continue;
		}
		if ( i ==  balsa_app.PrintCommand.linesize && printtext->str[j] != '\0') {
		    i = 0;
		    fprintf(fp, "\n");
		}
		fprintf(fp, "%c", printtext->str[j]);
		i++;
	    }
	}
	else
	    fprintf(fp, "%s\n",printtext->str);

	message_body_unref (message);
	fflush(fp);
	fclose (fp);

	file_print_execute();

	print_destroy (NULL, NULL);
	list = list->next;
    }
}
