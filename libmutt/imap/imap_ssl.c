/*
 * Copyright (C) 1999-2000 Tommi Komulainen <Tommi.Komulainen@iki.fi>
 * 
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 */

/* for SSL NO_* defines */
#include "config.h"

#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#undef _

#include <string.h>

#include "mutt.h"
#include "imap.h"
#include "imap_private.h"
#include "imap_socket.h"
#include "mutt_menu.h"
#include "mutt_curses.h"
#include "imap_ssl.h"

#if OPENSSL_VERSION_NUMBER >= 0x00904000L
#define READ_X509_KEY(fp, key)	PEM_read_X509(fp, key, NULL, NULL)
#else
#define READ_X509_KEY(fp, key)	PEM_read_X509(fp, key, NULL)
#endif

/* This is ugly, but as RAND_status came in on OpenSSL version 0.9.5
 * and the code has to support older versions too, this is seemed to
 * be cleaner way compared to having even uglier #ifdefs all around.
 */
#ifdef HAVE_RAND_STATUS
#define HAVE_ENTROPY()	(RAND_status() == 1)
#define GOT_ENTROPY()	return 0;
#else
static int needentropy = 1;
/* OpenSSL fills the entropy pool from /dev/urandom if it exists */
#define HAVE_ENTROPY()	(!access("/dev/urandom", R_OK) || !needentropy)
#define GOT_ENTROPY()	do { needentropy = 0; return 0; } while (0)
#endif

char *SslCertFile = NULL;
char *SslEntropyFile = NULL;

typedef struct _sslsockdata
{
  SSL_CTX *ctx;
  SSL *ssl;
  X509 *cert;
}
sslsockdata;

/* 
 * OpenSSL library needs to be fed with sufficient entropy. On systems
 * with /dev/urandom, this is done transparently by the library itself,
 * on other systems we need to fill the entropy pool ourselves.
 *
 * Even though only OpenSSL 0.9.5 and later will complain about the
 * lack of entropy, we try to our best and fill the pool with older
 * versions also. (That's the reason for the ugly #ifdefs and macros,
 * otherwise I could have simply #ifdef'd the whole ssl_init funcion)
 */
int ssl_init (void)
{
  char path[_POSIX_PATH_MAX], *file;

  if (HAVE_ENTROPY()) return 0;
  
  mutt_message (_("Filling entropy pool"));
  
  /* try egd */
#ifdef HAVE_RAND_EGD
  file = SslEntropyFile;
  if (file && RAND_egd(file) != -1)
    GOT_ENTROPY();
  file = getenv("EGDSOCKET");
  if (file && RAND_egd(file) != -1)
    GOT_ENTROPY();
  snprintf (path, sizeof(path), "%s/.entropy", NONULL(Homedir));
  if (RAND_egd(path) != -1)
    GOT_ENTROPY();
  if (RAND_egd("/tmp/entropy") != -1)
    GOT_ENTROPY();
#endif

  /* try some files */
  file = SslEntropyFile;
  if (!file || access(file, R_OK) == -1)
    file = getenv("RANDFILE");
  if (!file || access(file, R_OK) == -1) {
    snprintf (path, sizeof(path), "%s/.rnd", NONULL(Homedir));
    file = path;
  }
  if (access(file, R_OK) == 0) {
    if (RAND_load_file(file, 10240) >= 16)
      GOT_ENTROPY();
  }

  if (HAVE_ENTROPY()) return 0;

  mutt_error (_("Failed to find enough entropy on your system"));
  sleep (2);
  return -1;
}

void imap_set_ssl (IMAP_MBOX *mx)
{
  if (! (mx->flags & M_IMAP_PORT))
    mx->port = IMAP_SSL_PORT;
  mx->socktype = M_NEW_SSL_SOCKET;
  mx->flags |= M_IMAP_TYPE;
}

