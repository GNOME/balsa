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
/*
 * STARTTLS Command
 * see RFC2595, A. Appendix -- Compliance Checklist

   Rules (for client)                                    Section
   -----                                                 -------
   Mandatory-to-implement Cipher Suite                      2.1   OK
   SHOULD have mode where encryption required               2.2   OK
   client MUST check server identity                        2.4   
   client MUST use hostname used to open connection         2.4   OK
   client MUST NOT use hostname from insecure remote lookup 2.4   OK
   client SHOULD support subjectAltName of dNSName type     2.4   OK
   client SHOULD ask for confirmation or terminate on fail  2.4   OK
   MUST check result of STARTTLS for acceptable privacy     2.5   OK
   client MUST NOT issue commands after STARTTLS
      until server response and negotiation done        3.1,4,5.1 OK
   client MUST discard cached information             3.1,4,5.1,9 OK
   client SHOULD re-issue CAPABILITY/CAPA command       3.1,4     OK
   IMAP client MUST NOT issue LOGIN if LOGINDISABLED        3.2   OK

   client SHOULD warn when session privacy not active and/or
     refuse to proceed without acceptable security level    9
   SHOULD be configurable to refuse weak mechanisms or
     cipher suites                                          9
 */

#include "config.h"

#ifdef USE_TLS

#include <string.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <openssl/err.h>

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include "siobuf.h"
#include "imap_private.h"

static SSL_CTX *global_ssl_context = NULL;
#ifdef BALSA_USE_THREADS
static pthread_mutex_t global_tls_lock = PTHREAD_MUTEX_INITIALIZER;

/* provide support only for _POSIX_THREADS */
#define MUTEX_TYPE pthread_mutex_t
#define MUTEX_SETUP(m)   pthread_mutex_init (&(m), NULL)
#define MUTEX_CLEANUP(m) pthread_mutex_destroy(&(m))
#define MUTEX_LOCK(m)    pthread_mutex_lock(&(m))
#define MUTEX_UNLOCK(m)  pthread_mutex_unlock(&(m))
#define THREAD_ID        pthread_self()

/* OpenSSL static locks */
static MUTEX_TYPE *mutexes = NULL;

static void
locking_function(int mode, int n, const char *file, int line)
{
  if(mode & CRYPTO_LOCK)
    MUTEX_LOCK(mutexes[n]);
  else
    MUTEX_UNLOCK(mutexes[n]);
}

static unsigned long
id_function(void)
{
  return (unsigned long)THREAD_ID;
}

/* OpenSSL dynamic locks */
struct CRYPTO_dynlock_value {
  MUTEX_TYPE mutex;
};

static struct CRYPTO_dynlock_value*
dyn_create_function(const char *file, int line)
{
  struct CRYPTO_dynlock_value *value =
    (struct CRYPTO_dynlock_value*)malloc(sizeof(struct CRYPTO_dynlock_value));

  if(!value)
    return NULL;
  MUTEX_SETUP(value->mutex);
  return value;
}

static void
dyn_lock_function(int mode, struct CRYPTO_dynlock_value *l,
                  const char *file, int line)
{
  if(mode & CRYPTO_LOCK)
    MUTEX_LOCK(l->mutex);
  else
    MUTEX_UNLOCK(l->mutex);
}

static void
dyn_destroy_function(struct CRYPTO_dynlock_value *l,
                     const char *file, int line)
{
  MUTEX_CLEANUP(l->mutex);
  free(l);
}

static int
imaptls_thread_setup(void)
{
  int i, mutex_cnt = CRYPTO_num_locks();

  mutexes = (MUTEX_TYPE*)malloc(mutex_cnt*sizeof(MUTEX_TYPE));
  if(!mutexes)
    return 0;
  for(i=0; i<mutex_cnt; i++)
    MUTEX_SETUP(mutexes[i]);
  CRYPTO_set_id_callback(id_function);
  CRYPTO_set_locking_callback(locking_function);
  
  /* dynamic locking for future releases of OpenSSL */
  CRYPTO_set_dynlock_create_callback (dyn_create_function);
  CRYPTO_set_dynlock_lock_callback   (dyn_lock_function);
  CRYPTO_set_dynlock_destroy_callback(dyn_destroy_function);
  return 1;
}

