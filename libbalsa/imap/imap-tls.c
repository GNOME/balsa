/* libimap library.
 * Copyright (C) 2003-2013 Pawel Salek.
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

#include <string.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <openssl/err.h>

/* Support for SSLv3 should *not* be enabled as it is unsafe (see
 * <http://web.nvd.nist.gov/view/vuln/detail?vulnId=CVE-2014-3566> and
 * <http://web.nvd.nist.gov/view/vuln/detail?vulnId=CVE-2014-8730>.
 *
 * Uncomment the following line if you *really* want to enable SSLv3 support.
 * Otherwise, only the "safe" protocols TLS 1.0, TLS 1.1 and TLS 1.2 are used
 * (note that TLS 1.1 and TLS 1.2 support depends upon the OpenSSL version) */
/* #define ENABLE_SSL3 1 */

#include "siobuf.h"
#include "imap_private.h"

static SSL_CTX *global_ssl_context = NULL;
static GMutex global_tls_lock;

/* OpenSSL static locks */
static GMutex *mutexes = NULL;

static void
locking_function(int mode, int n, const char *file, int line)
{
  if(mode & CRYPTO_LOCK)
	  g_mutex_lock(&mutexes[n]);
  else
	  g_mutex_unlock(&mutexes[n]);
}

static unsigned long
id_function(void)
{
  return (unsigned long) g_thread_self();
}

/* OpenSSL dynamic locks */
struct CRYPTO_dynlock_value {
  GMutex mutex;
};

static struct CRYPTO_dynlock_value*
dyn_create_function(const char *file, int line)
{
  struct CRYPTO_dynlock_value *value =
    (struct CRYPTO_dynlock_value*)malloc(sizeof(struct CRYPTO_dynlock_value));

  if(!value)
    return NULL;
  g_mutex_init(&value->mutex);
  return value;
}

static void
dyn_lock_function(int mode, struct CRYPTO_dynlock_value *l,
                  const char *file, int line)
{
  if(mode & CRYPTO_LOCK)
	  g_mutex_lock(&l->mutex);
  else
	  g_mutex_unlock(&l->mutex);
}

static void
dyn_destroy_function(struct CRYPTO_dynlock_value *l,
                     const char *file, int line)
{
	g_mutex_clear(&l->mutex);
  free(l);
}

static int
imaptls_thread_setup(void)
{
  int i, mutex_cnt = CRYPTO_num_locks();

  mutexes = (GMutex*)malloc(mutex_cnt*sizeof(GMutex));
  if(!mutexes)
    return 0;
  for(i=0; i<mutex_cnt; i++)
	  g_mutex_init(&mutexes[i]);
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
	  g_mutex_clear(&mutexes[i]);
  free(mutexes); mutexes = NULL;
  return 1;
}
#endif /* DO_PROPER_OPENSSL_CLEANUP */

