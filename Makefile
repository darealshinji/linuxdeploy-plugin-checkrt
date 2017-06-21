CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += -include checkrt.h
LDFLAGS += -s
BIN = AppRun_patched


all: $(BIN)

clean:
	-rm -f $(BIN) *.o AppRun.c AppRun_patched.c

$(BIN): AppRun_patched.o checkrt.o

AppRun_patched.c: AppRun.c
	patch -p1 --output $@  < AppRun.c.patch

AppRun.c:
	wget -c "https://raw.githubusercontent.com/probonopd/AppImageKit/appimagetool/master/AppRun.c"

