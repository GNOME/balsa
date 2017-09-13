extramimetypes_imgs = [
  'application-pgp-signature.png',
  'application-pkcs7-mime.png',
  'application-pkcs7-signature.png',
  'application-x-pkcs7-signature.png',
  'message-disposition-notification.png',
  'message-external-body.png',
  'message.png',
  'multipart-alternative.png',
  'multipart-encrypted.png',
  'multipart.png',
  'multipart-signed.png'
  ]

extramimetypesdir = join_paths(get_option('datadir'), 'balsa', 'hicolor', '48x48', 'mimetypes')
foreach img : extramimetypes_imgs
  install_data(img, install_dir : extramimetypesdir)
endforeach
