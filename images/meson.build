# images/meson.build

subdirs = ['mimetypes', '16x16', '24x24']
foreach dir : subdirs
  subdir(dir)
endforeach

# balsa_logo.png used by About menu (small one).
balsa_imgs = [
  'attachment.png',
  'balsa-top.png',
  'balsa_icon.png',
  'balsa_logo.png'
  ]

balsabitmapsdir = join_paths(get_option('datadir'), 'balsa', 'pixmaps')
install_data(balsa_imgs, install_dir : balsabitmapsdir)

if gtk_update_icon_cache_program.found()
  icon_cache_dir = join_paths(get_option('prefix'), get_option('datadir'), 'balsa')
  result = run_command(gtk_update_icon_cache_program, '--ignore-theme-index', icon_cache_dir,
                       check : false)
  if result.returncode() != 0
    warning(result.stderr())
  endif # result.returncode() != 0
endif # gtk_update_icon_cache_program.found()
