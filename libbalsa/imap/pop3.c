/* libimap library.
 * Copyright (C) 2004 Pawel Salek.
 * APop authentication routines are copyright (C) 2004 Balsa team,
 *                                 See the file AUTHORS for a list. 
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

/* The POP protocol is defined in RFC1939. The extensions are in
   RFC2449. There is also SASL RFC but we do not implement that yet.
 */
#include "config.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#if defined(USE_TLS)
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#include "pop3.h"
#include "siobuf.h"
#include "imap_private.h"
#include "md5-utils.h"

#define ELEMENTS(x) (sizeof (x) / sizeof(x[0]))

enum {
  POP_CAP_USER = 0,    /* RFC 1939 */
  POP_CAP_TOP,         /* RFC 1939 */
  POP_CAP_LOGIN_DELAY, /* RFC 2449 */
  POP_CAP_UIDL,        /* RFC 1939 */
  POP_CAP_PIPELINING,  /* RFC 2449 */
  POP_CAP_STLS,        /* RFC 2449 */
  /* AUTH and SASL are another kettle of fish and go at the end. */
  POP_CAP_AUTH_CRAM,   /* RFC 2195 */
  POP_CAP_SASL_ANON,   /* RFC 2245 */
  POP_CAP_SASL_PLAIN,  /* RFC 2595 +  draft-ietf-sasl-plain*/
  POP_CAP_SASL_LOGIN,  /* draft-murchison-sasl-login */
  POP_CAP_SASL_OTP,    /* RFC 2444 */
  POP_CAP_SASL_CRAM,   /* RFC 2195 */
  POP_CAP_SASL_DIGEST, /* RFC 2831 */
  POP_CAP_LAST
};

/* Max pop line length */
#define POP_LINE_LEN 513

static const char* capa_names[] = {
  "USER", "TOP", "LOGIN-DELAY", "UIDL", "PIPELINING", "STLS"
};

struct PopHandle_ {
  int sd;  /* socket descriptor */
  char capabilities[POP_CAP_LAST];
  struct siobuf *sio;
  gchar *host;
  ImapConnectionState state;

  unsigned      msg_cnt;
  unsigned long total_size;
  GPtrArray *uids;

  ImapMonitorCb monitor_cb;
  void*         monitor_arg;
  ImapUserCb    user_cb;
  void*         user_arg;
  /* various options */
  unsigned disable_apop:1;
  unsigned filter_cr:1;
  unsigned over_ssl:1;
};
#define pop_can_do(pop, cap) ((pop)->capabilities[cap])

PopHandle *
pop_new(void)
{
  return g_new0(PopHandle, 1);
}

void
pop_set_option(PopHandle *pop, PopOption opt, gboolean state)
{
  switch(opt) {
  case IMAP_POP_OPT_DISABLE_APOP: pop->disable_apop = state; break;
  case IMAP_POP_OPT_FILTER_CR   : pop->filter_cr    = state; break;
  case IMAP_POP_OPT_OVER_SSL    : pop->over_ssl     = state; break;
  }
}

void
pop_set_monitorcb(PopHandle *pop, PopMonitorCb cb, void *arg)
{
  pop->monitor_cb  = cb;
  pop->monitor_arg = arg;
}

void
pop_set_usercb(PopHandle *pop, ImapUserCb user_cb, void *arg_cb)
{
  pop->user_cb  = user_cb;
  pop->user_arg = arg_cb;
}

/* parses single-line; we could have used a buffer just as well. */
static gboolean
pop_check_status(PopHandle *pop, GError **err)
{
  char buf[POP_LINE_LEN];
  gboolean res;

  if(!sio_gets(pop->sio, buf, sizeof(buf))) {
    g_set_error(err, IMAP_ERROR, IMAP_POP_SEVERED_ERROR,
                "Connection severed");
    return FALSE;
  }
     
  if(strncmp(buf, "+OK", 3) == 0)
    res = TRUE;
  else if(strncmp(buf, "-ERR", 4) == 0)
    res = FALSE;
  else
    res = FALSE;

  if(!res) {
    buf[POP_LINE_LEN-1] = '\0';
    g_set_error(err, IMAP_ERROR, IMAP_POP_PROTOCOL_ERROR, buf);
  }

  return res;
}

static gboolean
pop_exec(PopHandle *pop, const char *cmd, GError **err)
{
  sio_write(pop->sio, cmd, strlen(cmd));
  sio_flush(pop->sio);
  return pop_check_status(pop, err);
}