static int ssl_socket_open_err (CONNECTION *conn)
{
  mutt_error (_("SSL disabled due the lack of entropy"));
  sleep (2);
  return -1;
}

static int ssl_check_certificate (sslsockdata * data);

static int ssl_socket_read (CONNECTION * conn);
static int ssl_socket_write (CONNECTION * conn, const char *buf);
static int ssl_socket_open (CONNECTION * conn);
static int ssl_socket_close (CONNECTION * conn);

int ssl_socket_setup (CONNECTION * conn)
{
  if (ssl_init() < 0) {
    conn->open = ssl_socket_open_err;
    return -1;
  }

  conn->open	= ssl_socket_open;
  conn->read	= ssl_socket_read;
  conn->write	= ssl_socket_write;
  conn->close	= ssl_socket_close;

  return 0;
}

int ssl_socket_read (CONNECTION * conn)
{
  sslsockdata *data = conn->sockdata;
  return SSL_read (data->ssl, conn->inbuf, LONG_STRING);
}

int ssl_socket_write (CONNECTION * conn, const char *buf)
{
  sslsockdata *data = conn->sockdata;
  dprint (1, (debugfile, "ssl_socket_write():%s", buf));
  return SSL_write (data->ssl, buf, mutt_strlen (buf));
}

int ssl_socket_open (CONNECTION * conn)
{
  sslsockdata *data;
  int err;

  if (raw_socket_open (conn) < 0)
    return -1;

  data = (sslsockdata *) safe_calloc (1, sizeof (sslsockdata));
  conn->sockdata = data;

  SSL_library_init();
  data->ctx = SSL_CTX_new (SSLv23_client_method ());

  /* disable SSL protocols as needed */
  if (!option(OPTTLSV1)) 
  {
    SSL_CTX_set_options(data->ctx, SSL_OP_NO_TLSv1);
  }
  if (!option(OPTSSLV2)) 
  {
    SSL_CTX_set_options(data->ctx, SSL_OP_NO_SSLv2);
  }
  if (!option(OPTSSLV3)) 
  {
    SSL_CTX_set_options(data->ctx, SSL_OP_NO_SSLv3);
  }

  data->ssl = SSL_new (data->ctx);
  SSL_set_fd (data->ssl, conn->fd);

  if ((err = SSL_connect (data->ssl)) < 0)
  {
    ssl_socket_close (conn);
    return -1;
  }

  data->cert = SSL_get_peer_certificate (data->ssl);
  if (!data->cert)
  {
    mutt_error (_("Unable to get certificate from peer"));
    sleep (1);
    return -1;
  }

  if (!ssl_check_certificate (data))
  {
    ssl_socket_close (conn);
    return -1;
  }

  mutt_message (_("SSL connection using %s"), SSL_get_cipher (data->ssl));
  sleep (1);

  return 0;
}

int ssl_socket_close (CONNECTION * conn)
{
  sslsockdata *data = conn->sockdata;
  SSL_shutdown (data->ssl);

  X509_free (data->cert);
  SSL_free (data->ssl);
  SSL_CTX_free (data->ctx);

  return raw_socket_close (conn);
}



static char *x509_get_part (char *line, const char *ndx)
{
  static char ret[SHORT_STRING];
  char *c, *c2;

  strncpy (ret, _("Unknown"), sizeof (ret));

  c = strstr (line, ndx);
  if (c)
  {
    c += strlen (ndx);
    c2 = strchr (c, '/');
    if (c2)
      *c2 = '\0';
    strncpy (ret, c, sizeof (ret));
    if (c2)
      *c2 = '/';
  }

  return ret;
}

static void x509_fingerprint (char *s, int l, X509 * cert)
{
  unsigned char md[EVP_MAX_MD_SIZE];
  unsigned int n;
  int j;

  if (!X509_digest (cert, EVP_md5 (), md, &n))
  {
    snprintf (s, l, _("[unable to calculate]"));
  }
  else
  {
    for (j = 0; j < (int) n; j++)
    {
      char ch[8];
      snprintf (ch, 8, "%02X%s", md[j], (j % 2 ? " " : ""));
      strncat (s, ch, l);
    }
  }
}

