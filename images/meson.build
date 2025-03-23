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
  'balsa_attention.png',
  'balsa_logo.png'
  ]

balsa_bitmaps_dir = join_paths(get_option('datadir'), 'balsa', 'pixmaps')
install_data(balsa_imgs, install_dir : balsa_bitmaps_dir)

gtk_update_icon_cache_program = find_program('gtk-update-icon-cache', required : false)
if gtk_update_icon_cache_program.found()
  icon_cache_dir = join_paths(get_option('datadir'), 'balsa')
  mkdir_program = find_program('mkdir', required : false)
  if mkdir_program.found()
    run_command(mkdir_program, '-p', icon_cache_dir, check : false)
  endif # mkdir_program.found()
  gtk_update_icon_cache_script = join_paths(meson.current_build_dir(), 'gtk-update-icon-cache.sh')
  run_command('sh', '-c', 'echo "#!/bin/sh" > ' + gtk_update_icon_cache_script,
              check : false)
  run_command('sh', '-c', 'echo "gtk-update-icon-cache \$1 \${MESON_INSTALL_DESTDIR_PREFIX}/\$2" >> ' + gtk_update_icon_cache_script,
              check : false)
  meson.add_install_script(gtk_update_icon_cache_script, '--ignore-theme-index', icon_cache_dir)
endif # gtk_update_icon_cache_program.found()
