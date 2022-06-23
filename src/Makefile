BIN = checkrt
LIB = exec.so
EXEC_TEST = exec_test
ENV_TEST = env_test
CHECKRT_TEST = checkrt_test

CFLAGS := -Wall -O3
LDFLAGS := -s


all: $(BIN) $(LIB)

clean:
	-rm -f *.o *.gz res.h $(BIN) $(LIB) $(EXEC_TEST) $(ENV_TEST) $(CHECKRT_TEST)

distclean: clean
	-rm -rf cxx gcc

$(BIN): checkrt.o
	$(CC) -o $@ $^ $(LDFLAGS) -ldl

checkrt.c: res.h

res.h:
	gzip -k -f COPYING.RUNTIME COPYING3 && \
  xxd -i COPYING.RUNTIME.gz > $@ && \
  xxd -i COPYING3.gz >> $@ && \
  xxd -i COPYING.libgcc >> $@ && \
  xxd -i COPYING.libstdc++ >> $@

exec.o env.o: CFLAGS += -fPIC

$(LIB): exec.o env.o
	$(CC) -o $@ $^ -shared $(LDFLAGS) -ldl

$(CHECKRT_TEST): checkrt.c
	$(CC) -o $@ $(CFLAGS) -DCHECKRT_TEST $^ -ldl

$(EXEC_TEST): exec.c env.o
	$(CC) -o $@ $(CFLAGS) -DEXEC_TEST $^ -ldl

$(ENV_TEST): env.c
	$(CC) -o $@ $(CFLAGS) -DENV_TEST $^

run_tests: $(EXEC_TEST) $(ENV_TEST) $(CHECKRT_TEST)
	./$(ENV_TEST)
	./$(EXEC_TEST)
	./$(CHECKRT_TEST)

.PHONY: $(BIN) all clean

