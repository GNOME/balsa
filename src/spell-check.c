/* Balsa E-Mail Client
 * Copyright (C) 2000 Matthew Guenther
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

#include <ctype.h>
#include <gnome.h>
#include <pspell/pspell.h>
#include <stdio.h>

#include "balsa-app.h"
#include "quote-color.h"
#include "spell-check.h"


/* enumerations */
typedef enum _LearnType LearnType;
enum _LearnType
{
        SESSION_DICT,
        PERMANENT_DICT
};

enum 
{
	DONE_SPELLCHECK,
	LAST_SIGNAL
};

enum 
{
        ARG_0,
        ARG_MODULE,
        ARG_SUGGEST,
        ARG_IGNORE,
        ARG_LANGUAGE,
        ARG_CHARSET
};


/* initialization stuff */
static void balsa_spell_check_class_init (BalsaSpellCheckClass*);
static void balsa_spell_check_init (BalsaSpellCheck*);
static void balsa_spell_check_set_arg (GtkObject* , GtkArg* , guint );
static void balsa_spell_check_get_arg (GtkObject* , GtkArg* , guint );
static void balsa_spell_check_destroy (GtkObject* object);


/* signal callbacks */
static void done_cb (GtkButton*, gpointer);
static void change_cb (GtkButton*, gpointer);
static void change_all_cb (GtkButton*, gpointer);
static void ignore_cb (GtkButton*, gpointer);
static void ignore_all_cb (GtkButton*, gpointer);
static void learn_cb (GtkButton* button, gpointer);
static void cancel_cb (GtkButton* button, gpointer);
static void select_word_cb (GtkCList*, gint, gint, 
                            GdkEventButton*, gpointer);


/* function prototypes */
static gboolean next_word (BalsaSpellCheck* spell_check);
static gboolean check_word (BalsaSpellCheck* spell_check);
static void switch_word (BalsaSpellCheck* spell_check, const gchar* old_word, const gchar* new_word);
static void finish_check (BalsaSpellCheck* spell_check);
static gboolean check_pspell_errors (PspellManager* manager);

static void setup_suggestions (BalsaSpellCheck* spell_check);
static void balsa_spell_check_next (BalsaSpellCheck* spell_check);
static void balsa_spell_check_fix (BalsaSpellCheck* spell_check, gboolean fix_al);
static void balsa_spell_check_learn (BalsaSpellCheck* spell_check, LearnType learn);


/* marshallers */
typedef void (*BalsaSpellCheckSignal1) (GtkObject* object);

static gint balsa_spell_check_signals[LAST_SIGNAL] = {0};
static GtkFrameClass* parent_class=NULL;



/* begin actual code */
GtkType 
balsa_spell_check_get_type ()
{
	static GtkType balsa_spell_check_type = 0;

	if (!balsa_spell_check_type)
	{
		GtkTypeInfo balsa_spell_check_info = 
		{
			"BalsaSpellCheck",
			sizeof (BalsaSpellCheck),
			sizeof (BalsaSpellCheckClass),
			(GtkClassInitFunc) balsa_spell_check_class_init,
			(GtkObjectInitFunc) balsa_spell_check_init,
			(GtkArgSetFunc) NULL,
			(GtkArgGetFunc) NULL
		};
                
		balsa_spell_check_type = gtk_type_unique (gtk_frame_get_type (), &balsa_spell_check_info);
	}

	return balsa_spell_check_type;
}


static void 
balsa_spell_check_class_init (BalsaSpellCheckClass* klass)
{
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;
	GtkContainerClass *container_class;
	
	object_class = (GtkObjectClass*) klass;
	widget_class = (GtkWidgetClass*) klass;
	container_class = (GtkContainerClass*) klass;

	parent_class = gtk_type_class (GTK_TYPE_FRAME);

        gtk_object_add_arg_type ("BalsaSpellCheck::spell-module",
                                 GTK_TYPE_STRING,
                                 GTK_ARG_READWRITE,
                                 ARG_MODULE);
        gtk_object_add_arg_type ("BalsaSpellCheck::suggest-mode",
                                 GTK_TYPE_STRING,
                                 GTK_ARG_READWRITE,
                                 ARG_SUGGEST);
        gtk_object_add_arg_type ("BalsaSpellCheck::ignore-length",
                                 GTK_TYPE_UINT,
                                 GTK_ARG_READWRITE,
                                 ARG_IGNORE);
        gtk_object_add_arg_type ("BalsaSpellCheck::language-tag",
                                 GTK_TYPE_STRING,
                                 GTK_ARG_READWRITE,
                                 ARG_LANGUAGE);
        gtk_object_add_arg_type ("BalsaSpellCheck::character-set",
                                 GTK_TYPE_STRING,
                                 GTK_ARG_READWRITE,
                                 ARG_CHARSET);
	
	balsa_spell_check_signals[DONE_SPELLCHECK] =
                gtk_signal_new ("done-spell-check", GTK_RUN_LAST, 
                                object_class->type, 
                                GTK_SIGNAL_OFFSET (BalsaSpellCheckClass, 
                                                   done_spell_check),
				gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);
	
	gtk_object_class_add_signals (object_class, balsa_spell_check_signals,
                                      LAST_SIGNAL);

        object_class->destroy = balsa_spell_check_destroy;
        object_class->set_arg = balsa_spell_check_set_arg;
        object_class->get_arg = balsa_spell_check_get_arg;

	klass->done_spell_check = NULL;
}