/** pop_get_capa tries to get capabilities */
static void
pop_get_capa(PopHandle *pop, GError **err)
{
  char buf[POP_LINE_LEN];

  memset(pop->capabilities, '\0', sizeof(pop->capabilities));
  if(!pop_exec(pop, "CAPA\r\n", err)) {
    pop->capabilities[POP_CAP_USER] = 1;
    return;
  }

  while(sio_gets(pop->sio, buf, sizeof(buf)) &&
        strcmp(buf, ".\r\n")) {
    unsigned i;
    for(i=0; buf[i]; i++) buf[i] = toupper(buf[i]);
    if(strncmp(buf, "AUTH", 4) == 0) {
      /* FIXME: implement proper AUTH support */
      if(strstr(buf, " CRAM-MD5"))
        pop->capabilities[POP_CAP_AUTH_CRAM] = 1;
    } else if(strncmp(buf, "SASL", 4) == 0) {
      /* FIXME: implement SASL support */
    } else {
      unsigned i;
      for(i=0; i<ELEMENTS(capa_names); i++) {
        unsigned len = strlen(capa_names[i]);
        if(strncmp(buf, capa_names[i], len) == 0)
          pop->capabilities[i] = 1;
      }
    }
  }
}
/* ===================================================================
   AUTHENTICATION SECTION
   ===================================================================
*/

static ImapResult
pop_auth_cram(PopHandle *pop, const char *greeting)
{
  return IMAP_AUTH_UNAVAIL;
}

/* getApopStamp:
   Get the Server Timestamp for APOP authentication -kabir 
   return TRUE on success.
*/

static gboolean
get_apop_stamp(const char *greeting, char *stamp)
{
  char *start, *finish;

  start = strchr(greeting, '<');
  if (start) {
    finish = strchr(start, '>');
    if (finish) {
      strncpy(stamp, start, finish - start + 1);
      return TRUE;
    }
  } 
  return FALSE;
}

/* Compute the authentication hash to send to the server - kabir */

static void
compute_auth_hash(char *stamp, char *hash, const char *passwd)
{
  MD5Context mdContext;
  register unsigned char *dp;
  register char *cp;
  unsigned char *ep;
  unsigned char digest[16];
  
  md5_init(&mdContext);
  md5_update(&mdContext, (unsigned char *)stamp, strlen(stamp));
  md5_update(&mdContext, (unsigned char *)passwd, strlen(passwd));
  md5_final(&mdContext, digest);
  
  cp = hash;
  dp = digest;
  for(ep = dp + sizeof(digest)/sizeof(digest[0]); dp < ep; cp += 2) {
    sprintf(cp, "%02x", *dp);
    dp++;
  }
    
  *cp = '\0';
}

static ImapResult
pop_auth_apop(PopHandle *pop, const char *greeting)
{
  char stamp[POP_LINE_LEN], auth_hash[POP_LINE_LEN];
  char *user = NULL, *pass = NULL;
  int ok = 0;

  memset(stamp, '\0', sizeof(stamp));

  if( !get_apop_stamp(greeting, stamp) ) return IMAP_AUTH_UNAVAIL;

  if(pop->user_cb)
    pop->user_cb(IME_GET_USER_PASS, pop->user_arg,
                    "APOP", &user, &pass, &ok);
  if(!ok || user == NULL || pass == NULL)
    return IMAP_AUTH_FAILURE;

  compute_auth_hash(stamp, auth_hash, pass);
  g_snprintf(stamp, sizeof(stamp), "APop %s %s\r\n", user, auth_hash);
  g_free(user); g_free(pass);

  return pop_exec(pop, stamp, NULL) ? IMAP_SUCCESS : IMAP_AUTH_FAILURE;
}

static ImapResult
pop_auth_user(PopHandle *pop, const char *greeting)
{
  char *user = NULL, *pass = NULL;
  int ok = 0;
  char line[POP_LINE_LEN];

  if(!pop_can_do(pop, POP_CAP_USER))
    return IMAP_AUTH_UNAVAIL;

  if(pop->user_cb)
    pop->user_cb(IME_GET_USER_PASS, pop->user_arg,
                    "LOGIN", &user, &pass, &ok);
  if(!ok || user == NULL || pass == NULL)
    return IMAP_AUTH_FAILURE;

  g_snprintf(line, sizeof(line), "User %s\r\n", user);
  g_free(user); 
  if(!pop_exec(pop, line, NULL)) { /* RFC 1939: User is optional */
    g_free(line);
    return IMAP_AUTH_UNAVAIL;
  }
  g_snprintf (line, sizeof(line), "Pass %s\r\n", pass);
  g_free(pass);
  return pop_exec(pop, line, NULL) ? IMAP_SUCCESS : IMAP_AUTH_FAILURE;
}

