#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#include "buffer_mgr.h"
#include "dberror.h"
#include "record_mgr.h"
#include "storage_mgr.h"
#include "tables.h"

const int NUM_PAGES = 100;
const ReplacementStrategy REPLACEMENT_STRATEGY = RS_LRU;
const int TOTAL_RESERVED_PAGES = 1;            // 0th page is for table information
Schema *schem;

RC checkDuplicatePrimaryKey(RM_TableData *rel, Record *record) {
    BM_PageHandle *tableInfoPage = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));
    pinPage(rel->mgmtData, tableInfoPage, 0);

    int *integerTablePointer = (int*)tableInfoPage->data;
    int totalRecordPages = integerTablePointer[2];
    int maxRecordsPerPage = integerTablePointer[3];
    unpinPage(rel->mgmtData, tableInfoPage);
    free(tableInfoPage);

    int primaryKeyIndex = *(schem->keyAttrs);

    Value **thisRecordVal = (Value**)malloc(sizeof(Value));
    getAttr(record, schem, primaryKeyIndex, thisRecordVal);

    Record *otherRecord = (Record*)malloc(sizeof(Record));
    createRecord(&otherRecord, schem);

    Value **otherRecordVal = (Value**)malloc(sizeof(Value));

    Value *comparisionResult = (Value*)malloc(sizeof(Value));

    RID id;
    for(int page = TOTAL_RESERVED_PAGES; page < totalRecordPages + TOTAL_RESERVED_PAGES; page++) {
        for(int slot = 0; slot < maxRecordsPerPage; slot++) {

            id.page = page;
            id.slot = slot;
            if(getRecord(rel, id, otherRecord) == RC_OK) {
                getAttr(otherRecord, schem, primaryKeyIndex, otherRecordVal);
                valueEquals(*thisRecordVal, *otherRecordVal, comparisionResult);
                freeVal(*otherRecordVal);
                if(comparisionResult->v.boolV == TRUE) {
                    if(!(page == record->id.page && slot == record->id.slot)) {
                        freeVal(*thisRecordVal);
                        free(comparisionResult);
                        free(otherRecord);
                        return RC_IM_KEY_ALREADY_EXISTS;
                    }
                }
            }
        }
    }
    freeVal(*thisRecordVal);
    free(otherRecord);
    free(comparisionResult);
    return RC_OK;
}

RC initRecordManager(void *mgmtData) {
    initStorageManager();
    return RC_OK;
}

RC shutdownRecordManager() {
    return RC_OK;
}

RC createTable(char *name, Schema *schema) {
    createPageFile(name);

    BM_BufferPool *bufferPool = (BM_BufferPool*)malloc(sizeof(BM_BufferPool));
    initBufferPool(bufferPool, name, NUM_PAGES, REPLACEMENT_STRATEGY, NULL);  // initialize a new buffer pool

    BM_PageHandle *tableInfoPage = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));
    pinPage(bufferPool, tableInfoPage, 0);

    markDirty(bufferPool, tableInfoPage);
    int recordSize = getRecordSize(schema);
    schem = schema;
    int *integerTablePointer = (int*)tableInfoPage->data;

    // Each page has an integer array of length maxRecordsPerPage, which uses slotted pages to store records
    // The first sizeof(int) bytes store the number of records currently in the page
    int maxRecordsPerPage = (PAGE_SIZE - sizeof(int))/(sizeof(int) + recordSize);

    integerTablePointer[0] = recordSize;
    integerTablePointer[1] = 0;             // Initialize total records
    integerTablePointer[2] = 0;             // Initialize total pages
    integerTablePointer[3] = maxRecordsPerPage;

    unpinPage(bufferPool, tableInfoPage);

    shutdownBufferPool(bufferPool);
    free(bufferPool);
    return RC_OK;
}

RC openTable(RM_TableData *rel, char *name) {
    BM_BufferPool *bufferPool = (BM_BufferPool*)malloc(sizeof(BM_BufferPool));
    initBufferPool(bufferPool, name, NUM_PAGES, REPLACEMENT_STRATEGY, NULL);  // initialize a new buffer pool
    bufferPool->pageFile = name;
    rel->mgmtData = bufferPool;
    rel->name = name;
    rel->schema = schem;
    return RC_OK;
}

RC closeTable(RM_TableData *rel) {
    RC rc = shutdownBufferPool(rel->mgmtData);
    if(rc != RC_OK) {
        return rc;
    }
    free(rel->mgmtData);
    rel->mgmtData = NULL;
    return RC_OK;
}

