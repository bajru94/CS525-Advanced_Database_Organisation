#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "btree_mgr.h"
#include "buffer_mgr.h"
#include "dberror.h"
#include "expr.h"
#include "record_mgr.h"
#include "storage_mgr.h"
#include "tables.h"

typedef struct BT_BtreeInfoNode {
    int *rootNodeIdx;
    int *keyType;
    int *keySize;
    int *maxKeys;
    int *numNodes;
    int *totalKeys;
} BT_BtreeInfoNode;

typedef struct BT_BtreeNode {
    int *selfIdx;
    int *parentIdx;
    int *leftSiblingIdx;
    int *rightSiblingIdx;
    bool *isLeaf;
    int *numKeys;
    char *keyValues;
    int *childrenIdx;
} BT_BtreeNode;

typedef struct TreeMgmt{
    BM_BufferPool *bufferPool;
    BT_BtreeInfoNode *infoNode;
    BM_PageHandle *infoPage;
} TreeMgmt;


const int BT_NUM_PAGES = 100;
const ReplacementStrategy BT_REPLACEMENT_STRATEGY = RS_LRU;
const int MAX_STRING_KEY_LENGTH = 10;
const int BT_RESERVED_PAGES = 1;       // 0th page is for tree information

// reads the info node into the BT_BtreeInfoNode for easier access; page must be pinned
// memory must be freed by the caller
BT_BtreeInfoNode *readTreeInfoNode(BM_PageHandle *nodePage) {
    BT_BtreeInfoNode *infoNode = (BT_BtreeInfoNode*)malloc(sizeof(BT_BtreeInfoNode));

    infoNode->rootNodeIdx = (int*)(nodePage->data);
    infoNode->keyType = infoNode->rootNodeIdx + 1;
    infoNode->keySize = infoNode->keyType + 1;
    infoNode->maxKeys = infoNode->keySize + 1;
    infoNode->numNodes = infoNode->maxKeys + 1;
    infoNode->totalKeys = infoNode->numNodes + 1;

    return infoNode;
}

// reads a node into the BT_BtreeNode for easier access; page must be pinned
// memory must be freed by the caller
BT_BtreeNode *readTreeNodePage(BM_PageHandle *nodePage) {
    BT_BtreeNode *node = (BT_BtreeNode*)malloc(sizeof(BT_BtreeNode));

    node->selfIdx = (int*)(nodePage->data);
    node->parentIdx = node->selfIdx + 1;
    node->leftSiblingIdx = node->parentIdx + 1;
    node->rightSiblingIdx = node->leftSiblingIdx + 1;
    node->numKeys = node->rightSiblingIdx + 1;
    node->isLeaf = (bool*)((char*)node->numKeys + sizeof(int));
    node->keyValues = ((char*)node->isLeaf + sizeof(bool));

    // the pointers to the children are filled bottom up from the end of the block
    // in case of non leaf nodes, childrenIdx[-n] will access the nth key's pointer
    // in case of leaf nodes, childrenIdx[-2n-1] and childrenIdx[-2n] will access page and slot for the nth key respectively
    node->childrenIdx = (int*)((char*)nodePage->data + PAGE_SIZE - sizeof(int));

    return node;
}

// used to initialize the values in a newly created node
void initializeNewNode(BT_BtreeNode *node, int selfIdx) {
    *(node->selfIdx) = selfIdx;         // self page index
    *(node->parentIdx) = -1;            // page index of parent node
    *(node->leftSiblingIdx) = -1;       // page index of left sibling
    *(node->rightSiblingIdx) = -1;      // page index of right sibling
    *(node->isLeaf) = TRUE;             // flag to denote a leaf
    *(node->numKeys) = 0;               // total number of keys in this node
}

// converts the key from a char string to a Value
Value *readKeyValue(char *keyPtr, DataType keyType) {
    Value *keyValue = NULL;
    switch(keyType) {
        case DT_INT:
            MAKE_VALUE(keyValue, DT_INT, *(int*)(keyPtr));
            break;
        case DT_STRING:
            MAKE_STRING_VALUE(keyValue, keyPtr);
            break;
        case DT_FLOAT:
            MAKE_VALUE(keyValue, DT_FLOAT, *(float*)keyPtr);
            break;
        case DT_BOOL:
            MAKE_VALUE(keyValue, DT_BOOL, *(bool*)keyPtr);
            break;
        default:
            break;
    }
    return keyValue;
}

