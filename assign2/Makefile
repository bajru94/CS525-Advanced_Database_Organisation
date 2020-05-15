CC = gcc
CFLAGS = -g -Wall

OBJ = dberror.o storage_mgr.o buffer_mgr.o buffer_mgr_stat.o

default: test_assign2_1 test_assign2_2

test_assign2_1: $(OBJ) test_assign2_1.o
	$(CC) $(CFLAGS) -o $@ $^

test_assign2_2: $(OBJ) test_assign2_2.o
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

.PHONY: clean
clean:
	rm -f test_assign2_1 test_assign2_2 *.o
