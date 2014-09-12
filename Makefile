all: libfopen_override.so

libfopen_override.so: override.c
	${CC} ${CFLAGS} ${LDFLAGS} -Wall -g3 -shared -fPIC -ldl  -o $@ $<
