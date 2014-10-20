OBJ=md5.o upyun.o
TESTS=upyun-test
LIBNAME=libupyun

CC=gcc
OPTIMIZATION?=-O2
WARNINGS=-Wall -Wno-missing-field-initializers
DEBUG?= -g -ggdb
REAL_CFLAGS=$(OPTIMIZATION) -fPIC $(CFLAGS) $(WARNINGS) $(DEBUG)

DYLIBNAME=$(LIBNAME).so
DYLIB_MAKE_CMD=$(CC) -shared -o $(DYLIBNAME) $(LDFLAGS)
STLIBNAME=$(LIBNAME).a
STLIB_MAKE_CMD=ar rcs $(STLIBNAME)

all: $(DYLIBNAME)

$(DYLIBNAME): $(OBJ)
	$(DYLIB_MAKE_CMD) $(OBJ)

$(STLIBNAME): $(OBJ)
	$(STLIB_MAKE_CMD) $(OBJ)

dynamic: $(DYLIBNAME)
static: $(STLIBNAME)

test: $(TESTS)

$(TESTS): test.o $(STLIBNAME)
	$(CC) -o $(TESTS) $(REAL_CFLAGS) $< -I. $(STLIBNAME) -lcurl

md5.o: md5.h md5.c
upyun.o: upyun.h upyun.c
test.o: upyun.h test.c

%.o: %.c
	$(CC) -c $(REAL_CFLAGS) $<

.PHONY: all test clean

clean:
	$(RM) -rf *.o $(DYLIBNAME) $(STLIBNAME) $(TESTS) output/*
