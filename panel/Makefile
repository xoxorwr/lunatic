CC ?= cc
PREFIX ?= /usr/local
# Xfce derives the plugin library directory from each descriptor's prefix.
# Rebase the distro's lib/lib64/multiarch suffix onto our installation prefix;
# mixing /usr/local/share descriptors with /usr/lib libraries loads old copies.
XFCE_PREFIX := $(shell pkg-config --variable=prefix libxfce4panel-2.0)
XFCE_LIBDIR := $(shell pkg-config --variable=libdir libxfce4panel-2.0)
LIBDIR ?= $(patsubst $(XFCE_PREFIX)/%,$(PREFIX)/%,$(XFCE_LIBDIR))
GTK_MODULEDIR ?= $(shell pkg-config --variable=libdir gtk+-3.0)/gtk-3.0/modules
DATADIR ?= $(PREFIX)/share
PKGS = gtk+-3.0 gio-2.0 gio-unix-2.0 x11 libxfce4panel-2.0
CPPFLAGS += -Icommon $(shell pkg-config --cflags $(PKGS))
CFLAGS ?= -O2 -g
CFLAGS += -std=gnu11 -Wall -Wextra -Wformat=2
LDLIBS += $(shell pkg-config --libs $(PKGS))
COMMON = common/menu-registry.c common/menu-registrar.c common/menu-view.c common/dbus-menu.c common/remote-menu.c common/menu-model.c common/session-settings.c common/title-label.c common/app-name.c common/window-control.c
HEADERS = $(wildcard common/*.h)
.PHONY: all check demo install uninstall clean
all: build/liblunatic-global-menu.so build/liblunatic-panel-gtk-module.so
build:
	mkdir -p $@
build/liblunatic-global-menu.so: xfce-plugin/global-menu.c $(COMMON) $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -fPIC -shared -Wl,--no-undefined -o $@ xfce-plugin/global-menu.c $(COMMON) $(LDLIBS)
build/liblunatic-panel-gtk-module.so: common/gtk-module.c common/gtk-menu-export.c common/menu-model.c $(HEADERS) | build
	$(CC) $(shell pkg-config --cflags gtk+-3.0 gio-2.0 gmodule-2.0 x11) $(CFLAGS) $(LDFLAGS) -fPIC -shared -Wl,--no-undefined -Icommon -o $@ common/gtk-module.c common/gtk-menu-export.c common/menu-model.c $(shell pkg-config --libs gtk+-3.0 gio-2.0 gmodule-2.0 x11)
build/test-window-ui: tests/test-window-ui.c xfce-plugin/global-menu.c $(COMMON) $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test-window-ui.c $(COMMON) $(LDLIBS)
build/test-window-control: tests/test-window-control.c common/window-control.c common/window-control.h | build
	$(CC) $(shell pkg-config --cflags glib-2.0 x11) $(CFLAGS) $(LDFLAGS) -Icommon -o $@ tests/test-window-control.c common/window-control.c $(shell pkg-config --libs glib-2.0 x11)
build/test-app-name: tests/test-app-name.c common/app-name.c common/app-name.h | build
	$(CC) $(shell pkg-config --cflags gio-unix-2.0) $(CFLAGS) $(LDFLAGS) -Icommon -o $@ tests/test-app-name.c common/app-name.c $(shell pkg-config --libs gio-unix-2.0)
build/test-session-settings: tests/test-session-settings.c common/session-settings.c common/session-settings.h | build
	$(CC) $(shell pkg-config --cflags gio-2.0) $(CFLAGS) $(LDFLAGS) -Icommon -o $@ tests/test-session-settings.c common/session-settings.c $(shell pkg-config --libs gio-2.0)
build/test-registry: tests/test-registry.c common/menu-registry.c common/menu-registry.h | build
	$(CC) $(shell pkg-config --cflags gio-2.0) $(CFLAGS) $(LDFLAGS) -Icommon -o $@ tests/test-registry.c common/menu-registry.c $(shell pkg-config --libs gio-2.0)
build/test-menu-model: tests/test-menu-model.c common/menu-model.c common/remote-menu.c $(HEADERS) | build
	$(CC) $(shell pkg-config --cflags gio-2.0) $(CFLAGS) $(LDFLAGS) -Icommon -o $@ tests/test-menu-model.c common/menu-model.c common/remote-menu.c $(shell pkg-config --libs gio-2.0)
build/test-gtk-export: tests/test-gtk-export.c common/gtk-menu-export.c common/menu-model.c common/title-label.c $(HEADERS) | build
	$(CC) $(shell pkg-config --cflags gtk+-3.0) $(CFLAGS) $(LDFLAGS) -Icommon -o $@ tests/test-gtk-export.c common/gtk-menu-export.c common/menu-model.c common/title-label.c $(shell pkg-config --libs gtk+-3.0)
build/legacy-menu-demo: tests/legacy-menu-demo.c | build
	$(CC) $(shell pkg-config --cflags gtk+-3.0) $(CFLAGS) $(LDFLAGS) -o $@ $< $(shell pkg-config --libs gtk+-3.0)
build/menu-demo: tests/menu-demo.c | build
	$(CC) $(shell pkg-config --cflags gtk+-3.0) $(CFLAGS) $(LDFLAGS) -o $@ $< $(shell pkg-config --libs gtk+-3.0)
demo: build/menu-demo build/legacy-menu-demo
check: build/test-registry build/test-menu-model build/test-gtk-export build/test-session-settings build/test-app-name build/test-window-control build/test-window-ui
	./build/test-registry
	./build/test-menu-model
	./build/test-session-settings
	./build/test-app-name
	./build/test-window-control
	./build/test-gtk-export || test $$? -eq 77
	./build/test-window-ui || test $$? -eq 77
install: all
	install -Dm755 build/liblunatic-global-menu.so "$(DESTDIR)$(LIBDIR)/xfce4/panel/plugins/liblunatic-global-menu.so"
	install -Dm755 build/liblunatic-panel-gtk-module.so "$(DESTDIR)$(GTK_MODULEDIR)/liblunatic-panel-gtk-module.so"
	install -Dm644 xfce-plugin/lunatic-global-menu.desktop "$(DESTDIR)$(DATADIR)/xfce4/panel/plugins/lunatic-global-menu.desktop"
uninstall:
	rm -f "$(DESTDIR)$(GTK_MODULEDIR)/liblunatic-panel-gtk-module.so"
	rm -f "$(DESTDIR)$(LIBDIR)/xfce4/panel/plugins/liblunatic-global-menu.so" "$(DESTDIR)$(DATADIR)/xfce4/panel/plugins/lunatic-global-menu.desktop"
clean:
	rm -rf build