typedef ImapResult (*PopAuthenticator)(PopHandle*, const char*);
/* ordered from strongest to weakest */
static const PopAuthenticator pop_authenticators_arr[] = {
  pop_auth_cram,
  pop_auth_apop,
  pop_auth_user,
  NULL
};


static gboolean
pop_authenticate(PopHandle *pop, const char *greeting, GError **err)
{
  const PopAuthenticator *authenticator;
  ImapResult r;

  if(g_strncasecmp(greeting, "+OK", 3) != 0) {
    g_set_error(err, IMAP_ERROR, IMAP_POP_AUTH_ERROR,
                "Server does not welcome us: %s", greeting);
    return FALSE;
  }

  for(authenticator = pop_authenticators_arr;
      *authenticator; authenticator++) {
    if ((r = (*authenticator)(pop, greeting)) 
        != IMAP_AUTH_UNAVAIL) {
      return r == IMAP_SUCCESS;
    }
  }
  g_set_error(err, IMAP_ERROR, IMAP_POP_AUTH_ERROR,
              "No authentication method available");
              
  return FALSE;
}

/* ===================================================================
   END OF AUTHENTICATION SECTION
   ===================================================================
*/

#ifdef USE_TLS
static gboolean
pop_stls(PopHandle *pop, GError **err)
{
  SSL *ssl = imap_create_ssl();

  if(!ssl)
    return FALSE;

  if(!pop_exec(pop, "STLS\r\n", err)) {
    SSL_free(ssl);
    return FALSE;
  }
  if(imap_setup_ssl(pop->sio, pop->host, ssl,
                    pop->user_cb, pop->user_arg)) {
    return TRUE;
  } else {
    SSL_free(ssl);
    sio_detach(pop->sio); pop->sio = NULL; close(pop->sd);
    pop->state = IMHS_DISCONNECTED;
    return FALSE;
  }
}
#endif


/** pop_connect connects and authenticates using usercb */
gboolean
pop_connect(PopHandle *pop, const char *host, GError **err)
{
  static const int SIO_BUFSZ=8192;
  static const char *service = "pop3";
  char line[POP_LINE_LEN];

#ifdef USE_TLS
  if(pop->over_ssl) service = "pop3s";
#endif

  g_free(pop->host);
  pop->host = g_strdup(host);

  pop->sd = imap_socket_open(pop->host, service);
  if(pop->sd<0) {
    g_set_error(err, IMAP_ERROR, IMAP_POP_CONNECT_ERROR,
                "Could not connect to %s", host); /* FIXME: translate */
    return FALSE;
  }
  /* Add buffering to the socket */
  pop->sio = sio_attach(pop->sd, pop->sd, SIO_BUFSZ);
  if (pop->sio == NULL) {
    close(pop->sd);
    g_set_error(err, IMAP_ERROR, IMAP_POP_CONNECT_ERROR,
                "Could not connect to %s", host); /* FIXME: translate */
    return FALSE;
  }
#ifdef USE_TLS
  if(pop->over_ssl) {
    SSL *ssl = imap_create_ssl();
    if(!ssl || !imap_setup_ssl(pop->sio, pop->host, ssl,
                               pop->user_cb, pop->user_arg)) {
      sio_detach(pop->sio); pop->sio = NULL; close(pop->sd);
      pop->state = IMHS_DISCONNECTED;
      g_set_error(err, IMAP_ERROR, IMAP_POP_CONNECT_ERROR,
                  "Could not set up SSL");
      return IMAP_UNSECURE;
    }
  }
#endif
  if(pop->monitor_cb) 
    sio_set_monitorcb(pop->sio, pop->monitor_cb, pop->monitor_arg);

  if(!sio_gets(pop->sio, line, sizeof(line))) { /* get initial greeting */
      g_set_error(err, IMAP_ERROR, IMAP_POP_CONNECT_ERROR,
                  "Did not get initial greeting.");
      return FALSE;
  }
  pop_get_capa(pop, err); /* ignore error here */
#ifdef USE_TLS
  if(pop_can_do(pop, POP_CAP_STLS)) {
    if(!pop_stls(pop, err))
      return FALSE;
  }
#endif
  
  if(!pop_authenticate(pop, line, err)) {
    sio_detach(pop->sio); pop->sio = NULL; close(pop->sd);
    pop->state = IMHS_DISCONNECTED;
    return FALSE;
  }
  /* get basic information about the mailbox */
  sio_write(pop->sio, "Stat\r\n", 6);
  sio_flush(pop->sio);
  if(sio_gets(pop->sio, line, sizeof(line)) && strncmp(line, "+OK", 3) == 0) {
    if( sscanf(line + 3, " %u %lu", &pop->msg_cnt, &pop->total_size) < 2 ) {
      g_set_error(err, IMAP_ERROR, IMAP_POP_PROTOCOL_ERROR,
                  "Server %s did not return message count: %s",
                  pop->host, line);
      return FALSE;
    }
  } else {
    g_set_error(err, IMAP_ERROR, IMAP_POP_PROTOCOL_ERROR,
                "Server %s did not respond to Stat command.",
                pop->host);
    return FALSE;
  }
  return TRUE;
}

