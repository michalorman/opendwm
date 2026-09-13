X11_DIR = x11

all install clean:
	$(MAKE) -C $(X11_DIR) $@

.PHONY: all install clean
