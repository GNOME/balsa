/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* vim:set ts=4 sw=4 ai et: */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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

#include "filter.h"
#include "filter-funcs.h"
#include "balsa-index.h"
#include "find-search.h"

static GtkToggleButton*
add_check_button(GtkWidget* table, const gchar* label, gint x, gint y);

static GtkToggleButton*
add_check_button(GtkWidget* table, const gchar* label, gint x, gint y)
{
    GtkWidget* res = gtk_check_button_new_with_label(label);
    gtk_table_attach(GTK_TABLE(table),
                     res,
                     x, x+1, y, y+1,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
    return GTK_TOGGLE_BUTTON(res);
}

gint
find_real(BalsaIndex * bindex,gboolean again)
{
    /* FIXME : later we could do a search based on a complete filter */
    static LibBalsaFilter * f=NULL;
    /* Condition set up for the search, it will be of type
       CONDITION_NONE if nothing has been set up */
    static LibBalsaCondition * cnd=NULL;
    static gboolean reverse=FALSE;

    GSList * conditions;

    if (!cnd) {
	cnd=libbalsa_condition_new();
        CONDITION_SETMATCH(cnd,CONDITION_MATCH_FROM);
        CONDITION_SETMATCH(cnd,CONDITION_MATCH_SUBJECT);
    }


    /* first search, so set up the match rule(s) */
    if (!again || (!f && cnd->type==CONDITION_NONE)) {
	GnomeDialog* dia=
            GNOME_DIALOG(gnome_dialog_new(_("Search a message"),
                                          GNOME_STOCK_BUTTON_OK,
                                          GNOME_STOCK_BUTTON_CANCEL,
                                          NULL));
	GtkWidget *reverse_button, *search_entry, *w, *page, *table;
	GtkToggleButton *matching_body, *matching_from;
        GtkToggleButton *matching_to, *matching_cc, *matching_subject;
	GtkRadioButton * regex_type,* simple_type;
	GSList * rb_group;
	gint res=0;
	
	gnome_dialog_close_hides(dia,TRUE);

	/* FIXME : we'll set up this callback later when selecting
	   filters has been enabled
	   gtk_signal_connect(GTK_OBJECT(dia),"clicked",
	   find_dialog_button_cb,&f);
	*/
	reverse_button = gtk_check_button_new_with_label(_("Reverse search"));

	page = gtk_table_new(2, 1, FALSE);

	w = gtk_frame_new(_("Search for:"));
	gtk_frame_set_label_align(GTK_FRAME(w), GTK_POS_LEFT, GTK_POS_TOP);
	gtk_frame_set_shadow_type(GTK_FRAME(w), GTK_SHADOW_ETCHED_IN);
	gtk_table_attach(GTK_TABLE(page),w,0, 1, 0, 1,
			 GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);

	table=gtk_table_new(3, 1, FALSE);
	gtk_container_add(GTK_CONTAINER(w), table);
	
	search_entry = gtk_entry_new_with_max_length(30);
	gtk_table_attach(GTK_TABLE(table),search_entry,0, 1, 0, 1,
			 GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
	
	simple_type = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label(NULL,_("substring search")));
	rb_group= gtk_radio_button_group(simple_type);
	regex_type  = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label(rb_group,_("Regular Exp. search")));	
	gtk_table_attach(GTK_TABLE(table),
			 GTK_WIDGET(simple_type),
			 0, 1, 1, 2,
			 GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
	gtk_table_attach(GTK_TABLE(table),
			 GTK_WIDGET(regex_type),
			 0, 1, 2, 3,
			 GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
	

	/* builds the toggle buttons to specify fields concerned by
         * the search. */
    
	w = gtk_frame_new(_("In:"));
	gtk_frame_set_label_align(GTK_FRAME(w), GTK_POS_LEFT, GTK_POS_TOP);
	gtk_frame_set_shadow_type(GTK_FRAME(w), GTK_SHADOW_ETCHED_IN);
	gtk_table_attach(GTK_TABLE(page),
			 w,
			 1, 2, 0, 1,
			 GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
    
	table = gtk_table_new(3, 2, TRUE);
	gtk_container_add(GTK_CONTAINER(w), table);
		
	matching_body    = add_check_button(table, _("Body"),    0, 0);
        matching_subject = add_check_button(table, _("Subject"), 1, 0);
	matching_from    = add_check_button(table, _("From:"),   0, 1);
	matching_to      = add_check_button(table, _("To:"),     1, 1);
	matching_cc      = add_check_button(table, _("Cc:"),     0, 2);

	gtk_box_pack_start(GTK_BOX(dia->vbox), page, FALSE, FALSE, 2);

	gtk_box_pack_start(GTK_BOX(dia->vbox), gtk_hseparator_new(), 
                           FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(dia->vbox), reverse_button,TRUE,TRUE,0);
	gtk_widget_show_all(dia->vbox);

	if ((cnd->type==CONDITION_SIMPLE) && cnd->match.string)
	    gtk_entry_set_text(GTK_ENTRY(search_entry),cnd->match.string);
	else if ((cnd->type==CONDITION_REGEX) && 
		 cnd->match.regexs && cnd->match.regexs->data &&
		 ((LibBalsaConditionRegex *)cnd->match.regexs->data)->string)
	    gtk_entry_set_text(GTK_ENTRY(search_entry),
			       ((LibBalsaConditionRegex *)cnd->match.regexs->data)->string);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(reverse_button),reverse);
	gtk_toggle_button_set_active(matching_body,
				     CONDITION_CHKMATCH(cnd,
                                                        CONDITION_MATCH_BODY));
	gtk_toggle_button_set_active(matching_to,
				     CONDITION_CHKMATCH(cnd,
                                                        CONDITION_MATCH_TO));
	gtk_toggle_button_set_active(matching_from,
				     CONDITION_CHKMATCH(cnd,CONDITION_MATCH_FROM));
	gtk_toggle_button_set_active(matching_subject,
				     CONDITION_CHKMATCH(cnd,CONDITION_MATCH_SUBJECT));
	gtk_toggle_button_set_active(matching_cc,
				     CONDITION_CHKMATCH(cnd,CONDITION_MATCH_CC));

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(regex_type),
				     cnd->type==CONDITION_REGEX);	    
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(simple_type),
				     (cnd->type==CONDITION_SIMPLE)
				     || cnd->type==CONDITION_NONE);

        gtk_widget_grab_focus(search_entry);
        gnome_dialog_editable_enters(dia, GTK_EDITABLE(search_entry));
        gnome_dialog_set_default(dia, 0);

	while (gnome_dialog_run(dia)==0) {
	    /* OK --> Process the search dialog fields */
	    cnd->match_fields=CONDITION_EMPTY;
	    if (gtk_toggle_button_get_active(matching_body))
		CONDITION_SETMATCH(cnd,CONDITION_MATCH_BODY);
	    if (gtk_toggle_button_get_active(matching_to))
		CONDITION_SETMATCH(cnd,CONDITION_MATCH_TO);
	    if (gtk_toggle_button_get_active(matching_subject))
		CONDITION_SETMATCH(cnd,CONDITION_MATCH_SUBJECT);
	    if (gtk_toggle_button_get_active(matching_from))
		CONDITION_SETMATCH(cnd,CONDITION_MATCH_FROM);
	    if (gtk_toggle_button_get_active(matching_cc))
		CONDITION_SETMATCH(cnd,CONDITION_MATCH_CC);
	    if (cnd->match_fields==CONDITION_EMPTY) {
		libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                     _("You must specify at least one field to search"));
		res=-1;
		continue;
	    }
	    else if (!gtk_entry_get_text(GTK_ENTRY(search_entry))[0]) {
		libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                     _("You must provide a non-empty string"));
		res=-1;
		continue;
	    }
	    reverse=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(reverse_button));
	    
	    /* Free all condition */
	    switch (cnd->type) {
	    case CONDITION_SIMPLE:
		g_free(cnd->match.string);
		break;
	    case CONDITION_REGEX:
		regexs_free(cnd->match.regexs);
	    default:;
	    }
	    
	    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(simple_type))) {
		cnd->match.string =
		    g_strdup(gtk_entry_get_text(GTK_ENTRY(search_entry)));
		cnd->type=CONDITION_SIMPLE;
		res=1;
		break;
	    }
	    else {
		LibBalsaConditionRegex * new_reg = libbalsa_condition_regex_new();
		
		new_reg->string = g_strdup(gtk_entry_get_text(GTK_ENTRY(search_entry)));
		cnd->match.regexs = g_slist_prepend(NULL,new_reg);
		cnd->type=CONDITION_REGEX;
		libbalsa_condition_compile_regexs(cnd);
		if (filter_errno != FILTER_NOERR) {
		    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                         _("Unable to compile search Reg. Exp."));
		    res=-1;
		    continue;
		}
		res=1;
		break;
	    }
	}
	gtk_widget_destroy(GTK_WIDGET(dia));
	if (res!=1) return res;
    }

    if (f) {
	GSList * lst=g_slist_append(NULL,f);
	if (!filters_prepare_to_run(lst)) return -1;
	g_slist_free(lst);
	conditions=f->conditions;
    }
    else conditions=g_slist_append(NULL,cnd);

    balsa_index_find(bindex,
                     f ? f->conditions_op : FILTER_OP_OR,
                     conditions, reverse);

    /* FIXME : See if this does not lead to a segfault because of
       balsa_index_scan_info */
    if (!f) g_slist_free(conditions);
    return again ? 0 : 1;
}
