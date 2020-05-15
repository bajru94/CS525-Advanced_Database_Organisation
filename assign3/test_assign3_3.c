#include <stdlib.h>
#include "dberror.h"
#include "expr.h"
#include "record_mgr.h"
#include "tables.h"
#include "test_helper.h"


#define ASSERT_EQUALS_RECORDS(_l,_r, schema, message)			\
		do {									\
			Record *_lR = _l;                                                   \
			Record *_rR = _r;                                                   \
			ASSERT_TRUE(memcmp(_lR->data,_rR->data,getRecordSize(schema)) == 0, message); \
			int i;								\
			for(i = 0; i < schema->numAttr; i++)				\
			{									\
				Value *lVal, *rVal;                                             \
				char *lSer, *rSer; \
				getAttr(_lR, schema, i, &lVal);                                  \
				getAttr(_rR, schema, i, &rVal);                                  \
				lSer = serializeValue(lVal); \
				rSer = serializeValue(rVal); \
				ASSERT_EQUALS_STRING(lSer, rSer, "attr same");	\
				free(lVal); \
				free(rVal); \
				free(lSer); \
				free(rSer); \
			}									\
		} while(0)

#define ASSERT_EQUALS_RECORD_IN(_l,_r, rSize, schema, message)		\
		do {									\
			int i;								\
			boolean found = false;						\
			for(i = 0; i < rSize; i++)						\
			if (memcmp(_l->data,_r[i]->data,getRecordSize(schema)) == 0)	\
			found = true;							\
			ASSERT_TRUE(0, message);						\
		} while(0)

#define OP_TRUE(left, right, op, message)		\
		do {							\
			Value *result = (Value *) malloc(sizeof(Value));	\
			op(left, right, result);				\
			bool b = result->v.boolV;				\
			free(result);					\
			ASSERT_TRUE(b,message);				\
		} while (0)

// test methods
static void testAdding2ToIndex2ValuesSmallerThan3 (void);
static void testNegatingIndex2ValuesGreaterThan2 (void);

// struct for test records
typedef struct TestRecord {
	int a;
	char *b;
	int c;
} TestRecord;

// helper methods
Record *testRecord(Schema *schema, int a, char *b, int c);
Schema *testSchema (void);
Record *fromTestRecord (Schema *schema, TestRecord in);

// update functions
void add2ToIndex2(RM_TableData *table, Schema *schema, Record *record);
void negateIndex2(RM_TableData *table, Schema *schema, Record *record);

// test name
char *testName;

// main method
int
main (void)
{
	testName = "";

	testAdding2ToIndex2ValuesSmallerThan3();
	testNegatingIndex2ValuesGreaterThan2();

	return 0;
}

void
testAdding2ToIndex2ValuesSmallerThan3 (void)
{
	RM_TableData *table = (RM_TableData *) malloc(sizeof(RM_TableData));
	TestRecord inserts[] = {
			{1, "aaaa", 3},
			{2, "bbbb", 2},
			{3, "cccc", 1},
			{4, "dddd", 3},
			{5, "eeee", 5},
			{6, "ffff", 1},
			{7, "gggg", 3},
			{8, "hhhh", 3},
			{9, "iiii", 2}
	};

	TestRecord expectedResults[] = {
			{1, "aaaa", 3},
			{2, "bbbb", 4},
			{3, "cccc", 3},
			{4, "dddd", 3},
			{5, "eeee", 5},
			{6, "ffff", 3},
			{7, "gggg", 3},
			{8, "hhhh", 3},
			{9, "iiii", 4}
	};

	int numInserts = 9, i;
	Record *r;
	RID *rids;
	Schema *schema;
	testName = "test creating a new table and inserting duplicate tuples";
	schema = testSchema();
	RM_ScanHandle *sc = (RM_ScanHandle *) malloc(sizeof(RM_ScanHandle));
	rids = (RID *) malloc(sizeof(RID) * numInserts);
	Expr *sel, *left, *right;

	TEST_CHECK(initRecordManager(NULL));
	TEST_CHECK(createTable("test_table_r",schema));
	TEST_CHECK(openTable(table, "test_table_r"));

	// insert rows into table
	for(i = 0; i < numInserts; i++)
	{
		r = fromTestRecord(schema, inserts[i]);
		TEST_CHECK(insertRecord(table,r));
		rids[i] = r->id;
	}

	// make a condition c<3
	MAKE_ATTRREF(left, 2);
	MAKE_CONS(right, stringToValue("i3"));
	MAKE_BINOP_EXPR(sel, left, right, OP_COMP_SMALLER);

	TEST_CHECK(startScan(table, sc, sel));

	updateScan(table, sel, &add2ToIndex2);

	// check exepected results
	for(i = 0; i < numInserts; i++)
	{
		RID rid = rids[i];
		TEST_CHECK(getRecord(table, rid, r));
		ASSERT_EQUALS_RECORDS(fromTestRecord(schema, expectedResults[i]), r, schema, "compare records");
	}

	TEST_CHECK(closeTable(table));
	TEST_CHECK(deleteTable("test_table_r"));
	TEST_CHECK(shutdownRecordManager());

	free(rids);
	free(table);
	TEST_DONE();
}