unsigned
pop_get_exists(PopHandle *pop, GError **err)
{
  return pop->msg_cnt;
}

const char*
pop_get_uid(PopHandle *pop, unsigned msgno, GError **err)
{
  /* FIXME: should we check capabilities here? */
  if(!pop->uids) {
    char line[POP_LINE_LEN];
    unsigned curr_msgno = 1;
    if(!pop_exec(pop, "UIDL\r\n", err))
      return NULL;
    pop->uids = g_ptr_array_sized_new(pop->msg_cnt);
    while( sio_gets(pop->sio, line, sizeof(line)) &&
           strcmp(line, ".\r\n") ) {
      char* cr, *space = strchr(line, ' ');
      unsigned read_msgno = atoi(line);
      if(!space ||read_msgno != curr_msgno) /* Parsing error? */
        continue;

      if( (cr = strrchr(space, '\r')) || (cr = strrchr(space, '\n')))
        *cr = '\0';
      g_ptr_array_add(pop->uids, g_strdup(space+1));
      curr_msgno++;
    }
  }
  return g_ptr_array_index(pop->uids, msgno-1);
}

gboolean
pop_fetch_message(PopHandle *pop, unsigned msgno,
                  int (*cb)(unsigned len, char*buf, void *arg),
                  void *cb_arg, GError **err)
{
  gboolean resp;
  char line[POP_LINE_LEN];
  sio_printf(pop->sio, "RETR %u\r\n", msgno);
  sio_flush(pop->sio);
  resp = pop_check_status(pop, err);
  /* do here the fetch */
  if(resp) {
    while( sio_gets(pop->sio, line, sizeof(line)) &&
           strcmp(line, ".\r\n") ) {
      char *arg = line[0] == '.' ? line+1 : line;
      unsigned len = strlen(arg);
      if(pop->filter_cr && len>=2 && arg[len-2] == '\r') 
        arg[(--len)-1] = '\n';
      if(resp) 
        if(!cb(len, arg, cb_arg)) {
          g_set_error(err, IMAP_ERROR, IMAP_POP_SAVE_ERROR,
                      "Saving message failed.");
          resp = FALSE;
        }
    }
  }
  return resp;
}
gboolean
pop_delete_message(PopHandle *pop, unsigned msgno, GError **err)
{
  sio_printf(pop->sio, "DELE %u\r\n", msgno);
  sio_flush(pop->sio);
  return pop_check_status(pop, err);
}

gboolean
pop_destroy(PopHandle *pop, GError **err)
{
  gboolean res = pop_exec(pop, "Quit\r\n", err);

  if(pop->sio) {
    sio_detach(pop->sio); pop->sio = NULL;
    close(pop->sd);
  }
  if(pop->uids) {
    while(pop->uids->len) {
      char *s = g_ptr_array_index(pop->uids, pop->uids->len-1);
      g_ptr_array_remove_index(pop->uids, pop->uids->len-1);
      g_free(s);
    }
    g_ptr_array_free(pop->uids, TRUE);
  }
  g_free(pop);
  return res;
}
