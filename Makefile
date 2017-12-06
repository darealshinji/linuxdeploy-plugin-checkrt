CFLAGS ?= -O2 -Wall -Wextra
LDFLAGS += -s
BIN = AppRun_patched
LIB = exec.so


all: $(BIN) $(LIB)

clean:
	-rm -f $(BIN) $(LIB) *.o AppRun.c AppRun_patched.c

$(BIN): AppRun_patched.o checkrt.o

$(LIB): exec.o
	$(CC) -shared $(LDFLAGS) -o $@ $^ -ldl

AppRun_patched.o checkrt.o: CFLAGS += -include checkrt.h
exec.o: CFLAGS += -fPIC

AppRun_patched.c: AppRun.c
	patch -p1 --output $@  < AppRun.c.patch

AppRun.c:
	wget -c "https://raw.githubusercontent.com/probonopd/AppImageKit/appimagetool/master/AppRun.c"

