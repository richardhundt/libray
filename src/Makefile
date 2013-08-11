COPT = -O2 -fPIC

CWARNS = -Wall

CFLAGS = $(CWARNS) $(COPT) -I./libuv/include

OS_NAME=$(shell uname -s)
MH_NAME=$(shell uname -m)

LDFLAGS=-luv -lm -ldl -lpthread

AR = ar

ifeq ($(OS_NAME), Darwin)
LDFLAGS+=-bundle -undefined dynamic_lookup -framework CoreServices
ifeq ($(MH_NAME), x86_64)
endif
else
LDFLAGS+=-shared -lrt
endif

SRCS := ray.c
OBJS := $(patsubst %.c,%.o,$(SRCS))

LIBS := ./libuv/out/Debug/libuv.a

all: ./libuv/libuv.a $(OBJS) ../libray.so

../libray.so: $(OBJS)
	$(CC) $(COPT) -L./libuv $(LIBS) ray.c -o ../libray.so $(LDFLAGS)

$(OBJS):
	$(CC) -c $(CFLAGS) $(SRCS)

./libuv/libuv.a:
	$(MAKE) CFLAGS="-fPIC" -C ./libuv

clean:
	rm -f *.o *.so

realclean: clean
	$(MAKE) -C ./libuv clean

.PHONY: all clean realclean

