/*
 * STARTTLS Command
 * see RFC2595.
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

  pthread_mutex_lock(&global_tls_lock);
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
  pthread_mutex_unlock(&global_tls_lock);

  ssl = SSL_new(global_ssl_context);

  /* setup client certificates here */
  return ssl;
}


static int
verify_security(ImapMboxHandle *handle, SSL *ssl)
{
  X509 *cert;
  long vfy_result;

  vfy_result = SSL_get_verify_result (ssl);
  printf("vfy_result = %ld OK=%ld\n", vfy_result, (long)X509_V_OK);
  /* Check server credentials stored in the certificate.
   */
  cert = SSL_get_peer_certificate (ssl);
  if (cert != NULL)
    {
      printf("Could get the server certificate\n");
      X509_free (cert);
    } else {
      printf("Could NOT get the server certificate\n");
    }
  return 1; /* trust the server */
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
    if(!verify_security(handle, ssl)) {
      printf("SSL setup not trusted! Abort!\n");
      /* close connection here is the best behavior */
      return IMR_NO;
    }
  }
  return rc;
}
#endif /* USE_TLS */