// writes the Value to the given location
void writeKeyValue(char *keyPtr, Value *key, DataType keyType, int keySize) {
    Value *keyValue = NULL;
    switch(keyType) {
        case DT_INT:
            memcpy(keyPtr, &(key->v.intV), keySize);
            break;
        case DT_STRING:
            memcpy(keyPtr, key->v.stringV, keySize);
            break;
        case DT_FLOAT:
            memcpy(keyPtr, &(key->v.floatV), keySize);
            break;
        case DT_BOOL:
            memcpy(keyPtr, &(key->v.boolV), keySize);
            break;
        default:
            break;
    }
    return keyValue;
}


// finds the index of the first key value which is > given key value
int lowerBoundIdx(BT_BtreeInfoNode *infoNode, BT_BtreeNode *node, Value *key) {

    DataType keyType = *(infoNode->keyType);
    int keySize = *(infoNode->keySize);

    int numKeys = *(node->numKeys);

    int keyIdx = 0;
    char *currKeyValuePtr = node->keyValues;

    Value *currKeyValue;
    Value *comparisionResult = (Value*)malloc(sizeof(Value));

    while(keyIdx < numKeys) {
        currKeyValue = readKeyValue(currKeyValuePtr, keyType);
        valueSmaller(key, currKeyValue, comparisionResult);
        freeVal(currKeyValue);

        if(comparisionResult->v.boolV == TRUE) {
            break;
        }

        keyIdx++;
        currKeyValuePtr += keySize;
    }

    free(comparisionResult);
    return keyIdx;
}

// finds the index of a leaf node in which the given key belongs
int findLeafNodeForKey(BTreeHandle *tree, Value *key) {
    TreeMgmt *treeMgmt = (TreeMgmt *)(tree->mgmtData);
    BT_BtreeInfoNode *infoNode = treeMgmt->infoNode;

    int nodeIdx = *(infoNode->rootNodeIdx);
    BM_PageHandle *nodePage = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));

    pinPage(treeMgmt->bufferPool, nodePage, nodeIdx);
    markDirty(treeMgmt->bufferPool, nodePage);

    BT_BtreeNode *node = readTreeNodePage(nodePage);

    // loop while we don't reach a leaf
    while( *(node->isLeaf) == FALSE ) {
        // find the correct child to recurse to
        int keyIdx = lowerBoundIdx(infoNode, node, key);

        nodeIdx = (node->childrenIdx)[-keyIdx];
        unpinPage(treeMgmt->bufferPool, nodePage);
        free(node);

        pinPage(treeMgmt->bufferPool, nodePage, nodeIdx);
        markDirty(treeMgmt->bufferPool, nodePage);
        node = readTreeNodePage(nodePage);
    }
    unpinPage(treeMgmt->bufferPool, nodePage);
    free(nodePage);
    free(node);

    return nodeIdx;
}

// split leftSibling into 2 parts and join them as siblings
// leftSibling must be full and rightSibling must be a newly created node
// indexes < splitIdx remain in leftSibling, other indexes are moved to the rightSibling
void splitNode(BT_BtreeInfoNode *infoNode, BT_BtreeNode *leftSibling, BT_BtreeNode *rightSibling, int splitIdx) {

    int keySize = *(infoNode->keySize);

    int numKeys = *(leftSibling->numKeys);
    bool isLeaf = *(leftSibling->isLeaf);

    *(rightSibling->isLeaf) = isLeaf;

    // copy rightSibling's parent and rightSibling index from the leftSibling
    *(rightSibling->parentIdx) = *(leftSibling->parentIdx);
    *(rightSibling->rightSiblingIdx) = *(leftSibling->rightSiblingIdx);

    // join them as siblings
    *(leftSibling->rightSiblingIdx) = *(rightSibling->selfIdx);
    *(rightSibling->leftSiblingIdx) = *(leftSibling->selfIdx);

    // sizes after splitting
    int leftSiblingSize = splitIdx;
    int rightSiblingSize = numKeys - splitIdx;

    *(leftSibling->numKeys) = leftSiblingSize;
    *(rightSibling->numKeys) = rightSiblingSize;

    // copy rightSiblingSize keys from leftSibling to rightSibling starting from splitIdx
    memcpy( (rightSibling->keyValues), (leftSibling->keyValues + splitIdx*keySize), rightSiblingSize*keySize);

    // if it's a leaf, multiply the pointer copy size by 2 because RID occupies twice the space
    if(isLeaf) {
        memcpy( &(rightSibling->childrenIdx[-2*rightSiblingSize+1]), &(leftSibling->childrenIdx[-2*numKeys+1]), 2*rightSiblingSize*sizeof(int) );
    }
    else {
        memcpy( &(rightSibling->childrenIdx[-rightSiblingSize]), &(leftSibling->childrenIdx[-numKeys]), rightSiblingSize*sizeof(int) );
    }
}

