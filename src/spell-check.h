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

#ifndef __BALSA_SPELL_CHECK_H__
#define __BALSA_SPELL_CHECK_H__

#include "config.h"

#include <gnome.h>
#include <pspell/pspell.h>

#ifdef __cplusplus
extern "C" {
#endif				/* __cplusplus */


#define BALSA_TYPE_SPELL_CHECK         (balsa_spell_check_get_type ())
#define BALSA_SPELL_CHECK(obj)         GTK_CHECK_CAST (obj, BALSA_TYPE_SPELL_CHECK, BalsaSpellCheck)
#define BALSA_SPELL_CHECK_CLASS(klass) GTK_CHECK_CLASS_CAST (klass, BALSA_TYPE_SPELL_CHECK, BalsaSpellCheckClass)
#define BALSA_IS_SPELL_CHECK(obj)      GTK_CHECK_TYPE (obj, BALSA_TYPE_SPELL_CHECK)


    typedef struct _BalsaSpellCheck BalsaSpellCheck;
    typedef struct _BalsaSpellCheckClass BalsaSpellCheckClass;


    struct _BalsaSpellCheck {
	GtkFrame frame;

	GtkText *text;
	GdkFont *font;
	GdkColor *highlight_colour;
	GtkCList *list;
	GtkEntry *entry;

	/* actual spell checking variables */
	PspellConfig *spell_config;
	PspellManager *spell_manager;
	const PspellWordList *word_list;
	PspellStringEmulation *suggestions;

	/* restoration information */
	gchar *original_text;
	gint original_pos;

	/* word selection */
	guint start_pos;
	guint end_pos;
	guint length;

	/* config stuff */
	gchar *module;
	gchar *suggest_mode;
	guint ignore_length;
	gchar *language_tag;
	gchar *character_set;
    };


    struct _BalsaSpellCheckClass {
	GtkFrameClass parent_class;

	void (*done_spell_check) (BalsaSpellCheck * spell_check);
    };

    guint balsa_spell_check_get_type(void);

/* argument setters */
    void balsa_spell_check_set_module(BalsaSpellCheck *, const gchar *);
    void balsa_spell_check_set_suggest_mode(BalsaSpellCheck *, const gchar *);
    void balsa_spell_check_set_ignore_length(BalsaSpellCheck *, guint);
    void balsa_spell_check_set_language(BalsaSpellCheck *, const gchar *);
    void balsa_spell_check_set_character_set(BalsaSpellCheck *, const gchar *);

/* function prototypes */
    GtkWidget *balsa_spell_check_new(void);
    GtkWidget *balsa_spell_check_new_with_text(GtkText * text);
    void balsa_spell_check_set_text(BalsaSpellCheck * spell_check, GtkText * text);
    void balsa_spell_check_set_font(BalsaSpellCheck * spell_check, GdkFont * font);
    void balsa_spell_check_start(BalsaSpellCheck * spell_check);
    void balsa_spell_check_finish(BalsaSpellCheck * spell_check, gboolean keep_changes);


#ifdef __cplusplus
}
#endif				/* __cplusplus */
#endif				/* __BALSA_SPELL_CHECK_H__ */
