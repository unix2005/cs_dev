vmsv2_sources = [
  'main.c',
  'vmsv2-application.c',
  'vmsv2-window.c',
]

vmsv2_deps = [
  dependency('gtk4'),
]

vmsv2_sources += gnome.compile_resources('vmsv2-resources',
  'vmsv2.gresource.xml',
  c_name: 'vmsv2'
)

executable('vmsv2', vmsv2_sources,
  dependencies: vmsv2_deps,
       install: true,
)
