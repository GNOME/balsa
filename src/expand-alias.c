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

#include "libbalsa.h"

#include <stdio.h>
#include <string.h>
#include <gnome.h>
#include <ctype.h>

#include <sys/stat.h>		/* for check_if_regular_file() */
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>

#include "balsa-app.h"
#include "balsa-message.h"
#include "balsa-index.h"
#include "misc.h"
#include "mime.h"
#include "sendmsg-window.h"
#include "address-book.h"
#include "address-entry.h"
#include "main.h"
#include "expand-alias.h"

#define CASE_INSENSITIVE_NAME
#define PRESERVE_CASE TRUE
#define OVERWRITE_CASE FALSE


/*
 * Function prototypes or something...
 */
/*
static void clip_expand_cursor(inputData * input);
static void expand_input_wrapper(inputData * input, gboolean preserve);
static gchar *expand_input(gchar ** input, gint * tabs);
static inputData *create_input(void);
static inputData *fill_input(inputData * input, GtkWidget * widget);
*/
static inputData *new_inputData(void);
static inputData *fill_input(GtkWidget *);

static void free_inputData(inputData * data);

static void keystroke_backspace(inputData **, GtkWidget *);
static void keystroke_delete(inputData **, GtkWidget *);
static void keystroke_enter(inputData *);
static void keystroke_down(inputData *);
static void keystroke_left(inputData *);
static void keystroke_right(inputData *);
static void keystroke_home(inputData *);
static void keystroke_comma(inputData *);
static void keystroke_add_key(inputData **, gchar *, GtkWidget *);
static void process_keystroke(inputData **, GtkWidget *, GdkEventKey *);


/*
 * new_emailData()
 */
static emailData *
new_emailData(void)
{
    emailData *tmp;

    tmp = g_malloc(sizeof(emailData));
    tmp->user = NULL;
    tmp->match = NULL;
    tmp->cursor = -1;
    tmp->tabs = 0;
    return tmp;
}


/*
 * FIXME: Add a function definition to the comments.
 */
static void
free_emailData(emailData *addy)
{
    g_free(addy->user);
    addy->user = NULL;
    g_free(addy->match);
    addy->match = NULL;
    g_free(addy);
}


/*
 * new_inputData()
 *
 * Creates and returns an inputData structure which should be
 * initialized correctly.
 */
static inputData *
new_inputData(void)
{
    inputData *tmp;

    tmp = g_malloc(sizeof(inputData));
    tmp->list = NULL;
    tmp->active = NULL;
    return tmp;
}


/*
 * free_inputData()
 *
 * Input: *inputData to free.
 * Output: None.
 *
 * Frees an inputData structure.
 */
static void
free_inputData(inputData * data)
{
    g_list_foreach(data->list, (GFunc) free_emailData, NULL);
}



/*
 * next_entrybox()
 *
 * Transfers focus to the next visible entrybox in line.
 */
void
next_entrybox(GtkWidget * widget, GtkWidget * entry)
{
    GtkWidget *next;

    next = entry;
    while ((next = gtk_object_get_data(GTK_OBJECT(next), "next_in_line"))
	   != NULL) {
	if (GTK_WIDGET_DRAWABLE(GTK_WIDGET(next))) {
	    gtk_widget_grab_focus(GTK_WIDGET(next));
	    break;
	}
    }
}



/*
 * force_no_match()
 * 
 * Input: emailData *addy
 * Modifies: addy
 *
 * Sets 'addy' so that it looks like no match was found.
 */
static void
force_no_match(emailData *addy) {
    g_free(addy->match);
    addy->match = NULL;
    addy->tabs = 0;
}


/*
 * expand_alias_clear_to_send()
 *
 * Clears the current input buffer of incomplete alias matches, so that
 * its clear to send this message.
 */
