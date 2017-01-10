TARGETS=netconfig

DEBUG?=0
VERBOSE?=0

CROSS=
CC=$(CROSS)gcc
CFLAGS=-Wall -Werror
LDFLAGS=
RM:=rm -f

SRCS=main.c
SRCS+=network.c
SRCS+=dhcp.c
OBJS=${SRCS:.c=.o}

# debug option
ifeq ($(DEBUG), 1)
CFLAGS+=-O0 -g -DDEBUG -Wno-unused-function
else
CFLAGS+=-O3 -Wno-unused-function
endif

# verbose option
ifeq ($(VERBOSE), 1)
Q := 
echo-cmd := @echo $(1) > /dev/null
else
Q := @
echo-cmd := @echo $(1)
endif

all: $(TARGETS)

%.o : %.c
	$(echo-cmd) " CC    $@"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

netconfig: $(OBJS)
	$(echo-cmd) " LD    $@"
	$(Q)$(CC) $(LDFLAGS) -o $@ $^

clean:
	$(echo-cmd) " CLEAN"
	$(Q)$(RM) $(OBJS) $(TARGETS)
