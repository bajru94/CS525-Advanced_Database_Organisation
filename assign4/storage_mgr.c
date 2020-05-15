#include<stdio.h>
#include<stdlib.h>

#include "dberror.h"
#include "storage_mgr.h"

FILE* filePtr;

void initStorageManager(void) {
    filePtr = NULL;
}

RC createPageFile(char *fileName) {

    // Create a new file and open it for update(read & write)
    // If a file exists with the same name, discard its contents and create a new file
    filePtr = fopen(fileName, "wb+");

    // If could not create a new file, return error code
    if(filePtr == NULL) {
        return RC_FILE_NOT_FOUND;
    }

    // Allocate memory for a page
    SM_PageHandle* page = (SM_PageHandle*)calloc(PAGE_SIZE, sizeof(char));

    // Write page to the file
    int status = fwrite(page, PAGE_SIZE, 1, filePtr);

    // Free the allocated memory
    free(page);

    // If return value < number of elements(1), return write error code
    if(status < 1) {
        return RC_WRITE_FAILED;
    }

    status = fclose(filePtr);

    // Return error code if file wasn't closed successfully
    if(status != 0) {
        return RC_FILE_CLOSE_FAILED;
    }

    return RC_OK;
}

RC openPageFile(char *fileName, SM_FileHandle *fHandle) {

    // Open a file for update(read & write), the file must exist
    filePtr = fopen(fileName, "r+");

    // Check if the file was opened successfully
    if(filePtr == NULL) {
        return RC_FILE_NOT_FOUND;
    }

    // Go to the end of the file and get the position to calculate fileSize
    int status = fseek(filePtr, 0, SEEK_END);

    // Return error code if if seek was unsuccessful
    if(status != 0) {
        return RC_FILE_SEEK_ERROR;
    }

    // Get fileSize
    int fileSize = ftell(filePtr);

    // Move the file pointer back to the start
    status = fseek(filePtr, 0, SEEK_SET);

    // Return error code if if seek was unsuccessful
    if(status != 0) {
        return RC_FILE_SEEK_ERROR;
    }

    fHandle->fileName = fileName;
    fHandle->curPagePos = 0;
    fHandle->totalNumPages = fileSize/PAGE_SIZE;
    fHandle->mgmtInfo = filePtr;

    return RC_OK;
}

RC closePageFile(SM_FileHandle *fHandle) {

    // Close the file
    int status = fclose(fHandle->mgmtInfo);

    // Return error code if file wasn't closed successfully
    if(status != 0) {
        return RC_FILE_CLOSE_FAILED;
    }
    return RC_OK;
}

RC destroyPageFile(char *fileName) {

    // Delete the file with name fileName
    int status = remove(fileName);

    // Return error code if file wasn't removed successfully
    if(status != 0) {
        return RC_FILE_DESTROY_FAILED;
    }
    return RC_OK;
}

// To read a page number as requested by the client
RC readBlock(int pageNum, SM_FileHandle *fHandle, SM_PageHandle memPage) {

    // Check if the requested page number is greater than total no. of pages or is an invalid input(smaller than 0)
    if (fHandle->totalNumPages < (pageNum + 1) || pageNum < 0) {
		return RC_READ_NON_EXISTING_PAGE;
	}

	// Get the off_set from the starting point
	int off_set = sizeof(char) * pageNum * PAGE_SIZE;

	// To check if the pointer is set correctly
	int check = fseek(fHandle->mgmtInfo, off_set, SEEK_SET);

	if (check != 0) {
		return RC_FILE_SEEK_ERROR;
	}

	// To check if read succesfully or not
	int read = fread(memPage, sizeof(char), PAGE_SIZE, fHandle->mgmtInfo);
	if (read != PAGE_SIZE) {
		return RC_READ_NON_EXISTING_PAGE;
	}

	// Changing the current page number to the input page number
	fHandle->curPagePos = pageNum;

	return RC_OK;
}

// Getting the current page number
int getBlockPos(SM_FileHandle *fHandle) {
    return fHandle->curPagePos;
}

// Reading the first page of the block. This is done by calling readBlock function and passing 0 as page number for 1st page
RC readFirstBlock(SM_FileHandle *fHandle, SM_PageHandle memPage) {
    return readBlock(0, fHandle, memPage);
}

// Reading the first page of the block. This is done by calling readBlock function and passing current Page - 1 as page number
RC readPreviousBlock(SM_FileHandle *fHandle, SM_PageHandle memPage) {
    return readBlock(fHandle->curPagePos - 1, fHandle, memPage);
}

// Reading the first page of the block. This is done by calling readBlock function and passing current page number
RC readCurrentBlock(SM_FileHandle *fHandle, SM_PageHandle memPage) {
    return readBlock(fHandle->curPagePos, fHandle, memPage);
}

// Reading the first page of the block. This is done by calling readBlock function and passing current Page + 1 as page number
RC readNextBlock(SM_FileHandle *fHandle, SM_PageHandle memPage) {
    return readBlock(fHandle->curPagePos + 1, fHandle, memPage);
}

// Reading the first page of the block. This is done by calling readBlock function and passing total no. of pages -1 as page number
RC readLastBlock(SM_FileHandle *fHandle, SM_PageHandle memPage) {
    return readBlock(fHandle->totalNumPages - 1, fHandle, memPage);
}

RC writeBlock(int pageNum, SM_FileHandle *fHandle, SM_PageHandle memPage) {
    //if the page number is not right, i.e. pageNum < 0 || pageNum >= total, return failed.
    if (pageNum < 0 || fHandle->totalNumPages < (pageNum + 1)) {
        return RC_WRITE_FAILED;
    }

    //find the correct position
    int pos = pageNum * PAGE_SIZE * sizeof(char);
    int seekSuccess = fseek(fHandle->mgmtInfo, pos, SEEK_SET);

    //if seekSuccess != 0, cannot write, return failed.
    if(seekSuccess != 0){
        return RC_WRITE_FAILED;
    }

    fwrite(memPage, sizeof(char), PAGE_SIZE, fHandle->mgmtInfo);
    fHandle->curPagePos = pageNum;

    return RC_OK;

}

//write in current position
RC writeCurrentBlock(SM_FileHandle *fHandle, SM_PageHandle memPage) {
    return writeBlock (fHandle->curPagePos, fHandle, memPage);
}

RC appendEmptyBlock(SM_FileHandle *fHandle) {
    //move to the end of the file
    int seekSuccess = fseek(fHandle->mgmtInfo, 0, SEEK_END);

    if(seekSuccess != 0){
        return RC_WRITE_FAILED;
    }

    //write into file
    SM_PageHandle newPage = (char *) calloc(PAGE_SIZE, sizeof(char));
    fwrite(newPage, sizeof(char), PAGE_SIZE, fHandle->mgmtInfo);
    //update page number
    fHandle->totalNumPages = fHandle->totalNumPages + 1;
    fHandle->curPagePos = fHandle->totalNumPages;

    //free memory
    free(newPage);

    return RC_OK;
}

RC ensureCapacity(int numberOfPages, SM_FileHandle *fHandle) {
    //if already has enough capacity, return
    if(fHandle->totalNumPages >= numberOfPages){
        return RC_OK;
    }

    int num = numberOfPages - fHandle->totalNumPages;
    int i;
    for (i=0; i < num; i++){
        appendEmptyBlock(fHandle);
    }
    return RC_OK;
}