void
expand_alias_clear_to_send(GtkWidget *widget) {
    inputData *input;
    emailData *addy;

    /*
     * Grab the input.
     */
    input = libbalsa_address_entry_get_input(LIBBALSA_ADDRESS_ENTRY(widget));
    if (!input) return;
    if ((input->active) == NULL) return;

    /*
     * Set the active data to no match.
     */
    addy = input->active->data;
    force_no_match(addy);

    /*
     * Show the input, so that we fill the GtkEntry box.
     */
    libbalsa_address_entry_show(LIBBALSA_ADDRESS_ENTRY(widget));
}
    

/*
 * is_an_email()
 *
 * Tests if a given string is a complete e-mail address.  It does
 * this by examining the string for special characters used in
 * e-mail To: headers.
 *
 * This is used to check if the email should be expanded, and
 * is not a complete RFC822 validator.
 */
static gboolean
is_an_email(gchar *str) {
    gboolean found;
    gint i;
    
    found = FALSE;
    for (i = 0; i < strlen(str); i++)
	if ((str[i] == (gchar) '@') ||
	    (str[i] == (gchar) '%') ||
	    (str[i] == (gchar) '!')) found = TRUE;
    return found;
}


/*
 * FIXME: This function should wrap around address-list providers
 *        from the addressbooks.  GCompletion of LDAP is evil.
 *
 * Ian started this.
 * 
 * FIXME: This function is too long and evil.
 */
static void
expand_alias_find_match(emailData *addy)
{
    gchar *prefix = NULL;	/* the longest common string. */
    GList *match = NULL;	/* A list of matches.         */
    GList *search = NULL;	/* Used to search the list.   */
    gchar *output = NULL;	/* We return this.            */
    LibBalsaAddress *addr = NULL;	/* Process the list data.     */
    gint i;			/* A counter for the tabs.    */
    GList *ab_list;             /* To iterate address books   */
    GList *partial_res = NULL;  /* The result froma single address book */
    gchar *partial_prefix;
    gchar *str;
    gchar *input;
    gint tab;

    input = addy->user;
    tab = addy->tabs;
    g_free(addy->match);
    addy->match = NULL;

    if (strlen(input) > 0) {

	str = g_strdup(input);
#ifdef CASE_INSENSITIVE_NAME
	g_strup(str);
#endif

	/*
	 * Look at all addressbooks for a match.
	 */
	ab_list = balsa_app.address_book_list;
	while(ab_list) {
	    if ( !LIBBALSA_ADDRESS_BOOK(ab_list->data)->expand_aliases ) {
		ab_list = g_list_next(ab_list);
		continue;
	    }

	    partial_res = libbalsa_address_book_alias_complete
	        (LIBBALSA_ADDRESS_BOOK(ab_list->data), str, &partial_prefix);
	    
	    if ( partial_res != NULL ) {
		if ( match != NULL )
		    match = g_list_concat(match, partial_res);
		else 
		    match = partial_res;
		
		if ( prefix == NULL ) {
		    prefix = partial_prefix;
		} else {
		    gchar *new_pfix;
		    gint len = 0;

		    /* 
		     * We have to find the longest common prefix of all options
		     * Tedious.
		     */
		    if ( strlen(partial_prefix) < strlen(prefix) )
			new_pfix = g_strdup(prefix);
		    else
			new_pfix = g_strdup(partial_prefix);

		    while( TRUE ) {
			if (*(prefix+len) == 0 || *(partial_prefix+len) == 0) {
			    *(new_pfix+len) = '\0';
			    break;
			} else if ( *(prefix+len) != *(partial_prefix+len) ) {
			    *(new_pfix+len) = '\0';
			    break;
			} else {
			    *(new_pfix+len) = *(prefix+len);
			    len++;
			}
		    }
		    g_free(prefix); g_free(partial_prefix);
		    prefix = new_pfix;
		}
	    }
	    
	    ab_list = g_list_next(ab_list);
	}
	g_free(str);

	/*
	 * Now look through all the matches the above code generated, and
	 * find the one we want.
	 */
	if (match) {
	    i = tab;
	    if ((i == 0) && (strlen(prefix) > strlen(input))) {
		addr = LIBBALSA_ADDRESS(match->data);

		output = g_strdup_printf("%s <%s>", addr->full_name,
					 (gchar *) addr->address_list->data);
	    } else {
		for (search = match; i > 0; i--) {
		    search = g_list_next(search);
		    if (!search) {
			addy->tabs = i = 0;
			search = match;
		    }
		}
		addr = LIBBALSA_ADDRESS(search->data);
		output = g_strdup_printf("%s <%s>", addr->full_name,
					 (gchar *) addr->address_list->data);
	    }
	    g_list_foreach(match, (GFunc)gtk_object_unref, NULL);

	/*
	 * And now we handle the case of "No matches found."
	 */
	} else {
	    output = NULL;
	}

	if (prefix) g_free(prefix);
	prefix = NULL;
    } else {
	output = g_strdup("");
    }
    addy->match = output;
}