static void
balsa_spell_check_set_arg (GtkObject* object, GtkArg* arg, guint arg_id)
{
        BalsaSpellCheck* spell_check;
        
        spell_check = BALSA_SPELL_CHECK (object);
        switch (arg_id)
        {
        case ARG_MODULE:
                balsa_spell_check_set_module (spell_check, 
                                              GTK_VALUE_STRING (*arg));
                break;

        case ARG_SUGGEST:
                balsa_spell_check_set_suggest_mode (spell_check, 
                                                    GTK_VALUE_STRING (*arg));
                break;

        case ARG_IGNORE:
                balsa_spell_check_set_ignore_length (spell_check, 
                                                     GTK_VALUE_UINT (*arg));
                break;

        case ARG_LANGUAGE:
                balsa_spell_check_set_language (spell_check, 
                                                GTK_VALUE_STRING (*arg));
                break;

        case ARG_CHARSET:
                balsa_spell_check_set_character_set (spell_check, 
                                                     GTK_VALUE_STRING (*arg));
                break;

        default:
                break;
        }
}


static void 
balsa_spell_check_get_arg (GtkObject* object, GtkArg* arg, guint arg_id)
{
        BalsaSpellCheck* spell_check;
        
        spell_check = BALSA_SPELL_CHECK (object);
        switch (arg_id) {
        case ARG_MODULE:
                GTK_VALUE_STRING (*arg) = g_strdup (spell_check->module);
                break;

        case ARG_SUGGEST:
                GTK_VALUE_STRING (*arg) = g_strdup (spell_check->suggest_mode);
                break;

        case ARG_IGNORE:
                GTK_VALUE_UINT (*arg) = spell_check->ignore_length;
                break;

        case ARG_LANGUAGE:
                GTK_VALUE_STRING (*arg) = g_strdup (spell_check->language_tag);
                break;

        case ARG_CHARSET:
                GTK_VALUE_STRING (*arg) = g_strdup (spell_check->character_set);
                break;

        default:
                arg->type = GTK_TYPE_INVALID;
                break;
        }
}


/* balsa_spell_check_new ()
 * 
 * Create a new spell check widget.
 * */
GtkWidget* 
balsa_spell_check_new (void)
{
        BalsaSpellCheck* spell_check;
        
        spell_check = BALSA_SPELL_CHECK (gtk_type_new (balsa_spell_check_get_type ()));
        
        return GTK_WIDGET (spell_check);
}


/* balsa_spell_check_new_with_text
 * 
 * Create a new spell check widget, assigning the GtkText to check.
 * */
GtkWidget* 
balsa_spell_check_new_with_text (GtkText* check_text)
{
        BalsaSpellCheck* spell_check;
        
        spell_check = BALSA_SPELL_CHECK (gtk_type_new (balsa_spell_check_get_type ()));
        spell_check->text = check_text;
        
        return GTK_WIDGET (spell_check);
}


/* balsa_spell_check_set_text ()
 * 
 * Set the text widget the spell check should check.
 * */
void
balsa_spell_check_set_text (BalsaSpellCheck* spell_check, GtkText* check_text)
{
        g_return_if_fail (check_text != NULL);
        g_return_if_fail (GTK_IS_TEXT (check_text));
        
        spell_check->text = check_text;
}


/* balsa_spell_check_set_font ()
 * 
 * Set the font used in the GtkText, needed to properly replace words.
 * */
void
balsa_spell_check_set_font (BalsaSpellCheck* spell_check, GdkFont* text_font)
{
        if (spell_check->font)
                gdk_font_unref (spell_check->font);
        
        spell_check->font = text_font;
        gdk_font_ref (spell_check->font);
}


/* balsa_spell_check_set_module ()
 * 
 * Set the pspell module to use for spell checking (ispell, aspell,
 * etc.).
 * */
void 
balsa_spell_check_set_module (BalsaSpellCheck* spell_check, const gchar* module_name)
{
        g_return_if_fail (spell_check != NULL);
        g_return_if_fail (BALSA_IS_SPELL_CHECK (spell_check));
        
        g_free (spell_check->module);
        spell_check->module = g_strdup (module_name);
}


/* balsa_spell_check_set_suggest_mode ()
 * 
 * Select the suggestion mode to use.  This determines how hard the
 * spell checking engine works to find suggestions to use, and which
 * suggestions to put near the beginning.  There are three settings:
 * fast, normal, and bad-spellers.
 * */
void 
balsa_spell_check_set_suggest_mode (BalsaSpellCheck* spell_check, const gchar* suggest)
{
        g_return_if_fail (spell_check != NULL);
        g_return_if_fail (BALSA_IS_SPELL_CHECK (spell_check));
        
        g_free (spell_check->suggest_mode);
        spell_check->suggest_mode = g_strdup (suggest);
}


/* balsa_spell_check_set_ignore_length ()
 * 
 * Set the minimum length words must be to be considered for checking.
 * */