static char *asn1time_to_string (ASN1_UTCTIME *tm)
{
  static char buf[64];
  BIO *bio;

  strncpy (buf, _("[invalid date]"), sizeof (buf));
  
  bio = BIO_new (BIO_s_mem());
  if (bio)
  {
    if (ASN1_TIME_print (bio, tm))
      (void) BIO_read (bio, buf, sizeof (buf));
    BIO_free (bio);
  }

  return buf;
}

static int check_certificate_by_signer (X509 *peercert)
{
  X509_STORE_CTX xsc;
  X509_STORE *ctx;
  int pass = 0;

  ctx = X509_STORE_new ();
  if (ctx == NULL) return 0;

  if (option (OPTSSLSYSTEMCERTS))
  {
    if (X509_STORE_set_default_paths (ctx))
      pass++;
    else
      dprint (2, (debugfile, "X509_STORE_set_default_paths failed\n"));
  }

  if (X509_STORE_load_locations (ctx, SslCertFile, NULL))
    pass++;
  else
    dprint (2, (debugfile, "X509_STORE_load_locations_failed\n"));

  if (pass == 0)
  {
    /* nothing to do */
    X509_STORE_free (ctx);
    return 0;
  }

  X509_STORE_CTX_init (&xsc, ctx, peercert, NULL);

  pass = (X509_verify_cert (&xsc) > 0);
#ifdef DEBUG
  if (! pass)
  {
    char buf[SHORT_STRING];
    int err;

    err = X509_STORE_CTX_get_error (&xsc);
    snprintf (buf, sizeof (buf), "%s (%d)", 
	X509_verify_cert_error_string(err), err);
    dprint (2, (debugfile, "X509_verify_cert: %s\n", buf));
  }
#endif
  X509_STORE_CTX_cleanup (&xsc);
  X509_STORE_free (ctx);

  return pass;
}

static int check_certificate_by_digest (X509 *peercert)
{
  unsigned char peermd[EVP_MAX_MD_SIZE];
  unsigned int peermdlen;
  X509 *cert = NULL;
  int pass = 0;
  FILE *fp;

  /* expiration check */
  if (X509_cmp_current_time (X509_get_notBefore (peercert)) >= 0)
  {
    dprint (2, (debugfile, "Server certificate is not yet valid\n"));
    mutt_error (_("Server certificate is not yet valid"));
    sleep (2);
    return 0;
  }
  if (X509_cmp_current_time (X509_get_notAfter (peercert)) <= 0)
  {
    dprint (2, (debugfile, "Server certificate has expired"));
    mutt_error (_("Server certificate has expired"));
    sleep (2);
    return 0;
  }

  if ((fp = fopen (SslCertFile, "rt")) == NULL)
    return 0;

  if (!X509_digest (peercert, EVP_sha1(), peermd, &peermdlen))
  {
    fclose (fp);
    return 0;
  }
  
  while ((cert = READ_X509_KEY (fp, &cert)) != NULL)
  {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int mdlen;

    /* Avoid CPU-intensive digest calculation if the certificates are
     * not even remotely equal.
     */
    if (X509_subject_name_cmp (cert, peercert) != 0 ||
	X509_issuer_name_cmp (cert, peercert) != 0)
      continue;

    if (!X509_digest (cert, EVP_sha1(), md, &mdlen) || peermdlen != mdlen)
      continue;
    
    if (memcmp(peermd, md, mdlen) != 0)
      continue;

    pass = 1;
    break;
  }
  X509_free (cert);
  fclose (fp);

  return pass;
}