#if defined(DO_PROPER_OPENSSL_CLEANUP)
static int
imaptls_thread_cleanup(void)
{
  int i, mutex_cnt = CRYPTO_num_locks();
  if(!mutexes)
    return 0;
  CRYPTO_set_id_callback(NULL);
  CRYPTO_set_locking_callback(NULL);
  CRYPTO_set_dynlock_create_callback(NULL);
  CRYPTO_set_dynlock_lock_callback(NULL);
  CRYPTO_set_dynlock_destroy_callback(NULL);
  for(i=0; i<mutex_cnt; i++)
    MUTEX_CLEANUP(mutexes[i]);
  free(mutexes); mutexes = NULL;
  return 1;
}
#endif /* DO_PROPER_OPENSSL_CLEANUP */

#else /* BALSA_USE_THREADS */
static void
imaptls_thread_setup(void)
{
}
#endif /* BALSA_USE_THREADS */

SSL*
imap_create_ssl(void)
{
  SSL *ssl;

#ifdef BALSA_USE_THREADS
  pthread_mutex_lock(&global_tls_lock);
#endif
  if(!global_ssl_context) {
    /* Initialize OpenSSL library, follow "Network Security with
       OpenSSL", ISBN 0-596-00270-X, guidelines. Example 4-2. */
    imaptls_thread_setup();
    SSL_library_init();
    SSL_load_error_strings();
#if 1
    global_ssl_context = SSL_CTX_new (TLSv1_client_method ());
#else
    /* we could also enable SSLv3 but it doe not work very well with 
     * all servers. */
    global_ssl_context = SSL_CTX_new (SSLv23_client_method ());
    SSL_CTX_set_options(global_ssl_context, SSL_OP_ALL|SSL_OP_NO_SSLv2);
#endif
    /* no client certificate password support yet
     * SSL_CTX_set_default_passwd_cb (ctx, ctx_password_cb);
     * SSL_CTX_set_default_passwd_cb_userdata (ctx, ctx_password_cb_arg); 
     */

    /* Load the trusted CAs here. We just trust the system CA for the
       moment but we could try to be compatible with libESMTP in this
       respect.
    */
   SSL_CTX_set_default_verify_paths (global_ssl_context);
  }
#ifdef BALSA_USE_THREADS
  pthread_mutex_unlock(&global_tls_lock);
#endif

  ssl = SSL_new(global_ssl_context);

  /* setup client certificates here */
  return ssl;
}

/* check_server_identity() follows example 5-8 in the OpenSSL book. */
static int
host_matches_domain(const char* host, const char*domain, int host_len)
{ 
  if(!domain || !host)
    return 0;
  if(domain[0] == '*') { /* RFC2595, section 2.4 */
    int domain_len = strlen(domain);
    if(domain_len<2 || domain[1] != '.') /* wrong wildcard format! */
      return 0;
    if(host_len - domain_len + 2<= 0) /* host much shorter than wildcard */
      return 0;
    return g_ascii_strcasecmp(host + host_len - domain_len+2, 
                              domain+2) == 0;
  } else 
    return g_ascii_strncasecmp(host, domain, host_len) == 0;
}

