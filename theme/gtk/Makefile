PREFIX ?= /usr/local
DATADIR ?= $(PREFIX)/share
THEMEDIR ?= $(DATADIR)/themes
BINDIR ?= $(PREFIX)/bin

.PHONY: all install uninstall generate-xfwm check check-ui
all:

check:
	python3 tests/check-xfwm.py
	python3 tests/check-payload.py

PREVIEW_DIR ?= /tmp/lunatic-theme-preview
check-ui:
	python3 tests/check-widgets.py --output "$(PREVIEW_DIR)"

generate-xfwm:
	python3 generate-xfwm.py

install:
	install -d "$(DESTDIR)$(THEMEDIR)/Ambiance"
	# Replace managed decorations so upgrades also remove obsolete generated files.
	rm -rf "$(DESTDIR)$(THEMEDIR)/Ambiance/xfwm4"
	cp -R Ambiance/. "$(DESTDIR)$(THEMEDIR)/Ambiance/"
	install -m644 COPYING COPYRIGHT "$(DESTDIR)$(THEMEDIR)/Ambiance/"
	install -Dm755 xfce-settings.py "$(DESTDIR)$(BINDIR)/lunatic-xfce-settings"

uninstall:
	rm -rf "$(DESTDIR)$(THEMEDIR)/Ambiance"
	rm -f "$(DESTDIR)$(BINDIR)/lunatic-xfce-settings"
