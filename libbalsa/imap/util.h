#ifndef __IMAP_UTIL_H__
#define __IMAP_UTIL_H__ 1
/* libimap library.
 * Copyright (C) 2003-2016 Pawel Salek.
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <glib.h>

gchar *imap_quote_string(const gchar *src)
	G_GNUC_WARN_UNUSED_RESULT;
char* imap_next_word(char *s);
char* imap_skip_atom(char *s);

gchar* imap_mailbox_to_utf8(const char *src)
	G_GNUC_WARN_UNUSED_RESULT;
gchar* imap_utf8_to_mailbox(const char *src)
	G_GNUC_WARN_UNUSED_RESULT;

#endif
