CC = gcc
CFLAGS = -g -Wall

OBJ = dberror.o storage_mgr.o

default: test_assign1_1

test_assign1_1: $(OBJ) test_assign1_1.o
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

.PHONY: clean
clean:
	rm -f test_assign1_1 *.o