void
testNegatingIndex2ValuesGreaterThan2 (void)
{
	RM_TableData *table = (RM_TableData *) malloc(sizeof(RM_TableData));
	TestRecord inserts[] = {
			{1, "aaaa", 3},
			{2, "bbbb", 2},
			{3, "cccc", 1},
			{4, "dddd", 3},
			{5, "eeee", 5},
			{6, "ffff", 1},
			{7, "gggg", 3},
			{8, "hhhh", 3},
			{9, "iiii", 2}
	};

	TestRecord expectedResults[] = {
			{1, "aaaa", -3},
			{2, "bbbb", 2},
			{3, "cccc", 1},
			{4, "dddd", -3},
			{5, "eeee", -5},
			{6, "ffff", 1},
			{7, "gggg", -3},
			{8, "hhhh", -3},
			{9, "iiii", 2}
	};

	int numInserts = 9, i;
	Record *r;
	RID *rids;
	Schema *schema;
	testName = "test creating a new table and inserting duplicate tuples";
	schema = testSchema();
	RM_ScanHandle *sc = (RM_ScanHandle *) malloc(sizeof(RM_ScanHandle));
	rids = (RID *) malloc(sizeof(RID) * numInserts);
	Expr *sel, *left, *right;

	TEST_CHECK(initRecordManager(NULL));
	TEST_CHECK(createTable("test_table_r",schema));
	TEST_CHECK(openTable(table, "test_table_r"));

	// insert rows into table
	for(i = 0; i < numInserts; i++)
	{
		r = fromTestRecord(schema, inserts[i]);
		TEST_CHECK(insertRecord(table,r));
		rids[i] = r->id;
	}

	// make a condition c>2
	MAKE_CONS(left, stringToValue("i2"));
	MAKE_ATTRREF(right, 2);
	MAKE_BINOP_EXPR(sel, left, right, OP_COMP_SMALLER);

	TEST_CHECK(startScan(table, sc, sel));

	updateScan(table, sel, &negateIndex2);

	// check exepected results
	for(i = 0; i < numInserts; i++)
	{
		RID rid = rids[i];
		TEST_CHECK(getRecord(table, rid, r));
		ASSERT_EQUALS_RECORDS(fromTestRecord(schema, expectedResults[i]), r, schema, "compare records");
	}

	TEST_CHECK(closeTable(table));
	TEST_CHECK(deleteTable("test_table_r"));
	TEST_CHECK(shutdownRecordManager());

	free(rids);
	free(table);
	TEST_DONE();
}

Schema *
testSchema (void)
{
	Schema *result;
	char *names[] = { "a", "b", "c" };
	DataType dt[] = { DT_INT, DT_STRING, DT_INT };
	int sizes[] = { 0, 4, 0 };
	int keys[] = {0};
	int i;
	char **cpNames = (char **) malloc(sizeof(char*) * 3);
	DataType *cpDt = (DataType *) malloc(sizeof(DataType) * 3);
	int *cpSizes = (int *) malloc(sizeof(int) * 3);
	int *cpKeys = (int *) malloc(sizeof(int));

	for(i = 0; i < 3; i++)
	{
		cpNames[i] = (char *) malloc(2);
		strcpy(cpNames[i], names[i]);
	}
	memcpy(cpDt, dt, sizeof(DataType) * 3);
	memcpy(cpSizes, sizes, sizeof(int) * 3);
	memcpy(cpKeys, keys, sizeof(int));

	result = createSchema(3, cpNames, cpDt, cpSizes, 1, cpKeys);

	return result;
}

// This function adds 2 to the value of the record at index 2
void add2ToIndex2(RM_TableData *table, Schema *schema, Record *record) {
    Value **value = (Value**)malloc(sizeof(Value*));
    getAttr(record, schema, 2, value);
    (*value)->v.intV += 2;
    setAttr(record, schema, 2, *value);
    updateRecord(table, record);
    free(*value);
}

// This function negates the value of the record at index 2
void negateIndex2(RM_TableData *table, Schema *schema, Record *record) {
    Value **value = (Value**)malloc(sizeof(Value*));
    getAttr(record, schema, 2, value);
    (*value)->v.intV = -((*value)->v.intV);
    setAttr(record, schema, 2, *value);
    updateRecord(table, record);
    free(*value);
}

Record *
fromTestRecord (Schema *schema, TestRecord in)
{
	return testRecord(schema, in.a, in.b, in.c);
}

Record *
testRecord(Schema *schema, int a, char *b, int c)
{
	Record *result;
	Value *value;

	TEST_CHECK(createRecord(&result, schema));

	MAKE_VALUE(value, DT_INT, a);
	TEST_CHECK(setAttr(result, schema, 0, value));
	freeVal(value);

	MAKE_STRING_VALUE(value, b);
	TEST_CHECK(setAttr(result, schema, 1, value));
	freeVal(value);

	MAKE_VALUE(value, DT_INT, c);
	TEST_CHECK(setAttr(result, schema, 2, value));
	freeVal(value);

	return result;
}