static int
check_server_identity(ImapMboxHandle *handle, SSL *ssl)
{
  long vfy_result;
  X509 *cert;
  X509_NAME *subj;
  int ok, extcount, i, j, stack_len, host_len;
  gchar *colon;
  
  if(!handle->host)
    return 0;
  /* Check whether the certificate matches the server. */
  cert = SSL_get_peer_certificate(ssl);
  if(!cert)
    return 0;

  colon = strchr(handle->host, ':');
  host_len = colon ? colon - handle->host : (int)strlen(handle->host);
  ok = 0;
  extcount = X509_get_ext_count(cert);
  for(i=0; i<extcount; i++) {
    const char *extstr;
    X509_EXTENSION *ext = X509_get_ext(cert, i);

    extstr = OBJ_nid2sn(OBJ_obj2nid(X509_EXTENSION_get_object(ext)));

    if(strcmp(extstr, "subjectAltName") == 0) {
      unsigned char *data;
      STACK_OF(CONF_VALUE) *val;
      CONF_VALUE           *nval;
      X509V3_EXT_METHOD    *meth;

      if( !(meth = X509V3_EXT_get(ext)) )
        break;
      data = ext->value->data;

      val = meth->i2v(meth,
                      meth->d2i(NULL, &data, ext->value->length),
                      NULL);

      stack_len = sk_CONF_VALUE_num(val);
      for(j=0; j<stack_len; j++) {
        nval = sk_CONF_VALUE_value(val, j);
        if(strcmp(nval->name, "DNS") == 0 &&
           host_matches_domain(handle->host, nval->value, host_len)) {
          ok = 1;
          break;
        }
      }
    }
    if(ok)
      break;
  }
  if(!ok) { /* matching by subjectAltName failed, try commonName */
    char data[256];
    if( (subj = X509_get_subject_name(cert)) &&
        X509_NAME_get_text_by_NID(subj, NID_commonName, data, sizeof(data))>0){
      data[sizeof(data)-1] = 0;
      if(host_matches_domain(handle->host, data, host_len))
        ok =1;
    }
  }
  X509_free(cert);
  if(ok)
    vfy_result = SSL_get_verify_result(ssl);
  else
    vfy_result = X509_V_ERR_APPLICATION_VERIFICATION;

  if(vfy_result == X509_V_OK)
    return 1;
  /* There was a problem with the verification, one has to leave it up to the
   * application what to do with this.
   */
  ok = 0;
  if(handle->user_cb)
    handle->user_cb(handle, IME_TLS_VERIFY_ERROR, handle->user_arg, &ok, 
                    vfy_result, ssl);
  return ok;
}

static int
check_cipher_strength(ImapMboxHandle *handle, SSL *ssl)
{
  int ok, bits = SSL_get_cipher_bits (ssl, NULL);

  if (bits > 40) 
    return 1;
  ok = 0;
  if (handle->user_cb != NULL)
    handle->user_cb(handle, IME_TLS_WEAK_CIPHER, handle->user_arg,
                    bits, &ok);
  return ok;
}

ImapResponse
imap_handle_setup_ssl(ImapMboxHandle *handle, SSL *ssl)
{
  if(ERR_peek_error()) {
    fprintf(stderr, "OpenSSL error in %s():\n", __FUNCTION__);
    ERR_print_errors_fp(stderr);
    fprintf(stderr, "\nEnd of print_errors\n");
  }
  if(sio_set_tlsclient_ssl (handle->sio, ssl)) {
    handle->using_tls = 1;
    /* forget capabilities. */
    handle->has_capabilities = 0;
    if(!check_server_identity(handle, ssl)) {
      printf("Server identity not confirmed\n");
      sio_detach(handle->sio); handle->sio = NULL; close(handle->sd);
      handle->state = IMHS_DISCONNECTED;
      return IMR_NO;
    }
    if(!check_cipher_strength(handle, ssl)) {
      printf("Cipher too weak\n");
      sio_detach(handle->sio); handle->sio = NULL; close(handle->sd);
      handle->state = IMHS_DISCONNECTED;
      return IMR_NO;
    }
    return IMR_OK;
  } else {
    printf("set_tlscliend failed!\n");
    return IMR_NO;
  }
}

ImapResponse
imap_handle_starttls(ImapMboxHandle *handle)
{
  ImapResponse rc;
  SSL *ssl;

  if(!imap_mbox_handle_can_do(handle, IMCAP_STARTTLS)) 
    return IMR_NO;


  ssl = imap_create_ssl();
  if(!ssl) { 
    printf("ssl=%p ctx=%p\n", ssl, global_ssl_context);
    return IMR_NO;
  }

  if( (rc=imap_cmd_exec(handle, "StartTLS")) != IMR_OK) {
    SSL_free(ssl);
    return rc;
  }
  return imap_handle_setup_ssl(handle, ssl);
}
#endif /* USE_TLS */