/*
 * fill_input()
 *
 * Set all the data in the inputData structure to values reflecting
 * the current user input.
 */
static inputData *
fill_input(GtkWidget * widget)
{
    gint cursor = 0, size = 0, prev = 0;
    gchar *str = NULL;
    gchar *typed = NULL;
    gchar **str_array = NULL, **el = NULL;
    GList *list = NULL;
    emailData *addy;
    inputData *input;

    g_return_val_if_fail(widget != NULL, NULL);

    /*
     * Grab data from the widget.
     */
    input = new_inputData();
    cursor = (gint) gtk_editable_get_position(GTK_EDITABLE(widget));
    typed = gtk_editable_get_chars(GTK_EDITABLE(widget), 0, -1);
    if (typed == NULL) {
	str = g_strdup("");
    } else {
        str = g_strdup(typed);
    }
    g_free(typed);
    typed = NULL;

    /*
     * Split the input string by comma, and store the result in
     * input->list.
     * str contains a list of e-mail addresses seperated by ','.
     */
    el = str_array = g_strsplit(str, ",", 2048);
    g_assert(el != NULL);
    g_free(str);
    str = NULL;

    /*
     * Store it all in a glist.
     */
    if (*el != NULL) {
        for (str = *el;
             str != NULL;
             str = *(++el)) {
           addy = new_emailData();
           addy->user = g_strdup(str);
           input->list = g_list_append(input->list, addy);
        }
        g_strfreev(str_array);
    } else {
       addy = new_emailData();
       addy->user = g_strdup("");
       input->list = g_list_append(input->list, addy);
    }

    /*
     * Search for the currently active list member.
     * We have to match the cursor in GtkEntry to the list.
     */
    g_assert(input->list != NULL);
    size = prev = 0;
    for (list = g_list_first(input->list);
         list != NULL;
	 list = g_list_next(list)) {
	if (cursor >= size) {
	    prev = size;
	    input->active = list;
	}
        addy = (emailData *)list->data;
        size = size + strlen(addy->user) + 1;
    }

    addy = (emailData *)input->active->data;
    addy->cursor = cursor - prev;
    
    return input;
}


/*
 * keystroke_backspace()
 *
 * If the last key hit was TAB, cancel the last alias expansion that TAB
 * did.  Otherwise delete one character.  If its at the beginning of the
 * input, move one e-mail address left.
 */