// moves keys and pointers to the right by shiftCount starting from startIdx
void shiftKeysAndPointers(BT_BtreeInfoNode *infoNode, BT_BtreeNode *node, int startIdx, int shiftCount) {

    int keySize = *(infoNode->keySize);

    int numKeys = *(node->numKeys);
    bool isLeaf = *(node->isLeaf);

    // shift (numKeys-startIdx) keys to the right, starting from startIdx
    memmove( (node->keyValues + (startIdx+shiftCount) * keySize), (node->keyValues + startIdx * keySize), (numKeys - startIdx) * keySize);

    // if it's a leaf, multiply the copy size by 2 because of RID
    if(isLeaf) {
        memmove( (node->childrenIdx - 2*(numKeys+shiftCount) + 1), (node->childrenIdx - 2*numKeys + 1), 2*(numKeys - startIdx)*sizeof(int) );
    }
    else {
        memmove( (node->childrenIdx - numKeys - shiftCount), (node->childrenIdx - numKeys), (numKeys - startIdx)*sizeof(int) );
    }
}

// inserts the given key into a node along with the rid; the node must not be full
// increments key count for both the tree and the node
void insertKeyAndIndexIntoNode(BTreeHandle *tree, BT_BtreeNode *node, Value *key, void *value) {
    TreeMgmt *treeMgmt = (TreeMgmt *)(tree->mgmtData);
    BT_BtreeInfoNode *infoNode = treeMgmt->infoNode;

    DataType keyType = *(infoNode->keyType);
    int keySize = *(infoNode->keySize);

    // if the node is full, create a sibling and split
    if(*(node->numKeys) >= *(infoNode->maxKeys)) {
        (*(infoNode->numNodes))++;

        BM_PageHandle *parentPage = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));
        BT_BtreeNode *parentNode;

        int rightSiblingIdx = *(infoNode->numNodes) + BT_RESERVED_PAGES - 1;

        BM_PageHandle *rightSiblingPage = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));
        pinPage(treeMgmt->bufferPool, rightSiblingPage, rightSiblingIdx);
        markDirty(treeMgmt->bufferPool, rightSiblingPage);

        BT_BtreeNode *rightSiblingNode = readTreeNodePage(rightSiblingPage);
        initializeNewNode(rightSiblingNode, rightSiblingIdx);

        // find the position the new key would occupy after insertion
        int keyIdx = lowerBoundIdx(infoNode, node, key);

        int splitIdx = (*(infoNode->maxKeys))/2;

        if(keyIdx>splitIdx) {
            splitIdx++;
            splitNode(infoNode, node, rightSiblingNode, splitIdx);
            insertKeyAndIndexIntoNode(tree, rightSiblingNode, key, value);
        }
        else {
            splitNode(infoNode, node, rightSiblingNode, splitIdx);
            insertKeyAndIndexIntoNode(tree, node, key, value);
        }

        Value *keyIntoParent = readKeyValue(rightSiblingNode->keyValues, keyType);

        // if it is also a root node, we also need to create a new root node
        if(*(node->parentIdx) == -1) {
            (*(infoNode->numNodes))++;
            // idx of the new node
            int parentIdx = *(infoNode->numNodes) + BT_RESERVED_PAGES - 1;
            pinPage(treeMgmt->bufferPool, parentPage, parentIdx);
            markDirty(treeMgmt->bufferPool, parentPage);

            parentNode = readTreeNodePage(parentPage);
            initializeNewNode(parentNode, parentIdx);

            // update parent
            *(parentNode->isLeaf) = FALSE;
            *(parentNode->numKeys) = 1;

            // update the parent idx
            *(node->parentIdx) = parentIdx;
            *(rightSiblingNode->parentIdx) = parentIdx;

            // update the tree's root
            *(infoNode->rootNodeIdx) = parentIdx;

            memcpy(parentNode->childrenIdx, (node->selfIdx), sizeof(int));
            memcpy(parentNode->childrenIdx - 1, &rightSiblingIdx, sizeof(int));

            writeKeyValue(parentNode->keyValues, keyIntoParent, keyType, keySize);
        }
        // otherwise add rightSibling and the leftmost key of the rightSibling into the parent
        else {
            int parentIdx = *(node->parentIdx);
            pinPage(treeMgmt->bufferPool, parentPage, parentIdx);
            markDirty(treeMgmt->bufferPool, parentPage);

            parentNode = readTreeNodePage(parentPage);

            insertKeyAndIndexIntoNode(tree, parentNode, keyIntoParent, &rightSiblingIdx);
        }

        freeVal(keyIntoParent);

        unpinPage(treeMgmt->bufferPool, rightSiblingPage);
        free(rightSiblingPage);
        free(rightSiblingNode);

        unpinPage(treeMgmt->bufferPool, parentPage);
        free(parentPage);
        free(parentNode);
    }
    else {
        DataType keyType = *(infoNode->keyType);
        int keySize = *(infoNode->keySize);

        bool isLeaf = *(node->isLeaf);

        // find the correct index for the provided key
        int keyIdx = lowerBoundIdx(infoNode, node, key);

        // make space to write by shifting to the right by 1
        shiftKeysAndPointers(infoNode, node, keyIdx, 1);

        // write key
        writeKeyValue(node->keyValues + keyIdx*keySize, key, keyType, keySize);

        if(isLeaf) {
            // copy RID info
            memcpy(node->childrenIdx - 2*keyIdx - 1, value, 2*sizeof(int));
        }
        else {
            // copy child index
            memcpy(node->childrenIdx - keyIdx - 1, value, sizeof(int));
        }

        // increment key count
        (*(node->numKeys))++;
    }
}