void 
balsa_spell_check_set_ignore_length (BalsaSpellCheck* spell_check, guint length)
{
        g_return_if_fail (spell_check != NULL);
        g_return_if_fail (BALSA_IS_SPELL_CHECK (spell_check));
        
        spell_check->ignore_length = length;
}


/* balsa_spell_check_set_language ()
 * 
 * Set the language to do spell checking in.
 * */
void 
balsa_spell_check_set_language (BalsaSpellCheck* spell_check, const gchar* language)
{
        g_return_if_fail (spell_check != NULL);
        g_return_if_fail (BALSA_IS_SPELL_CHECK (spell_check));
        
        g_free (spell_check->language_tag);
        spell_check->language_tag = g_strdup (language);
}


/* balsa_spell_check_set_character_set ()
 * 
 * Set the character set to spell check with.
 * */
void 
balsa_spell_check_set_character_set (BalsaSpellCheck* spell_check, const gchar* char_set)
{
        g_return_if_fail (spell_check != NULL);
        g_return_if_fail (BALSA_IS_SPELL_CHECK (spell_check));

        g_free (spell_check->character_set);
        spell_check->character_set = g_strdup (char_set);
}


/* balsa_spell_check_init ()
 * 
 * Initialization of the class to reasonable default values, set up
 * buttons signal callbacks.  
 * */
static void 
balsa_spell_check_init (BalsaSpellCheck* spell_check)
{
        GtkWidget* hbox;
        GtkWidget* vbox0;
        GtkWidget* vbox1;
        GtkWidget* vbox2;
        GtkWidget* new_word_text;
        GtkWidget* sw;
        GtkWidget* suggestion_list;
        GtkWidget* change;
        GtkWidget* change_all;
        GtkWidget* ignore;
        GtkWidget* ignore_all;
        GtkWidget* learn;
        GtkWidget* done;
        GtkWidget* cancel;
        
        const guint padding = 4;

        /* Set spell checker */
        spell_check->highlight_colour = g_malloc0 (sizeof (GdkColor));
        spell_check->highlight_colour->red = 65535;
        spell_check->highlight_colour->green = 0;
        spell_check->highlight_colour->blue = 0;
        gdk_colormap_alloc_color (balsa_app.colormap, 
                                  spell_check->highlight_colour, 
                                  TRUE, TRUE);
        
        spell_check->font = NULL;
        spell_check->start_pos = 0;
        spell_check->end_pos = 0;
        spell_check->length = 0;
        spell_check->original_text = NULL;
        spell_check->original_pos = 0;
        spell_check->word_list = NULL;
        spell_check->suggestions = NULL;

        spell_check->module = g_strdup ("aspell");
        spell_check->suggest_mode = g_strdup ("normal");
        spell_check->ignore_length = 0;
        spell_check->language_tag = g_strdup ("en");
        spell_check->character_set = g_strdup ("iso8859-*");

        /* Set up interface elements */
        gtk_container_set_border_width (GTK_CONTAINER(spell_check), padding/2);

        hbox = gtk_hbox_new (FALSE, padding);
        gtk_container_set_border_width (GTK_CONTAINER (hbox), padding/2);
        gtk_container_add (GTK_CONTAINER (spell_check), hbox);
        
        vbox0 = gtk_vbox_new (FALSE, padding/2);
        gtk_box_pack_start (GTK_BOX (hbox), vbox0, TRUE, TRUE, 0);
        
        vbox1 = gtk_vbox_new (FALSE, padding);
        gtk_box_pack_start (GTK_BOX (hbox), vbox1, FALSE, FALSE, 0);
        
        vbox2 = gtk_vbox_new (FALSE, padding);
        gtk_box_pack_start (GTK_BOX (hbox), vbox2, FALSE, FALSE, 0);

        /* setup suggestion display */
        new_word_text = gtk_entry_new ();
        spell_check->entry = GTK_ENTRY (new_word_text);
        gtk_box_pack_start (GTK_BOX (vbox0), new_word_text, 
                            FALSE, FALSE, 0);

        sw = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
                                        GTK_POLICY_AUTOMATIC,
                                        GTK_POLICY_AUTOMATIC);
        gtk_box_pack_start (GTK_BOX (vbox0), sw, 
                            TRUE, TRUE, 0);

        /* setup suggestion list */
        suggestion_list = gtk_clist_new (1);
        gtk_clist_set_selection_mode (GTK_CLIST (suggestion_list),
                                      GTK_SELECTION_BROWSE);
        gtk_clist_set_column_auto_resize (GTK_CLIST (suggestion_list), 
                                          0, TRUE);
        gtk_clist_column_titles_hide (GTK_CLIST (suggestion_list));
        gtk_container_add (GTK_CONTAINER (sw), suggestion_list);
        spell_check->list = GTK_CLIST (suggestion_list);

        /* setup buttons to perform actions */
        change = gnome_stock_button_with_label (GNOME_STOCK_PIXMAP_REDO, 
                                                "Change");
        set_tooltip (change, _("Replace the current word with the selected suggestion"));
        gtk_box_pack_start (GTK_BOX (vbox1), change, FALSE, FALSE, 0);

        change_all = gnome_stock_button_with_label (GNOME_STOCK_PIXMAP_REFRESH,
                                                    "Change All");
        set_tooltip (change_all, _ ("Replace all occurances of the current word with the selected suggestion"));
        gtk_box_pack_start (GTK_BOX (vbox2), change_all, 
                            FALSE, FALSE, 0);
        
        ignore = gnome_stock_button_with_label (GNOME_STOCK_PIXMAP_FORWARD,
                                                "Ignore");
        set_tooltip (ignore, _ ("Skip the current word"));
        gtk_box_pack_start (GTK_BOX (vbox1), ignore, FALSE, FALSE, 0);
        
        ignore_all = gnome_stock_button_with_label (GNOME_STOCK_PIXMAP_LAST,
                                                    "Ignore All");
        set_tooltip (ignore_all, _ ("Skip all occurances of the current word"));
        gtk_box_pack_start (GTK_BOX (vbox2), ignore_all, 
                            FALSE, FALSE, 0);

        learn = gnome_stock_button_with_label (GNOME_STOCK_PIXMAP_BOOK_OPEN,
                                               "Learn");
        set_tooltip (learn, _ ("Add the current word to your personal dictionar"));
        gtk_box_pack_start (GTK_BOX (vbox1), learn, FALSE, FALSE, 0);
        
        done = gnome_stock_button_with_label (GNOME_STOCK_BUTTON_OK, 
                                              "Done");
        set_tooltip (done, _ ("Finish spell checking"));
        gtk_box_pack_end (GTK_BOX (vbox1), done, FALSE, FALSE, 0);

        cancel = gnome_stock_button (GNOME_STOCK_BUTTON_CANCEL);
        set_tooltip (cancel, _ ("Revert all changes and finish spell checking"));
        gtk_box_pack_end (GTK_BOX (vbox2), cancel, FALSE, FALSE, 0);
        
        /* connect signal handlers */
        gtk_signal_connect (GTK_OBJECT (suggestion_list), "select-row",
                            GTK_SIGNAL_FUNC (select_word_cb), spell_check);

        gtk_signal_connect (GTK_OBJECT (change), "clicked",
                            GTK_SIGNAL_FUNC (change_cb), spell_check);

        gtk_signal_connect (GTK_OBJECT (change_all), "clicked",
                            GTK_SIGNAL_FUNC (change_all_cb), spell_check);
        
        gtk_signal_connect (GTK_OBJECT (ignore), "clicked",
                            GTK_SIGNAL_FUNC (ignore_cb), spell_check);
        
        gtk_signal_connect (GTK_OBJECT (ignore_all), "clicked",
                            GTK_SIGNAL_FUNC (ignore_all_cb), spell_check);
        
        gtk_signal_connect (GTK_OBJECT (learn), "clicked",
                            GTK_SIGNAL_FUNC (learn_cb), spell_check);
        
        gtk_signal_connect (GTK_OBJECT (cancel), "clicked",
                            GTK_SIGNAL_FUNC (cancel_cb), spell_check);

        gtk_signal_connect (GTK_OBJECT (done), "clicked", 
                            GTK_SIGNAL_FUNC (done_cb), spell_check);
        
        gtk_widget_show_all (GTK_WIDGET (spell_check));
}