RC deleteTable(char *name) {
    RC rc = destroyPageFile(name);
    if(rc != RC_OK) {
        return rc;
    }
    return RC_OK;
}

int getNumTuples(RM_TableData *rel) {
    BM_PageHandle *tableInfoPage = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));
    pinPage(rel->mgmtData, tableInfoPage, 0);
    int *integerTablePointer = (int*)tableInfoPage->data;
    int numTuples = integerTablePointer[1];
    unpinPage(rel->mgmtData, tableInfoPage);
    free(tableInfoPage);
    return numTuples;
}

RC insertRecord(RM_TableData *rel, Record *record) {

    if(checkDuplicatePrimaryKey(rel, record) != RC_OK) {
        return RC_IM_KEY_ALREADY_EXISTS;
    }

    BM_PageHandle *tableInfoPage = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));
    pinPage(rel->mgmtData, tableInfoPage, 0);
    markDirty(rel->mgmtData, tableInfoPage);

    int *integerTablePointer = (int*)tableInfoPage->data;
    int recordSize = integerTablePointer[0];
    int totalRecords = integerTablePointer[1];
    int totalRecordPages = integerTablePointer[2];
    int maxRecordsPerPage = integerTablePointer[3];

    BM_PageHandle *freePage = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));
    int i = 0, totalRecordsInPage = 0;

    // if all pages are full, add another page
    if(totalRecords == totalRecordPages * maxRecordsPerPage) {
        pinPage(rel->mgmtData, freePage, totalRecordPages + TOTAL_RESERVED_PAGES);

        // initialize the new page
        *((int*)(freePage->data)) = 0;  // initialize the number of records
        int *slotIndexArray = (int*)(freePage->data + sizeof(int));
        for(int j = 0; j < maxRecordsPerPage; j++) {
            slotIndexArray[j] = -1;     // initialize the slot indexes
        }

        integerTablePointer[2]++;   // increment the number of pages;
        i = totalRecordPages + TOTAL_RESERVED_PAGES;
    }
    // else find a page with an empty slot for the record
    else {
        // loop through all the pages
        for(i = TOTAL_RESERVED_PAGES; i < TOTAL_RESERVED_PAGES + totalRecordPages; i++) {
            pinPage(rel->mgmtData, freePage, i);
            totalRecordsInPage = *((int*)(freePage->data));
            if(totalRecordsInPage < maxRecordsPerPage) {
                markDirty(rel->mgmtData, freePage);
                break;
            }
            else {
                unpinPage(rel->mgmtData, freePage);
            }
        }
    }

    int *slotIndexArray = (int*)(freePage->data + sizeof(int));

    // find first free slot
    int j;
    for(j = 0; j < maxRecordsPerPage; j++) {
        if(slotIndexArray[j] == -1) {       // free record slot
            break;
        }
    }

    slotIndexArray[j] = j;
    record->id.page = i;
    record->id.slot = j;

    char *recordPointer = (freePage->data + sizeof(int) + maxRecordsPerPage * sizeof(int) + recordSize * j);
    memcpy(recordPointer, record->data, recordSize);

    integerTablePointer[1]++;       // increment the number of records in the table
    ++ *((int*)(freePage->data));   // increment the number of records in the page

    unpinPage(rel->mgmtData, freePage);
    unpinPage(rel->mgmtData, tableInfoPage);
    free(freePage);
    free(tableInfoPage);
    return RC_OK;
}

RC deleteRecord(RM_TableData *rel, RID id) {
    BM_PageHandle *page = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));
    pinPage(rel->mgmtData, page, id.page);
    markDirty(rel->mgmtData, page);

    int *slotIndexArray = (int*)(page->data + sizeof(int));
    int slotIndex = slotIndexArray[id.slot];

    if(slotIndex == -1) {
        unpinPage(rel->mgmtData, page);
        free(page);
        return RC_DELETING_UNEXISTING_RECORD;
    }
    else {
        slotIndexArray[id.slot] = -1;   // set to a tombstone
    }

    BM_PageHandle *tableInfoPage = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));
    pinPage(rel->mgmtData, tableInfoPage, 0);
    markDirty(rel->mgmtData, tableInfoPage);

    int *integerTablePointer = (int*)(tableInfoPage->data);
    integerTablePointer[1]--;     // decrement the number of records in the table
    (*((int*)(page->data)))--;    // decrement the number of records in this page

    unpinPage(rel->mgmtData, tableInfoPage);
    free(tableInfoPage);
    unpinPage(rel->mgmtData, page);
    free(page);
    return RC_OK;
}