RC initIndexManager (void *mgmtData) {
    initStorageManager();
    return RC_OK;
}

RC shutdownIndexManager () {
    return RC_OK;
}

//create a new tree, with the name idxId, data type and the number in each node
RC createBtree (char *idxId, DataType keyType, int n) {
    RC rc;
    rc = createPageFile(idxId);
    if (rc != RC_OK)
        return rc;

    BM_BufferPool *bufferPool = (BM_BufferPool*)malloc(sizeof(BM_BufferPool));
    initBufferPool(bufferPool, idxId, BT_NUM_PAGES, BT_REPLACEMENT_STRATEGY, NULL);  // initialize a new buffer pool

    BM_PageHandle *infoPage = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));
    pinPage(bufferPool, infoPage, 0);
    markDirty(bufferPool, infoPage);

    int keySize;
    if (keyType == DT_INT)
        keySize = sizeof(int);
    else if (keyType == DT_FLOAT)
        keySize = sizeof(float);
    else if (keyType == DT_BOOL)
        keySize = sizeof(bool);
    else if (keyType == DT_STRING)
        keySize = MAX_STRING_KEY_LENGTH * sizeof(char);

    BT_BtreeInfoNode *infoNode = readTreeInfoNode(infoPage);
    *(infoNode->rootNodeIdx) = BT_RESERVED_PAGES;       // root node index
    *(infoNode->keyType) = keyType;                     // type of the key
    *(infoNode->keySize) = keySize;                     // size of given type
    *(infoNode->maxKeys) = n;                           // max number of keys per node
    *(infoNode->numNodes) = 1;                          // total number of nodes; by default, only root node
    *(infoNode->totalKeys) = 0;                         // total number of keys

    unpinPage(bufferPool, infoPage);
    free(infoPage);
    free(infoNode);

    BM_PageHandle *treeRootNodePage = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));
    pinPage(bufferPool, treeRootNodePage, BT_RESERVED_PAGES);
    markDirty(bufferPool, treeRootNodePage);

    BT_BtreeNode *treeRootNode = readTreeNodePage(treeRootNodePage);

    // initialize the root
    initializeNewNode(treeRootNode, BT_RESERVED_PAGES);

    unpinPage(bufferPool, treeRootNodePage);
    free(treeRootNodePage);
    free(treeRootNode);

    shutdownBufferPool(bufferPool);
    free(bufferPool);
    return RC_OK;
}