static void
keystroke_backspace(inputData ** in, GtkWidget *widget)
{
    emailData *addy, *extra;
    gchar *left, *right, *str;
    GList *list;
    gint i;
    GtkEditable *editable;
    inputData *input;

    /*
     * FIXME: I don't know how to check if any data is/was selected.
     *        The gtk-reference manual describes how to set it, not get it.
     *        Is it even possible to select any data in the widget?
     *        After all, we are in control...
     *
     *        The following hack works, but it /is/ a hack.  No way to
     *        make it prettier without sub-classing GtkEntry.
     */
    input = *in;
    addy = input->active->data;
    editable = GTK_EDITABLE(widget);
    if (editable->selection_start_pos != editable->selection_end_pos) {
	g_message("keystroke_backspace(): we have a selection.");
	gtk_editable_cut_clipboard(editable);
	force_no_match(addy);
	free_inputData(input);
        input = fill_input(widget);
	*in = input;
	return;
    }

    /*
     * Check if the user wanted to delete a match.
     * This is only valid if the user has hit tab.
     */
    if ((addy->cursor >= strlen(addy->user)) &&
       (addy->match != NULL) && (addy->tabs > 0)) {
       force_no_match(addy);
       return;
    }
    /*
     * Lets see if the user is at the beginning of an e-mail entry.
     */
    if (addy->cursor == 0) {
       list = g_list_previous(input->active);
       if (list != NULL) {
           /*
            * Concatenate the two e-mails.
            */
           extra = list->data;
           str = g_strconcat(extra->user, addy->user, NULL);
           g_free(extra->user);
           extra->user = str;
           extra->cursor = strlen(extra->user);
           
           /*
            * Free a whole bunch of RAM.
            */
           input->list = g_list_remove_link(input->list, input->active);
           free_emailData(addy);
           input->active->data = NULL;
           g_list_free(input->active);
           input->active = list;
       }

    /*
     * Normal character needs deleting.
     */
    } else {
       left = g_strndup(addy->user, (addy->cursor - 1));
       right = addy->user;
       for (i = 0; i < (addy->cursor); i++) right++;
       str = g_strconcat(left, right, NULL);
       g_free(addy->user);
       g_free(left);
       addy->user = str;
       addy->cursor--;
       expand_alias_find_match(addy);
    }
}


/*
 * FIXME: Add a function definition to the comments.
 *
 * If there is text selected (by mouse, or whatever) delete that.
 * If there is no text selected:
 *   If there is a "temporary match" just to the right of the cursor:
 *     Assume the user wishes to delete the "temporary match".
 *     This is 'silly'.  The user didn't type it.  Do it nonetheless.
 *   Otherwise:
 *     Delete the character to the right of the cursor.
 */
static void
keystroke_delete(inputData ** in, GtkWidget *widget)
{
    emailData *addy, *extra;
    gchar *left, *right, *str;
    GList *list;
    gint i;
    GtkEditable *editable;
    inputData *input;
    
    /*
     * FIXME: I don't know how to check if any data is/was selected.
     *        The gtk-reference manual describes how to set it, not get it.
     *        Is it even possible to select any data in the widget?
     *        After all, we are in control...
     *
     *        The following hack works, but it /is/ a hack.  No way to
     *        make it prettier without sub-classing GtkEntry.
     */
    input = *in;
    addy = input->active->data;
    editable = GTK_EDITABLE(widget);
    if (editable->selection_start_pos != editable->selection_end_pos) {
	g_message("keystroke_backspace(): we have a selection.");
	gtk_editable_cut_clipboard(editable);
	force_no_match(addy);
	free_inputData(input);
        input = fill_input(widget);
	*in = input;
	return;
    }

    /*
     * Check if the user wanted to delete a match.
     */
    if ((addy->cursor >= strlen(addy->user)) &&
       (addy->match != NULL)) {
       force_no_match(addy);
       return;
    }

    /*
     * Lets see if the user is at the end of an e-mail entry.
     */
    if (addy->cursor >= strlen(addy->user)) {
        list = g_list_next(input->active);
        if (list != NULL) {
            /*
             * Concatenate the two e-mails.
             */
            extra = list->data;
            str = g_strconcat(addy->user, extra->user, NULL);
            g_free(addy->user);
            addy->user = str;
           
            /*
             * Free a whole bunch of RAM.
             */
            input->list = g_list_remove_link(input->list, list);
	    free_emailData(extra);
            list->data = NULL;
	    g_list_free(list);
	}
    /*
     * Normal character needs deleting.
     */
    } else {
        left = g_strndup(addy->user, addy->cursor);
        right = addy->user;
        for (i = 0; i <= addy->cursor; i++) right++;
        str = g_strconcat(left, right, NULL);
        g_free(addy->user);
        g_free(left);
        addy->user = str;
    }
}