static int ssl_check_certificate (sslsockdata * data)
{
  char *part[] =
  {"/CN=", "/Email=", "/O=", "/OU=", "/L=", "/ST=", "/C="};
  char helpstr[SHORT_STRING];
  char buf[SHORT_STRING];
  MUTTMENU *menu;
  int done, row, i;
  FILE *fp;
  char *name = NULL, *c;

  if (check_certificate_by_signer (data->cert))
  {
    dprint (1, (debugfile, "ssl_check_certificate: signer check passed\n"));
    return 1;
  }

  /* automatic check from user's database */
  if (SslCertFile && check_certificate_by_digest (data->cert))
  {
    dprint (1, (debugfile, "ssl_check_certificate: digest check passed\n"));
    return 1;
  }

  /* interactive check from user */
  menu = mutt_new_menu ();
  menu->max = 19;
  menu->dialog = (char **) safe_calloc (1, menu->max * sizeof (char *));
  for (i = 0; i < menu->max; i++)
    menu->dialog[i] = (char *) safe_calloc (1, SHORT_STRING * sizeof (char));

  row = 0;
  strncpy (menu->dialog[row++], _("This certificate belongs to:"), SHORT_STRING);
  name = X509_NAME_oneline (X509_get_subject_name (data->cert),
			    buf, sizeof (buf));
  for (i = 0; i < 5; i++)
  {
    c = x509_get_part (name, part[i]);
    snprintf (menu->dialog[row++], SHORT_STRING, "   %s", c);
  }

  row++;
  strncpy (menu->dialog[row++], _("This certificate was issued by:"), SHORT_STRING);
  name = X509_NAME_oneline (X509_get_issuer_name (data->cert),
			    buf, sizeof (buf));
  for (i = 0; i < 5; i++)
  {
    c = x509_get_part (name, part[i]);
    snprintf (menu->dialog[row++], SHORT_STRING, "   %s", c);
  }

  row++;
  snprintf (menu->dialog[row++], SHORT_STRING, _("This certificate is valid"));
  snprintf (menu->dialog[row++], SHORT_STRING, _("   from %s"), 
      asn1time_to_string (X509_get_notBefore (data->cert)));
  snprintf (menu->dialog[row++], SHORT_STRING, _("     to %s"), 
      asn1time_to_string (X509_get_notAfter (data->cert)));

  row++;
  buf[0] = '\0';
  x509_fingerprint (buf, sizeof (buf), data->cert);
  snprintf (menu->dialog[row++], SHORT_STRING, _("Fingerprint: %s"), buf);

  menu->title = _("SSL Certificate check");
  if (SslCertFile)
  {
    menu->prompt = _("(r)eject, accept (o)nce, (a)ccept always");
    menu->keys = _("roa");
  }
  else
  {
    menu->prompt = _("(r)eject, accept (o)nce");
    menu->keys = _("ro");
  }
  
  helpstr[0] = '\0';
  mutt_make_help (buf, sizeof (buf), _("Exit  "), MENU_GENERIC, OP_EXIT);
  strncat (helpstr, buf, sizeof (helpstr));
  mutt_make_help (buf, sizeof (buf), _("Help"), MENU_GENERIC, OP_HELP);
  strncat (helpstr, buf, sizeof (helpstr));
  menu->help = helpstr;

  done = 0;
  while (!done)
  {
    switch (mutt_menuLoop (menu))
    {
      case -1:			/* abort */
      case OP_MAX + 1:		/* reject */
      case OP_EXIT:
        done = 1;
        break;
      case OP_MAX + 3:		/* accept always */
        done = 0;
        if ((fp = fopen (SslCertFile, "a")))
	{
	  if (PEM_write_X509 (fp, data->cert))
	    done = 1;
	  fclose (fp);
	}
	if (!done)
	  mutt_error (_("Warning: Couldn't save certificate"));
	else
	  mutt_message (_("Certificate saved"));
	sleep (1);
        /* fall through */
      case OP_MAX + 2:		/* accept once */
        done = 2;
        break;
    }
  }
  mutt_menuDestroy (&menu);
  return (done == 2);
}
