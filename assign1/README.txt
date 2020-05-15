To run the code:
-------------------

1. Go to project folder in terminal.

2. Use "make clean" command to remove old .o files in case compiled previously

3. Use "make" command to compile the program

4. Use "./test_assign1_1" to run the test in "test_assign1_1.c" file

Solution
---------------------

initStorageManager()
Not much needs to be done here, just initailizing the file pointer to NULL is enough

createPageFile()
1. We first create a new page file using fopen and mode "w+". If the file already exists, it is destroyed.
2. Since we want to initialize it with a null page, we must have a null page in the memory.
3. So, we allocate memory of size PAGE_SIZE by using calloc. calloc also takes care of initializing the bits to 0s.
4. After that, we simply write the allocated memory block to the file.
5. In the end, we deallocate the memory for the page to prevent memory leaks and then close the file handle.

openPageFile()
1. To open the file, we simply use fopen with mode "r+". If the file doesn't exist, it doesn't create a new file.
2. For calculating the total number of pages, we can divide the total size of the file by the size of each page.
3. To find the size of the file, we go to the end of the file using fseek, get the position using ftell, and then go back to the beginning again using fseek.
4. We then divide the result of ftell by PAGE_SIZE to get the total number of pages

closePageFile()
To close a page file, we simple call fclose method.

destroyPageFile:
To destroy a page file, we simply call the remove method.

readBlock()
1. To read a page number as requested by the client
2. We check if the requested page number is valid or no by checking if it is greater than total no. of pages or is smaller than 0
3. Update the curPagePos variable to the page requested if page read was successful

getBlockPos()
Getting the current page number

readFirstBlock()
Reading the first page of the block. This is done by calling readBlock function and passing 0 as page number for 1st page

readPreviousBlock(....)
Reading the first page of the block. This is done by calling readBlock function and passing current Page - 1 as page number

readCurrentBlock:
Reading the first page of the block. This is done by calling readBlock function and passing current page number

readNextBlock:
Reading the first page of the block. This is done by calling readBlock function and passing current Page + 1 as page number

readLastBlock:
Reading the first page of the block. This is done by calling readBlock function and passing total no. of pages -1 as page number

writeBlock()
Write one page back to the file with the input page number.

writeCurrentBlock()
Write current page back to the file.

appendEmptyBlock()
Add a new page to the end of file.

ensureCapacity()
Check if there is enough space, i.e. empty page, if yes then return, if no then initiate a new page.