/*
 * FIXME: Add a function definition to the comments.
 */
static void
expand_alias_accept_match(emailData *addy) {
    g_assert(addy->match != NULL);
    g_free(addy->user);
    addy->user = addy->match;
    addy->match = NULL;
}


/*
 * FIXME: Add a function definition to the comments.
 */
static void 
keystroke_enter(inputData * input)
{
    emailData *addy;
    gchar *str;

    /*
     * User pressed ENTER.  If there was a match, accept the match.
     */
    addy = input->active->data;
    if (addy->match != NULL) {
        expand_alias_accept_match(addy);
        return;
    }
    /*
     * Else no match was found.  Check if there was a default
     * domain to add to e-mail addresses.
     */
    if (balsa_app.domain == NULL) return;
    if (strlen(balsa_app.domain) == 0) return;

    /*
     * There is a default domain to add.  Do we need to add it?
     */
    if (is_an_email(addy->user)) return;
    if (strlen(addy->user) == 0) return;

    /*
     * Okay, add it.
     */
    str = g_strconcat(addy->user, "@", balsa_app.domain, NULL);
    g_free(addy->user);
    addy->user = str;
    return;
}


/*
 * FIXME: Add a function definition to the comments.
 */
static void
keystroke_down(inputData * input)
{
    emailData *addy;

    /*
     * Default policy => do not expand.  User pressed DOWN means
     * go to the next box, without acting on anything.
     */
    addy = input->active->data;
    force_no_match(addy);
}


/*
 * FIXME: Add a function definition to the comments.
 */
static void
keystroke_home(inputData * input)
{
    emailData *addy;
    
    addy = input->active->data;
    if (addy->match != NULL)
        expand_alias_accept_match(addy);
    input->active = g_list_first(input->list);
    addy = input->active->data;
    addy->cursor = 0;
}


/*
 * FIXME: Add a function definition to the comments.
 */
static void
keystroke_left(inputData * input)
{
    emailData *addy;

    addy = input->active->data;
    if (addy->cursor > 0) {
        addy->cursor--;
        force_no_match(addy);
    } else if (g_list_previous(input->active)) {
        input->active = g_list_previous(input->active);
        addy = input->active->data;
        addy->cursor = strlen(addy->user);
    }
}


static void
keystroke_right(inputData * input)
{
    emailData *addy;

    addy = input->active->data;
    if (addy->cursor < strlen(addy->user)) {
        addy->cursor++;
        force_no_match(addy);
    } else if (g_list_next(input->active)) {
        input->active = g_list_next(input->active);
        addy = input->active->data;
        addy->cursor = strlen(addy->user);
    }
}


/*
 * FIXME: Add a function definition to the comments.
 */
static void
keystroke_end(inputData *input) {
    emailData *addy;

    addy = input->active->data;
    if (addy->match != NULL)
        expand_alias_accept_match(addy);
    input->active = g_list_last(input->list);
    addy = input->active->data;
    addy->cursor = strlen(addy->user);
}


/*
 * keystroke_comma()
 *
 * A comma indicates acceptance of the current alias expansion.
 * We need to make sure we expand to what the user currently sees, and
 * add a comma to the list.
 *
 * Possible scenarios:
 *   User sees: "abc|" (| represents cursor)
 *   After must be: "abc@default.com, |"
 *
 *   User sees: "abc@def.com|"
 *   After must be: "abc@def.com, |"
 *
 *   User sees: "abc| (John Doe <john@default.com>)"
 *   After must be: "John Doe <john@default.com, |"
 *
 *   User sees: "abc|def"
 *   After must be: "abc@default.com, |def"
 */
