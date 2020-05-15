#include <stdlib.h>
#include <limits.h>
#include "buffer_mgr.h"
#include "dberror.h"
#include "storage_mgr.h"

typedef struct PageFrame {
    SM_PageHandle data;
	PageNumber pageNum;
    bool isDirty;
    int fixCount;
    // counter to keep track of frequency of hits
	int hitNum;
	// array to keep track of recent accesses
	// LRU_array[i] is the time of (i+1)th most recent access
	int *LRU_array;
} PageFrame;

int K;
int front, rear;
int clock;
int globalHitCount;
int writeCnt, readCnt;
bool *dirtyFlags;
int *fixCounts;
PageNumber *pageNums;

void FIFO(BM_BufferPool *const bm, PageFrame *page)
{
	PageFrame *pf = (PageFrame *) bm->mgmtData;
	int i;
	// if the page already exists, increment its fixCount
	for(i = 0; i < bm->numPages; i++) {
		if(pf[i].pageNum == page->pageNum) {
			pf[i].fixCount++;
			return;
		}
	}

	for(i = 0; i < bm->numPages; i++) {
		if(pf[i].pageNum == NO_PAGE) {
			rear++;
			pf[rear].data = page->data;
			pf[rear].pageNum = page->pageNum;
			pf[rear].isDirty = page->isDirty;
			pf[rear].fixCount = page->fixCount;
			return;
		}
	}

	for(i=0; i < bm->numPages; i++) {
		if(pf[front].fixCount == 0) {
			if(pf[front].isDirty == TRUE) {
				SM_FileHandle fh;
				openPageFile(bm->pageFile, &fh);
				writeBlock(pf[front].pageNum, &fh, pf[front].data);
				closePageFile(&fh);
				writeCnt++;
			}
			pf[front].data = page->data;
			pf[front].pageNum = page->pageNum;
			pf[front].isDirty = page->isDirty;
			pf[front].fixCount = page->fixCount;
			front++;
			front = (front % bm->numPages == 0) ? 0 : front;
			break;
		}
		else {
			front++;
			front = (front % bm->numPages == 0) ? 0 : front;
		}
	}
}

void CLOCK(BM_BufferPool *const bm, PageFrame *page)
{
	PageFrame *pf = (PageFrame *) bm->mgmtData;
	while(1)
	{
		clock %= bm->numPages;

		if(pf[clock].hitNum == 0 && pf[clock].fixCount == 0) {
			if(pf[clock].isDirty == TRUE) {
				SM_FileHandle fh;
				openPageFile(bm->pageFile, &fh);
				writeBlock(pf[clock].pageNum, &fh, pf[clock].data);
				closePageFile(&fh);
				writeCnt++;
			}

			pf[clock].data = page->data;
			pf[clock].pageNum = page->pageNum;
			pf[clock].isDirty = page->isDirty;
			pf[clock].fixCount = page->fixCount;
			pf[clock].hitNum = page->hitNum;
			clock++;
			break;
		}
		else {
			pf[clock++].hitNum = 0;
		}
	}
}

void LRU_K(BM_BufferPool *const bm, PageFrame *page) {

	PageFrame *pf = (PageFrame *) bm->mgmtData;

	// go through all the pages to check if there's empty spot
	for(int i = 0; i < bm->numPages; i++) {
		if(pf[i].pageNum == NO_PAGE) {
			pf[i].data = page->data;
			pf[i].pageNum = page->pageNum;
			pf[i].isDirty = page->isDirty;
			pf[i].fixCount = page->fixCount;
			pf[i].LRU_array[0] = globalHitCount;
			return;
		}
	}

	// go through all the pages and find the page with lowest Kth accessed time (least recently accessed) that is not pinned
	int LRU_index = -1, LRU_hitNum = globalHitCount;
	for(int i = 0; i < bm->numPages; i++) {
		if(pf[i].fixCount == 0 && pf[i].LRU_array[K-1] < LRU_hitNum) {
			LRU_index = i;
			LRU_hitNum = pf[i].LRU_array[K-1];
		}
	}

	// if we find suitable page that can be evicted
	if(LRU_index != -1) {
		// if the found page is dirty, write it back
		if(pf[LRU_index].isDirty == TRUE) {
			SM_FileHandle fh;
			openPageFile(bm->pageFile, &fh);
			writeBlock(pf[LRU_index].pageNum, &fh, pf[LRU_index].data);
			closePageFile(&fh);
			writeCnt++;
		}
		pf[LRU_index].data = page->data;
		pf[LRU_index].pageNum = page->pageNum;
		pf[LRU_index].isDirty = page->isDirty;
		pf[LRU_index].fixCount = page->fixCount;
		for(int i = 1; i < K; i++) {
			pf[LRU_index].LRU_array[i] = 0;
		}
		pf[LRU_index].LRU_array[0] = globalHitCount;
	}
}

