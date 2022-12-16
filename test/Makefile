CC = arm-linux-gnueabi-gcc
CFLAGS=-I ../include
SRC = $(wildcard *.c)
OBJS = $(SRC:.c=.o)
TARGETS = $(SRC:.c=)

.SUFFIXES : .c .o

all: $(TARGETS)

clean:
	rm -f $(TARGETS)
	rm -f *.o