static void
keystroke_comma(inputData * input)
{
    emailData *addy, *extra;
    GList *list;
    gint i;
    gchar *left, *right, *str;
    
    /*
     * First, check if it expanded, and accept it.
     */
    addy = input->active->data;
    left = addy->user;
    if (addy->match != NULL) {
        right = NULL;
        expand_alias_accept_match(addy);
    } else {
        if ((addy->cursor > 0) && (addy->cursor < strlen(addy->user))) {
            right = addy->user;
            for (i = 0; i < addy->cursor; i++) right++;
        } else {
            right = NULL;
        }
    }

    /*
     * At this point, right points to the split where the comma must
     * happen, or it points to NULL
     */

    /*
     * Now we add a new entry.
     */
    extra = new_emailData();
    list = g_list_next(input->active);
    if (list == NULL)
        g_list_append(input->list, extra);
    else
        g_list_insert(input->list, extra, g_list_position(input->list, list));
    if (right != NULL) {
        extra->user = g_strdup(right);
        str = g_strndup(left, addy->cursor);
        g_free(addy->user);
        addy->user = str;
    } else
        extra->user = g_strdup("");

    /*
     * We move the cursor to the right spot in the NEW entry.
     */
    input->active = g_list_next(input->active);
    extra->cursor = 0;
    
    /*
     * And we add the default domain to the original entry.
     */
    if (balsa_app.domain == NULL) return;
    if (strlen(balsa_app.domain) == 0) return;
    if (is_an_email(addy->user)) return;
    str = g_strconcat(addy->user, "@", balsa_app.domain, NULL);
    g_free(addy->user);
    addy->user = str;
}


/*
 * keystroke_add_key()
 *
 * Add a normal alphanumeric key to the input.
 */
static void
keystroke_add_key(inputData ** in, gchar * add, GtkWidget *widget)
{
    emailData *addy;
    gchar *str, *left, *right;
    gint i;
    GtkEditable *editable;
    inputData *input;
    
    g_message("keystroke_add_key(): adding [%s]", add);
    /*
     * User typed a key, so cancel any matches we may have had.
     */
    input = *in;
    addy = input->active->data;
    force_no_match(addy);
    
    /*
     * If this is at the beginning, and the user pressed ' ',
     * ignore it.
     */
    if ((addy->cursor == 0) && (add[0] == (gchar) ' ')) return;
    
    /*
     * FIXME: I don't know how to check if any data is/was selected.
     *        The gtk-reference manual describes how to set it, not get it.
     *        Is it even possible to select any data in the widget?
     *        After all, we are in control...
     *
     *        The following hack works, but it /is/ a hack.  No way to
     *        make it prettier without sub-classing GtkEntry.
     */
    editable = GTK_EDITABLE(widget);
    if (editable->selection_start_pos != editable->selection_end_pos) {
	g_message("keystroke_backspace(): we have a selection.");
	gtk_editable_cut_clipboard(editable);
	force_no_match(addy);
	free_inputData(input);
        input = fill_input(widget);
	*in = input;
        g_message("keystroke_add_key(): Stop: deleted selection.");
	return;
    }

    /*
     * Split the string at the correct cursor position.
     */
    left = g_strndup(addy->user, addy->cursor);
    right = addy->user;
    for (i = 0; i < addy->cursor; i++) right++;
    
    /*
     * Add the keystroke to the end of user input.
     */
    str = g_strconcat(left, add, right, NULL);
    g_free(addy->user);
    g_free(left);
    addy->user = str;
    addy->cursor++;

    /*
     * Now search for (any) match.
     */
    addy->tabs = 0;
    expand_alias_find_match(addy);
    g_message("keystroke_add_key(): Stop.");
}


/*
 * FIXME: Add a function definition to the comments.
 */
static void
keystroke_tab(inputData * input, GtkWidget *widget) {
    emailData *addy;
       
    /*
     * Are we at the first cursor position in the GtkEntry box,
     */
    if (gtk_editable_get_position(GTK_EDITABLE(widget)) == 0) {
        next_entrybox(widget, widget);
        return;
    }
    
    /*
     * Are we at the last position?
     */
    addy = input->active->data;
    if (input->active == g_list_last(input->list)) {
        if (addy->cursor >= strlen(addy->user) && (addy->match == NULL)) {
            keystroke_enter(input);
            next_entrybox(widget, widget);
            return;
        }
    }

    /*
     * Nope - so cycle through the possible matches.
     */
    addy->tabs += 1;
    expand_alias_find_match(addy);
}


