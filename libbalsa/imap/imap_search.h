#ifndef __IMAP_SEARCH_H__
#define __IMAP_SEARCH_H__ 1

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

#include "imap-handle.h"

typedef enum { IMSE_S_BCC, IMSE_S_BODY, IMSE_S_CC, IMSE_S_FROM, 
               IMSE_S_SUBJECT, IMSE_S_TEXT, IMSE_S_TO, 
               IMSE_S_HEADER } ImapSearchHeader;

typedef enum { IMSE_D_BEFORE = -1, IMSE_D_ON = 0,
               IMSE_D_SINCE  = 1 } ImapSearchDateRange;

typedef struct ImapSearchKey_ ImapSearchKey;

ImapSearchKey *imap_search_key_new_not(unsigned negated,
                                       ImapSearchKey *list);
ImapSearchKey *imap_search_key_new_or(unsigned negated,
                                      ImapSearchKey *a, ImapSearchKey *b);
ImapSearchKey *imap_search_key_new_flag(unsigned negated, ImapMsgFlag flg);
ImapSearchKey *imap_search_key_new_string(unsigned negated,
                                          ImapSearchHeader hdr,
                                          const char *string, 
                                          const char *user_header);
ImapSearchKey* imap_search_key_new_size_greater(unsigned negated, size_t sz);
ImapSearchKey* imap_search_key_new_date(ImapSearchDateRange range,
                                        int internal, time_t tm);
ImapSearchKey* imap_search_key_new_range(unsigned negated, int uid,
                                         unsigned lo, unsigned hi);
ImapSearchKey* imap_search_key_new_set(unsigned negated, int uid,
				       int cnt, unsigned *seqnos);

void imap_search_key_free(ImapSearchKey *s);
void imap_search_key_set_next(ImapSearchKey *list, ImapSearchKey *next);
ImapResponse imap_search_exec(ImapMboxHandle *h, gboolean uid,
                              ImapSearchKey *s, ImapSearchCb cb, void *cb_arg);

#endif