RC openBtree (BTreeHandle **tree, char *idxId) {
    BM_BufferPool *bufferPool = (BM_BufferPool*)malloc(sizeof(BM_BufferPool));
    initBufferPool(bufferPool, idxId, BT_NUM_PAGES, BT_REPLACEMENT_STRATEGY, NULL);  // initialize a new buffer pool

    BM_PageHandle *infoPage = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));
    pinPage(bufferPool, infoPage, 0);
    markDirty(bufferPool, infoPage);

    BT_BtreeInfoNode *infoNode = readTreeInfoNode(infoPage);

    *tree = (BTreeHandle *) malloc(sizeof(BTreeHandle));
    (*tree)->idxId = idxId;
    (*tree)->keyType = *(infoNode->keyType);

    TreeMgmt *treeMgmt = (TreeMgmt *) malloc(sizeof(TreeMgmt));
    treeMgmt->bufferPool = bufferPool;
    treeMgmt->infoNode = infoNode;
    treeMgmt->infoPage = infoPage;
    (*tree)->mgmtData = treeMgmt;

    return RC_OK;
}

RC closeBtree (BTreeHandle *tree) {
    tree->idxId = NULL;
    TreeMgmt *treeMgmt = (TreeMgmt *)(tree->mgmtData);
    unpinPage(treeMgmt->bufferPool, treeMgmt->infoPage);
    shutdownBufferPool(treeMgmt->bufferPool);
    free(treeMgmt->infoNode);
    free(treeMgmt->infoPage);
    free(treeMgmt);
    free(tree);
    return RC_OK;
}

RC deleteBtree (char *idxId) {
    RC rc = destroyPageFile(idxId);
    if(rc != RC_OK) {
        return rc;
    }
    return RC_OK;
}

RC getNumNodes (BTreeHandle *tree, int *result) {
    BT_BtreeInfoNode *infoNode = ((TreeMgmt*)(tree->mgmtData))->infoNode;
    *result = *(infoNode->numNodes);
    return RC_OK;
}

RC getNumEntries (BTreeHandle *tree, int *result) {
    BT_BtreeInfoNode *infoNode = ((TreeMgmt*)(tree->mgmtData))->infoNode;
    *result = *(infoNode->totalKeys);
    return RC_OK;
}

RC getKeyType (BTreeHandle *tree, DataType *result) {
    *result = tree->keyType;
    return RC_OK;
}

RC findKey (BTreeHandle *tree, Value *key, RID *result) {
    TreeMgmt *treeMgmt = (TreeMgmt *)(tree->mgmtData);
    BT_BtreeInfoNode *infoNode = treeMgmt->infoNode;

    DataType keyType = *(infoNode->keyType);
    int keySize = *(infoNode->keySize);

    // find the leaf node in which the key belongs
    int nodeIdx = findLeafNodeForKey(tree, key);

    BM_PageHandle *nodePage = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));
    pinPage(treeMgmt->bufferPool, nodePage, nodeIdx);
    markDirty(treeMgmt->bufferPool, nodePage);

    BT_BtreeNode *node = readTreeNodePage(nodePage);

    // get the first key value > given key value; if the given key exists, it must exist just before that key value
    int keyIdx = lowerBoundIdx(infoNode, node, key) - 1;

    Value *keyValue = readKeyValue((node->keyValues + keyIdx*keySize), keyType);

    Value *comparisionResult = (Value*)malloc(sizeof(Value));

    valueEquals(key, keyValue, comparisionResult);
    freeVal(keyValue);

    RC rc = RC_IM_KEY_NOT_FOUND;

    if(comparisionResult->v.boolV == TRUE) {
        rc = RC_OK;
        memcpy(result, (node->childrenIdx - 2*keyIdx - 1), 2*sizeof(int));
    }

    free(comparisionResult);
    unpinPage(treeMgmt->bufferPool, nodePage);
    free(nodePage);
    free(node);

    return rc;
}