/* select_word_cb ()
 * 
 * When the user selects a word from the list of available
 * suggestions, replace the text in the entry text box with the text
 * from the clist selection.
 * */
static void 
select_word_cb (GtkCList* clist, gint row, gint column, 
                GdkEventButton* button, gpointer data)
{
        BalsaSpellCheck* spell_check;
        gchar* selection[1];
        gint result;

        
        spell_check = BALSA_SPELL_CHECK (data);
        result = gtk_clist_get_text (clist, row, column, selection);
        if (result) {
                gtk_entry_set_text (spell_check->entry, selection[0]);
        }
}


/* ignore_cb ()
 * 
 * Selecting the ignore button causes the current word to be skipped
 * for checking.
 * */
static void 
ignore_cb (GtkButton* button, gpointer data)
{
        BalsaSpellCheck* spell_check;
        
        g_return_if_fail (data != NULL);
        g_return_if_fail (BALSA_IS_SPELL_CHECK (data));
        
        spell_check = BALSA_SPELL_CHECK (data);

        /* Ignore the current word, go to next */
        finish_check (spell_check);
        balsa_spell_check_next (spell_check);
}


/* ignore_all_cb ()
 * 
 * Add the current word to the session library, causing it to be
 * skipped for the rest of the checking session.
 * */
static void 
ignore_all_cb (GtkButton* button, gpointer data)
{
        /* add the current word to the session library */
        BalsaSpellCheck* spell_check;
        
        g_return_if_fail (data != NULL);
        g_return_if_fail (BALSA_IS_SPELL_CHECK (data));
        
        spell_check = BALSA_SPELL_CHECK (data);
        balsa_spell_check_learn (spell_check, SESSION_DICT);
        balsa_spell_check_next (spell_check);
}


/* change_cb ()
 * 
 * Change the current word being checked to the selected suggestion or
 * what the user enters in the entry box.
 * */
