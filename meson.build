project(
  'goodhaptic',
  'c',
  version : '1.0'
)

gtk = dependency('gtk4')
adw = dependency('libadwaita-1')

# i18n
localedir = join_paths(get_option('prefix'), get_option('localedir'))
libexecdir = join_paths(get_option('prefix'), get_option('libexecdir'))

conf_data = configuration_data()
conf_data.set_quoted('GETTEXT_PACKAGE', 'goodhaptic')
conf_data.set_quoted('LOCALEDIR', localedir)
conf_data.set_quoted('libexecdir', libexecdir)
configure_file(
  input: 'i18n-config.h.in',
  output: 'i18n-config.h',
  configuration: conf_data
)

# systemd service file (path generated from build options)
configure_file(
  input: 'data/goodhapticd.service.in',
  output: 'goodhapticd.service',
  configuration: conf_data,
  install: true,
  install_dir: join_paths(get_option('prefix'), 'lib', 'systemd', 'system')
)

# Common sources without i18n (daemon)
common_sources = [
  'src/haptic.c',
  'src/config.c',
]

# GUI sources
gui_sources = [
  'src/main.c',
  'src/window.c',
] + common_sources

# Main GUI application
executable(
  'goodhaptic',
  gui_sources,
  dependencies : [
    gtk,
    adw
  ],
  install : true
)

# Privileged daemon (systemd service, runs as root)
executable(
  'goodhapticd',
  'src/daemon.c',
  common_sources,
  dependencies : [],
  install : true,
  install_dir : get_option('libexecdir')
)

# Desktop entry
install_data(
  'data/io.github.nwkyz.goodhaptic.desktop',
  install_dir : join_paths(get_option('datadir'), 'applications')
)

# Application icon
install_data(
  'icon.png',
  install_dir : join_paths(get_option('datadir'), 'icons', 'hicolor',
                           '256x256', 'apps'),
  rename : 'io.github.nwkyz.goodhaptic.png'
)


# Translations
subdir('po')

# Post-install: enable and start the daemon
meson.add_install_script('scripts/postinstall.sh')
