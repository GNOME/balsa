#ifndef __IMAP_UTIL_H__
#define __IMAP_UTIL_H__ 1
/* libimap library.
 * Copyright (C) 2003-2004 Pawel Salek.
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

void imap_quote_string(char *dest, size_t dlen, const char *src);
void imap_unquote_string(char *s);
char* imap_next_word(char *s);
char* imap_skip_atom(char *s);

void lit_conv_to_base64(char *out, const char *in, 
                        size_t len, size_t olen);
int lit_conv_from_base64(char *out, const char *in);

#endif
