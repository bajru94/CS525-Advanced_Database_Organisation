CC = gcc
CFLAGS = -g -Wall

OBJ = btree_mgr.o dberror.o storage_mgr.o buffer_mgr.o buffer_mgr_stat.o expr.o record_mgr.o rm_serializer.o

TARGET = test_assign4_1

default: $(TARGET)

test_assign4_1: $(OBJ) test_assign4_1.o
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

.PHONY: clean
clean:
	rm -f $(TARGET) *.o
