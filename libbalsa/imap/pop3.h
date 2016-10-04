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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
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

#define IMAP_ERROR pop_imap_error_quark()
GQuark pop_imap_error_quark(void);

/*
 * Error codes for GError: only one for now, more to come.
 */
enum {
  IMAP_POP_PROTOCOL_ERROR,
  IMAP_POP_CONNECT_ERROR,
  IMAP_POP_SEVERED_ERROR,
  IMAP_POP_AUTH_ERROR,
  IMAP_POP_SAVE_ERROR
};

typedef enum {
  IMAP_POP_OPT_DISABLE_APOP,
  IMAP_POP_OPT_FILTER_CR,
  IMAP_POP_OPT_OVER_SSL,
  IMAP_POP_OPT_PIPELINE /* disable pipelines for buggy servers */
} PopOption;


typedef struct PopHandle_ PopHandle;
typedef int (*PopUserCb)(PopHandle*, void*);
typedef void (*PopMonitorCb)(const char *buffer, int length, int direction,
			     void *arg);

PopHandle *pop_new         (void);
void     pop_set_option    (PopHandle *pop, PopOption opt, gboolean state);
ImapTlsMode pop_set_tls_mode(PopHandle *h, ImapTlsMode option);
void     pop_set_timeout   (PopHandle *pop, int milliseconds);
void     pop_set_monitorcb (PopHandle *pop, PopMonitorCb cb, void*);
void     pop_set_usercb    (PopHandle *pop, ImapUserCb user_cb, void *arg_cb);
void     pop_set_infocb    (PopHandle *pop, PopUserCb user_cb, void *arg_cb);
gboolean pop_connect       (PopHandle *pop, const char *host, GError **err);
unsigned pop_get_exists    (PopHandle *pop, GError **err);
unsigned pop_get_msg_size  (PopHandle *pop, unsigned msgno);
unsigned long pop_get_total_size(PopHandle *pop);
const char* pop_get_uid    (PopHandle *pop, unsigned msgno, GError **err);

gboolean pop_fetch_message_s(PopHandle *pop, unsigned msgno, 
			     int (*cb)(unsigned len, char*buf, void *arg),
			     void *cb_arg, GError **err);
gboolean pop_delete_message_s(PopHandle *pop, unsigned msgno, GError **err);
gboolean pop_destroy(PopHandle *pop, GError **err);

/* asynchronous interface's purpose is to enable pipelining usage
   whenever possible. The requests are queued with respective response
   handlers and the request queue is flushed now and then - or on a
   explicit request of obviously, before the destroy
   request. Generally, asynchronous request should not be interleaved
   with synchronous ones. The callback may be called several times for
   fetch request. The first one is always one of OK or ERR. If the
   first response is ERR, the callback should not hope any more. If
   the first response is OK, and the request is fetch, subsequent
   multiple calls with codes DATA and eveventally last one with DONE
   will follow.
*/
typedef enum {
  POP_REQ_OK,
  POP_REQ_ERR,
  POP_REQ_DATA,
  POP_REQ_DONE
}  PopReqCode;

typedef void (*PopAsyncCb)(PopReqCode prc, void *arg, ...);

void pop_fetch_message(PopHandle *pop, unsigned msgno, 
		       PopAsyncCb cb, void *cb_arg, GDestroyNotify dn);
void pop_delete_message(PopHandle *pop, unsigned msgno, 
		       PopAsyncCb cb, void *cb_arg, GDestroyNotify dn);
void pop_complete_pending_requests(PopHandle *pop);

#endif /* __POP_HANDLE_H__ */
