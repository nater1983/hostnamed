#!/usr/bin/env python3
"""Check daemon gettext initialization and Meson locale installation wiring."""
from pathlib import Path
import sys

main = Path(sys.argv[1]).read_text(encoding="utf-8")
meson = Path(sys.argv[2]).read_text(encoding="utf-8")
po_meson = Path(sys.argv[3]).read_text(encoding="utf-8")

def require(condition, message):
    if not condition:
        raise AssertionError(message)

locale_init = main.index('setlocale(LC_ALL, "");')
bind_domain = main.index("bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR)")
bind_codeset = main.index('bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8")')
set_domain = main.index("textdomain(GETTEXT_PACKAGE)")
require(locale_init < bind_domain < bind_codeset < set_domain,
        "daemon must initialize locale before binding gettext")
require("conf.set_quoted('LOCALEDIR',       localedir)" in meson,
        "Meson must configure the selected locale directory")
require("localedir  = join_paths(prefix, get_option('localedir'))" in meson,
        "configured locale directory must include the install prefix")
require("install_dir   : localedir" in po_meson,
        "compiled gettext catalogs must install under the configured locale directory")
print("gettext installation wiring: PASS")