static void 
change_cb (GtkButton* button, gpointer data)
{
        BalsaSpellCheck* spell_check;

        g_return_if_fail (data != NULL);
        g_return_if_fail (BALSA_IS_SPELL_CHECK (data));
        
        spell_check = BALSA_SPELL_CHECK (data);
        balsa_spell_check_fix (spell_check, FALSE);
        balsa_spell_check_next (spell_check);
}


/* change_all_cb ()
 * 
 * Replace all occurances of the currently misspelled word in the text
 * to the current suggestion.
 * */
static void 
change_all_cb (GtkButton* button, gpointer data)
{
        BalsaSpellCheck* spell_check;

        /* change all similarly misspelled words without asking */
        g_return_if_fail (data != NULL);
        g_return_if_fail (BALSA_IS_SPELL_CHECK (data));
        
        spell_check = BALSA_SPELL_CHECK (data);
        
        balsa_spell_check_fix (spell_check, TRUE);
        balsa_spell_check_next (spell_check);
}


/* learn_cb ()
 * 
 * Add the current word to the permanent personal dictionary.
 * */
static void 
learn_cb (GtkButton* button, gpointer data)
{
        BalsaSpellCheck* spell_check;

        g_return_if_fail (data != NULL);
        g_return_if_fail (BALSA_IS_SPELL_CHECK (data));
        
        spell_check = BALSA_SPELL_CHECK (data);
        balsa_spell_check_learn (spell_check, PERMANENT_DICT);
        balsa_spell_check_next (spell_check);
}


/* cancel_cb ()
 * 
 * Cancel the check, restoring the original text.
 * */
static void 
cancel_cb (GtkButton* button, gpointer data)
{
        BalsaSpellCheck* spell_check;

        g_return_if_fail (data != NULL);
        g_return_if_fail (BALSA_IS_SPELL_CHECK (data));
        
        spell_check = BALSA_SPELL_CHECK (data);
        balsa_spell_check_finish (spell_check, FALSE);
}


/* done_cb ()
 * 
 * Signal callback for the done button, end the spell check, keeping
 * all changes up to this point.
 * */
static void 
done_cb (GtkButton* button, gpointer data)
{
        BalsaSpellCheck* spell_check;

        g_return_if_fail (data != NULL);
        g_return_if_fail (BALSA_IS_SPELL_CHECK (data));
        spell_check = BALSA_SPELL_CHECK (data);
        
        balsa_spell_check_finish (spell_check, TRUE);
}


/* balsa_spell_check_start ()
 * 
 * Start the spell check, allocating the PSpellConfig and
 * PSpellManager to do the checking.
 * */
void 
balsa_spell_check_start (BalsaSpellCheck* spell_check)
{
        PspellCanHaveError* spell_error;
        gchar* string;

        
        /* Config the spell check */
        spell_check->spell_config = new_pspell_config ();
        pspell_config_replace (spell_check->spell_config, 
                               "language-tag", spell_check->language_tag);
        pspell_config_replace (spell_check->spell_config,
                               "module", spell_check->module);
        pspell_config_replace (spell_check->spell_config,
                               "sug-mode", spell_check->suggest_mode);

        string = g_strdup_printf ("%d", spell_check->ignore_length);
        pspell_config_replace (spell_check->spell_config,
                               "ignore", string);
        g_free (string);

        spell_error = new_pspell_manager (spell_check->spell_config);
        delete_pspell_config (spell_check->spell_config);

        if (pspell_error_number (spell_error) != 0) {
                /* quit without breaking things */
                balsa_information (LIBBALSA_INFORMATION_WARNING, 
                                   pspell_error_message (spell_error));
                
                gtk_signal_emit (GTK_OBJECT (spell_check),
				 balsa_spell_check_signals[DONE_SPELLCHECK]); 
                return;
        }
        
        spell_check->spell_manager = to_pspell_manager (spell_error);
        spell_check->spell_config = pspell_manager_config (spell_check->spell_manager);

        gtk_text_set_editable (spell_check->text, FALSE);
        spell_check->original_pos = gtk_editable_get_position (GTK_EDITABLE (spell_check->text));

        /* Get the original text so we can always revert */
        spell_check->original_text = gtk_editable_get_chars (GTK_EDITABLE (spell_check->text), 0, -1);
        spell_check->length = strlen (spell_check->original_text);

        if (balsa_app.debug)
                balsa_information (LIBBALSA_INFORMATION_DEBUG, 
                                   "BalsaSpellCheck: Start\n");
        
        balsa_spell_check_next (spell_check);
}


/* balsa_spell_check_next ()
 * 
 * Continue the spell check, clear the old words and suggestions, and
 * moving onto the next incorrect word.  Replace the incorrect word
 * with a highlighted version, move the text to display it, and setup
 * the suggestions.
 * */
