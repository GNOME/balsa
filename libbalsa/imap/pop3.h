#ifndef __POP_HANDLE_H__
#define __POP_HANDLE_H__
/* libimap library.
 * Copyright (C) 2004 Pawel Salek.
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
/** 
   rfc1939 is the basic one.
   extensions are in 
   RFC 2449
   Our goal is to implement at least following extensions:
   CAPA, STLS, AUTH. SASL would be nice too but not now.
   
*/


#include <glib.h>

#include "libimap.h"
/*
 * Error domains for GError: only one for now, more to come.
 */

enum {
    IMAP_ERROR
};
 
/*
 * Error codes for GError: only one for now, more to come.
 */
enum {
  IMAP_POP_PROTOCOL_ERROR,
  IMAP_POP_CONNECT_ERROR,
  IMAP_POP_SEVERED_ERROR,
  IMAP_POP_AUTH_ERROR
};

typedef enum {
  IMAP_POP_OPT_DISABLE_APOP,
  IMAP_POP_OPT_FILTER_CR
} PopOption;


typedef struct PopHandle_ PopHandle;
typedef int (*PopUserCb)(PopHandle*, void*);
typedef void (*PopMonitorCb)(const char *buffer, int length, int direction,
			     void *arg);

PopHandle *pop_new         (void);
void     pop_set_option    (PopHandle *pop, PopOption opt, gboolean state);
void     pop_set_monitorcb (PopHandle *pop, PopMonitorCb cb, void*);
void     pop_set_usercb    (PopHandle *pop, ImapUserCb user_cb, void *arg_cb);
void     pop_set_infocb    (PopHandle *pop, PopUserCb user_cb, void *arg_cb);
gboolean pop_connect       (PopHandle *pop, const char *host, GError **err);
unsigned pop_get_exists    (PopHandle *pop, GError **err);
const char* pop_get_uid    (PopHandle *pop, unsigned msgno, GError **err);

gboolean pop_fetch_message (PopHandle *pop, unsigned msgno, 
                            void (*cb)(int len, char*buf, void *arg),
                            void *cb_arg, GError **err);
gboolean pop_delete_message(PopHandle *pop, unsigned msgno, GError **err);
gboolean pop_destroy(PopHandle *pop, GError **err);

#endif /* __POP_HANDLE_H__ */
