/** @file imap_tst.c tests some IMAP capabilities. It is very useful
    for stress-testing the imap part of balsa. */

#include <sys/ioctl.h>
#include <termios.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "libimap.h"
#include "imap-handle.h"
#include "imap-commands.h"

static void
monitor_cb(const char *buffer, int length, int direction, void *arg)
{
  int i;
  printf("IMAP %c: ", direction ? 'C' : 'S');
  for (i = 0; i < length; i++)
    putchar(buffer[i]);

  fflush(NULL);
}


static gchar*
get_user(const char* method) {
  char buf[256];
  size_t len;

  printf("Login with method %s as user: ", method);
  fflush(stdout);
  if(!fgets(buf, sizeof(buf), stdin))
    return NULL;

  len = strlen(buf);
  if(len>0 && buf[len-1] == '\n')
    buf[len-1] = '\0';

  printf("User: %s\n", buf);
  return g_strdup(buf);
}

static gchar*
get_password()
{
  char buf[255];
  int ttyfd = open ("/dev/tty", O_RDWR);
  int n;
  struct termios tty, oldtty;

  if(write (ttyfd, "Password: ", 10) != 10)
    return NULL;
  tcgetattr(ttyfd, &oldtty);
  tty = oldtty;
  tty.c_lflag &= ~ECHO;
  tcsetattr(ttyfd, TCSANOW, &tty);

  n = read(ttyfd, buf, sizeof(buf)-1);
  tcsetattr(ttyfd, TCSANOW, &oldtty);
  close (ttyfd);

  buf[n+1] = '\0';
  printf("A Password:%d %s %d\n", n, buf, buf[n]);
  while(n && !isgraph(buf[n]))
    buf[n--] = '\0';

  printf("B:Password: %d %s\n", n, buf);
  return g_strdup(buf);
}

static void
user_cb(ImapUserEventType ue, void *arg, ...)
{
    va_list alist;
    int *ok;
    va_start(alist, arg);
    switch(ue) {
    case IME_GET_USER_PASS: {
        gchar *method = va_arg(alist, gchar*);
        gchar **user = va_arg(alist, gchar**);
        gchar **pass = va_arg(alist, gchar**);
        ok = va_arg(alist, int*);
	g_free(*user); *user = get_user(method);
	g_free(*pass); *pass = get_password();
        *ok = 1; printf("Logging in with %s\n", method);
        break;
    }
    case IME_GET_USER:  { /* for eg kerberos */
        gchar **user;
        gchar *method;
        method = va_arg(alist, gchar*);
        user = va_arg(alist, gchar**);
        ok = va_arg(alist, int*);
        *ok = 1; /* consider popping up a dialog window here */
        g_free(*user); *user = get_user(method);
        break;
    }
    case IME_TLS_VERIFY_ERROR:
        ok = va_arg(alist, int*);
	*ok = 1;
        break;
    case IME_TLS_NO_PEER_CERT: {
        ok = va_arg(alist, int*); *ok = 0;
        printf("IMAP:TLS: Server presented no cert!\n");
        break;
    }
    case IME_TLS_WEAK_CIPHER: {
        ok = va_arg(alist, int*); *ok = 1;
        printf("IMAP:TLS: Weak cipher accepted.\n");
        break;
    }
    case IME_TIMEOUT: {
        ok = va_arg(alist, int*); *ok = 1;
        break;
    }
    default: g_warning("unhandled imap event type! Fix the code."); break;
    }
    va_end(alist);
}

/* =================================================================== 
   Test routines.
*/

/** a wrapper around imap_address_new allocating its args. */
#define imap_address_newa(a,b) imap_address_new(g_strdup(a),g_strdup(b))

/** Tests whether Envelope object is serialized and restored
    properly. */
