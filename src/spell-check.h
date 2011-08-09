/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
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

#ifndef __BALSA_SPELL_CHECK_H__
#define __BALSA_SPELL_CHECK_H__

#include <gtk/gtk.h>

#define USE_ORIGINAL_MANAGER_FUNCS

#ifdef __cplusplus
extern "C" {
#endif				/* __cplusplus */

#define BALSA_TYPE_SPELL_CHECK \
        (balsa_spell_check_get_type ())
#define BALSA_SPELL_CHECK(obj) \
        G_TYPE_CHECK_INSTANCE_CAST (obj, BALSA_TYPE_SPELL_CHECK, BalsaSpellCheck)
#define BALSA_SPELL_CHECK_CLASS(klass) \
        G_TYPE_CHECK_CLASS_CAST (klass, BALSA_TYPE_SPELL_CHECK, \
                BalsaSpellCheckClass)
#define BALSA_IS_SPELL_CHECK(obj) \
        G_TYPE_CHECK_INSTANCE_TYPE (obj, BALSA_TYPE_SPELL_CHECK)
#define BALSA_IS_SPELL_CHECK_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE ((klass), BALSA_TYPE_SPELL_CHECK))


/* opaque structures, declared in spell-check.c */
    typedef struct _BalsaSpellCheck BalsaSpellCheck;
    typedef struct _BalsaSpellCheckClass BalsaSpellCheckClass;

    GType balsa_spell_check_get_type(void);

/* argument setters */
    void balsa_spell_check_set_language(BalsaSpellCheck *, const gchar *);

/* function prototypes */
    GtkWidget *balsa_spell_check_new(GtkWindow * parent);
    GtkWidget *balsa_spell_check_new_with_text(GtkWindow * parent,
                                               GtkTextView * view);
    void balsa_spell_check_set_text(BalsaSpellCheck * spell_check,
				    GtkTextView * view);
    void balsa_spell_check_start(BalsaSpellCheck * spell_check);


#ifdef __cplusplus
}
#endif				/* __cplusplus */
#endif				/* __BALSA_SPELL_CHECK_H__ */