RC insertKey (BTreeHandle *tree, Value *key, RID rid) {
    TreeMgmt *treeMgmt = (TreeMgmt *)(tree->mgmtData);
    BT_BtreeInfoNode *infoNode = treeMgmt->infoNode;

    // find the leaf node in which the key belongs
    int nodeIdx = findLeafNodeForKey(tree, key);

    BM_PageHandle *nodePage = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));
    pinPage(treeMgmt->bufferPool, nodePage, nodeIdx);
    markDirty(treeMgmt->bufferPool, nodePage);

    BT_BtreeNode *node = readTreeNodePage(nodePage);
    insertKeyAndIndexIntoNode(tree, node, key, &rid);

    // increment total keys in the tree
    (*(infoNode->totalKeys))++;

    unpinPage(treeMgmt->bufferPool, nodePage);
    free(nodePage);
    free(node);

    return RC_OK;
}

RC deleteKey (BTreeHandle *tree, Value *key) {

    // first, check if the given key exists
    TreeMgmt *treeMgmt = (TreeMgmt *)(tree->mgmtData);
    BT_BtreeInfoNode *infoNode = treeMgmt->infoNode;

    DataType keyType = *(infoNode->keyType);
    int keySize = *(infoNode->keySize);

    // find the leaf node in which the key belongs
    int nodeIdx = findLeafNodeForKey(tree, key);


    BM_PageHandle *nodePage = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));
    pinPage(treeMgmt->bufferPool, nodePage, nodeIdx);
    markDirty(treeMgmt->bufferPool, nodePage);

    BT_BtreeNode *node = readTreeNodePage(nodePage);

    // get the first key value > given key value; is the given key exists, it must exist just before that key value
    int keyIdx = lowerBoundIdx(infoNode, node, key) - 1;

    Value *keyValue = readKeyValue((node->keyValues + keyIdx*keySize), keyType);

    Value *comparisionResult = (Value*)malloc(sizeof(Value));

    valueEquals(key, keyValue, comparisionResult);
    freeVal(keyValue);

    RC rc = RC_IM_KEY_NOT_FOUND;

    // if the key exists, reduce the key count for the node and the tree and left shift by 1
    if(comparisionResult->v.boolV == TRUE) {
        rc = RC_OK;
        shiftKeysAndPointers(infoNode, node, keyIdx+1, -1);
        (*(infoNode->totalKeys))--;
        (*(node->numKeys))--;
    }

    free(comparisionResult);
    unpinPage(treeMgmt->bufferPool, nodePage);
    free(nodePage);
    free(node);

    return rc;
}

RC openTreeScan (BTreeHandle *tree, BT_ScanHandle **handle) {
    TreeMgmt *treeMgmt = (TreeMgmt *)(tree->mgmtData);
    BT_BtreeInfoNode *infoNode = treeMgmt->infoNode;

    int nodeIdx = *(infoNode->rootNodeIdx);
    BM_PageHandle *nodePage = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));

    pinPage(treeMgmt->bufferPool, nodePage, nodeIdx);
    markDirty(treeMgmt->bufferPool, nodePage);

    BT_BtreeNode *node = readTreeNodePage(nodePage);

    // loop while we don't reach a leaf
    while( *(node->isLeaf) == FALSE ) {

        // recurse into the left most child
        nodeIdx = (node->childrenIdx)[0];
        unpinPage(treeMgmt->bufferPool, nodePage);
        free(node);

        pinPage(treeMgmt->bufferPool, nodePage, nodeIdx);
        markDirty(treeMgmt->bufferPool, nodePage);
        node = readTreeNodePage(nodePage);
    }

    unpinPage(treeMgmt->bufferPool, nodePage);
    free(nodePage);
    free(node);

    RID *id = (RID*)malloc(sizeof(RID));

    // store the node index and key index
    id->page = nodeIdx;
    id->slot = 0;

    *handle = (BT_ScanHandle*)malloc(sizeof(BT_ScanHandle));
    (*handle)->tree = tree;
    (*handle)->mgmtData = id;

    return RC_OK;
}

