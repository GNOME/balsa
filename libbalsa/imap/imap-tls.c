/*
 * STARTTLS Command
 * see RFC2595, A. Appendix -- Compliance Checklist

   Rules (for client)                                    Section
   -----                                                 -------
   Mandatory-to-implement Cipher Suite                      2.1   OK
   SHOULD have mode where encryption required               2.2
   client MUST check server identity                        2.4
   client MUST use hostname used to open connection         2.4
   client MUST NOT use hostname from insecure remote lookup 2.4   OK
   client SHOULD support subjectAltName of dNSName type     2.4
   client SHOULD ask for confirmation or terminate on fail  2.4
   MUST check result of STARTTLS for acceptable privacy     2.5
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

#include <openssl/ssl.h>

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include "siobuf.h"
#include "imap_private.h"

static SSL_CTX *global_ssl_context = NULL;
#ifdef BALSA_USE_THREADS
static pthread_mutex_t global_tls_lock = PTHREAD_MUTEX_INITIALIZER;
#endif
static SSL*
create_ssl(void)
{
  SSL *ssl;

#ifdef BALSA_USE_THREADS
  pthread_mutex_lock(&global_tls_lock);
#endif
  if(!global_ssl_context) {
    SSL_load_error_strings();
    SSL_library_init();
    global_ssl_context = SSL_CTX_new (TLSv1_client_method ());
    /* no client certificate password support yet
     * SSL_CTX_set_default_passwd_cb (ctx, ctx_password_cb);
     * SSL_CTX_set_default_passwd_cb_userdata (ctx, ctx_password_cb_arg); 
     */
    /* Load the trusted CAs here */
  }
#ifdef BALSA_USE_THREADS
  pthread_mutex_unlock(&global_tls_lock);
#endif

  ssl = SSL_new(global_ssl_context);

  /* setup client certificates here */
  return ssl;
}


static int
check_server_identity(ImapMboxHandle *handle, SSL *ssl)
{
  long vfy_result;
  X509 *cert;
  int ok;

  vfy_result = SSL_get_verify_result (ssl);
  if(vfy_result == X509_V_OK)
    return 1;
  /* There was a problem with the verification, one has to leave it up to the
   * application what to do with this.
   */
   ok = 0;
   if(handle->user_cb)
     handle->user_cb(handle, IME_TLS_VERIFY_ERROR, handle->user_arg, &ok, 
                vfy_result, ssl);
  if(!ok)
    return ok;

  /* Check whether the certificate matches the server. */
  cert = SSL_get_peer_certificate(ssl);
  if (cert == NULL) {
    ok = 0;
    if(handle->user_cb)
      handle->user_cb(handle, IME_TLS_NO_PEER_CERT, handle->user_arg,
                      &ok, ssl);
    if(!ok)
      return ok;
  }
#if 0
  nm = X509_get_subject_name(cert);
  lastpos = -1;
  do {
    lastpos = X509_NAME_get_index_by_NID(nm, NID_commonName, lastpos);
  }
#endif
  X509_free (cert);
  return 1;
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
imap_handle_starttls(ImapMboxHandle *handle)
{
  ImapResponse rc;
  SSL *ssl;

  if(!imap_mbox_handle_can_do(handle, IMCAP_STARTTLS)) 
    return IMR_NO;


  ssl = create_ssl();
  if(!ssl) { 
    printf("ssl=%p ctx=%p\n", ssl, global_ssl_context);
    return IMR_NO;
  }

  if( (rc=imap_cmd_exec(handle, "StartTLS")) != IMR_OK)
    return rc;

  if(sio_set_tlsclient_ssl (handle->sio, ssl)) {
    handle->using_tls = 1;
    /* forget capabilities. */
    handle->has_capabilities = 0;
    if(!check_server_identity(handle, ssl)) {
      printf("Server identity not confirmed\n");
      /* close connection here is the best behavior */
      return IMR_NO;
    }
    if(!check_cipher_strength(handle, ssl)) {
      printf("Cipher too weak\n");
      /* close connection here is the best behavior */
      return IMR_NO;
    }

  }
  return rc;
}
#endif /* USE_TLS */