/*
 * process_keystroke()
 *
 * Chooses which function to call, depending on the user's keystroke.
 */
static void
process_keystroke(inputData ** in, GtkWidget * widget,
		  GdkEventKey * event)
{
    inputData *input;
    
    g_return_if_fail(in != NULL);
    g_return_if_fail(event != NULL);

    input = *in;
    switch (event->keyval) {
    case GDK_BackSpace:
	keystroke_backspace(&input, widget);
	break;

    case GDK_Delete:
	keystroke_delete(&input, widget);
	break;

    case GDK_Up:
    case GDK_Down:
        keystroke_tab(input, widget);
	break;

    case GDK_Linefeed:
    case GDK_Tab:
    case GDK_Return:
	keystroke_enter(input);
	next_entrybox(widget, widget);
	break;

    case GDK_Home:
	keystroke_home(input);
	break;

    case GDK_Left:
	keystroke_left(input);
	break;

    case GDK_Page_Up:
    case GDK_Page_Down:
	keystroke_down(input);
	next_entrybox(widget, widget);
	break;

    case GDK_Right:
	keystroke_right(input);
	break;

    case GDK_End:
	keystroke_end(input);
	break;

    case GDK_comma:
	keystroke_comma(input);
	break;

    case GDK_Shift_L:
    case GDK_Shift_R:
	break;

    /*
     * FIXME: some keys come here that are not a-zA-Z0-9.
     *        this is fine for international keyboards, but there might
     *        be keys that are not supposed to hit this piece of code.
     */
    default:
	keystroke_add_key(&input, event->string, widget);
    }
    *in = input;
    return;
}


/*
 * key_pressed_cb()
 *
 * This gets called every time a user hits a key in the gtkEntry() box.
 */
gboolean
key_pressed_cb(GtkWidget * widget, GdkEventKey * event, gpointer user_data)
{
    inputData *input;
    gint *i;

    g_return_val_if_fail(widget != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(widget), FALSE);
    g_return_val_if_fail(event != NULL, FALSE);

    /*
     * Skip ALL special characters, and let gtk_entry() process them.
     * This allows, for example, Ctrl-S to send the message.
     *
     * FIXME:
     * Things like Ctrl-A (alias for Home), Ctrl-K (kill line), etc.
     * should be trapped.
     */
    switch (event->keyval) {
    case GDK_Control_L:
    case GDK_Control_R:
    case GDK_Meta_L:
    case GDK_Meta_R:
    case GDK_Alt_L:
    case GDK_Alt_R:
    case GDK_Super_L:
    case GDK_Super_R:
    case GDK_Hyper_L:
    case GDK_Hyper_R:
	return FALSE;
    }

    /*
     * We cannot use GDK_MODIFIER_MASK because that would trap SHIFT.
     * MOD2_MASK traps NUM_LOCK - turns of alias completion if NUM_LOCK
     * is on.
     *
     * We need to assume that gtk_entry() will mess stuff up for us,
     * so we need to act as if we lost focus.
     */
    if (event->state & (GDK_CONTROL_MASK|GDK_MOD1_MASK/*|GDK_MOD2_MASK*/)) {
	lost_focus_cb(widget, NULL, user_data);
	return FALSE;
    }

    /*
     * Grab the old information from the widget - this way the user
     * can switch back and forth between To: and Cc:
     */
    input = libbalsa_address_entry_get_input(LIBBALSA_ADDRESS_ENTRY(widget));
    if (!input) {
	input = new_inputData();
	libbalsa_address_entry_set_input(LIBBALSA_ADDRESS_ENTRY(widget), input);
    }

    /*
     * Check if we have lost focus.
     */
    i = (gint *) gtk_object_get_data(GTK_OBJECT(widget), "focus_c");
    if (*i != 2) {
    	/*
         * Load the input with data from the GtkEntry() box.
         */
	free_inputData(input);
        input = fill_input(widget);
	libbalsa_address_entry_set_input(LIBBALSA_ADDRESS_ENTRY(widget), input);
    }
    *i = 2;
    gtk_object_set_data(GTK_OBJECT(widget), "focus_c", (gpointer) i);

    /*
     * Modify the data according to the key-pressed event.
     * FIXME:
     * Shouldn't this read: input->process_keystroke(widget, event);?
     * I'm learning bad habits from Gtk.
     */
    process_keystroke(&input, widget, event);
    libbalsa_address_entry_set_input(LIBBALSA_ADDRESS_ENTRY(widget), input);

    /*
     * Show the data.
     */
    libbalsa_address_entry_show(LIBBALSA_ADDRESS_ENTRY(widget));

    /*
     * Save the data, stop the signal, and quit.
     */
    libbalsa_address_entry_set_input(LIBBALSA_ADDRESS_ENTRY(widget), input);
    gtk_signal_emit_stop(GTK_OBJECT(widget),
			 gtk_signal_lookup("key-press-event",
					   GTK_TYPE_EDITABLE));
    return TRUE;
}