SSL*
imap_create_ssl(void)
{
  SSL *ssl;

  g_mutex_lock(&global_tls_lock);
  if(!global_ssl_context) {
    /* Initialize OpenSSL library, follow "Network Security with
       OpenSSL", ISBN 0-596-00270-X, guidelines. Example 4-2. */
    imaptls_thread_setup();
    SSL_library_init();
    SSL_load_error_strings();
#if 0
    global_ssl_context = SSL_CTX_new (TLSv1_client_method ());
#else
    /* Note: SSLv23_client_method() actually enables *all* protocols, including
     * SSLv(2|3) and TLSv1.(0|1|2), so we must switch all unsafe ones off */
    global_ssl_context = SSL_CTX_new (SSLv23_client_method ());
#ifdef ENABLE_SSL3
    SSL_CTX_set_options(global_ssl_context, SSL_OP_ALL|SSL_OP_NO_SSLv2);
#else
    SSL_CTX_set_options(global_ssl_context, SSL_OP_ALL|SSL_OP_NO_SSLv2|SSL_OP_NO_SSLv3);
#endif
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
  g_mutex_unlock(&global_tls_lock);

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
imap_check_server_identity(SSL *ssl, const char *host,
                           ImapUserCb user_cb, void *user_arg)
{
  long vfy_result;
  X509 *cert;
  X509_NAME *subj;
  int ok, host_len;
  gchar *colon;
  int has_extension_with_dns_name = 0;
  STACK_OF(GENERAL_NAME) *altnames;

  if(!host)
    return 0;
  /* Check whether the certificate matches the server. */
  cert = SSL_get_peer_certificate(ssl);
  if(!cert)
    return 0;

  colon = strchr(host, ':');
  host_len = colon ? colon - host : (int)strlen(host);
  ok = 0;

  altnames = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);

  if (altnames) {
    int i;

    for (i=0; i< sk_GENERAL_NAME_num(altnames); i++) {
      const GENERAL_NAME *name = sk_GENERAL_NAME_value(altnames, i);

      /* We handle only GEN_DNS. GEN_IP (certificates for IP numbers)
         are too weird to be real in IMAP case. */
      if (name->type == GEN_DNS) {
        const ASN1_IA5STRING *ia5 = name->d.ia5;
        const char *name = (const char*)ia5->data;
        has_extension_with_dns_name = 1;

        if (strlen(name) == (size_t)ia5->length &&
            host_matches_domain(host, name, host_len)) {
          /* printf("verified name using extension\n"); */
          ok = 1;
          break;
        }
      }
    }
    sk_GENERAL_NAME_pop_free(altnames, GENERAL_NAME_free);
  }

  if (!has_extension_with_dns_name) {
    char data[256];
    size_t name_len;
    if( (subj = X509_get_subject_name(cert)) &&
        (name_len = 
         X509_NAME_get_text_by_NID(subj, NID_commonName,
                                   data, sizeof(data)))){
      data[sizeof(data)-1] = 0;

      /* Remember to check whether there was no truncation or NUL
         characters embedded in the text. */
      if(name_len == strlen(data) &&
         host_matches_domain(host, data, host_len)) {
        ok =1;
        /* printf("verified name using common name\n"); */
      }
    }
  }

  X509_free(cert);
  if(ok)
    vfy_result = SSL_get_verify_result(ssl);
  else
    vfy_result = X509_V_ERR_APPLICATION_VERIFICATION;

  if(vfy_result == X509_V_OK)
    return 1;
  /* There was a problem with the verification, one has to leave it up
   * to the application what to do with this.
   */
  ok = 0;
  if(user_cb)
    user_cb(IME_TLS_VERIFY_ERROR, user_arg, &ok, 
            vfy_result, ssl);
  return ok;
}

static int
check_cipher_strength(SSL *ssl, ImapUserCb user_cb, void *user_arg)
{
  int ok, bits = SSL_get_cipher_bits(ssl, NULL);

  if (bits > 40) 
    return 1;
  ok = 0;
  if (user_cb != NULL)
    user_cb(IME_TLS_WEAK_CIPHER, user_arg,
                    bits, &ok);
  return ok;
}

int
imap_setup_ssl(struct siobuf *sio, const char* host, SSL *ssl,
               ImapUserCb user_cb, void *user_arg)
{
  if(ERR_peek_error()) {
    fprintf(stderr, "OpenSSL error in %s():\n", __FUNCTION__);
    ERR_print_errors_fp(stderr);
    fprintf(stderr, "\nEnd of print_errors\n");
  }
  if(sio_set_tlsclient_ssl (sio, ssl)) {
    if(!imap_check_server_identity(ssl, host, user_cb, user_arg)) {
      printf("Server identity not confirmed\n");
      return 0;
    }
    if(!check_cipher_strength(ssl, user_cb, user_arg)) {
      printf("Cipher too weak\n");
      return 0;
    }
    return 1;
  } else {
    printf("set_tlsclient failed for %s\n", host);
    return 0;
  }
}

ImapResponse
imap_handle_starttls(ImapMboxHandle *handle)
{
  ImapResponse rc;
  SSL *ssl;

  IMAP_REQUIRED_STATE1(handle, IMHS_CONNECTED, IMR_BAD);
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
  if(imap_setup_ssl(handle->sio, handle->host, ssl,
                    handle->user_cb, handle->user_arg)) {
    handle->using_tls = 1;
    handle->has_capabilities = 0;
    return IMR_OK;
  } else {
    /* ssl is owned now by sio, no need to free it SSL_free(ssl); */
    imap_handle_disconnect(handle);
    return IMR_NO;
  }
}
