CC ?= cc
PKGS = gtk+-3.0 gio-unix-2.0 x11 xi xfixes xrender
CPPFLAGS += $(shell pkg-config --cflags $(PKGS))
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wno-overlength-strings -Wformat=2
LDLIBS += $(shell pkg-config --libs $(PKGS)) -lm
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share

.PHONY: all clean install uninstall check check-renderer check-renderer-parity check-upstream check-barriers bake-assets
build/assets.c: data/lunatic.gresource.xml $(wildcard data/assets/*)
	mkdir -p build
	glib-compile-resources --generate-source --c-name=lunatic_assets --sourcedir=data --target=$@ $<
.DEFAULT_GOAL := all
all: lunatic-launcher
lunatic-launcher: src/main.c src/application-launch.c src/application-launch.h src/dash.c src/dash.h src/desktop.c src/desktop.h src/settings.c src/settings.h src/launcher-core.c src/launcher-core.h src/quicklist.c src/quicklist.h src/renderer.c src/renderer-software.inc src/renderer.h build/assets.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ src/main.c src/application-launch.c src/dash.c src/desktop.c src/settings.c src/launcher-core.c src/quicklist.c src/renderer.c build/assets.c $(LDLIBS)
tests/test-core: tests/test-core.c src/launcher-core.c src/launcher-core.h src/desktop.c src/desktop.h src/settings.c src/settings.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Isrc -o $@ tests/test-core.c src/launcher-core.c src/desktop.c src/settings.c $(LDLIBS)
tests/test-model: tests/test-model.c src/main.c src/application-launch.c src/application-launch.h src/dash.c src/dash.h src/desktop.c src/desktop.h src/settings.c src/settings.h src/launcher-core.c src/launcher-core.h src/quicklist.c src/quicklist.h src/renderer.c src/renderer-software.inc src/renderer.h build/assets.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Isrc -o $@ tests/test-model.c src/application-launch.c src/dash.c src/desktop.c src/settings.c src/launcher-core.c src/quicklist.c src/renderer.c build/assets.c $(LDLIBS)
tests/test-tracking: tests/test-tracking.c src/main.c src/application-launch.c src/application-launch.h src/dash.c src/dash.h src/desktop.c src/desktop.h src/settings.c src/settings.h src/launcher-core.c src/launcher-core.h src/quicklist.c src/quicklist.h src/renderer.c src/renderer-software.inc src/renderer.h build/assets.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Isrc -o $@ tests/test-tracking.c src/application-launch.c src/dash.c src/desktop.c src/settings.c src/launcher-core.c src/quicklist.c src/renderer.c build/assets.c $(LDLIBS)
tests/test-barrier: tests/test-barrier.c src/main.c src/application-launch.c src/application-launch.h src/dash.c src/dash.h src/desktop.c src/desktop.h src/settings.c src/settings.h src/launcher-core.c src/launcher-core.h src/quicklist.c src/quicklist.h src/renderer.c src/renderer-software.inc src/renderer.h build/assets.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Isrc -o $@ tests/test-barrier.c src/application-launch.c src/dash.c src/desktop.c src/settings.c src/launcher-core.c src/quicklist.c src/renderer.c build/assets.c $(LDLIBS) $(shell pkg-config --libs xtst)
tests/test-dash: tests/test-dash.c src/dash.c src/dash.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Isrc -o $@ tests/test-dash.c $(LDLIBS)
tests/test-application-launch: tests/test-application-launch.c src/application-launch.c src/application-launch.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Isrc -o $@ tests/test-application-launch.c src/application-launch.c $(LDLIBS)
check: lunatic-launcher tests/test-core tests/test-model tests/test-tracking tests/test-dash tests/test-application-launch
	./tests/test-core
	./tests/test-model
	./tests/test-tracking
	./tests/test-dash
	./tests/test-application-launch
	./lunatic-launcher --help >/dev/null
	desktop-file-validate data/lunatic-launcher.desktop
tests/test-renderer: tests/test-renderer.c src/quicklist.c src/quicklist.h src/renderer.c src/renderer-software.inc src/renderer.h src/settings.h src/launcher-core.c src/launcher-core.h build/assets.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Isrc -o $@ tests/test-renderer.c src/renderer.c src/launcher-core.c build/assets.c $(LDLIBS)
tests/test-renderer-gl: tests/test-renderer.c src/renderer.c src/renderer.h src/settings.h src/launcher-core.c src/launcher-core.h build/assets.c
	$(CC) $(CPPFLAGS) $(shell pkg-config --cflags epoxy) $(CFLAGS) $(LDFLAGS) -DLAUNCHER_RENDER_GL -Isrc -o $@ tests/test-renderer.c src/renderer.c src/launcher-core.c build/assets.c $(LDLIBS) $(shell pkg-config --libs epoxy)
check-renderer: tests/test-renderer
	./tests/test-renderer
check-renderer-parity: check-renderer tests/test-renderer-gl
	./tests/test-renderer-gl
	python3 tests/compare-renderers.py
check-barriers: tests/test-barrier
	./tests/test-barrier
check-upstream:
	python3 tools/verify-upstream.py
	python3 tests/compare-upstream.py
bake-assets:
	python3 tools/bake-assets.py
install: lunatic-launcher
	install -Dm755 lunatic-launcher "$(DESTDIR)$(BINDIR)/lunatic-launcher"
	install -Dm644 data/lunatic-launcher.desktop "$(DESTDIR)$(DATADIR)/applications/lunatic-launcher.desktop"
	install -Dm644 data/launcher_mark.svg "$(DESTDIR)$(DATADIR)/icons/hicolor/scalable/apps/lunatic-launcher.svg"
uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/lunatic-launcher" "$(DESTDIR)$(DATADIR)/applications/lunatic-launcher.desktop"
	rm -f "$(DESTDIR)$(DATADIR)/icons/hicolor/scalable/apps/lunatic-launcher.svg"
clean:
	rm -f tests/test-barrier lunatic-launcher tests/test-core tests/test-model tests/test-tracking tests/test-dash tests/test-application-launch tests/test-renderer tests/test-renderer-gl build/assets.c