/*
 * button_pressed_cb()
 *
 * This gets called when the user clicks a mouse button.
 * A side effect is that this moves the cursor in the GtkEntry() box,
 * so we must treat it like we lost focus!  Especially since the mouse
 * /could/ put is inside text that the user did not type!
 *
 * FIXME: What happens when the mouse cursor goes into the wrong area?
 */
gboolean
button_pressed_cb(GtkWidget * widget,
		  GdkEventButton * event, gpointer user_data)
{
    gint *i;

    i = (gint *) gtk_object_get_data(GTK_OBJECT(widget), "focus_c");
    *i = 1;
    gtk_object_set_data(GTK_OBJECT(widget), "focus_c", (gpointer) i);

    return FALSE;
}


/*
 * lost_focus_cb()
 * 
 * Called when focus is lost.  Its main purpose is to keep track,
 * so we can cache user-input safely.
 */
gboolean
lost_focus_cb(GtkWidget * widget,
	      GdkEventFocus * event, gpointer user_data)
{
    gint *i;
    inputData *input;
    emailData *addy;


    /*
     * Check if the input is 'tainted'.
     */
    if ((i = (gint *) gtk_object_get_data(GTK_OBJECT(widget), "focus_c"))) {
	/*
	 * First grab the input, and get rid of any matches we may
         * or may not have found.  This is done for two reasons:
         * - It makes it easier to parse the matches back.
         * - It makes it impossible to put the cursor out of bounds.
         */
        input = libbalsa_address_entry_get_input(LIBBALSA_ADDRESS_ENTRY(widget));
	if (input != NULL) {
            addy = input->active->data;
	    force_no_match(addy);
            /*
             * Now show the changes.
             */
            libbalsa_address_entry_show(LIBBALSA_ADDRESS_ENTRY(widget));
	}
        /*
         * And mark the GtkEntry as 'tainted'.
         */
	*i = 1;
	gtk_object_set_data(GTK_OBJECT(widget), "focus_c", (gpointer) i);
    }
    return TRUE;
}



/*
 * destroy_cb()
 *
 * Input: The widget with the data.
 * Output: None.
 *
 * Call this when the entry box gets destroyed to free user data.
 */
void
destroy_cb(GtkWidget * widget, gpointer user_data)
{
    gint *i;
    inputData *data;

    i = (gint *) gtk_object_get_data(GTK_OBJECT(widget), "focus_c");

    if (i)
	g_free(i);
    i = NULL;

    gtk_object_set_data(GTK_OBJECT(widget), "focus_c", (gpointer) i);
    data = libbalsa_address_entry_get_input(LIBBALSA_ADDRESS_ENTRY(widget));
    if (data)
	free_inputData(data);
    data = NULL;

    libbalsa_address_entry_set_input(LIBBALSA_ADDRESS_ENTRY(widget), data);
}