static void
test_envelope_strings()
{
  ImapEnvelope *e1, *e2;
  gchar *e1str;

  e1 = imap_envelope_new();
  e1->date = 1234567;
  e1->subject = g_strdup("Test \"Subject");
  e1->from = imap_address_newa("FROM", "from@example.com");
  e1->sender = imap_address_newa("\"sender bender\"","sender@example.com");
  e1->replyto = imap_address_newa("", "replyto@kth.se");
  e1->to = imap_address_newa(NULL, "to@example.com");
  e1->cc = imap_address_newa("cc", "cc@example.com");
  e1->bcc = NULL;
  e1->in_reply_to = g_strdup("to-some-string");
  e1->message_id = g_strdup("Message\"id");

  e1str = imap_envelope_to_string(e1);
  e2 = imap_envelope_from_string(e1str);
  g_free(e1str);

  printf("DATE %10u %10u\n", (unsigned)e1->date, (unsigned)e2->date);
  printf("SUBJ %s | %s\n", e1->subject, e2->subject);
  printf("FROM %s %s | %s %s\n", e1->from->name, e1->from->addr_spec,
	 e2->from->name, e2->from->addr_spec);
  printf("SEND %s %s | %s %s\n", e1->sender->name, e1->sender->addr_spec,
	 e2->sender->name, e2->sender->addr_spec);
  printf("REPL %s %s | %s %s\n", e1->replyto->name, e1->replyto->addr_spec,
	 e2->replyto->name, e2->replyto->addr_spec);
  printf("TO %s %s | %s %s\n", e1->to->name, e1->to->addr_spec,
	 e2->to->name, e2->to->addr_spec);
  printf("CC %s %s | %s %s\n", e1->cc->name, e1->cc->addr_spec,
	 e2->cc->name, e2->cc->addr_spec);
  printf("BCC %p | %p\n", e1->bcc, e2->bcc);

  printf("INRT %s %s\n", e1->in_reply_to, e2->in_reply_to);
  printf("MSGI %s %s\n", e1->message_id, e2->message_id);
  imap_envelope_free(e1);
  imap_envelope_free(e2);
}

/** Tests whether the ImapBody structure can be serialized and
    restored. */
static gboolean
test_body_strings()
{
  ImapBody *b1, *b2;
  gchar *s;
  b1 = imap_body_new();

  b1->encoding = IMBENC_QUOTED;
  b1->media_basic = IMBMEDIA_TEXT;
  b1->media_basic_name = g_strdup("text");
  b1->media_subtype = g_strdup("plain");

  /* Test b1->params */
  b1->octets = 0xdeed;
  b1->lines = 0x1ea;
  b1->content_id = g_strdup("content_id");
  b1->desc = NULL;
  /* ImapEnvelope *envelope; used only if media/basic == MESSAGE */
  b1->content_dsp = IMBDISP_INLINE;
  b1->dsp_params =
    g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  g_hash_table_insert(b1->dsp_params, g_strdup("display"),
                      g_strdup("immediately"));
  b1->content_dsp_other = g_strdup("content_dsp_other");
  b1->content_uri = g_strdup("URI");

  b1->ext.onepart.md5 = g_strdup("MD5 checksum");
  b1->child = NULL;
  b1->next = NULL;

  s = imap_body_to_string(b1);
  imap_body_free(b1);
  printf("BODY1: %s\n", s);
  b2 = imap_body_from_string(s);
  g_free(s);
  s = imap_body_to_string(b2);
  printf("BODY2: %s\n", s);
  imap_body_free(b2);
  g_free(s);

  return TRUE;
}

/** Tests whether all messages from a test mailbox can be extracted
    from a mailbox. */
static gboolean
test_mbox_dump(const gchar *host, const gchar *mailbox)
{
  gboolean read_only;
  ImapMboxHandle *h;

  h = imap_mbox_handle_new();

  imap_handle_set_tls_mode(h, IMAP_TLS_DISABLED);
  if(0) imap_handle_set_monitorcb(h, monitor_cb, NULL);
  imap_handle_set_usercb(h,    user_cb, NULL);
  if(imap_mbox_handle_connect(h, host, FALSE) != IMAP_SUCCESS) {
    printf("Connection to %s failed.\n", host);
    return FALSE;
  }

  if(imap_mbox_select(h, mailbox, &read_only) == IMR_OK) {
    unsigned cnt = imap_mbox_handle_get_exists(h);
    unsigned i;
    FILE *fl = fopen("dump.mbox", "wt");
    if(!fl) {
      fprintf(stderr, "Cannot open dump.mbox for writing\n");
    } else {
#define FETCH_AT_ONCE 500
      for(i=0; i<cnt; i+= FETCH_AT_ONCE) {
	unsigned arr[FETCH_AT_ONCE], j;
	unsigned batch_length = i+FETCH_AT_ONCE > cnt ? cnt-i : FETCH_AT_ONCE;
	printf("Fetching %u:%u\n", i+1, i+batch_length);
	for(j=0; j<batch_length; j++) arr[j] = i+1+j;
	if( imap_mbox_handle_fetch_rfc822(h, batch_length, arr, fl) != IMR_OK) {
	  fprintf(stderr, "fetching %u:%u failed: %s\n", i+1,
		  i+batch_length,
		  imap_mbox_handle_get_last_msg(h));
	  break;
	}
      }
      fclose(fl);
    }
  }
  g_object_unref(h);
  return TRUE;
}

int main(int argc, char *argv[]) {
  g_type_init();

  test_envelope_strings();
  test_body_strings();
  if(argc>2) {
    test_mbox_dump(argv[1], argv[2]);
  } else {
    printf("Call as \"imap_tst host mailbox\" to dump messages from given mailbox.\n");
  }

  return 0;
}
