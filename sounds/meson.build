# sounds/meson.build

balsa_sounds = ['startup.wav', 'newmail.wav']

balsa_sounds_dir = join_paths(get_option('datadir'), 'sounds', 'balsa')
install_data(balsa_sounds, install_dir : balsa_sounds_dir)

sound_list_dir = join_paths(get_option('sysconfdir'), 'sound', 'events')

i18n_merge_knows_description = false
if i18n_merge_knows_description
  i18n.merge_file(input       : 'balsa.soundlist.meson',
                  output      : 'balsa.soundlist',
                  type        : 'desktop',
                  po_dir      : '../po',
                  install     : true,
                  install_dir : sound_list_dir)
else
  custom_target('soundlist-file',
                input       : 'balsa.soundlist.meson',
                output      : 'balsa.soundlist',
                install     : true,
                install_dir : sound_list_dir,
                command: [
                  'msgfmt', '--desktop',
                  '--keyword', 'description',
                  '--template', '@INPUT@',
                  '-d', '../po',
                  '-o', '@OUTPUT@'
                  ])
endif