static void 
balsa_spell_check_next (BalsaSpellCheck* spell_check)
{
        gchar* word;
        

        if (!next_word (spell_check)) {
                balsa_spell_check_finish (spell_check, TRUE);
                return;
        }

        while (check_word (spell_check)) {
                if (!next_word (spell_check)) {
                        balsa_spell_check_finish (spell_check, TRUE);
                        return;
                }
        }

        /* replace the current word with the same word in a different
         * colour */
        gtk_text_freeze (spell_check->text);
        word = gtk_editable_get_chars (GTK_EDITABLE (spell_check->text), 
                                       spell_check->start_pos, 
                                       spell_check->end_pos);
        gtk_text_set_point (spell_check->text, spell_check->start_pos);
        gtk_text_forward_delete(spell_check->text, 
                                spell_check->end_pos - spell_check->start_pos);
        gtk_text_insert (spell_check->text, spell_check->font, 
                         spell_check->highlight_colour, NULL,
                         word, spell_check->end_pos - spell_check->start_pos);
        g_free (word);

        /* scroll text window to show current word */
        gtk_editable_set_position (GTK_EDITABLE (spell_check->text), 
                                   spell_check->start_pos);
        
        gtk_text_thaw (spell_check->text);
}


/* balsa_spell_check_learn ()
 * 
 * Learn the current word, either to the personal or session
 * dictionaries.  
 * */
static void 
balsa_spell_check_learn (BalsaSpellCheck* spell_check, LearnType learn_type)
{
        gchar* word;
        gint result;
        
        word = gtk_editable_get_chars (GTK_EDITABLE (spell_check->text), 
                                       spell_check->start_pos,
                                       spell_check->end_pos);
        
        if (balsa_app.debug)
                balsa_information (LIBBALSA_INFORMATION_DEBUG,
                                   "BalsaSpellCheck: Learn %s\n", word);
                
        if (learn_type == SESSION_DICT) {
                result = pspell_manager_add_to_session (spell_check->spell_manager, word);
        } else {
                result = pspell_manager_add_to_personal (spell_check->spell_manager, word);
        }
        
        /* If result is 0, the learn operation failed */
        if (!result) {
                if(pspell_manager_error_number(spell_check->spell_manager)!=0){
                        balsa_information (BALSA_INFORMATION_SHOW_DIALOG,
                                           "BalsaSpellCheck: Learn operation failed;\n%s\n", pspell_manager_error_message (spell_check->spell_manager));
                } else {
                        balsa_information (BALSA_INFORMATION_SHOW_DIALOG, 
                                           "BalsaSpellCheck: Learn operation failed.\n");
                }
        }
        
        g_free (word);
        finish_check (spell_check);
}


/* balsa_spell_check_fix ()
 * 
 * Replace the current word with the currently selected word, and if
 * fix_all is true, replace all other occurances of the current word
 * in the text with the correction.
 * */
static void 
balsa_spell_check_fix (BalsaSpellCheck* spell_check, gboolean fix_all)
{
        gchar* new_word = NULL;
        gchar* old_word = NULL;
        gchar* wrong_word = NULL;
        gint saved_start_pos;
        gint saved_end_pos;


        new_word = NULL;
        old_word = gtk_editable_get_chars (GTK_EDITABLE (spell_check->text), 
                                           spell_check->start_pos, 
                                           spell_check->end_pos);
        new_word = gtk_editable_get_chars (GTK_EDITABLE (spell_check->entry), 
                                           0, -1);

        if (!new_word) {
                /* no word to replace, ignore */
                g_free (old_word);
                g_free (new_word);
                return;
        }
        
        /* Some spelling modules can learn from user
         * replacement choices. */
        pspell_manager_store_replacement (spell_check->spell_manager, 
                                          old_word, new_word);
                
        if (check_pspell_errors (spell_check->spell_manager)) {
                g_free (new_word);
                g_free (old_word);
                balsa_spell_check_finish (spell_check, TRUE);
                return;
        }
                
        if (balsa_app.debug)
                 balsa_information (LIBBALSA_INFORMATION_DEBUG, 
                                    "BalsaSpellCheck: Replace %s with %s\n", 
                                    old_word, new_word);

        gtk_text_freeze (spell_check->text);
        switch_word (spell_check, old_word, new_word);


        if (fix_all) {
                saved_start_pos = spell_check->start_pos;
                saved_end_pos = spell_check->end_pos;
                                
                while (next_word (spell_check)) {
                        wrong_word = gtk_editable_get_chars (GTK_EDITABLE (spell_check->text), spell_check->start_pos, spell_check->end_pos);
                        if (g_strcasecmp (old_word, wrong_word) == 0) {
                                switch_word(spell_check, wrong_word, new_word);
                        }
                        g_free (wrong_word);
                }
                
                spell_check->start_pos = saved_start_pos;
                spell_check->end_pos = saved_end_pos;
        }

        gtk_text_thaw (spell_check->text);
        g_free (new_word);
        g_free (old_word);
        finish_check (spell_check);
}


/* balsa_spell_check_destroy ()
 * 
 * Clean up variables if the widget is destroyed.
 * */
