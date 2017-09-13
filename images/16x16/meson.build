extra_appicons_imgs = [
  'balsa-encrypted.png',
  'balsa-mark-all.png',
  'balsa-mbox-draft.png',
  'balsa-mbox-sent.png',
  'balsa-mbox-tray-empty.png',
  'balsa-mbox-tray-full.png',
  'balsa-next-flagged.png',
  'balsa-next-part.png',
  'balsa-next.png',
  'balsa-next-unread.png',
  'balsa-postpone.png',
  'balsa-previous-part.png',
  'balsa-previous.png',
  'balsa-reply-all.png',
  'balsa-send-queued.png',
  'balsa-signature-bad.png',
  'balsa-signature-good.png',
  'balsa-signature-notrust.png',
  'balsa-signature-unknown.png',
  'stock_mail-receive.png',
  'stock_contact.png',
  'stock_mail.png',
  'stock_book_red.png',
  'stock_book_open.png',
  'mail-queue.png',
  'mail-inbox.png',
  'mail-outbox.png'
  ]

extra_appiconsdir = join_paths(get_option('datadir'), 'balsa', 'hicolor', '16x16', 'apps')
foreach img : extra_appicons_imgs
  install_data(img, install_dir : extra_appiconsdir)
endforeach
