# ─────────────────────────────────────────────────────────────────────────────
# po/meson.build
# ─────────────────────────────────────────────────────────────────────────────

# i18n.gettext() sets up:
#   • a 'hostnamed-pot' target that regenerates hostnamed.pot from POTFILES
#   • a 'hostnamed-update-po' target that merges the .pot into each .po file
#   • install rules for the compiled .mo files under <localedir>
#
# Add new language codes to LINGUAS and create the corresponding .po file;
# Meson picks them up automatically on the next configure.

i18n.gettext(
  gettext_package,
  args          : [
    '--add-comments=TRANSLATORS:',
    '--keyword=_',
    '--keyword=N_',
  ],
  preset        : 'glib',
  install       : true,
  install_dir   : localedir,
)
