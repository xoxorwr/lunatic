.PHONY: all launcher panel check check-launcher check-panel check-renderer check-renderer-parity check-upstream clean install uninstall install-launcher install-panel uninstall-launcher uninstall-panel check-install
all: launcher
launcher:
	$(MAKE) -C launcher
panel:
	$(MAKE) -C panel
check: check-launcher check-panel
check-launcher:
	$(MAKE) -C launcher check
check-panel:
	$(MAKE) -C panel check
check-install:
	python3 panel/tests/check-install.py
check-renderer check-renderer-parity check-upstream:
	$(MAKE) -C launcher $@
clean:
	$(MAKE) -C launcher clean
	$(MAKE) -C panel clean
# Build both before installing either component. The root install command is
# the entry point for updating the complete desktop integration.
install: launcher panel
	$(MAKE) -C launcher install PREFIX="$(if $(PREFIX),$(PREFIX),/usr/local)"
	$(MAKE) -C panel install PREFIX="$(if $(PREFIX),$(PREFIX),/usr/local)"
uninstall:
	$(MAKE) -C launcher uninstall PREFIX="$(if $(PREFIX),$(PREFIX),/usr/local)"
	$(MAKE) -C panel uninstall PREFIX="$(if $(PREFIX),$(PREFIX),/usr/local)"
install-launcher uninstall-launcher:
	$(MAKE) -C launcher $(patsubst %-launcher,%,$@) PREFIX="$(if $(PREFIX),$(PREFIX),/usr/local)"
install-panel uninstall-panel:
	$(MAKE) -C panel $(patsubst %-panel,%,$@) PREFIX="$(if $(PREFIX),$(PREFIX),/usr/local)"
