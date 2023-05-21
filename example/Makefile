APPIMAGE=example-1-$(shell uname -m).AppImage

all: $(APPIMAGE)

clean:
	-rm -f $(APPIMAGE) example linuxdeploy-plugin-checkrt.sh
	-rm -rf appdir

distclean: clean
	-rm -f linuxdeploy-x86_64.AppImage

$(APPIMAGE):
	./build.sh