void LFU(BM_BufferPool *const bm, PageFrame *page) {

	PageFrame *pf = (PageFrame *) bm->mgmtData;

	// go through all the pages to check if there's empty spot
	for(int i = 0; i < bm->numPages; i++) {
		if(pf[i].pageNum == NO_PAGE) {
			pf[i].data = page->data;
			pf[i].pageNum = page->pageNum;
			pf[i].isDirty = page->isDirty;
			pf[i].fixCount = page->fixCount;
			pf[i].hitNum = 1;
			// to break ties
			pf[i].LRU_array[0] = globalHitCount;
			return;
		}
	}

	// go through all the pages and find the page with lowest hitNum (least frequently used) that is not pinned
	// in case of a tie, choose the one which was least recently accessed
	int LFU_index = -1, LFU_hitNum = INT_MAX;
	for(int i = 0; i < bm->numPages; i++) {
		if(pf[i].fixCount == 0) {
			if(pf[i].hitNum < LFU_hitNum || (pf[i].hitNum == LFU_hitNum && pf[i].LRU_array[0] < pf[LFU_index].LRU_array[0])) {
				LFU_index = i;
				LFU_hitNum = pf[i].hitNum;
			}
		}
	}

	// if we find suitable page that can be evicted
	if(LFU_index != -1) {

		// if the found page is dirty, write it back
		if(pf[LFU_index].isDirty == TRUE) {
			SM_FileHandle fh;
			openPageFile(bm->pageFile, &fh);
			writeBlock(pf[LFU_index].pageNum, &fh, pf[LFU_index].data);
			closePageFile(&fh);
			writeCnt++;
		}

		pf[LFU_index].data = page->data;
		pf[LFU_index].pageNum = page->pageNum;
		pf[LFU_index].isDirty = page->isDirty;
		pf[LFU_index].fixCount = page->fixCount;
		pf[LFU_index].hitNum = 1;
		pf[LFU_index].LRU_array[0] = globalHitCount;
	}
}

RC initBufferPool(BM_BufferPool *const bm, const char *const pageFileName,
        const int numPages, ReplacementStrategy strategy,
        void *stratData) {
	SM_FileHandle fh;

	RC err = openPageFile(pageFileName, &fh);

	if(err != RC_OK){
		return RC_FILE_NOT_FOUND;
	}

	front = 0;
	rear = -1;
	clock = 0;
	globalHitCount = 0;
	writeCnt = readCnt = 0;

    bm->pageFile = (char*)pageFileName;
    bm->numPages = numPages;
    bm->strategy = strategy;

	if(stratData == NULL) {
		K = 1;
	}
	else {
		K = *((int *)(stratData));
	}

    // allocate memory and zero initalize everything
    PageFrame *pf = malloc(numPages * sizeof(PageFrame));

	for(int i = 0; i < numPages; i++) {
		pf[i].data = NULL;
		pf[i].pageNum = NO_PAGE;
		pf[i].isDirty = 0;
		pf[i].fixCount = 0;
		pf[i].LRU_array = (int*)malloc(K * sizeof(int));
	}
	bm->mgmtData = pf;
    return RC_OK;
}

RC shutdownBufferPool(BM_BufferPool *const bm) {

	if(bm->mgmtData == NULL){
		return RC_NON_EXISTING_BUFFERPOOL;
	}

    PageFrame *pf = (PageFrame*)bm->mgmtData;

    // return error if trying to shutdown while there are pinned pages
    for(int i = 0; i < bm->numPages; i++) {
        if(pf[i].fixCount != 0) {
            return RC_SHUTDOWN_WHILE_PINNED_PAGES;
        }
    }

    // write back dirty pages before shutting down
    forceFlushPool(bm);

    // free allocated pages
    free(pf);
    // prevent dangling pointer
    bm->mgmtData = NULL;

    return RC_OK;
}

RC forceFlushPool(BM_BufferPool *const bm) {

	if(bm->mgmtData == NULL){
		return RC_NON_EXISTING_BUFFERPOOL;
	}

    PageFrame *pf = (PageFrame*)bm->mgmtData;

    for(int i = 0; i < bm->numPages; i++) {
		if(pf[i].fixCount == 0 && pf[i].isDirty == TRUE)
		{
		   	SM_FileHandle fh;
			openPageFile(bm->pageFile, &fh);
			writeBlock(pf[i].pageNum, &fh, pf[i].data);
			closePageFile(&fh);
			pf[i].isDirty = FALSE;
			writeCnt++;
        }
    }
    return RC_OK;
}

RC markDirty (BM_BufferPool *const bm, BM_PageHandle *const page) {
	PageFrame *pf = (PageFrame *)bm->mgmtData;

	for(int i = 0; i < bm->numPages; i++) {
		if(pf[i].pageNum == page->pageNum) {
			pf[i].isDirty = TRUE;
			return RC_OK;
		}
	}
	return RC_READ_NON_EXISTING_PAGE;
}

