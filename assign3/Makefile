CC = gcc
CFLAGS = -g -Wall

OBJ = dberror.o storage_mgr.o buffer_mgr.o buffer_mgr_stat.o expr.o record_mgr.o rm_serializer.o

TARGET = test_assign3_1 test_assign3_2 test_assign3_3 test_expr

default: $(TARGET)

test_assign3_1: $(OBJ) test_assign3_1.o
	$(CC) $(CFLAGS) -o $@ $^

test_assign3_2: $(OBJ) test_assign3_2.o
	$(CC) $(CFLAGS) -o $@ $^

test_assign3_3: $(OBJ) test_assign3_3.o
	$(CC) $(CFLAGS) -o $@ $^

test_expr: $(OBJ) test_expr.o
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

.PHONY: clean
clean:
	rm -f $(TARGET) *.o