static void balsa_spell_check_destroy (GtkObject* object)
{
        BalsaSpellCheck* spell_check;
        
        g_return_if_fail (object != NULL);
        g_return_if_fail (BALSA_IS_SPELL_CHECK (object));
        
        spell_check = BALSA_SPELL_CHECK (object);

        if (spell_check->suggestions)
                finish_check (spell_check);
        
        if (spell_check->spell_manager) 
                balsa_spell_check_finish (spell_check, TRUE);
        
        if (GTK_OBJECT_CLASS (parent_class)->destroy)
                (* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}


/* balsa_spell_check_finish ()
 * 
 * Clean up the variables from the spell check, and emit the done
 * signal so that the main program knows to resume normal operation.
 * */
void 
balsa_spell_check_finish (BalsaSpellCheck* spell_check, gboolean keep_changes)
{
        gint length;
        

        finish_check (spell_check);
        g_free (spell_check->original_text);

        if (keep_changes) {
                pspell_manager_save_all_word_lists(spell_check->spell_manager);
        } else {
                /* replace corrected text with original text */
                gtk_text_freeze (spell_check->text);
                length = gtk_text_get_length (spell_check->text);
                gtk_text_set_point (spell_check->text, 0);
                gtk_text_forward_delete (spell_check->text, length);
                gtk_text_insert (spell_check->text, spell_check->font, 
                                 NULL, NULL, 
                                 spell_check->original_text, 
                                 strlen (spell_check->original_text));
                gtk_text_thaw (spell_check->text);
        }
        
        check_pspell_errors (spell_check->spell_manager);
        
        delete_pspell_manager (spell_check->spell_manager);
        spell_check->spell_manager = NULL;

        gtk_editable_set_position (GTK_EDITABLE (spell_check->text), 
                                   spell_check->original_pos);
        
        gtk_text_set_editable (spell_check->text, TRUE);
        spell_check->start_pos = 0;
        spell_check->end_pos = 0;
        spell_check->length = 0;
        
        if (balsa_app.debug)
                balsa_information (LIBBALSA_INFORMATION_DEBUG, 
                                   "BalsaSpellCheck: Finished\n");

        gtk_signal_emit (GTK_OBJECT (spell_check), 
                         balsa_spell_check_signals[DONE_SPELLCHECK]);
}


/* setup_suggestions ()
 * 
 * Retrieves the suggestions for the word that is currently being
 * checked, and place them in the word list.
 * */
static void 
setup_suggestions (BalsaSpellCheck* spell_check)
{
        const PspellWordList* wl;
        const gchar* new_word;
        gchar* row_text[1];
        

        wl = spell_check->word_list;
        gtk_clist_freeze (spell_check->list);
        spell_check->suggestions = pspell_word_list_elements (wl);
        
        while ((new_word = pspell_string_emulation_next (spell_check->suggestions)) != NULL) {
                if (balsa_app.debug)
                        balsa_information (LIBBALSA_INFORMATION_DEBUG,
                                           "BalsaSpellCheck: Suggest %s\n", 
                                           new_word);
                
                row_text[0] = g_strdup (new_word);
                gtk_clist_append (spell_check->list, row_text);
        }
        gtk_clist_thaw (spell_check->list);
}


/* check_word ()
 * 
 * Check the current word of the BalsaSpellCheck object.  If
 * incorrect, fill the clist and entrybox with the suggestions
 * obtained from pspell, and retrurn a false value.  Otherwise return
 * true.
 * */
static gboolean 
check_word (BalsaSpellCheck* spell_check)
{
        gboolean correct;
        gchar* word = NULL;


        word = gtk_editable_get_chars (GTK_EDITABLE (spell_check->text), 
                                       spell_check->start_pos, 
                                       spell_check->end_pos);
        
        if (word) {

                if (balsa_app.debug)
                        balsa_information (LIBBALSA_INFORMATION_DEBUG, 
                                           "BalsaSpellCheck: Check %s", word);

                correct = pspell_manager_check (spell_check->spell_manager, 
                                                word);
        } else {
                return TRUE;
        }
        
        if (!correct) {
                if (balsa_app.debug)
                        balsa_information (LIBBALSA_INFORMATION_DEBUG,
                                           " ...incorrect.\n");

                spell_check->word_list = 
                        pspell_manager_suggest (spell_check->spell_manager, 
                                                word);
                setup_suggestions (spell_check);
                
                gtk_clist_select_row (spell_check->list, 0, 0);
                if (gtk_clist_row_is_visible (spell_check->list, 1) != GTK_VISIBILITY_FULL) {
                        gtk_clist_moveto (spell_check->list, 1, -1, 0.0, 0.0);
                }
        } else {
                if (balsa_app.debug)
                        balsa_information (LIBBALSA_INFORMATION_DEBUG,
                                           " ...correct.\n");
        }
        
        g_free (word);
        return correct;
}


/* finish_check ()
 * 
 * Clean up all of the variables from the spell checking, 
 * freeing the suggestions.  
 * */
static void 
finish_check (BalsaSpellCheck* spell_check)
{
        gchar* word;
        
        /* get rid of the suggestions */
        gtk_clist_freeze (spell_check->list);
        gtk_clist_clear (spell_check->list);
        gtk_clist_thaw (spell_check->list);
        
        gtk_entry_set_text (spell_check->entry, "");
      
        free (spell_check->suggestions);
        spell_check->suggestions = NULL;

        /* we need to make sure there's no highlighted text left */
        gtk_text_freeze (spell_check->text);
        word = gtk_editable_get_chars (GTK_EDITABLE (spell_check->text), 
                                       spell_check->start_pos, 
                                       spell_check->end_pos);
        gtk_text_set_point (spell_check->text, spell_check->start_pos);
        gtk_text_forward_delete(spell_check->text, 
                                spell_check->end_pos - spell_check->start_pos);
        gtk_text_insert (spell_check->text, spell_check->font, 
                         NULL, NULL,
                         word, spell_check->end_pos - spell_check->start_pos);
        g_free (word);
        gtk_text_thaw (spell_check->text);
}


/* check_pspell_errors ()
 * 
 * To be called after trying things with the pspell manager, if there
 * were any errors with the operation it will generate an error
 * message and return true.
 * */
static gboolean 
check_pspell_errors (PspellManager * manager)
{
  if (pspell_manager_error_number(manager) != 0) {
    balsa_information (BALSA_INFORMATION_SHOW_LIST, 
                       "BalsaSpellCheck: Pspell Error: %s\n", 
                       pspell_manager_error_message(manager));
    return TRUE;
  }
  return FALSE;
}


/* next_word() 
 * 
 * Move the pointer positions to the next word in preparation for
 * checking.  Returns true if successful, false if it has reached the
 * end of the text.
 * */
static gboolean
next_word (BalsaSpellCheck* spell_check)
{
        regex_t new_word_rex;
        regex_t quoted_rex;
        regmatch_t rm[1];
        gchar* text;
        gchar* line;
        gchar** line_array;
        gchar** line_array_base;
        gint offset = 0;
        gboolean at_end = FALSE;

        static gboolean in_line = FALSE;
        const gchar* new_word_regex = "\\<[[:alpha:]']*\\>";


        /* compile the regular expressions */
        if (regcomp (&quoted_rex, balsa_app.quote_regex, REG_EXTENDED)) {
                balsa_information (LIBBALSA_INFORMATION_ERROR, "BalsaSpellCheck: Quoted text regular expression compilation failed\n");
                return FALSE;
        }
        
        if (regcomp (&new_word_rex, new_word_regex, REG_EXTENDED | REG_NEWLINE)) {
                balsa_information (LIBBALSA_INFORMATION_ERROR, "BalsaSpellCheck: New word regular expression compilation failed\n");
                regfree (&quoted_rex);
                return FALSE;
        }

        /* get the message text */
        spell_check->start_pos = spell_check->end_pos;
        text =  gtk_editable_get_chars (GTK_EDITABLE (spell_check->text),
                                        spell_check->end_pos, -1);
        
        /* skip quoted text */
        line_array_base = g_strsplit (text, "\n", -1);
        line_array = line_array_base;
        line = g_strconcat (*line_array, "\n", NULL);
                
        while (!in_line && line && is_a_quote (line, &quoted_rex)) {
                if (balsa_app.debug) {
                        balsa_information (LIBBALSA_INFORMATION_DEBUG, "BalsaSpellCheck: Ignore Quoted: %s\n", 
                                 line);
                }
                
                offset += strlen (line); 
                in_line = FALSE;

                g_free (line);
                line = *(++line_array);

                if (line)
                        line = g_strconcat (line, "\n", NULL);
        }

        /* match the next word */      
        if (line) {
                rm[0].rm_so = 0;
                rm[0].rm_eo = 0;
                regexec (&new_word_rex, line, 1, rm, 0);
                
                if (rm[0].rm_so == rm[0].rm_eo) {
                        spell_check->start_pos += strlen (line) + offset;
                        spell_check->end_pos += strlen (line) + offset;
                        in_line = FALSE;
                } else {
                        spell_check->start_pos += (rm[0].rm_so + offset);
                        spell_check->end_pos += (rm[0].rm_eo + offset);
                        in_line = TRUE;
                }
                
                g_free (line);
        } else {
                at_end = TRUE;
        }
        
        g_strfreev (line_array_base);
        g_free (text);
        regfree (&quoted_rex);
        regfree (&new_word_rex);

        /* check to see if we're at the end yet */
        if (spell_check->end_pos >= spell_check->length) 
                at_end = TRUE;
        else if (spell_check->start_pos == spell_check->end_pos)
                at_end = !next_word (spell_check);

        if (at_end) {
                in_line = FALSE;
                spell_check->start_pos = spell_check->length;
                spell_check->end_pos = spell_check->length;
                return FALSE;
        } else {
                return !at_end;
        }
}


/* switch_word ()
 * 
 * Perform the actual replacement of the incorrect old_word with the
 * new_word, calculating the overall change in length and change in
 * position for the beginning and end of the word.
 * */
static void
switch_word (BalsaSpellCheck* spell_check, const gchar* old_word, const gchar* new_word)
{
        gint old_length;
        gint new_length;
        gint drift;
        
        old_length = 0;
        new_length = 0;

        /* calculate the difference in lengths between the new
         * and old word
         * */
        old_length = spell_check->end_pos - spell_check->start_pos;
        new_length = strlen (new_word);
        drift = new_length - old_length;
        spell_check->end_pos += drift;
        spell_check->original_pos += drift;
        spell_check->length += drift;
        
        /* remove and replace the current word. */
        gtk_text_set_point (spell_check->text, spell_check->start_pos);
        gtk_text_forward_delete (spell_check->text, old_length);
        gtk_text_insert (spell_check->text, spell_check->font, NULL, 
                         NULL, new_word, new_length);
}