RC updateRecord(RM_TableData *rel, Record *record) {

    if(checkDuplicatePrimaryKey(rel, record) != RC_OK) {
        return RC_IM_KEY_ALREADY_EXISTS;
    }

    BM_PageHandle *tableInfoPage = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));
    pinPage(rel->mgmtData, tableInfoPage, 0);
    int *integerTablePointer = (int*)tableInfoPage->data;
    int recordSize = integerTablePointer[0];
    int maxRecordsPerPage = integerTablePointer[3];
    unpinPage(rel->mgmtData, tableInfoPage);
    free(tableInfoPage);

    BM_PageHandle *page = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));
    pinPage(rel->mgmtData, page, record->id.page);

    int *slotIndexArray = (int*)(page->data + sizeof(int));
    int slotIndex = slotIndexArray[record->id.slot];
    char *recordPointer = (page->data + sizeof(int) + maxRecordsPerPage * sizeof(int) + recordSize * slotIndex);
    memcpy(recordPointer, record->data, recordSize);

    unpinPage(rel->mgmtData, page);
    free(page);
    return RC_OK;
}

RC getRecord(RM_TableData *rel, RID id, Record *record) {

    BM_PageHandle *tableInfoPage = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));
    pinPage(rel->mgmtData, tableInfoPage, 0);
    int *integerTablePointer = (int*)tableInfoPage->data;
    int recordSize = integerTablePointer[0];
    int maxRecordsPerPage = integerTablePointer[3];
    unpinPage(rel->mgmtData, tableInfoPage);
    free(tableInfoPage);

    BM_PageHandle *page = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));
    pinPage(rel->mgmtData, page, id.page);

    int *slotIndexArray = (int*)(page->data + sizeof(int));
    int slotIndex = slotIndexArray[id.slot];
    if(slotIndex == -1) {
        unpinPage(rel->mgmtData, page);
        free(page);
        return RC_GETTING_UNEXISTING_RECORD;
    }

    char *recordPointer = (page->data + sizeof(int) + maxRecordsPerPage * sizeof(int) + recordSize * slotIndex);
    memcpy(record->data, recordPointer, recordSize);

    record->id.page = id.page;
    record->id.slot = id.slot;

    unpinPage(rel->mgmtData, page);
    free(page);

    return RC_OK;
}

RC startScan(RM_TableData *rel, RM_ScanHandle *scan, Expr *cond) {

    RM_ScanCond *scan_cond = (RM_ScanCond *) malloc(sizeof(RM_ScanCond));
    scan_cond->id = (RID *) malloc(sizeof(RID));
    scan_cond->cond = cond;
    scan_cond->id->page = 1;
    scan_cond->id->slot = 0;

    scan->rel = rel;
    scan->mgmtData = scan_cond;

    return RC_OK;
}

RC next(RM_ScanHandle *scan, Record *record) {
	RM_ScanCond *scan_cond = (RM_ScanCond *) scan->mgmtData;
	Value *val = (Value*)malloc(sizeof(Value));
	BM_PageHandle *tableInfoPage = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));
    pinPage(scan->rel->mgmtData, tableInfoPage, 0);
	int *integerTablePointer = (int*)tableInfoPage->data;

	int totalRecordPages = integerTablePointer[2];
	int maxRecordsPerPage = integerTablePointer[3];

    unpinPage(scan->rel->mgmtData, tableInfoPage);
    free(tableInfoPage);
	bool found = FALSE;
	while(found == FALSE) {
		if(getRecord(scan->rel, *(scan_cond->id), record) == RC_OK) {
	       evalExpr(record, scan->rel->schema, scan_cond->cond, &val);
	       found = val->v.boolV;

            if(scan_cond->id->page == totalRecordPages && scan_cond->id->slot == maxRecordsPerPage) {
                freeVal(val);
                return RC_RM_NO_MORE_TUPLES;
            }

        }
        if (scan_cond->id->slot == maxRecordsPerPage){
            ++scan_cond->id->page;
            scan_cond->id->slot=0;
        }
        else{
            ++scan_cond->id->slot;
        }
	}
    freeVal(val);
	return RC_OK;
}

RC closeScan(RM_ScanHandle *scan) {

    RM_ScanCond *scan_cond = (RM_ScanCond *) scan->mgmtData;
    free(scan_cond->id);
    free(scan_cond);

    scan->mgmtData = NULL;
    scan->rel = NULL;

    return RC_OK;
}