RC unpinPage (BM_BufferPool *const bm, BM_PageHandle *const page) {
	PageFrame *pf = (PageFrame *)bm->mgmtData;

	for(int i = 0; i < bm->numPages; i++) {
		if(pf[i].pageNum == page->pageNum) {
			pf[i].fixCount--;
			return RC_OK;
		}
	}
	return RC_READ_NON_EXISTING_PAGE;
}

RC forcePage (BM_BufferPool *const bm, BM_PageHandle *const page) {
	PageFrame *pf = (PageFrame *)bm->mgmtData;

	for(int i = 0; i < bm->numPages; i++)
	{
		if(pf[i].pageNum == page->pageNum)
		{
			SM_FileHandle fh;
			openPageFile(bm->pageFile, &fh);
			writeBlock(pf[i].pageNum, &fh, pf[i].data);
			closePageFile(&fh);
			pf[i].isDirty = FALSE;

			writeCnt++;
			return RC_OK;
		}
	}

    return RC_READ_NON_EXISTING_PAGE;
}

RC pinPage (BM_BufferPool *const bm, BM_PageHandle *const page,
        const PageNumber pageNum) {

	if(bm->mgmtData == NULL){
		return RC_NON_EXISTING_BUFFERPOOL;
	}

	PageFrame *pf = (PageFrame *) bm->mgmtData;

	globalHitCount++;
	if(pageNum<0){
		return RC_READ_NON_EXISTING_PAGE;
	}

	// check if the page already exists, if so - increment its fixCount and update the hitNum
	for(int i = 0; i < bm->numPages; i++) {
		if(pf[i].pageNum == pageNum) {

			pf[i].fixCount++;

			switch(bm->strategy) {
				case RS_LRU_K:
					{
						// shift the accesses to the right
						for(int j = K - 1; j > 0; j--) {
							pf[i].LRU_array[j] = pf[i].LRU_array[j-1];
						}
					}
					break;
				case RS_LFU:
					pf[i].hitNum++;
					break;
			}
			pf[i].LRU_array[0] = globalHitCount;

			page->pageNum = pageNum;
			page->data = pf[i].data;

			return RC_OK;
		}
	}

	int pin_cnt = 0;
	for(int j = 0;j < bm->numPages; j++){
		if(pf[j].fixCount != 0){
			pin_cnt++;
		}
	}
	if(pin_cnt == bm->numPages){
		return RC_REPLACE_WHILE_PINNED_PAGES;
	}

	// else we need to read the pageFile
	PageFrame *newPage = (PageFrame *) malloc(sizeof(PageFrame));

	SM_FileHandle fh;
	openPageFile(bm->pageFile, &fh);
	newPage->data = (SM_PageHandle) malloc(PAGE_SIZE);
	ensureCapacity(pageNum + 1, &fh);

	readBlock(pageNum, &fh, newPage->data);
	newPage->pageNum = pageNum;
	newPage->isDirty = 0;
	newPage->fixCount = 1;
	page->pageNum = pageNum;
	page->data = newPage->data;
	readCnt++;
	closePageFile(&fh);

	switch(bm->strategy) {
		case RS_FIFO: // Using FIFO algorithm
			FIFO(bm, newPage);
			break;

		case RS_LRU: // Using LRU algorithm, LRU is simply LRU-K with K=1
			LRU_K(bm, newPage);
			break;

		case RS_CLOCK: // Using CLOCK algorithm
			CLOCK(bm,newPage);
			break;

		case RS_LFU: // Using LFU algorithm
			LFU(bm, newPage);
			break;

		case RS_LRU_K:	// Using LRU-K algorithm
			LRU_K(bm, newPage);
			break;

		default:
			printf("\nNone\n");
			break;
	}
	return RC_OK;
}


PageNumber *getFrameContents (BM_BufferPool *const bm) {
   PageFrame *pf = (PageFrame *) bm->mgmtData;
   pageNums = (PageNumber *) malloc (sizeof(PageNumber) * bm->numPages);

   int i=0;
   while(i<bm->numPages){
        pageNums[i] = pf[i].pageNum;
        i++;
   }
   return pageNums;
}

//iterate all the frames, update the value of dirty flags, then return result.
bool *getDirtyFlags (BM_BufferPool *const bm) {
    PageFrame *pf = (PageFrame *) bm->mgmtData;
    dirtyFlags = (bool *) malloc (sizeof(bool) * bm->numPages);

    int i=0;
    while(i<bm->numPages){
        dirtyFlags[i] = pf[i].isDirty;
        i++;
    }
    return dirtyFlags;
}


//iterate all the frames, update the value of fix count, then return result.
int *getFixCounts (BM_BufferPool *const bm) {
    PageFrame *pf = (PageFrame *) bm->mgmtData;
    fixCounts = (int *) malloc(sizeof(int) * bm->numPages);

    int i=0;
    while(i<bm->numPages){
        fixCounts[i] = pf[i].fixCount;
        i++;
    }

    return fixCounts;
}

int getNumReadIO (BM_BufferPool *const bm) {
    return readCnt;
}

int getNumWriteIO (BM_BufferPool *const bm) {
    return writeCnt;
}