RC nextEntry (BT_ScanHandle *handle, RID *result) {
    BTreeHandle *tree = handle->tree;
    TreeMgmt *treeMgmt = (TreeMgmt *)(tree->mgmtData);

    RID *id = (handle->mgmtData);

    int nodeIdx = id->page;
    int keyIdx = id->slot;

    BM_PageHandle *nodePage = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));
    pinPage(treeMgmt->bufferPool, nodePage, nodeIdx);

    BT_BtreeNode *node = readTreeNodePage(nodePage);

    // if at the last key of this node, try loading next sibling
    if(*(node->numKeys) == keyIdx) {
        id->page = *(node->rightSiblingIdx);
        id->slot = 0;

        unpinPage(treeMgmt->bufferPool, nodePage);
        free(node);

        // if no more right siblings
        if( id->page == -1) {
            return RC_IM_NO_MORE_ENTRIES;
        }
        // load the right sibling page
        else {
            pinPage(treeMgmt->bufferPool, nodePage, id->page);
            node = readTreeNodePage(nodePage);
        }
    }
    do {
        memcpy(result, (node->childrenIdx - 2*(id->slot) - 1), sizeof(RID));
    } while(result->page == 0 || result->slot == 0);

    id->slot++;
    free(node);
    return RC_OK;
}

RC closeTreeScan (BT_ScanHandle *handle) {
    free(handle->mgmtData);
    free(handle);
    return RC_OK;
}

char *dfs(BTreeHandle *tree, BT_BtreeNode *curNode, int *idx) {
    VarString *result;
    VarString *childStr;

    MAKE_VARSTRING(result);
    MAKE_VARSTRING(childStr);

    TreeMgmt *treeMgmt = (TreeMgmt *)(tree->mgmtData);
    BT_BtreeInfoNode *infoNode = treeMgmt->infoNode;

    BM_PageHandle *curChildPage = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));
    BT_BtreeNode *curChildNode;

    Value *keyValue;
    char *serializedKeyValue;

    DataType keyType = *(infoNode->keyType);
    int keySize = *(infoNode->keySize);
    int numKeys = *(curNode->numKeys);
    bool isLeaf = *(curNode->isLeaf);

    APPEND(result, "(%d", *idx);
    (*idx)++;

    for(int i = 0; i<=numKeys; i++) {
        if(!isLeaf) {
            int curChildPageIdx;
            memcpy(&curChildPageIdx, (curNode->childrenIdx - i), sizeof(int));
            if(curChildPageIdx == 0)
                continue;
            // load child node
            pinPage(treeMgmt->bufferPool, curChildPage, curChildPageIdx);
            curChildNode = readTreeNodePage(curChildPage);

            APPEND(result, "%d,", *idx);
            char *curChildStr = dfs(tree, curChildNode, idx);
            APPEND_STRING(childStr, curChildStr);
            unpinPage(treeMgmt->bufferPool, curChildPage);
            free(curChildNode);
        }
        else if(i<numKeys) {
            RID *id = (RID*)malloc(sizeof(RID));
            memcpy(id, (curNode->childrenIdx - 2*i - 1), 2*sizeof(int));
            APPEND(result, "%d.%d,", id->page, id->slot);
            free(id);
        }
        if(i<numKeys) {
            keyValue = readKeyValue((curNode->keyValues + i*keySize), keyType);
            serializedKeyValue = serializeValue(keyValue);
            APPEND(result, "%s,", serializedKeyValue);
            free(serializedKeyValue);
        }
    }
    APPEND_STRING(result, "]\n");
    char *childStrBuf;
    GET_STRING(childStrBuf, childStr);
    APPEND_STRING(result, childStrBuf);

    RETURN_STRING(result);
}

char *printTree (BTreeHandle *tree) {
    TreeMgmt *treeMgmt = (TreeMgmt *)(tree->mgmtData);
    BT_BtreeInfoNode *infoNode = treeMgmt->infoNode;

    int rootNodeIdx = *(infoNode->rootNodeIdx);

    BM_PageHandle *rootNodePage = (BM_PageHandle*)malloc(sizeof(BM_PageHandle));

    pinPage(treeMgmt->bufferPool, rootNodePage, rootNodeIdx);
    markDirty(treeMgmt->bufferPool, rootNodePage);

    BT_BtreeNode *rootNode = readTreeNodePage(rootNodePage);

    int idx = 0;
    char *result = dfs(tree, rootNode, &idx);

    free(rootNodePage);
    free(rootNode);
    return result;
}