RC updateScan(RM_TableData *rel, Expr *cond, void (*updateFunction)(RM_TableData*, Schema*, Record*) ) {
    RM_ScanHandle *sc = (RM_ScanHandle*)malloc(sizeof(RM_ScanHandle));
    startScan(rel, sc, cond);
    Record *record = (Record*)malloc(sizeof(Record));
    createRecord(&record, schem);
    while(next(sc, record) == RC_OK) {
        updateFunction(rel, schem, record);
    }
    return RC_OK;
}

//returns the size of record for a schema
int getRecordSize(Schema *schema) {

    int size = 0;
    for(int i = 0; i < schema->numAttr; i++) {
        switch(schema->dataTypes[i]) {
            case DT_INT:
                size += sizeof(int);
                break;
            case DT_STRING:
                size += schema->typeLength[i];
                break;
            case DT_FLOAT:
                size += sizeof(float);
                break;
            case DT_BOOL:
                size += sizeof(bool);
		break;
        }
    }
    return size;
}

//this method creates a new schema
Schema *createSchema(int numAttr, char **attrNames, DataType *dataTypes, int *typeLength, int keySize, int *keys) {
    Schema *schema = (Schema *) malloc(sizeof(Schema));

    schema->numAttr = numAttr;
    schema->attrNames = attrNames;
    schema->dataTypes = dataTypes;
    schema->typeLength = typeLength;
    schema->keyAttrs = keys;
    schema->keySize = keySize;

    return schema;
}

RC freeSchema(Schema *schema) {
    free(schema);
    return RC_OK;
}

//Allocates memory for the record
RC createRecord(Record **record, Schema *schema) {
    *record = (Record *) malloc(sizeof(Record));
    (*record)->data = (char *) malloc(getRecordSize(schema));
    return RC_OK;
}

RC freeRecord(Record *record) {
    free(record->data);
    free(record);
    return RC_OK;
}

RC getAttr(Record *record, Schema *schema, int attrNum, Value **value) {
    int offset = 0;
    int typeLength = 0;
    //allocating space
    Value *tempValue = (Value *) malloc(sizeof(Value));
    tempValue->dt = schema->dataTypes[attrNum];

    //get the offsets of attributes
    int i;
    for(i = 0; i < attrNum; i++){
        if (schema->dataTypes[i] == DT_INT)
            offset += sizeof(int);
        else if (schema->dataTypes[i] == DT_FLOAT)
            offset += sizeof(float);
        else if (schema->dataTypes[i] == DT_STRING)
            offset += schema->typeLength[i] * sizeof(char);
        else if (schema->dataTypes[i] == DT_BOOL)
            offset += sizeof(bool);
    }

    if (schema->dataTypes[attrNum] == DT_INT) {
        memcpy(&(tempValue->v.intV), record->data + offset, sizeof(int));
    }
    else if (schema->dataTypes[attrNum] == DT_FLOAT) {
        memcpy(&(tempValue->v.floatV), record->data + offset, sizeof(float));
    }
    else if (schema->dataTypes[attrNum] == DT_STRING) {
        typeLength = schema->typeLength[attrNum];
        tempValue->v.stringV = (char *)malloc(sizeof(char)*(typeLength+1));
        memcpy(tempValue->v.stringV, record->data + offset, typeLength);
        tempValue->v.stringV[typeLength + 1] = '\0';
    }
    else if (schema->dataTypes[attrNum] == DT_BOOL) {
        memcpy(&(tempValue->v.boolV), record->data + offset, sizeof(bool));
    }

    *value = tempValue;
    return RC_OK;
}

RC setAttr(Record *record, Schema *schema, int attrNum, Value *value) {
    int offset = 0;
    char *data;

    int i;
    for(i = 0; i < attrNum; i++){
        if (schema->dataTypes[i] == DT_INT)
            offset += sizeof(int);
        else if (schema->dataTypes[i] == DT_FLOAT)
            offset += sizeof(float);
        else if (schema->dataTypes[i] == DT_STRING)
            offset += schema->typeLength[i] * sizeof(char);
        else if (schema->dataTypes[i] == DT_BOOL)
            offset += sizeof(bool);
    }

    data = record->data + offset;

    if (schema->dataTypes[attrNum] == DT_INT) {
        memcpy(data,&(value->v.intV), sizeof(int));
    }
    else if (schema->dataTypes[attrNum] == DT_FLOAT) {
       memcpy(data,&((value->v.floatV)), sizeof(float));
    }
    else if (schema->dataTypes[attrNum] == DT_STRING) {
        memcpy(data, value->v.stringV, schema->typeLength[attrNum]);
    }
    else if (schema->dataTypes[attrNum] == DT_BOOL) {
        memcpy(data,&((value->v.boolV)), sizeof(bool));
    }

    return RC_OK;
}
