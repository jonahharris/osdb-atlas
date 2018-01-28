// ****************************************************************************
// * atlas_test.cpp - Simple test/sample code for Atlas.  Good regression     *
// * testing stuff as well.                                                   *
// * (c) 2002,2003 Shawn Houser, All Rights Reserved                          *
// * This property and it's ancillary properties are completely and solely    *
// * owned by Shawn Houser, and no part of it is a work for hire, or in any   *
// * way the property of any other.                                           *
// ****************************************************************************
// ****************************************************************************
// *  This program is free software; you can redistribute it and/or modify    *
// *  it under the terms of the GNU General Public License as published by    *
// *  the Free Software Foundation, version 2 of the License.                 *
// *                                                                          *
// *  This program is distributed in the hope that it will be useful,         *
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
// *  GNU Library General Public License for more details.                    *
// *                                                                          *
// *  You should have received a copy of the GNU General Public License       *
// *  along with this program; if not, write to the Free Software             *
// *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,   *
// *  USA.                                                                    *
// *                                                                          *
// *  Other license options may possibly be arranged with the author.         *
// ****************************************************************************

#include <stdlib.h>                                                             // Standard library header we need
#include <string.h>
#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#ifdef      AT_WIN32
    #include	<windows.h>
#else
    #include    <unistd.h>
    #include    <sys/types.h>
    #include    <sys/ipc.h>
    #include    <sys/sem.h>
#endif

#include "general.h"                                                            // Atlas specific headers
#include "sem.h"
#include "memory.h"
#include "cgi.h"
#include "support.h"
#include "table.h"
#include "btree.h"
#include "template.h"
#include "session.h"
#include "arithmetic.h"
#ifdef  AT_USE_BKDB
    #include "vtable.h"
#endif
#include "testdata.h"

void    Cleanup();                                                              // Our cleanup routine

#define     NAME        35                                                      // The sizes of our test record fields
#define     MI          1
#define     ADDRESS     35
#define     CITY        20
#define     STATE       2
#define     POSTAL      5
#define     EMAIL       50
#define     PHONE       20
struct  UserDemo {                                                              // The test record stucture
    ATLOCK              ALock;                                                  // Record lock
    volatile    long    Count;                                                  // Operations count
    volatile    long    Status;                                                 // Current status
    volatile    long    CustomerID;
    volatile    char    FName[NAME + 1];
    volatile    char    M[MI + 1];
    volatile    char    LName[NAME + 1];
    volatile    char    Addr1[ADDRESS + 1];
    volatile    char    Addr2[ADDRESS + 1];
    volatile    char    City[CITY + 1];
    volatile    char    State[STATE + 1];
    volatile    char    Postal[POSTAL + 1];
    volatile    char    Email[EMAIL + 1];
    volatile    char    Phone1[PHONE + 1];
    volatile    char    Phone2[PHONE + 1];
};
typedef struct UserDemo Demo;
struct  ConcurrencyDemo {                                                       // The structure used for the ATTable concurrency test
    volatile    ULONG   Kilroy;                                                 // My kilroy
    volatile    long    Key;                                                    // Key for this record
    volatile    long    Status;                                                 // Status of this record
    volatile    long    Count;                                                  // Number of operations on this record
    volatile    long    Um;                                                     // Troubleshooting aid
    struct  ConcurrencyDemo *CD;                                                // Ptr to this record where it is stored in the table
};
typedef struct ConcurrencyDemo CDemo;

Demo    *Users = NULL;                                                          // An often shared allocation of memory for test data
int     CreateData(int Number, Demo *Allocated);                                // This routine creates test data for our tests

#define BUFFERSIZE      65536                                                   // Size of a general test buffer to create
char    Buffer[BUFFERSIZE];                                                     // Actual test buffer

#define BTINC           0                                                       // This allows you to step up all higher associated the IPC keys together as a group- useful in debugging




#define TTKEYLOW        (775544 + BTINC)
#define TTKEYHIGH       (776544 + BTINC)

#define STKEYLOW        (773544 + BTINC)
#define STKEYHIGH       (774544 + BTINC)


#define VTTEST          (91929811 + BTINC)



struct VTTest {
    char    Key[12];
    char    Data[200];
};
typedef struct VTTest VTT;

ATBTree         BTree, Email;                                                   // Object declarations- global because it makes cleanup easier
ATSharedTable   Table;
ATKernelSem     Sem;
ATSharedMem     Mem;
ATXTemplate     Template;
ATSession	    Session;
#ifdef  AT_USE_BKDB
    ATVTable        VTable;
#endif

long LongCompare(void *P1, void *P2, long Size);                                // This is one of our test comparison routines
void *MakeLongKey(void *Tuple);                                                 // This is one of our test make key routines
void *MakeCustomerIDKey(void *Tuple);                                           // This is one of our test make key routines
void *MakeEmailKey(void *Tuple);                                                // This is one of our test make key routines
long StringCompare(void *P1, void *P2, long Size);                              // This is one of our test comparison routines
void *VTMakeKey(void *inKey);



// ****************************************************************************
//                      TESTS CONFIGURATION
// ****************************************************************************

                                                                                // *** KernelSemaphores Config
#define KERNEL_SEM_LOOPS        10                                              // Number of reps for the kernel sem test

                                                                                // *** SharedMemory Config
#define SHARED_MEMORY_REPS      15                                              // Number of times to repeat the shared memory test

                                                                                // *** ScratchMemory Config
#define SCRATCH_MEM_RESETS      2500                                            // Number of resets to execute on the pool
#define SCRATCH_MEM_ALLOCS      500                                             // Number of allocs to attempt in the pool each reset (note the object must be large enough to allow)
#define SCRATCH_MEM_SIZE        1024000                                         // Size of the scratch mem object to create
#define SCRATCH_MEM_GET         1000                                            // Number of bytes for each alloc (never less than 7 bytes for this test purpose)

                                                                                // *** SpinLocks Config
#define SPIN_LOCK_LOOPS         10                                              // Number of reps for the spin lock tests

                                                                                // *** ShareLocks Config
#define SHARE_LOCK_LOOPS        10                                              // Number of reps for the share lock tests

                                                                                // *** Atomics Config
#define ATOMIC_REPS             10                                              // Number of times to repeat the atomics tests
#define ATOMIC_OPS              2000000                                         // Number of operations to perform for each step of the tests

                                                                                // *** Tables Config
#define TABLE_FASTLOOSE                                                         // Leave defined for fast/loose operation in the table test, or comment out see more laid back approach
#define TABLE_CONCURRENCY_RUN   1500000                                         // Number of reps to make on the table concurrency test
#define TABLE_UPDATE            (TABLE_CONCURRENCY_RUN / 50)                    // How many times during the concurrency test do we want updates
#define TABLE_CONCURRENCY_DATA  25                                              // Size of the table to create (per process) for the concurrency test
                                                                                // Note we should use a fairly small number for this test to ensure there is LOTS of contention
#define TABLE_IPC               (787129 + BTINC)                                // A unique IPC for the tables test
#define CTIALLOCSIZE            9                                               // Concurrency test initial alloc size
#define CTGALLOCSIZE            11                                              // Concurrency test growth alloc size
// NOTE: If TABLE_DATA <= (DELETETIPS * 2) there will be failures!!!
#define TABLE_DATA              1000                                            // Number of test records to create for the main table tests- go as big or small as you'd like
#define IALLOCSIZE              100                                             // Initial allocation size for our main test table
#define GALLOCSIZE              150                                             // Growth allocations size for our main test table
#define DELETETIPS              300                                             // This is a boundary test- how many records to delete of each tip of the table
Demo    Dels[DELETETIPS];                                                       // The array we use to keep a copy of the deleted records

                                                                                // *** BTree Config
#define BTTESTTABLE             (2000000 + BTINC)                                // IPC Key for the first BTree test
#define BTTEST1KEYSPER          100                                             // Keys per page in the first BTree test
#define BTTEST1ALLOC            ((BTTEST1SIZE / BTTEST1KEYSPER) / 12 )          // Pages to allocate each time for the first BTree test
#define BTTEST1SIZE             1400                                            // Number of tuples to create for the first BTree test
#define BTTEST2SIZE             1500                                            // Number of tuples to create for the second BTree test
#define BTTEST2KEYSPER          50                                              // Keys per page in the second BTree test
#define BTTEST2ALLOC            ((BTTEST2SIZE / BTTEST2KEYSPER) / 5 )           // Pages to alloc each time for the 2nd Btree test
//#define BTTEST2ALLOC            5                                               // Pages to alloc each time for the 2nd Btree test
#define BTTEST3SIZE             1000                                            // Number of tuples to create for the 2rd BTree test
#define BTTEST3KEYSPER          25                                             // Keys per page in the third BTree test
//#define BTTEST3ALLOC            ((BTTEST3SIZE / BTTEST3KEYSPER) / 7 )         // Pages to alloc each time for the 3rd Btree test
#define BTTEST3ALLOC            10                                              // Pages to alloc each time for the 3rd Btree test
#define BTTEST3REPS             1000                                            // Reps to run the 3rd BTree test for
#define BTTEST3UPDATE           (BTTEST3REPS / 100)                             // Number of updates to print during the 3rd test
#define EMAILTESTTABLE          (1000000 + BTINC)                               // IPC for the email test table

                                                                                // *** ATVTable config
#define VTTESTDATAPATH "/vhosts/atlashome.org/www/atlas/testdata/testvt/"

// **************************************************************************** BTrees
int     BTrees() {                                                              // Test the BTrees
    long    Kilroy = 1, i, Result, Adds = 0, Deletes = 0, o;
    ATTuple *Found, *CData;
    char    Scratch[64];
    Demo    Test, *Dptr;
    long    Interval = 0, Cumu = 0, Validate = 0;

    printf("Testing BTrees...\r\n");
    printf("This uses tables, so test them first!\r\n");
    printf("You should run another atlas_test process at the same time to\r\n");
    printf("verify operation. (preferably in side by side windows so you can\r\n");
    printf("see the volley back and forth).\r\n\r\n");

    printf("Initializing the sems...\r\n");
    Result = ATInitSems();                                                      // We'll need a semaphore to test two processes at once (just to coordinate)
    if ( Result != ATERR_SUCCESS ) {
        printf("Failed to init the sems!  Test failure!\r\n");
        return 0;
    }
    Result = Sem.Create(78062 + BTINC);                                         // Create a kernel semaphore
    if ( Result != ATERR_SUCCESS ) {                                            // If it fails I may note be the first one up
        printf("Create() failed- trying Open() in case it already exists...\r\n");
        Result = Sem.Open(78062 + BTINC);                                       // So try open instead
        if ( Result != ATERR_SUCCESS ) {
            printf("Open() also failed- test failure.\r\n");
            return 0;
        }
        printf("Open() successful!\r\n");
        Kilroy = 2;                                                             // Since I am the second one up, set my kilroy to 2
        printf("I am the second proc up, so I will wait for the signal to start...\r\n");
        Sem.GetLock();                                                          // Wait here for the concurrency tests to start
        goto concurrency;
    }
    else {
        printf("Create() successful!\r\n");
        Sem.GetLock();                                                          // Make sure to lock the sem so the second guy will wait
        printf("You should also run another process to validate concurrency.\r\n");
        Kilroy = 1;                                                             // I am first up, so my kilroy is 1
    }
//goto concurrency;                                                               // Uncomment this to skip to the concurrency tests

    printf("Creating a table of long integers...\r\n");                         // Create a test table
    if ( (Result = Table.CreateTable(TABLE_IPC, sizeof(long), BTTEST1SIZE / 3,
            BTTEST1SIZE / 3, 1, 20, 5, Kilroy)) != ATERR_SUCCESS) {
        printf("Could not create table!  Test failure!\r\n");return 0;}

    printf("Creating a BTree of long integer keys for the table...\r\n");       // Now create a BTree to tie to the table we just created
    if ( (Result = BTree.Create(BTTESTTABLE,                                    // Systemwide unique IPC ID for this BTree- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                                &Table,                                         // Which table to create it for (pointer to a valid created ATSharedTable)
                                &LongCompare,                                   // Pointer to the function that will be called to compare the key values- should behave just like memcmp(), returning <0,0, or >0 integer.
                                &MakeLongKey,                                   // Pointer to the function that will create a key from a tuple
                                sizeof(long),                                   // Length of the keys
                                BTTEST1KEYSPER,                                 // Number of keys to store in BTree page.  Very important factor in balancing parallel performance.  Is tree read or write heavy?  Etc.
                                BTTEST1ALLOC,                                   // Number of BTree pages to allocate in a block each time growth is needed
                                AT_BTREE_PRIMARY,                               // Set this up as a primary key
                                Kilroy                                          // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                                )) != ATERR_SUCCESS) {
        printf("Could not create BTree!  Test failure!\r\n");return 0;}

    printf("Inserting %i records into table...\r\n", BTTEST1SIZE);              // Now fill the test table with integers
    for ( i = 0; i < BTTEST1SIZE; ++i ) {                                       // Which also updates our BTree of course
        Table.AddTuple((void*)&i);
    }
//    printf("Validating the BTree...\r\n");
//    BTree.CheckBTree();                                                         // This can be helpful if you are having REAL issues

    printf("Now making sure they are there with finds...\r\n");                 // Lets use find to verify they all made it
    for ( i = 0; i < BTTEST1SIZE; ++i ) {
        Found = BTree.FindTuple((void*)&i, AT_BTREE_READ_OPTIMISTIC, AT_BTREE_FINDDIRECT, sizeof(long));// Test both locking modes
        if ( !Found ) {                                                         // Make sure we found it
            printf("Could not find tuple %i!  Test failure!\r\n", i);return 0;}
        if ( (*(long*)Found) != i ) {                                           // Make sure it is the right one
            printf("Return tuple %i for request %i!  Test failure!\r\n", *(long*)Found, i);return 0;}
        Found = BTree.FindTuple((void*)&i, AT_BTREE_READ_CRABLOCK, AT_BTREE_FINDDIRECT, sizeof(long));// Test both locking modes
        if ( !Found ) {                                                         // Make sure we found it
            printf("Could not find tuple %i!  Test failure!\r\n", i);return 0;}
        if ( (*(long*)Found) != i ) {                                           // Make sure it is the right one
            printf("Return tuple %i for request %i!  Test failure!\r\n", *(long*)Found, i);return 0;}
    }
    printf("Passed...\r\n");

    printf("Scroll forwards...\r\n");                                           // Let's verify every record scrolling forward
    i = 0;
    if ( !(Found = BTree.SetCursor((void*)&i,AT_BTREE_FINDDIRECT, sizeof(long))) ) {// Start at the beginning of the index
        printf("Could not find tuple 0!  Test failure!\r\n");return 0;}
    if ( *(long*)Found != 0 ) {
        printf("Tuple out of order at 0! (expected 0, found %i) Test failure!\r\n", *(long*)Found);return 0;}
    for ( i = 1; i < BTTEST1SIZE; ++i ) {                                       // Scroll thru all the records
        if ( !(Found = BTree.CursorNext()) ) {
            printf("Could not find tuple %i!  Test failure!\r\n", i);return 0;}
        if ( *(long*)Found != i ) {
            printf("Tuple out of order at %i! (expected %i, found %i) Test failure!\r\n", i, i, *(long*)Found);return 0;}
    }
    if ( (Found = BTree.CursorNext()) ) {                                       // Make sure that is all of them
        printf("Found too many! (Next returned %i)  Test failure!\r\n", *(long*)Found);return 0;}
    printf("Passed...\r\n");

    printf("Scroll backwards...\r\n");                                          // Let's verify every record scrolling backward
    i = BTTEST1SIZE - 1;
    if ( !(Found = BTree.SetCursor((void*)&i, AT_BTREE_FINDDIRECT, sizeof(long))) ) {// Start at the end of the index
        printf("Could not find tuple %i!  Test failure!\r\n", i);return 0;}
    if ( *(long*)Found != i ) {
        printf("Tuple out of order at %i! (expected %i, found %i) Test failure!\r\n", i, i, *(long*)Found);return 0;}
    for ( i = i - 1; i >= 0; --i ) {
        if ( !(Found = BTree.CursorPrev()) ) {                                  // Scroll thru all the records
            printf("Could not find tuple %i!  Test failure!\r\n", i);return 0;}
        if ( *(long*)Found != i ) {
            printf("Tuple out of order at %i! (expected %i, found %i) Test failure!\r\n", i, i, *(long*)Found);return 0;}
    }
    if ( (Found = BTree.CursorPrev()) ) {                                       // Make sure that is all of them
        printf("Found too many! (Next returned %i)  Test failure!\r\n", *(long*)Found);return 0;}
    printf("Passed...\r\n");
    BTree.FreeCursor();                                                         // Free up our cursor and its locks

    printf("Full check of tree...\r\n");
    BTree.CheckBTree();                                                         // Run a comprehensive check on the tree
    printf("Passed...\r\n");

    printf("Testing Close()...\r\n");
    if ( ((Result = BTree.Close()) != ATERR_SUCCESS) || (Table.CloseTable()) ) {// Close the BTree and close the table
        printf("Close failed!  Test Failure!\r\n"); return 0;}

    printf("Creating a customer table with %i entries... \r\n", BTTEST2SIZE);   // Let's create a more substantial table
    if ( (Result = Table.CreateTable(
                            TABLE_IPC,                                          // Systemwide unique IPC ID for this table- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                            sizeof(Demo),                                       // Size of the tuples in bytes
                            BTTEST2SIZE /3,                                     // Number of records to alloc initially
                            BTTEST2SIZE /3,                                     // Chunks of records to alloc as the table grows
                            1,                                                  // Set to true means that changes made to the table are queued until flushed, false means always flush changes to disk
                            3,                                                  // Number of delete lists
                            3,                                                  // Number of add lists
                            Kilroy                                              // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                            )) != ATERR_SUCCESS) {
        printf("Could not create table!  Test failure!\r\n");return 0;}

    printf("Creating a BTree of unique customer ID's for the table...\r\n");    // Let's create a primary key database for it
    if ( (Result = BTree.Create(BTTESTTABLE,                                    // Systemwide unique IPC ID for this BTree- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                                &Table,                                         // Which table to create it for (pointer to a valid created ATSharedTable)
                                &LongCompare,                                   // Pointer to the function that will be called to compare the key values- should behave just like memcmp(), returning <0,0, or >0 integer.
                                &MakeCustomerIDKey,                             // Pointer to the function that will create a key from a tuple
                                sizeof(long),                                   // Length of the keys
                                BTTEST2KEYSPER,                                 // Number of keys to store in BTree page.  Very important factor in balancing parallel performance.  Is tree read or write heavy?  Etc.
                                BTTEST2ALLOC,                                   // Number of BTree pages to allocate in a block each time growth is needed
                                AT_BTREE_PRIMARY,                               // Set this up as a primary key
                                Kilroy                                          // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                                )) != ATERR_SUCCESS) {
        printf("Could not create BTree!  Test failure!\r\n");return 0;}

    printf("Creating a BTree of non-unique emails for the table...\r\n");       // Create a secondary key for the table as well
    if ( (Result = Email.Create(EMAILTESTTABLE,                                 // Systemwide unique IPC ID for this BTree- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                                &Table,                                         // Which table to create it for (pointer to a valid created ATSharedTable)
                                &StringCompare,                                 // Pointer to the function that will be called to compare the key values- should behave just like memcmp(), returning <0,0, or >0 integer.
                                &MakeEmailKey,                                  // Pointer to the function that will create a key from a tuple
                                sizeof(Test.Email),                             // Length of the keys
                                BTTEST2KEYSPER,                                 // Number of keys to store in BTree page.  Very important factor in balancing parallel performance.  Is tree read or write heavy?  Etc.
                                BTTEST2ALLOC,                                   // Number of BTree pages to allocate in a block each time growth is needed
                                AT_BTREE_SECONDARY,                             // Set this up as a secondary key
                                Kilroy                                          // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                                )) != ATERR_SUCCESS) {
        printf("Could not create BTree!  Test failure!\r\n");return 0;}

    printf("Adding the records...\r\n");
    if (CreateData(BTTEST2SIZE, NULL))  {                                       // Create our test data set
        printf("Out of memory!\r\nStopping test!\r\n"); return 0;}

    for ( i = 0; i < BTTEST2SIZE; ++i ) {                                       // Insert the data into the table
        Table.AddTuple((void*)&(Users[i]));
    }

    printf("Full check of tree...\r\n");                                        // Run a full check of the BTree
    BTree.CheckBTree();
    Email.CheckBTree();
    printf("Passed...\r\n");

    printf("Checking primary integrity...\r\n");                                // Make sure we can find every tuple via the primary key
    for ( i = 0; i < BTTEST2SIZE; ++i ) {                                       // Run thru every record number
        Found = BTree.FindTuple((void*)&i, AT_BTREE_READ_OPTIMISTIC,AT_BTREE_FINDDIRECT, sizeof(long));
        if ( Found ) {                                                          // As long as we find it
            if ( memcmp((void*)Found, (void*)&(Users[i]), sizeof(Demo)) ) {     // Make sure it is an exact copy
                printf("Bad tuple at %i!  Test failure!\r\n", i); return 0;}
        }
        else {
            printf("Tuple not found at %i!  Test failure!\r\n", i); return 0;}
    }
    printf("Passed.\r\n");

    printf("Testing primary key constraint...\r\n");                            // Test the primary key constraint (no dupes allowed)
    for ( i = 0; i < BTTEST2SIZE; ++i ) {                                       // Run thru every record number
        if ( (Found = Table.AddTuple((void*)&(Users[i]))) ) {                   // Try to insert every known dupe
            printf("Failed primary key constraint at %i!  Test failure!\r\n", i); return 0;}
    }
    printf("Passed.\r\n");

    printf("Checking primary integrity...\r\n");                                // Now make sure the above did not alter the table integrity
    for ( i = 0; i < BTTEST2SIZE; ++i ) {                                       // Run thru every record number
    Found = BTree.FindTuple((void*)&i, AT_BTREE_READ_OPTIMISTIC,AT_BTREE_FINDDIRECT, sizeof(long));
        if ( Found ) {                                                          // As long as we find it
            if ( memcmp((void*)Found, (void*)&(Users[i]), sizeof(Demo)) ) {     // Make sure it is an exact copy
                printf("Bad tuple at %i!  Test failure!\r\n", i); return 0;}
        }
        else {
            printf("Tuple not found at %i!  Test failure!\r\n", i); return 0;}
    }
    printf("Passed.\r\n");

    /* NOTE: MANY OF THESE TESTS MAY FAIL IF YOU MAKE THE DATASET TOO SMALL- NOT BECAUSE OF ANY PROBLEM WITH ATLAS BUT
    BECAUSE THE TEMP DATA DOES NOT CONTAIN THE DESIRED RECORDS! SO, SUSPECT THAT CAUSE FIRST!*/
    printf("Scroll forward with partial find first...\r\n");                    // Set the cursor to where the very first "a" would start
    if ( !(Dptr = (Demo*)Email.SetCursor((void*)"a", AT_BTREE_FINDFIRST, 1)) ) {
        printf("Failed SetCursor! Test failure!\r\n");return 0;}
//        printf("%s\r\n",Dptr->Email);                                         // Uncomment to print the records DUPES ARE FINE- JUST THE TESTDATA
    i = 0;
    while ( (Dptr = (Demo*)Email.CursorNext()) ) {                              // Now scroll thru all the records
//        printf("%s\r\n",Dptr->Email);                                         // Uncomment to print the records DUPES ARE FINE- JUST THE TESTDATA
        i++;
    }
    if ( i != BTTEST2SIZE - 1 ) {                                               // Make sure we se found the right number
        // NOTE: THIS MIGHT FAIL IF YOUR TABLE IS SO SMALL THAT THERE ARE NO "a" TUPLES!  And that is just fine.
        printf("Found %i instead of %i!  Test failure!\r\n", i + 1, BTTEST2SIZE); }//return 0;}

    printf("SetToStart and scroll forward...\r\n");
    if ( !(Dptr = (Demo*)Email.SetCursorToStart()) ) {                          // Set to the start of the index
        printf("Failed SetCursor! Test failure!\r\n");return 0;}
//        printf("%s\r\n",Dptr->Email);                                         // Uncomment to print the records DUPES ARE FINE- JUST THE TESTDATA
    strcpy(Scratch, (char*)(Dptr->Email));
    i = 0;
    while ( (Dptr = (Demo*)Email.CursorNext()) ) {                              // Scroll thru all the records
        if ( (Result = stricmp(Scratch, (char*)(Dptr->Email))) > 0 ) {          // Make sure each record is greater or equal to the last on
            printf("Out of order! O: %s N: %s Test failure!\r\n", Scratch, Dptr->Email);
            return 0;}
        strcpy(Scratch, (char*)(Dptr->Email));                                  // Save a copy for the next comparison
//        printf("%s\r\n",Dptr->Email);                                         // Uncomment to print the records DUPES ARE FINE- JUST THE TESTDATA
        i++;
    }
    if ( i != BTTEST2SIZE - 1 ) {                                               // Make sure we found the right number
        printf("Found %i instead of %i!  Test failure!\r\n", i + 1, BTTEST2SIZE); return 0;}

    printf("SetToEnd and scroll backwards...\r\n");
    if ( !(Dptr = (Demo*)Email.SetCursorToEnd()) ) {                            // Set to the end of the index
        printf("Failed SetCursor! Test failure!\r\n");return 0;}
//        printf("%s\r\n",Dptr->Email);                                         // Uncomment to print the records DUPES ARE FINE- JUST THE TESTDATA
    i = 0;
    while ( (Dptr = (Demo*)Email.CursorPrev()) ) {                              // Scroll backwards thru all the records
//        printf("%s\r\n",Dptr->Email);                                         // Uncomment to print the records DUPES ARE FINE- JUST THE TESTDATA
        i++;
    }
    if ( i != BTTEST2SIZE - 1 ) {                                               // Make sure we found the right number
        printf("Found %i instead of %i!  Test failure!\r\n", i + 1, BTTEST2SIZE); return 0;}

    /* NOTE: MANY OF THESE TESTS MAY FAIL IF YOU MAKE THE DATASET TOO SMALL- NOT BECAUSE OF ANY PROBLEM WITH ATLAS BUT
    BECAUSE THE TEMP DATA DOES NOT CONTAIN THE DESIRED RECORDS! SO, SUSPECT THAT CAUSE FIRST!*/
    printf("Scroll backward with partial find last...\r\n");
    if ( !(Dptr = (Demo*)Email.SetCursor((void*)"v", AT_BTREE_FINDLAST, 1)) ) { // Do a find last partial on the last known temp data record
        printf("Failed SetCursor! Test failure!\r\n");return 0;}
//        printf("%s\r\n",Dptr->Email);                                         // Uncomment to print the records DUPES ARE FINE- JUST THE TESTDATA
    i = 0;
    while ( (Dptr = (Demo*)Email.CursorPrev()) ) {                              // Now scroll back thru the records
//        printf("%i %i %s\r\n",i,Dptr->CustomerID,Dptr->Email);
        i++;
    }
    if ( i != BTTEST2SIZE - 1 ) {                                               // Make sure we found the right number
        printf("Found %i instead of %i!  Test failure!\r\n", i + 1, BTTEST2SIZE);return 0;}

    BTree.FreeCursor();                                                         // Release our cursors (and their locks!)
    Email.FreeCursor();
    printf("Testing deletes...\r\n");                                           // Test deletes

    for ( i = 0; i < BTTEST2SIZE; ++i) {                                        // Run thru all the records
        if ( !(i % 5) ) {                                                       // Every fifth record
            if ( !(Found = BTree.FindTuple((void*)&i, AT_BTREE_READ_OPTIMISTIC,AT_BTREE_FINDDIRECT, sizeof(long))) ) {// Find the tuple by its key
                printf("Failed find for delete at %i!  Test Failure!\r\n", i);return 0;}
            if ( (Result = Table.DeleteTuple()) != ATERR_SUCCESS) {             // Delete the tuple
                printf("Failed on tuple delete at %i!  Test failure!\r\n", i);return 0;}
        }
    }

    for ( i = 0; i < BTTEST2SIZE; ++i) {                                        // Now let's check to make sure they are gone (and nothing else is)
        Found = BTree.FindTuple((void*)&i, AT_BTREE_READ_OPTIMISTIC,AT_BTREE_FINDDIRECT, sizeof(long));// Find each tuple
        if ( !(i % 5) ) {                                                       // If this is the fifth record (and should therefore have been deleted)
            if ( Found ) {                                                      // If it was found that is BAD
                printf("Found a deleted record via primary at %i!  Test Failure!\r\n", i);return 0;}
            Dptr = (Demo*)Email.SetCursor((void*)(Users[i].Email), AT_BTREE_FINDFIRST, 3);// Now let's test the secondary too- try to locate to it
            while ( Dptr ) {                                                    // As long as we find any matching emails
                if ( Dptr->CustomerID == i ) {                                  // See if it is the right customer ID
                    printf("Found a deleted record via primary at %i!  Test Failure!\r\n", i);return 0;}
                if ( strnicmp((char*)Dptr->Email,(char*)Users[i].Email, 3 ) )   // Stop when the emails start to change
                    break;
                Dptr = (Demo*)Email.CursorNext();                               // Otherwise keep looking
            }
        }
        else {                                                                  // This is not supposed to be deleted
            if ( !Found ) {                                                     // If we don't find it that is bad
                printf("Lost a valid record via primary at %i!  Test Failure!\r\n", i); return 0;}
            Dptr = (Demo*)Email.SetCursor((void*)(Users[i].Email), AT_BTREE_FINDFIRST, 3); // Now let's check the secondary
            while ( Dptr ) {                                                    // As long as we find any matching emails
                if ( Dptr->CustomerID == i )                                    // See if it is the right customer ID
                    break;                                                      // If it is we are good to go
                if ( strnicmp((char*)Dptr->Email,(char*)Users[i].Email, 3 ) ) { // If the emails start to change
                    printf("(1)Lost a valid record via secondary at %i!  Test Failure!\r\n", i);
                    return 0;}
                Dptr = (Demo*)Email.CursorNext();                               // Otherwise keep looking
            }
            if ( !Dptr ) {                                                      // If we never found it
                printf("(2)Lost a valid record via secondary at %i!  Test Failure!\r\n", i);return 0;}
            Dptr = (Demo*)Email.SetCursor((void*)(Users[i].Email), AT_BTREE_FINDLAST, 3); // Backwards check as well
            while ( Dptr ) {                                                    // As long as we find any matching emails
                if ( Dptr->CustomerID == i )                                    // See if it is the right customer ID
                    break;                                                      // If it is we are good to go
                if ( strnicmp((char*)Dptr->Email,(char*)Users[i].Email, 3 ) ) { // If the emails start to change
                    printf("(3)Lost a valid record via secondary at %i!  Test Failure!\r\n", i);
                    return 0;}
                Dptr = (Demo*)Email.CursorPrev();                               // Otherwise keep looking
            }
            if ( !Dptr ) {                                                      // If we never found it
                printf("(4)Lost a valid record via secondary at %i!  Test Failure!\r\n", i);return 0;}

            if ( memcmp((void*)&(Users[i]), (void*)Found, sizeof(Demo)) ) {     // Make sure what we found is exactly correct
                printf("Incorrect or corrupt record at %i!  Test Failure!\r\n", i); return 0;}
        }
    }

    printf("Running full checks of the trees...\r\n");                          // Thoroughly check out the BTrees
    BTree.FreeCursor();                                                         // Release our cursors (and their locks!)
    Email.FreeCursor();
    BTree.CheckBTree();
    Email.CheckBTree();
    printf("Passed.\r\n");

    printf("Closing...\r\n");                                                   // Close both the BTrees and the table
    if ( ((Result = BTree.Close()) != ATERR_SUCCESS) || (Email.Close()) || (Table.CloseTable()) ) {
        printf("Close failed!  Test Failure!\r\n"); return 0;}

concurrency:                                                                    // Start the concurrency tests
    printf("Preparing for concurrency test...\r\n");
    if ( Users ) {                                                              // If there is already test data (might have been skipped)
        delete Users;                                                           // Delete it
        Users = NULL;
    }
    Result = Mem.CreateSharedMem(234235 + BTINC, BTTEST3SIZE * sizeof(Demo));   // Create a shared memory block to house out data
    if ( Result != ATERR_SUCCESS ) {
        printf("Create() failed- trying Attach() in case it already exists...\r\n");
        Result = Mem.AttachSharedMem(234235 + BTINC);                           // If I could't create it, I'll try attaching to it instead
        if ( Result != ATERR_SUCCESS ) {
            printf("Attach() also failed- test failure.\r\n");
            return 0;
        }
        printf("Attach() successful!\r\n");
    }
    else {
        printf("Create() successful!\r\n");
    }
    CData = Mem.GetBasePointer();                                               // Get the base ptr to the memory

    if ( Kilroy == 1 ) {                                                        // If I am the first one up
        CreateData(BTTEST3SIZE, (Demo*)CData);                                  // Create the random test data into our shared block
        printf("Creating a customer table with %i entries... \r\n", BTTEST3SIZE);
        if ( (Result = Table.CreateTable(                                       // Create a table to put the data into
                            TABLE_IPC,                                          // Systemwide unique IPC ID for this table- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                            sizeof(Demo),                                       // Size of the tuples in bytes
                            BTTEST3SIZE / 10,                                   // Number of records to alloc initially
                            BTTEST3SIZE / 10,                                   // Chunks of records to alloc as the table grows
                            1,                                                  // Set to true means that changes made to the table are queued until flushed, false means always flush changes to disk
                            12,                                                 // Number of delete lists
                            12,                                                 // Number of add lists
                            Kilroy                                              // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                                )) != ATERR_SUCCESS) {
            printf("Could not create table!  Test failure!\r\n");return 0;}

        printf("Creating a BTree of unique customer ID's for the table...\r\n");// Create a primary key for the table
        if ( (Result = BTree.Create(BTTESTTABLE,                                // Systemwide unique IPC ID for this BTree- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                                &Table,                                         // Which table to create it for (pointer to a valid created ATSharedTable)
                                &LongCompare,                                   // Pointer to the function that will be called to compare the key values- should behave just like memcmp(), returning <0,0, or >0 integer.
                                &MakeCustomerIDKey,                             // Pointer to the function that will create a key from a tuple
                                sizeof(long),                                   // Length of the keys
                                BTTEST3KEYSPER,                                 // Number of keys to store in BTree page.  Very important factor in balancing parallel performance.  Is tree read or write heavy?  Etc.
                                BTTEST3ALLOC,                                   // Number of BTree pages to allocate in a block each time growth is needed
                                AT_BTREE_PRIMARY,                               // Set this up as a primary key
                                Kilroy                                          // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                                )) != ATERR_SUCCESS) {
            printf("Could not create BTree!  Test failure!\r\n");return 0;}

        printf("Creating a BTree of non-unique emails for the table...\r\n");   // Create a secondary key for the table
        if ( (Result = Email.Create(EMAILTESTTABLE,                             // Systemwide unique IPC ID for this BTree- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                                &Table,                                         // Which table to create it for (pointer to a valid created ATSharedTable)
                                &StringCompare,                                 // Pointer to the function that will be called to compare the key values- should behave just like memcmp(), returning <0,0, or >0 integer.
                                &MakeEmailKey,                                  // Pointer to the function that will create a key from a tuple
                                sizeof(Test.Email),                             // Length of the keys
                                BTTEST3KEYSPER,                                 // Number of keys to store in BTree page.  Very important factor in balancing parallel performance.  Is tree read or write heavy?  Etc.
                                BTTEST3ALLOC,                                   // Number of BTree pages to allocate in a block each time growth is needed
                                AT_BTREE_SECONDARY,                             // Set this up as a secondary key
                                Kilroy                                          // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                                )) != ATERR_SUCCESS) {
            printf("Could not create EMail!  Test failure!\r\n");return 0;}

        printf("I'll pause a moment to make sure the other proc is ready...\r\n");
        sleep(3);                                                               // Make sure the other guy makes it up in time
        Sem.FreeLock();                                                         // Now release the sem to allow th other guy to start
// This snippet would let both procs run, but only one at a time...
//printf("Now I'll wait for him to finish...\r\n");
//        Sem.GetLock();
//printf("My turn...\r\n");
//        Sem.FreeLock();
    }
    else {                                                                          // I must have been the 2nd one up
        printf("Opening table & BTrees...\r\n");
        if ( (Result = Table.OpenTable(                                             // Open the customer table
                                TABLE_IPC,                                          // Systemwide unique IPC ID for this table- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                                Kilroy                                              // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                                )) != ATERR_SUCCESS) {
            printf("Could not open table!  Test failure!\r\n");return 0;}

        if ( (Result = BTree.Open(                                                  // Open up the existing primary BTree
                                BTTESTTABLE,                                        // Systemwide unique IPC ID for this BTree- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                                &Table,                                             // Which table to create it for (pointer to a valid created ATSharedTable)
                                &LongCompare,                                       // Pointer to the function that will be called to compare the key values- should behave just like memcmp(), returning <0,0, or >0 integer.
                                &MakeCustomerIDKey,                                 // Pointer to the function that will create a key from a tuple
                                Kilroy                                              // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                                )) != ATERR_SUCCESS) {
            printf("Could not open BTree!  Test failure!\r\n");return 0;}

                                                                                    // Open up the secondary key
        if ( (Result = Email.Open(  EMAILTESTTABLE,                                 // Systemwide unique IPC ID for this BTree- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                                    &Table,                                         // Which table to create it for (pointer to a valid created ATSharedTable)
                                    &StringCompare,                                 // Pointer to the function that will be called to compare the key values- should behave just like memcmp(), returning <0,0, or >0 integer.
                                    &MakeEmailKey,                                  // Pointer to the function that will create a key from a tuple
                                    Kilroy                                          // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                                    )) != ATERR_SUCCESS) {
            printf("Could not open Email!  Test failure!\r\n");return 0;}
        Users = (Demo*)CData;
    }

    printf("Starting %i repetitions of test....\r\n", BTTEST3REPS);                 // Now start the test
    if  ( Kilroy == 1 ) {                                                           // If I am the first guy up
        for ( o = 0; o < BTTEST3REPS; ++o) {                                        // Let's repeat the entire test the requested # of times
            for ( i = 0; i < BTTEST3SIZE; ++i ) {                                   // Let's run thru all the tuples forwards
                Result = (rand()) % 5;                                              // Get a random #
                if ( Result == 1) {                                                 // Got a 20% chance..
                    if ( (Result = ATBounceSpinLock(Kilroy, &(Users[i].ALock)) == ATERR_SUCCESS ) ) {// If I can lock this tuple's internal lock
                        if ( Users[i].Status ) {                                    // If this tuple is in the table let's delete it
                            Found = BTree.FindTuple((void*)&i, AT_BTREE_READ_CRABLOCK,AT_BTREE_FINDDIRECT, sizeof(long));// Locate it
                            if ( !Found ) {                                         // Big problem if I don't find it!
                                printf("Lost a record at %i!  Test failure! \r\n", i);return 0;}
                            Table.LockTuple();                                      // Lock the tuple first
                            if ( (Result = Table.DeleteTuple()) ) {                 // Then delete it
                                printf("Failed to delete a tuple at %i!  Test failure! \r\n", i);return 0;}
                            Users[i].Status = 0;                                    // Note that this is now a deleted tuple
                            ++Deletes;                                              // Track our total deletes
                        }
                        else {                                                      // If it is not in the table le'ts add it
                            Users[i].Count++;                                       // Inc this variable (acts like a version ctr for later validations)
                            Users[i].Status = 1;                                    // Mark that it is now inserted
                            if ( !(Dptr = (Demo*)Table.AddTuple((void*)&(Users[i]))) ) {// Add it in
                                printf("Failed adding a tuple at %i! Test failure!\r\n", i);return 0;}
                            Dptr->ALock = 0;                                        // Clear the tuple's internal lock
                            Table.UnlockTuple();                                    // Always unlock a tuple after inserting it
                            ++Adds;                                                 // Track our total adds
                        }
                        ATFreeSpinLock(Kilroy, &(Users[i].ALock));                  // Free the internal lock
                    }
                }
                else if ( Result == 2) {                                            // Got a 20% chance..
                    BTree.SetCursor((void*)&i, AT_BTREE_FINDDIRECT, sizeof(long));  // Do a direct find & cursor set on the current tuple w/ the primary
                    BTree.FreeCursor();                                             // Now free the cursor
                }
                else if ( Result == 3) {                                            // Got a 20% chance..
                    if ( (Email.SetCursor((void*)"a", AT_BTREE_FINDFIRST, 1)) ){    // Set the secondary index to the start of the a's with a find first partial
                        while ( Email.CursorNext() );                               // Scroll through the whole stinkin' index
                    }
                    Email.FreeCursor();                                             // Now free the cursor
                }
                else if ( Result == 4) {                                            // Got a 20% chance..
                    if ( !(Email.SetCursor((void*)"v", AT_BTREE_FINDLAST, 1)) ) {   // Set the secondary index to the end of the v's with a find last partial
                        while ( Email.CursorPrev() );                               // Scroll through the whole stinkin' index
                    }
                    Email.FreeCursor();                                             // Now free the cursor
                }
            }
            Interval++;                                                             // See if I need to post an update on our progress
            if ( Interval == BTTEST3UPDATE ) {
                Cumu += BTTEST3UPDATE;
                Interval = 0;
                printf("Working %i....\r\n", Cumu);
            }
        }
        printf("Task 1 finished successfully with %i adds and %i deletes.\r\n",     // All done with the test
            Adds, Deletes);
        printf("Waiting for task 2 to finish....\r\n");
        Sem.GetLock();                                                              // Wait for the second guy to finish
    }
    else {                                                                          // If I am the second guy up
        for ( o = 0; o < BTTEST3REPS; ++o) {                                        // Let's repeat the entire test the requested # of times
            for ( i = BTTEST3SIZE - 1; i >= 0; --i ) {                              // Let's run thru all the tuples backwards
                Result = (rand()) % 5;                                              // Get a random #
                if ( Result == 1) {                                                 // Got a 20% chance..
                    if ( (Result = ATBounceSpinLock(Kilroy, &(Users[i].ALock)) == ATERR_SUCCESS ) ) {// If I can lock this tuple's internal lock
                        if ( Users[i].Status ) {                                    // If this tuple is in the table let's delete it
                            Found = BTree.FindTuple((void*)&i, AT_BTREE_READ_CRABLOCK,AT_BTREE_FINDDIRECT, sizeof(long));// Locate it
                            if ( !Found ) {                                         // Big problem if I don't find it!
                                printf("Lost a record at %i!  Test failure! \r\n", i);return 0;}
                            Table.LockTuple();                                      // Lock the tuple first
                            if ( (Result = Table.DeleteTuple()) ) {                 // Then delete it
                                printf("Failed to delete a tuple at %i!  Test failure! \r\n", i);return 0;}
                            Users[i].Status = 0;                                    // Note that this is now a deleted tuple
                            ++Deletes;                                              // Track our total deletes
                        }
                        else {                                                      // If it is not in the table le'ts add it
                            Users[i].Count++;                                       // Inc this variable (acts like a version ctr for later validations)
                            Users[i].Status = 1;                                    // Mark that it is now inserted
                            if ( !(Dptr = (Demo*)Table.AddTuple((void*)&(Users[i]))) ) {// Add it in
                                printf("Failed adding a tuple at %i! Test failure!\r\n", i);return 0;}
                            Dptr->ALock = 0;                                        // Clear the tuple's internal lock
                            Table.UnlockTuple();                                    // Always unlock a tuple after inserting it
                            ++Adds;                                                 // Track our total adds
                        }
                        ATFreeSpinLock(Kilroy, &(Users[i].ALock));                  // Free the internal lock
                    }
                }
                else if ( Result == 2) {                                            // Got a 20% chance..
                    BTree.SetCursor((void*)&i, AT_BTREE_FINDDIRECT, sizeof(long));  // Do a direct find & cursor set on the current tuple w/ the primary
                    BTree.FreeCursor();                                             // Now free the cursor
                }
                else if ( Result == 3) {                                            // Got a 20% chance..
                    if ( (Email.SetCursor((void*)"a", AT_BTREE_FINDFIRST, 1)) ){    // Set the secondary index to the start of the a's with a find first partial
                        while ( Email.CursorNext() );                               // Scroll through the whole stinkin' index
                    }
                    Email.FreeCursor();                                             // Now free the cursor
                }
                else if ( Result == 4) {                                            // Got a 20% chance..
                    if ( !(Email.SetCursor((void*)"v", AT_BTREE_FINDLAST, 1)) ) {   // Set the secondary index to the end of the v's with a find last partial
                        while ( Email.CursorPrev() );                               // Scroll through the whole stinkin' index
                    }
                    Email.FreeCursor();                                             // Now free the cursor
                }
            }
            Interval++;                                                             // See if I need to post an update on our progress
            if ( Interval == BTTEST3UPDATE ) {
                Cumu += BTTEST3UPDATE;
                Interval = 0;
                printf("Working %i....\r\n", Cumu);
            }
        }
        Sem.FreeLock();                                                             // Let the first guy know I am done
        printf("Task 2 finished successfully with %i adds and %i deletes- leaving.\r\n", // All done with the test
            Adds, Deletes);
        printf("Task 1 will validate the structures & continue the test.\r\n");
        return 1;
    }

revalidate:
    printf("Validating the BTrees...\r\n");                                         // Do a comprehensive check of the btrees
    BTree.CheckBTree();
    Email.CheckBTree();
    printf("Passed.\r\nValidating the data's integrity & structures....\r\n");
    for ( i = 0; i < BTTEST3SIZE; ++i) {                                            // Loop for every record in our test
        Found = BTree.FindTuple((void*)&i, AT_BTREE_READ_CRABLOCK,AT_BTREE_FINDDIRECT,// Try to find each tuple
            sizeof(long));
        if ( Found && !(Users[i].Status) ) {                                        // If found and shouldn't have been...
            printf("Found a deleted tuple via primary at %i!  Test Failure!\r\n",i); return 0;}
        if ( !Found && (Users[i].Status) ) {                                        // If not found and should have been...
            printf("Lost a tuple via primary at %i!  Test Failure!\r\n",i); return 0;}
        if ( (Users[i].Status ) ) {                                                 // If the tuple was in the table
            if ( memcmp((void*)&(Users[i]), (void*)Found, sizeof(Demo)) ) {         // Make sure an exact match (includes versioning)
                printf("Found a corrupted tuple at %i!  Test Failure!\r\n",i); return 0;}
            Dptr = (Demo*)Email.SetCursor((void*)(Users[i].Email), AT_BTREE_FINDLAST, 3); // Set the last range of user's email
            while ( Dptr ) {                                                        // As long as there are still records
                if ( Dptr->CustomerID == i )                                        // If we find the right one
                    break;                                                          // Then we are done
                if ( strnicmp((char*)Dptr->Email,(char*)Users[i].Email, 3 ) ) {     // Stop when we leave this range
                    printf("(3)Lost a valid record via secondary at %i!  Test Failure!\r\n", i);return 0;}
                Dptr = (Demo*)Email.CursorPrev();                                   // Otherwise check the previous tuple
            }
            if ( !Dptr ) {                                                          // If I did not find it
                printf("(4)Lost a valid record via secondary at %i!  Test Failure!\r\n", i);return 0;}
            Dptr = (Demo*)Email.SetCursor((void*)(Users[i].Email), AT_BTREE_FINDFIRST, 3); // Forwards check
            while ( Dptr ) {                                                        // As long as there are still records
                if ( Dptr->CustomerID == i )                                        // If we find the right one
                    break;                                                          // Then we are done
                if ( strnicmp((char*)Dptr->Email,(char*)Users[i].Email, 3 ) ) {     // Stop when we leave this range
                    printf("(5)Lost a valid record via secondary at %i!  Test Failure!\r\n", i);return 0;}
                Dptr = (Demo*)Email.CursorNext();                                   // Otherwise check the next tuple
            }
            if ( !Dptr ) {                                                          // If I did not find it
                printf("(5)Lost a valid record via secondary at %i!  Test Failure!\r\n", i);return 0;}

            Table.ResetCursor();                                                    // Lets test the table itself- start at the beginning
            while( (Dptr = (Demo*)Table.NextTuple()) ) {                            // Search thru all the records
                if ( Dptr->CustomerID == i )                                        // As long as I find it
                    break;                                                          // Then we are done
            }
            if ( !Dptr ) {                                                          // If I did not find it
                printf("Could not find tuple %i via the table itself! Test failure!\r\n",i);}
        }
        else {                                                                      // If the tuple was not in the table
            Dptr = (Demo*)Email.SetCursor((void*)(Users[i].Email), AT_BTREE_FINDLAST, 3); // Set the last range of user's email
            while ( Dptr ) {                                                        // As long as there are still records
                if ( Dptr->CustomerID == i ) {                                      // If I found a deleted tuple
                    printf("(1)Found a deleted record via secondary at %i!  Test failure!\r\n",i);return 0;}
                if ( strnicmp((char*)Dptr->Email,(char*)Users[i].Email, 3 ) )       // Stop when I leave the range
                    break;
                Dptr = (Demo*)Email.CursorPrev();                                   // Otherwise keep looking
            }
            Dptr = (Demo*)Email.SetCursor((void*)(Users[i].Email), AT_BTREE_FINDFIRST, 3); // Forwards check
            while ( Dptr ) {                                                        // As long as there are still records
                if ( Dptr->CustomerID == i ) {                                      // If I found a deleted tuple
                    printf("(2)Found a deleted record via secondary at %i!  Test failure!\r\n",i);return 0;}
                if ( strnicmp((char*)Dptr->Email,(char*)Users[i].Email, 3 ) )       // Stop when I leave the range
                    break;
                Dptr = (Demo*)Email.CursorNext();                                   // Otherwise keep looking
            }

            Table.ResetCursor();                                                    // Lets test the table itself- start at the beginning
            while( (Dptr = (Demo*)Table.NextTuple()) ) {                            // Search thru all the records
                if ( Dptr->CustomerID == i ) {                                      // If I find a deleted entry
                    printf("Found a deleted record via the table itself at %i!  Test failure!\r\n",i);return 0;}
            }
        }
    }
    printf("Passed.\r\n");


    if ( Validate < 2 ) {                                                           // Makes sure we only do this twice
        printf("Testing WriteBTree...\r\n");                                        // Write out the primary BTree
        if ( (Result = BTree.WriteBTree("../testdata/BTree.btr")) != ATERR_SUCCESS ) {
            printf("Failed writing BTree!  Test failure!\r\n"); return 0;}          // Write out the secondary BTree
        if ( (Result = Email.WriteBTree("../testdata/Email.btr")) != ATERR_SUCCESS ) {
            printf("Failed writing Email!  Test failure!\r\n"); return 0;}
        if ( (Result = BTree.Close()) != ATERR_SUCCESS ) {                          // Close the primary
            printf("Failed closing BTree!  Test failure!\r\n"); return 0;}
        if ( (Result = Email.Close()) != ATERR_SUCCESS ) {                          // Close the secondary
            printf("Failed closing Email!  Test failure!\r\n"); return 0;}

        if ( Validate == 1 ) {                                                      // First time thru let's do a create/load
            printf("Testing LoadBTree...\r\n");                                     // Now let's recreate them
            if ( (Result = BTree.Create(BTTESTTABLE,                                // Systemwide unique IPC ID for this BTree- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                                    &Table,                                         // Which table to create it for (pointer to a valid created ATSharedTable)
                                    &LongCompare,                                   // Pointer to the function that will be called to compare the key values- should behave just like memcmp(), returning <0,0, or >0 integer.
                                    &MakeCustomerIDKey,                             // Pointer to the function that will create a key from a tuple
                                    sizeof(long),                                   // Length of the keys
                                    BTTEST3KEYSPER,                                 // Number of keys to store in BTree page.  Very important factor in balancing parallel performance.  Is tree read or write heavy?  Etc.
                                    BTTEST3ALLOC,                                   // Number of BTree pages to allocate in a block each time growth is needed
                                    AT_BTREE_PRIMARY,                               // Set this up as a primary key
                                    Kilroy                                          // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                                    )) != ATERR_SUCCESS) {
                printf("Could not create BTree!  Test failure!\r\n");return 0;}

            printf("Creating a BTree of non-unique emails for the table...\r\n");   // Create a secondary key for the table
            if ( (Result = Email.Create(EMAILTESTTABLE,                             // Systemwide unique IPC ID for this BTree- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                                    &Table,                                         // Which table to create it for (pointer to a valid created ATSharedTable)
                                    &StringCompare,                                 // Pointer to the function that will be called to compare the key values- should behave just like memcmp(), returning <0,0, or >0 integer.
                                    &MakeEmailKey,                                  // Pointer to the function that will create a key from a tuple
                                    sizeof(Test.Email),                             // Length of the keys
                                    BTTEST3KEYSPER,                                 // Number of keys to store in BTree page.  Very important factor in balancing parallel performance.  Is tree read or write heavy?  Etc.
                                    BTTEST3ALLOC,                                   // Number of BTree pages to allocate in a block each time growth is needed
                                    AT_BTREE_SECONDARY,                             // Set this up as a secondary key
                                    Kilroy                                          // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                                    )) != ATERR_SUCCESS) {
                printf("Could not create EMail!  Test failure!\r\n");return 0;}
            if ( (Result = BTree.LoadBTree("../testdata/BTree.btr")) != ATERR_SUCCESS ){// Restore the primary BTree from the save files
                printf("Failed loading BTree!  Test failure!\r\n"); return 0;}
            if ( (Result = Email.LoadBTree("../testdata/Email.btr")) != ATERR_SUCCESS ){// Restore the secondary BTree from the save files
                printf("Failed loading Email!  Test failure!\r\n"); return 0;}
        }
        else {                                                                      // Now let's test test the create from file
            printf("Testing CreateFromFile...\r\n");                                // Now let's recreate them
            if ( (Result = BTree.CreateFromFile("../testdata/BTree.btr",            // File name
                                    BTTESTTABLE,                                    // Systemwide unique IPC ID for this BTree- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                                    &Table,                                         // Which table to create it for (pointer to a valid created ATSharedTable)
                                    &LongCompare,                                   // Pointer to the function that will be called to compare the key values- should behave just like memcmp(), returning <0,0, or >0 integer.
                                    &MakeCustomerIDKey,                             // Pointer to the function that will create a key from a tuple
                                    Kilroy                                          // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                                    )) != ATERR_SUCCESS) {
                printf("Could not create BTree!  Test failure!\r\n");return 0;}

            if ( (Result = Email.CreateFromFile("../testdata/Email.btr",            // File name
                                    EMAILTESTTABLE,                                 // Systemwide unique IPC ID for this BTree- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                                    &Table,                                         // Which table to create it for (pointer to a valid created ATSharedTable)
                                    &StringCompare,                                 // Pointer to the function that will be called to compare the key values- should behave just like memcmp(), returning <0,0, or >0 integer.
                                    &MakeEmailKey,                                  // Pointer to the function that will create a key from a tuple
                                    Kilroy                                          // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                                    )) != ATERR_SUCCESS) {
                printf("Could not create EMail!  Test failure!\r\n");return 0;}
        }
        Validate++;;                                                                // Note we were here
        goto revalidate;                                                            // Now let's revalidate the whole thing
    }


    if ( (Result = BTree.Close()) != ATERR_SUCCESS ) {                          // Close the primary
        printf("Failed closing BTree!  Test failure!\r\n"); return 0;}
    if ( (Result = Email.Close()) != ATERR_SUCCESS ) {                          // Close the secondary
        printf("Failed closing BTree!  Test failure!\r\n"); return 0;}
    if ( (Result = Table.CloseTable()) != ATERR_SUCCESS ) {                     // Close the table
        printf("Failed closing BTree!  Test failure!\r\n"); return 0;}

    printf("All BTree tests successful!\r\n");
    return 1;
}
// ****************************************************************************
void *MakeEmailKey(void *Tuple) {                                               // This is one of our test make key routines
    Demo *D = (Demo*)Tuple;
    return (void*)(D->Email);
}
// ****************************************************************************
long StringCompare(void *P1, void *P2, long Size) {                             // This is one of our test comparison routines
    return strnicmp((char*)P1, (char*)P2, Size);
}
// ****************************************************************************
void *MakeCustomerIDKey(void *Tuple) {                                          // This is one of our test make key routines
    Demo *D = (Demo*)Tuple;
    return (void*)&(D->CustomerID);
}
// ****************************************************************************
void *MakeLongKey(void *Tuple) {                                                // This is one of our test make key routines
    return Tuple;
}
// ****************************************************************************
long LongCompare(void *P1, void *P2, long Size) {                               // This is one of our test comparison routines
    long    Result = *(long*)P1 - *(long*)P2;
    if ( !Result )
        return 0;
    if ( Result > 0 )
        return 1;
    return -1;
}

// **************************************************************************** Tables
int     Tables() {                                                              // Test the ATSharedTables class

    FILE            *TestFile;
    long            Result, i, inner, CB, CT, Kilroy, Inserts = 0, Deletes = 0, NumberTuples = 0;
    ATTuple         *Tuple, *Tuple2;
    volatile CDemo  *CTT, *CurrCD, *CTIC;
    long            Interval = 0, Cumu = 0;
    Demo            *DB;

    srand( time(NULL) );                                                        // Make sure we generate random results

    printf("Testing Tables...\r\n");
    printf("This uses most of the lower level stuff, so test them first!\r\n");
    printf("You should run another atlas_test process at the same time to\r\n");
    printf("verify operation. (preferably in side by side windows so you can\r\n");
    printf("see the volley back and forth).\r\n\r\n");

    printf("Creating a kernel semaphore to synch the procs...\r\n");

    printf("Initializing the sems...\r\n");                                     // Initialize the semaphores
    Result = ATInitSems();
    if ( Result != ATERR_SUCCESS ) {
        printf("Failed to init the sems!  Test failure!\r\n");
        return 0;
    }
    Result = Sem.Create(78961);                                                 // To help us coordinate the test with more than one proc, we'll create a kernel semaphore- the IPC # is random
    if ( Result != ATERR_SUCCESS ) {                                            // If I could not create it, I may not be first up...
        printf("Create() failed- trying Open() in case it already exists...\r\n");
        Result = Sem.Open(78961);                                               // So let's try an open instead
        if ( Result != ATERR_SUCCESS ) {
            printf("Open() also failed- test failure.\r\n");
            return 0;
        }
        printf("Open() successful!\r\n");
        Kilroy = 2;                                                             // Since I am second up, set my kilroy to 2
        printf("I am the second proc up, so I will wait for the signal to start...\r\n");
        Sem.GetLock();                                                          // Wait for the signal to start
        goto concurrency;                                                       // Jump straight to the concurrency tests
    }
    else {
        printf("Create() successful!\r\n");
        Sem.GetLock();                                                          // Make sure to get the lock before the other proc starts
        printf("You should also run another process to validate concurrency.\r\n");
        Kilroy = 1;                                                             // Since I am first up, set my kilroy to 1
    }
//goto concurrency;                                                               // Uncomment this to jump straight to the concurrency tests

    printf("Creating a test array with %i entries...\r\n", TABLE_DATA);
    if (CreateData(TABLE_DATA, NULL))  {                                        // First, let's create lots of test records to test with
        printf("Out of memory!\r\nStopping test!\r\n"); return 0;}

    printf("Saving as a file...\r\n");                                          // Now save our test data to a normal fixed length file
    if ( !(TestFile = fopen("../testdata/testdata.dat", "wb")) ) {
        printf("File error!\r\nStopping test!\r\n"); return 0;}
    if ( !(fwrite(Users, (TABLE_DATA * sizeof(Demo)), 1, TestFile)) ) {
        printf("File error!\r\nStopping test!\r\n"); return 0;}
    fclose(TestFile);

    printf("Creating a table...\r\n");                                          // Create the test table
    if ( (Result = Table.CreateTable(
                            TABLE_IPC,                                          // Systemwide unique IPC ID for this table- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                            sizeof(Demo),                                       // Size of the tuples in bytes
                            IALLOCSIZE,                                         // Number of records to alloc initially
                            GALLOCSIZE,                                         // Chunks of records to alloc as the table grows
                            1,                                                  // Set to true means that changes made to the table are queued until flushed, false means always flush changes to disk
                            3,                                                  // Number of delete lists
                            3,                                                  // Number of add lists
                            Kilroy                                              // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                            )) != ATERR_SUCCESS) {
        printf("Could not create table!  Test failure!\r\n");return 0;}

    printf("Testing Table Import...\r\n");                                      // Let's import the test data we created earlier
    if ( ( Result = Table.ImportTable(
                            "../testdata/testdata.dat",                         // Filename to load the table from
                            Buffer,                                             // A buffer that may be used for file I/O
                            BUFFERSIZE                                          // The size of the I/O buffer provided (recommend at least 32-64k)
                            )) != ATERR_SUCCESS) {
        printf("Could not import table!  Test failure!\r\n");return 0;}
    printf("Table imported successfully.\r\n");
    
    printf("Testing Table Export...\r\n");
    if ( ( Result = Table.ExportTable(                                          // Let's try exporting it right back out again to a fixed length file
                            "../testdata/testdata2.dat",                        // Filename to write the table to
                            Buffer,                                             // A buffer that may be used for file I/O
                            BUFFERSIZE                                          // The size of the I/O buffer provided (recommend at least 32-64k)
                            )) != ATERR_SUCCESS) {
        printf("Could not export table!  Test failure!\r\n");return 0;}
    printf("Table exported successfully.\r\n");

    printf("Testing Close()...\r\n");
    if ( ( Result = Table.CloseTable()) != ATERR_SUCCESS ) {                    // Close the table
        printf("Could not close table!  Test failure!\r\n");return 0;}

    printf("Recreating the same table...\r\n");                                 // Now let's re-Create the test table
    if ( (Result = Table.CreateTable(
                            TABLE_IPC,                                          // Systemwide unique IPC ID for this table- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                            sizeof(Demo),                                       // Size of the tuples in bytes
                            IALLOCSIZE,                                         // Number of records to alloc initially
                            GALLOCSIZE,                                         // Chunks of records to alloc as the table grows
                            1,                                                  // Set to true means that changes made to the table are queued until flushed, false means always flush changes to disk
                            3,                                                  // Number of delete lists
                            3,                                                  // Number of add lists
                            Kilroy                                              // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                            )) != ATERR_SUCCESS) {
        printf("Could not create table!  Test failure!\r\n");return 0;}

    printf("Testing Table Import (from previously exported table)...\r\n");
    if ( ( Result = Table.ImportTable(                                          // Import the data once again
                            "../testdata/testdata.dat",                         // Filename to load the table from
                            Buffer,                                             // A buffer that may be used for file I/O
                            BUFFERSIZE                                          // The size of the I/O buffer provided (recommend at least 32-64k)
                            )) != ATERR_SUCCESS) {
        printf("Could not import table!  Test failure!\r\n");return 0;}
    printf("Table imported successfully.\r\n");

    printf("Doing an integrity check forwards (may take a while)...\r\n");      // After the above imports & exports, make sure out data is still exactly as it started out
    Table.ResetCursor();                                                        // Start at the beginning of the table
    for ( i = 0; i < TABLE_DATA; i++) {                                         // Repeat this test for each tuple
        if ( !(DB = (Demo*)Table.NextTuple()) ) {                               // Get the next tuple in the table
            printf("Ran out of tuples at %i!! Test Failure!!\r\n", i); return 0;}// If we ran out, we lost one somewhere...
        for ( inner = 0; inner < TABLE_DATA; inner++) {                         // Search through our originally created test data for the tuple
            if ( !(memcmp(&(Users[inner]), DB, sizeof(Demo))) ) {               // If we have an exact match
                Users[inner].Count++;                                           // To make sure we get right versions recount (don't allow dupes)
                DB->Count++;
                break;
            }
        }
        if ( inner == TABLE_DATA ) {                                            // Better not find any more tuples than what we wrote out...
            printf("Found a bogus tuple at %i!! Test Failure!!\r\n", i); return 0;}
    }
    for ( i = 0; i < TABLE_DATA; ++i) {                                         // Now let's check our original table again
        if ( Users[i].Count != 1 ) {                                            // And make sure each tuple was found once and ONLY once
            printf("Missing a tuple at %i!! Test Failure!!\r\n", i); return 0;}
    }
    if ( (Tuple = Table.NextTuple()) ) {                                        // Better not find any more tuples than what we wrote out...
            printf("Found too many records!! Test Failure!!\r\n"); return 0;}
    printf("Passed.\r\n");

    printf("Doing an integrity check backwards (may take a while)...\r\n");
    for ( i = 0; i < TABLE_DATA; i++) {                                         // Let's scroll back from where the cursor was left, and verify the table backwards
        if ( !(DB = (Demo*)Table.PrevTuple()) ) {                               // Get the previous tuple in the table
            printf("Ran out of tuples at %i!! Test Failure!!\r\n", i); return 0;}// If we ran out, we lost one somewhere...
        for ( inner = 0; inner < TABLE_DATA; inner++) {                         // Search through our originally created test data for the tuple
            if ( !(memcmp(&(Users[inner]), DB, sizeof(Demo))) ) {               // If we have an exact match
                Users[inner].Count++;                                           // To make sure we get right versions recount (don't allow dupes)
                DB->Count++;
                break;
            }
        }
        if ( inner == TABLE_DATA ) {                                            // Better not find any more tuples than what we wrote out...
            printf("Found a bogus tuple at %i!! Test Failure!!\r\n", i); return 0;}
    }
    for ( i = 0; i < TABLE_DATA; ++i) {                                         // Now let's check our original table again
        if ( Users[i].Count != 2 ) {                                            // And make sure each tuple was found twice and ONLY twice
            printf("Missing a tuple at %i!! Test Failure!!\r\n", i); return 0;}
    }
    if ( (Tuple = Table.PrevTuple()) ) {                                        // Better not find any more tuples than what we wrote out...
            printf("Found too many records!! Test Failure!!\r\n"); return 0;}
    printf("Passed.\r\n");

    printf("Deleting the first %i records...\r\n", DELETETIPS);                 // Test the deletes
    Table.ResetCursor();                                                        // Start at the beginning of the table
    Tuple = Table.NextTuple();                                                  // Get the first tuple
    for ( i = 0; i < DELETETIPS; i++) {                                         // Run thru the number requested
        Table.LockTuple();                                                      // Lock the tuple
        memcpy(&(Dels[i]), (void*)Tuple, sizeof(Demo));                         // Make a copy of it
        if ( (Result = Table.DeleteTuple()) != ATERR_SUCCESS) {                 // Now delete it
            printf("Failed delete at %i!! Test Failure!!\r\n", i); return 0;}
        memset((void*)Tuple, 0, sizeof(Demo));                                  // Set the whole thing to zeros (NORMALLY NOT SAFE to do if someone else might be in the table!)
        if ( !(Tuple = Table.NextTuple()) ) {                                   // And move to the next one
            printf("Failed delete at %i!! Test Failure!!\r\n", i); return 0;}
    }
    i = 0;
    Table.ResetCursor();                                                        // Now reset to the start of the table again
    while(Table.NextTuple()) i++;                                               // And count all the tuples in the table
    printf("%i records left, %i SHOULD be left.\r\n", i, TABLE_DATA - DELETETIPS);
    if ( i != (TABLE_DATA - DELETETIPS) ) {                                     // If the number remaining is not correct
        printf("Test failure!\r\n"); return 0;}

    printf("Now reinserting them...\r\n");                                      // Let's put all the deleted records back in the table
    for ( i = 0; i < DELETETIPS; i++) {                                         // Run thru the whole lsit
        if ( !Table.AddTuple((void*)(&(Dels[i])) ) ) {                          // Add each one in
            printf("Failed reinsert at %i!! Test Failure!!\r\n", i); return 0;}
        Table.UnlockTuple();                                                    // Always unlock after Add
    }
    printf("Passed.\r\n");

    printf("Doing an integrity check forwards (may take a while)...\r\n");      // Let's do a byte by byte integrity check now
    Table.ResetCursor();                                                        // Start at the beginning
    for ( i = 0; i < TABLE_DATA; i++) {                                         // Run thru the whole list
        if ( !(DB = (Demo*)Table.NextTuple()) ) {                               // Grab each tuple
            printf("Ran out of tuples at %i!! Test Failure!!\r\n", i); return 0;}// Better not run out!
        for ( inner = 0; inner < TABLE_DATA; inner++) {                         // Seach through our whole original list
            if ( !(memcmp(&(Users[inner]), DB, sizeof(Demo))) ) {               // If it is an exact match
                Users[inner].Count++;                                           // To make sure we get right versions recount
                DB->Count++;
                break;
            }
        }
        if ( inner == TABLE_DATA ) {                                            // Better not have any more records than we started with
            printf("Found a bogus tuple at %i!! Test Failure!!\r\n", i); return 0;}
    }
    for ( i = 0; i < TABLE_DATA; ++i) {                                         // Now look in the original list
        if ( Users[i].Count != 3 ) {                                            // Make sure each record had been found 3 times and ONLY 3 times
            printf("Missing a tuple at %i!! Test Failure!!\r\n", i); return 0;}
    }
    if ( (Tuple = Table.NextTuple()) ) {                                        // Make sure that is ALL the tables in the tuple
            printf("Found too many records!! Test Failure!!\r\n"); return 0;}
    printf("Passed.\r\n");

    printf("Deleting the last %i records...\r\n", DELETETIPS);                  // Now let's delete some more records
    while(Table.NextTuple());                                                   // This time go all the way to the end of the list
    Tuple = Table.PrevTuple();                                                  // Get the very last tuple
    for ( i = 0; i < DELETETIPS; i++) {                                         // Go backwards the number of time specified
        Table.LockTuple();                                                      // Lock the tuple
        memcpy(&(Dels[i]), (void*)Tuple, sizeof(Demo));                         // Make a copy of it
        if ( (Result = Table.DeleteTuple()) != ATERR_SUCCESS) {                 // Delete it
            printf("Failed delete at %i!! Test Failure!!\r\n", i); return 0;}
        memset((void*)Tuple, 0, sizeof(Demo));                                         // Set the whole thing to zeros (NORMALLY NOT SAFE to do if someone else might be in the table!)
        if ( !(Tuple = Table.PrevTuple()) ) {                                   // Make sure no other records remain
            printf("Failed delete at %i!! Test Failure!!\r\n", i); return 0;}
    }
    i = 0;
    Table.ResetCursor();                                                        // Now start at the beginning again
    while(Table.NextTuple()) i++;                                               // Count all the records left
    printf("%i records left, %i SHOULD be left.\r\n", i, TABLE_DATA - DELETETIPS);
    if ( i != (TABLE_DATA - DELETETIPS) ) {                                     // If the number remaining is not correct
        printf("Test failure!\r\n"); return 0;}

    printf("Now reinserting them...\r\n");                                      // Let's put those back in now too
    for ( i = 0; i < DELETETIPS; i++) {                                         // Go thru the deleted copies
        if ( !Table.AddTuple((void*)(&(Dels[i])) ) ) {                          // Add them back in
            printf("Failed reinsert at %i!! Test Failure!!\r\n", i); return 0;}
        Table.UnlockTuple();                                                    // Always unlock after add
    }
    printf("Passed.\r\n");

    printf("Doing an integrity check forwards (may take a while)...\r\n");      // Let's do a byte by byte integrity check now
    Table.ResetCursor();                                                        // Start at the beginning
    for ( i = 0; i < TABLE_DATA; i++) {                                         // Run thru the whole list
        if ( !(DB = (Demo*)Table.NextTuple()) ) {                               // Grab each tuple
            printf("Ran out of tuples at %i!! Test Failure!!\r\n", i); return 0;}// Better not run out!
        for ( inner = 0; inner < TABLE_DATA; inner++) {                         // Seach through our whole original list
            if ( !(memcmp(&(Users[inner]), DB, sizeof(Demo))) ) {               // If it is an exact match
                Users[inner].Count++;                                           // To make sure we get right versions recount
                DB->Count++;
                break;
            }
        }
        if ( inner == TABLE_DATA ) {                                            // Better not have any more records than we started with
            printf("Found a bogus tuple at %i!! Test Failure!!\r\n", i); return 0;}
    }
    for ( i = 0; i < TABLE_DATA; ++i) {                                         // Now look in the original list
        if ( Users[i].Count != 4 ) {                                            // Make sure each record had been found 4 times and ONLY 4 times
            printf("Missing a tuple at %i!! Test Failure!!\r\n", i); return 0;}
    }
    if ( (Tuple = Table.NextTuple()) ) {                                        // Make sure that is ALL the tables in the tuple
            printf("Found too many records!! Test Failure!!\r\n"); return 0;}
    printf("Passed.\r\n");

    printf("Deleting ALL of the records...\r\n");                               // Now let's delete all of them- what the heck
    Table.ResetCursor();                                                        // Start at the beginning
    Tuple = Table.NextTuple();                                                  // Get the first record
    for ( i = 0; i < TABLE_DATA; i++) {                                         // Loop thru all of them
        Table.LockTuple();                                                      // Lock each one
        if ( (Result = Table.DeleteTuple()) != ATERR_SUCCESS) {                 // Delete it
            printf("Failed delete at %i!! Test Failure!!\r\n", i); return 0;}
        memset((void*)Tuple, 0, sizeof(Demo));                                  // Set the whole thing to zeros (NORMALLY NOT SAFE to do if someone else might be in the table!)
        if ( !(Tuple = Table.NextTuple()) && (i < TABLE_DATA - 1) ) {           // Get the next tuple
            printf("Failed delete at %i!! Test Failure!!\r\n", i); return 0;}
    }
    i = 0;
    Table.ResetCursor();                                                        // Now start at the beginning again
    while(Table.NextTuple()) i++;                                               // Count all the records left
    printf("%i records left, %i SHOULD be left.\r\n", i, 0);
    if ( i != 0) {                                                              // If the number remaining is not correct
        printf("Test failure!\r\n"); return 0;}
    Table.ResetCursor();                                                        // Reset the cursor
    if ( (Tuple = Table.NextTuple()) ) {                                        // Make sure there are no records found at all
        printf("Found non-existent record! Test Failure!\r\n"); return 0;}

    printf("Now reinserting them ALL...\r\n");                                  // Let's put them all back int
    for ( i = 0; i < TABLE_DATA; i++) {                                         // For the whole list
        if ( !Table.AddTuple((void*)(&(Users[i])) ) ) {                         // Add each one back in
            printf("Failed reinsert at %i!! Test Failure!!\r\n", i); return 0;}
        Table.UnlockTuple();                                                    // Always unlock after add
    }
    printf("Passed.\r\n");

    printf("Doing an integrity check forwards (may take a while)...\r\n");      // Let's do a byte by byte integrity check now
    Table.ResetCursor();                                                        // Start at the beginning
    for ( i = 0; i < TABLE_DATA; i++) {                                         // Run thru the whole list
        if ( !(DB = (Demo*)Table.NextTuple()) ) {                               // Grab each tuple
            printf("Ran out of tuples at %i!! Test Failure!!\r\n", i); return 0;}// Better not run out!
        for ( inner = 0; inner < TABLE_DATA; inner++) {                         // Seach through our whole original list
            if ( !(memcmp(&(Users[inner]), DB, sizeof(Demo))) ) {               // If it is an exact match
                Users[inner].Count++;                                           // To make sure we get right versions recount
                DB->Count++;
                break;
            }
        }
        if ( inner == TABLE_DATA ) {                                            // Better not have any more records than we started with
            printf("Found a bogus tuple at %i!! Test Failure!!\r\n", i); return 0;}
    }
    for ( i = 0; i < TABLE_DATA; ++i) {                                         // Now look in the original list
        if ( Users[i].Count != 5 ) {                                            // Make sure each record had been found 5 times and ONLY 5 times
            printf("Missing a tuple at %i!! Test Failure!!\r\n", i); return 0;}
    }
    if ( (Tuple = Table.NextTuple()) ) {                                        // Make sure that is ALL the tables in the tuple
            printf("Found too many records!! Test Failure!!\r\n"); return 0;}
    printf("Passed.\r\n");

    printf("Miscellaneous operations on all tuples...\r\n");                    // Miscellaneous tests
    Table.ResetCursor();
    for ( i = 0; i < TABLE_DATA; ++i) {
        if ( !(Tuple = Table.NextTuple()) ) {
            printf("Failed in misc section!  Tes t failure!\r\n"); return 0;}
        if ( !(Tuple2 = Table.LockTuple()) || Tuple != Tuple2 ) {
            printf("Failed in misc section (LockTuple)!  Test failure!\r\n"); return 0;}
        if ( (Result = Table.UnlockTuple()) != ATERR_SUCCESS ) {
            printf("Failed in misc section (UnlockTuple)!  Test failure!\r\n"); return 0;}
        if ( !(Tuple = Table.LockedGetTuple()) ) {
            printf("Failed in misc section (LockedGetTuple)!  Test failure!\r\n"); return 0;}
        if ( (Result = Table.UnlockTuple()) != ATERR_SUCCESS ) {
            printf("Failed in misc section (UnlockTuple)!  Test failure!\r\n"); return 0;}
        if ( !(Tuple2 = Table.BounceLockTuple()) )  {
            printf("Failed in misc section (BounceLockTuple)!  Test failure!\r\n"); return 0;}
        if ( (Result = Table.UnlockTuple()) != ATERR_SUCCESS ) {
            printf("Failed in misc section (UnlockTuple)!  Test failure!\r\n"); return 0;}
        if ( !(Tuple2 = Table.GetTupleLong(&CB, &CT)) || Tuple != Tuple2 ) {
            printf("Failed in misc section (GetTupleLong)!  Test failure!\r\n"); return 0;}
        Table.ResetCursor();
        if ( !(Tuple2 = Table.SetTuple(CB, CT)) || Tuple != Tuple2 ) {
            printf("Failed in misc section (SetTupleLong)!  Test failure!\r\n"); return 0;}
    };

    printf("Testing Close()...\r\n");
    if ( ( Result = Table.CloseTable()) != ATERR_SUCCESS ) {
        printf("Could not close table!  Test failure!\r\n");return 0;}

    printf("Now preparing for concurrency tests.\r\n");
    printf("Note these tests help validate the table structures and algorithms, but\r\n");
    printf("NOT the tuple atomicity- that is up to the USER via locking.\r\n");
    delete Users;
    Users = NULL;

concurrency:                                                                    // This marks the start of the concurrency test

    /*  The concurrency test that follows has two purposes.  First, it shows how to safely use the library in a contentious
    environment.  Second, it makes a handy little regression test when porting the library ;^).

    Now, the way these tests actually perform the work is horrendous.  You would never structure a real program like this
    with all the linear searches- you would use an indexed access path like ATBTree.  But here we are focusing only on the tables,
    and beating the living daylights out of them, too.  This is a reasonably thorough contention test if run properly.

    There are two ways to run the test.  TABLE_FASTLOOSE shows running with non locking calls in an extremely contentious environment.
    If TABLE_FASTLOOSE is turned off, is shows a little simpler way of operating the table in such an environment
    */

    CTT = new CDemo[TABLE_CONCURRENCY_DATA];                                    // Start off by creating a table to track the actions performed on each of our records
    if ( !CTT ) {printf("Out of memory!"); return 0;}
    for ( i = 0; i < TABLE_CONCURRENCY_DATA; i++) {                             // Initialize our table values
        CTT[i].Kilroy = Kilroy;
        CTT[i].Key = i;
        CTT[i].Count = 0;
        CTT[i].Status = 0;
        CTT[i].Um = 0;
    }

    if ( Kilroy == 1) {                                                         // If I am the first process up...
        printf("Creating a new table...\r\n");                                  // I must create the test table
        if ( (Result = Table.CreateTable(
                                TABLE_IPC,                                      // Systemwide unique IPC ID for this table- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                                sizeof(CDemo),                                  // Size of the tuples in bytes
                                CTIALLOCSIZE,                                   // Number of records to alloc initially
                                CTGALLOCSIZE,                                   // Chunks of records to alloc as the table grows
                                1,                                              // Set to true means that changes made to the table are queued until flushed, false means always flush changes to disk
                                20,                                             // Number of delete lists
                                6,                                              // Number of add lists
                                Kilroy                                          // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                                )) != ATERR_SUCCESS) {
            printf("Could not create table!  Test failure!\r\n");return 0;}

        Sem.FreeLock();                                                         // Let the other guy start now
//goto test_single;                                                             // This can be uncommented to check single SECOND proc operation
    }
    else {                                                                      // Since I am NOT the first guy up....
        printf("Opening the table...\r\n");                                     // I just open the test table
        if ( (Result = Table.OpenTable(
                                TABLE_IPC,                                      // Systemwide unique IPC ID for this table- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                                Kilroy                                          // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                                )) != ATERR_SUCCESS) {
            printf("Could not open table!  Test failure!\r\n");return 0;}
    }

    printf("Running concurrency test over %i iterations...\r\n", TABLE_CONCURRENCY_RUN);

#ifdef  TABLE_FASTLOOSE
    for ( i = 0; i < TABLE_CONCURRENCY_RUN; ++i) {                              // Run the entire concurrency test as many reps as requested

        ++Interval;                                                             // This little section justs posts updates at defined intervals
        if ( Interval == TABLE_UPDATE ) {
            Cumu += TABLE_UPDATE;
            Interval = 0;
            printf("Working on %i of %i...\r\n", Cumu, TABLE_CONCURRENCY_RUN);
        }

        for ( inner = 0; inner < TABLE_CONCURRENCY_DATA; inner++) {             // Now, for each test rep let's run thru our entire test list
            Result = (rand()) % 10;                                             // Pick a random number for each record
            if ( Result == 5 ){                                                 // And give it a 10% chance of performing either an insert or a delete

                if ( CTT[inner].Status ) {                                      // If the record IS in the table, delete it
                    Table.ResetCursor();                                        // Reset our table cursor
                    while ( (CurrCD = (volatile CDemo*)Table.NextTuple()) ) {   // Search through the table for this tuple (keep reading as long a NextTuple gives me back records)
                        if ( Kilroy == CurrCD->Kilroy && CurrCD->Key == CTT[inner].Key ) {// If this is one of our records (Kilroy matches) and it is the particular one we are looking for (CTT[].Key)...
                            CurrCD = (volatile CDemo *)Table.LockTuple();       // Get a lock on the tuple
                                                                                // Now, I looked at the record first, but in the real world it might have changed before I could lock it....
                            if ( !CurrCD || Kilroy != CurrCD->Kilroy || CurrCD->Key != CTT[inner].Key ) {// So I MUST make sure that it is still the right record AFTER I get the lock
                                                                                // I must have grabbed one of my old deleted records, AFTER it was not in the deletes list, but BEFORE the new data was copied in
                                printf("Phew!  That was close!  That's parallel programming for ya!\r\n");// Probably a window of a less than a millisecond, barring the scheduler was involved
                                Table.UnlockTuple();                            // Let's this record go
                                continue;                                       // And move on
                                }
                            if ( memcmp((void*)&(CTT[inner]),(void*)CurrCD, sizeof(CDemo)) ) {// Make sure this tuple is an EXACT copy of my own
                                printf("Error!  Tuple does not match what I stored!\r\n");// If not, we have a serious failure!
                                return 0;
                            }
                            CurrCD->Um = 1;                                     // Troubleshooting aid
                            Table.DeleteTuple();                                // Now that we have verified this is the right tuple, delete it
                            CTT[inner].Status = 0;                              // Mark it as deleted in our status
                            Deletes++;                                          // Track the number of deletes performed
                            break;                                              // On to the next record
                        }
                    }
                    if ( !CurrCD ) {                                            // If I could not find a tuple that I have marked as inserted...
                        printf("I have lost a tuple! Test failure!\r\n");
                        return 0;
                    }
                }
                else {                                                          // If the record IS NOT in the table insert it
                    CTT[inner].Count++;                                         // Inc the count of the number of times this record was inserted
                    CTT[inner].Status = 1;                                      // Mark it as inserted
                    CTT[inner].Um = 0;                                          // Troubleshooting flag

                    Tuple = Table.AddTuple((void*)(&(CTT[inner])));             // Add the tuple to the table
                    ((CDemo*)Tuple)->CD = (CDemo*)Tuple;                        // Note in the record where it is stored (had to get ptr back from add first)
                    Table.UnlockTuple();                                        // Since Add always locks a tuple, unlock it, but only AFTER we are done writing to it
                    CTT[inner].CD = (CDemo*)Tuple;                              // Copy that ptr internally
                    Inserts++;                                                  // Keep track of the total number of inserts I have made
                }
            }
        }
    }
#else
    for ( i = 0; i < TABLE_CONCURRENCY_RUN; ++i) {                              // Run the entire concurrency test as many reps as requested

        ++Interval;                                                             // This little section justs posts updates at defined intervals
        if ( Interval == TABLE_UPDATE ) {
            Cumu += TABLE_UPDATE;
            Interval = 0;
            printf("Working on %i of %i...\r\n", Cumu, TABLE_CONCURRENCY_RUN);
        }

        for ( inner = 0; inner < TABLE_CONCURRENCY_DATA; inner++) {             // Now, for each test rep let's run thru our entire test list
            Result = (rand()) % 10;                                             // Pick a random number for each record
            if ( Result == 5 ){                                                 // And give it a 10% chance of performing either an insert or a delete
                if ( CTT[inner].Status ) {                                      // If the record IS in the table, delete it
                    Table.ResetCursor();                                        // Reset our table cursor
                    while ( (CurrCD = (CDemo*)Table.LockedNextTuple()) ) {      // Search through the table for this tuple (keep reading as long a LockedNextTuple gives me back records)
                        if ( Kilroy == CurrCD->Kilroy && CurrCD->Key == CTT[inner].Key ) {// If this is one of our records (Kilroy matches) and it is the particular one we are looking for (CTT[].Key)...
                                                                                // Unlike the above test run, we no longer have to get a lock, then see if we still have the right tuple
                            if ( memcmp((void*)&(CTT[inner]),(void*)CurrCD, sizeof(CDemo)) ) {// Make sure this tuple is an EXACT copy of my own
                                printf("Error!  Tuple does not match what I stored!\r\n");// If not, we have a serious failure!
                                return 0;
                            }
                            CurrCD->Um = 1;                                     // Troubleshooting aid

                            Table.DeleteTuple();                                // Now that we have verified this is the right tuple, delete it
                            CTT[inner].Status = 0;                              // Mark it as deleted in our status
                            Deletes++;                                          // Track the number of deletes performed
                            break;                                              // On to the next record
                        }
                        else {                                                  // Not the one I am looking for
                            Table.UnlockTuple();                                // Don't forget to unlock tuple before continuing!
                        }
                    }
                    if ( !CurrCD ) {                                            // If I could not find a tuple that I have marked as inserted...
                        printf("I have lost a tuple! Test failure!\r\n");
                        return 0;
                    }
                }
                else {                                                          // If the record IS NOT in the table insert it
                    CTT[inner].Count++;                                         // Inc the count of the number of times this record was inserted
                    CTT[inner].Status = 1;                                      // Mark it as inserted
                    CTT[inner].Um = 0;                                          // Troubleshooting flag

                    Tuple = Table.AddTuple((void*)(&(CTT[inner])));             // Add the tuple to the table
                    ((CDemo*)Tuple)->CD = (CDemo*)Tuple;                        // Note in the record where it is stored (had to get ptr back from add first)
                    Table.UnlockTuple();                                        // Since Add always locks a tuple, unlock it, but only AFTER we are done writing to it
                    CTT[inner].CD = (CDemo*)Tuple;                              // Copy that ptr internally
                    Inserts++;                                                  // Keep track of the total number of inserts I have made
                }
            }
        }
    }
#endif

    printf("Finished concurrency tests with %i inserts and %i deletes.\r\n", Inserts, Deletes);
    printf("Checking table integrity...\r\n");                                  // Here is where we make a complete check, after all we have done, to make darned sure it is PRECISELY correct
    for ( i = 0; i < TABLE_CONCURRENCY_DATA; i++ ) {                            // Go through all the records
        if ( CTT[i].Status ) {                                                  // If they are supposed to be in the table, make sure they are AND they are current
            Table.ResetCursor();                                                // Start the search at the beginning of the table
            while ( (CurrCD = (CDemo*)Table.NextTuple()) ) {                    // Search through the table for this tuple
                if ( Kilroy == CurrCD->Kilroy && CurrCD->Key == CTT[i].Key ) {  // If this is one of ours and is the one we are looking for
                    if ( memcmp((void*)&(CTT[i]),(void*)CurrCD, sizeof(CDemo)) ) {// Make sure this tuple is an EXACT copy of my own (which includes a version # of sorts)
                        printf("Error!  Tuple does not match what I stored!\r\n");// If not, we have a serious failure!
                        return 0;
                    }
                    else break;
                }
            }
            if ( !CurrCD ) {
                printf("Lost a tuple!  Test failure!\r\n");return 0;}
        }
        else {                                                                  // Make sure this ISN'T in the table since it is marked as deleted
            Table.ResetCursor();                                                // Start the search at the beginning of the table
            while ( (CurrCD = (CDemo*)Table.NextTuple()) ) {                    // Search through the table for this tuple
                if ( Kilroy == CurrCD->Kilroy && CurrCD->Key == CTT[i].Key ) {
                    printf("Found a deleted tuple at %i!  Um: %i Test failure!\r\n", i, CurrCD->Um);break;}
            }
        }
    }
    printf("Passed check.\r\n");

    if ( Kilroy ==2 ) {                                                         // The second guy is now done
        Sem.FreeLock();
        goto exit;
    }
test_single:                                                                    // Jump to this point only for special troubleshooting issues
    printf("Waiting for second task to complete...\r\n");
    Sem.GetLock();                                                              // This makes sure the 2nd proc finishes its test before I start mine
    printf("Now preparing to test Load/Write...\r\n");
    delete CTT;                                                                 // Delete the list of just my own stuff
    CTIC = new CDemo[TABLE_CONCURRENCY_DATA * 2];                               // Create a new list big enough to hold entries from both procs
    if ( !CTIC ) {printf("Out of memory!"); return 0;}
    Table.ResetCursor();                                                        // Start at the beginning of the table
    while( (CurrCD = (CDemo*)Table.NextTuple()) ) {                             // Read as long as records are returned
        memcpy((void*)(&CTIC[NumberTuples]), (void*)CurrCD, sizeof(CDemo));     // Copy each record
        NumberTuples++;
    }

    printf("Writing out the table...\r\n");
    Table.WriteTable("../testdata/testtable.tab");                              // Now write the table to disk
    printf("Closing the table...\r\n");
    Table.CloseTable();                                                         // Close it
    printf("Recreating the table...\r\n");
    if ( (Result = Table.CreateTable(                                           // Recreate it
                            TABLE_IPC,                                          // Systemwide unique IPC ID for this table- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                            sizeof(CDemo),                                      // Size of the tuples in bytes
                            CTIALLOCSIZE,                                       // Number of records to alloc initially
                            CTGALLOCSIZE,                                       // Chunks of records to alloc as the table grows
                            1,                                                  // Set to true means that changes made to the table are queued until flushed, false means always flush changes to disk
                            20,                                                 // Number of delete lists
                            6,                                                  // Number of add lists
                            Kilroy                                              // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                            )) != ATERR_SUCCESS) {
        printf("Could not create table!  Test failure!\r\n");return 0;}
    printf("Now loading the table...\r\n");
    if( (Result = Table.LoadTable("../testdata/testtable.tab", Buffer, BUFFERSIZE)) != ATERR_SUCCESS) {// Load the table in from disk
        printf("Could not load table!  Test Failure!\r\n"); return 0;}

    printf("Doing one last byte-by-byte integrity check on %i tuples...\r\n", NumberTuples);
    Table.ResetCursor();                                                        // Start at the beginning of the table
    for ( i = 0; i < NumberTuples; ++i) {                                       // Run through the number of tuples we have locally
        if ( !(CurrCD = (CDemo*)Table.NextTuple() ) ) {                         // Now loop thru all the tuples in the table
            printf("Lost a tuple!  Test failure!\r\n"); return 0; }             // If we ran out we lost something!
        if ( memcmp((void*)(&(CTIC[i])), (void*)CurrCD, sizeof(CDemo)) ) {      // Compare each tuple to make sure it is exacly what it was before being written
            printf("Tuples invalid!  Test failure!\r\n"); return 0; }
    }
    if ( (CurrCD = (CDemo*)Table.NextTuple() ) ) {                              // If there are any tuples left in the table there is a serious problem
        printf("Found extra tuples!  Test failure!\r\n"); return 0; }
    printf("Passed check.\r\n");

    printf("Closing the table...\r\n");
    Table.CloseTable();                                                         // Close the table

exit:
    printf("Phew!! All Table tests successful!\r\n");
    printf("You should run ipcs to ensure nothing got left behind in memory.\r\n");
    return 1;
}
// **************************************************************************** CreateData
int     CreateData(int Number, Demo *Allocated) {                               // Create the test data for the various routines
    int i;

    srand( time(NULL) );                                                        // Make sure to randomize

    if ( Allocated ) {                                                          // If we were passed memory to use, use it
        Users = Allocated;
    }
    else {                                                                      // Otherwise allocate our own
        Users = new Demo[Number];
        if ( !Users ) return ATERR_OUT_OF_MEMORY;
    }

    for ( i = 0; i < Number; ++i ) {                                            // Create the requested number of random entries
        Users[i].CustomerID = i;
        strncpy((char*)Users[i].FName, FirstName[(rand())%AT_TEST_ENTRIES], NAME);
        Users[i].FName[NAME] = '\0';
        strncpy((char*)Users[i].M, Middle[(rand())%AT_TEST_ENTRIES], MI);
        Users[i].M[MI] = '\0';
        strncpy((char*)Users[i].LName, LastName[(rand())%AT_TEST_ENTRIES], NAME);
        Users[i].LName[NAME] = '\0';
        strncpy((char*)Users[i].Addr1, Address1[(rand())%AT_TEST_ENTRIES], ADDRESS);
        Users[i].Addr1[ADDRESS] = '\0';
        strncpy((char*)Users[i].Addr2, Address2[(rand())%AT_TEST_ENTRIES], ADDRESS);
        Users[i].Addr2[ADDRESS] = '\0';
        strncpy((char*)Users[i].City, City[(rand())%AT_TEST_ENTRIES], CITY);
        Users[i].City[CITY] = '\0';
        strncpy((char*)Users[i].State, State[(rand())%AT_TEST_ENTRIES], STATE);
        Users[i].State[STATE] = '\0';
        strncpy((char*)Users[i].Postal, Postal[(rand())%AT_TEST_ENTRIES], POSTAL);
        Users[i].Postal[POSTAL] = '\0';
        strncpy((char*)Users[i].Phone1, Phone[(rand())%AT_TEST_ENTRIES], PHONE);
        Users[i].Phone1[PHONE] = '\0';
        strncpy((char*)Users[i].Phone2, Phone[(rand())%AT_TEST_ENTRIES], PHONE);
        Users[i].Phone2[PHONE] = '\0';
        memset((char*)Users[i].Email, 0, EMAIL + 1);
        strcpy((char*)Users[i].Email, Handle[(rand())%AT_TEST_ENTRIES]);
        strcat((char*)Users[i].Email,"@");
        strcat((char*)Users[i].Email, Domain[(rand())%AT_TEST_ENTRIES]);
        Users[i].Count = 0;
        Users[i].ALock = 0;
        Users[i].Status = 0;
    }
    return ATERR_SUCCESS;
}


// **************************************************************************** Logs
int     Logs() {                                                                // Test Atlas' logs
    ATLog   Log1("../testdata/testlog.log", 0, 1), *Log2;                       // Log1 is "statically" implemented (well, not really, but the syntax is the same, let's not be nitpicky!)

    printf("Testing ATLog...\r\n");
    printf("Testing log1 for overwrite output...\r\n");

    Log1.Write("This is line 1: Integer-> %i", 12345);                          // Write a formatted string to the log
    Log1.Close();                                                               // Close the log
    printf("Testing log2 for append output...\r\n");

    Log2 = new ATLog("../testdata/testlog.log", 1, 1);                          // Log2 is dynamically allocated
    Log2->Write("This is line 2: String-> %s", "Roger Rabbit rocks!");          // Write a formatted string to the log
    Log2->Close();                                                              // Close the log
    delete Log2;                                                                // Delete the log instance

    printf("To verify tests were successful, check testlog.log and make sure\r\n");
    printf("there are two output lines present.\r\n");
    return 1;
}
// **************************************************************************** Timing
int     Timing() {                                                              // Tests timing ops
    long long   Test[6], Avg[5], Result;
    int i;

    printf("Testing Timing...\r\n");
    printf("Testing CPUTicks.  This will take a few seconds...\r\n");

    for ( i = 0; i < 6; i++) {
        Test[i] = ATGetCPUTicks();
        if (i < 5) sleep(1);
    }
    Avg[0] = Test[1] - Test[0];
    Avg[1] = Test[2] - Test[1];
    Avg[2] = Test[3] - Test[2];
    Avg[3] = Test[4] - Test[3];
    Avg[4] = Test[5] - Test[4];

    Result = Avg[0] + Avg[1] + Avg[2] + Avg[3] + Avg[4];
    Result /= 5;
    Result /= 1000000;

    printf("CPU Ticks shows your CPU MHz at about %i.\r\n", Result);
    printf("If that is reasonably close (won't be precise), then you are\r\ngood to go!\r\n");

    return 1;
}
// **************************************************************************** Atomics
int     Atomics() {                                                           // Tests the atomic ops
    volatile void *Data;
    int     i, Result, Count = 0, x;
    volatile long *Test;
    long    Kilroy;

    printf("Testing Atomics...\r\n");
    printf("This test requires Shared Memory, so sure make you have tested it first!\r\n");
    printf("You may need to adjust the number of loops to compensate for your CPU.\r\n");
    printf("You should run another atlas_test process at the same time to\r\n");
    printf("verify operation. (preferably in side by side windows so you can\r\n");
    printf("see the volley back and forth).\r\n\r\n");

    printf("Create a shared memory object..\r\n");                              // In order to test the atomic ops between processes, we must place them in a shared memory segment so both processes can see them.
    Result = Mem.CreateSharedMem(21212, 4096);                                  // Create a 4k shared memory object
    if ( Result != ATERR_SUCCESS ) {
        printf("Create() failed- trying Attach() in case it already exists...\r\n");
        Result = Mem.AttachSharedMem(21212);                                    // If the create failed, perhaps I am not the first guy up, so try Attach
        if ( Result != ATERR_SUCCESS ) {
            printf("Attach() also failed- test failure.\r\n");
            return 0;
        }
        printf("Attach() successful!\r\n");
        Kilroy = 2;                                                             // If I am the second guy up set my kilroy to 2
    }
    else {
        printf("Create() successful!\r\n");
        Kilroy = 1;                                                             // If I am the first guy up set my kilroy to 1
    }
    Data = Mem.GetBasePointer();                                                // Get a ptr to the shared memory
    Test = (volatile long *)(Data);                                             // Make the first int my test variable

    printf("Initializing the test variable...\r\n");
    if ( Kilroy == 1) *Test = 0;                                                // Init the variable to zero if I am the first one up

    printf("Starting tests (%i iterations)...\r\n", ATOMIC_REPS);
    for ( i = 0; i < ATOMIC_REPS; i++) {                                        // Perform the requested # of reps of all tests
        printf("%i AtomicAdds...(currently at %i)\r\n", ATOMIC_OPS, *Test);
        for ( x = 0; x < ATOMIC_OPS; x++)
            ATAtomicAdd(5, Test);                                               // AtomicAdd
        printf("%i AtomicSubtracts...(currently at %i)\r\n", ATOMIC_OPS, *Test);
        for ( x = 0; x < ATOMIC_OPS; x++)
            ATAtomicSubtract(5, Test);                                          // AtomicSubtract
        printf("%i AtomicIncs...(currently at %i)\r\n", ATOMIC_OPS, *Test);
        for ( x = 0; x < ATOMIC_OPS; x++)
            ATAtomicInc(Test);                                                  // AtomicInc
        printf("%i AtomicDecs...(currently at %i)\r\n", ATOMIC_OPS, *Test);
        for ( x = 0; x < ATOMIC_OPS; x++)
            ATAtomicDec(Test);                                                  // AtomicDec
    }

    printf("Test = %i\r\n", *Test);
    printf("\r\nAll Atomics tests appear successful!\r\n");
    printf("\r\nTo make sure, check that Test = 0 on the last process to finish...\r\n");
    printf("\r\nNote that CompareAndExchange is tested in the SpinLocks section.\r\n");
    return 1;

}

// **************************************************************************** ShareLocks
int     ShareLocks() {                                                          // Tests the share locks
    volatile void *Data;
    int     i, Result, Count = 0;
    ATLOCK  *ALock;
    long    Kilroy;

    printf("Testing Share Locks...\r\n");
    printf("This test requires Shared Memory, so make you have tested it first!\r\n");
    printf("You should run another atlas_test process at the same time to\r\n");
    printf("verify operation. (preferably in side by side windows so you can\r\n");
    printf("see the volley back and forth).\r\n\r\n");

    printf("Create a shared memory object..\r\n");                              // In order to test the share locks between processes, we must place them in a shared memory segment so both processes can see them.
    Result = Mem.CreateSharedMem(22222, 4096);                                  // Create a 4k shared memory object
    if ( Result != ATERR_SUCCESS ) {
        printf("Create() failed- trying Attach() in case it already exists...\r\n");
        Result = Mem.AttachSharedMem(22222);                                    // If Create failed I may not be the first one up, so try Attach instead
        if ( Result != ATERR_SUCCESS ) {
            printf("Attach() also failed- test failure.\r\n");
            return 0;
        }
        printf("Attach() successful!\r\n");
        Kilroy = 2;                                                             // If I am the second one up set my kilroy to 2
    }
    else {
        printf("Create() successful!\r\n");
        Kilroy = 1;                                                             // If I am the first one up set my kilroy to 1
    }
    Data = Mem.GetBasePointer();                                                // Get a ptr to the shared memory
    ALock = (ATLOCK *)(Data);                                                   // Make the first int in the shared memory my lock

    printf("Initializing the share lock...\r\n");
    if ( Kilroy == 1) *ALock = 0;                                               // Init the spin lock to zero if I am the first one up

    printf("Initializing the sems...\r\n");
    Result = ATInitSems();                                                      // Init the semaphore library
    if ( Result != ATERR_SUCCESS ) {
        printf("Failed to init the sems!  Test failure!\r\n");
        return 0;
    }

    printf("Test the following %i times...\r\n", SHARE_LOCK_LOOPS);
    for ( i = 0; i < SHARE_LOCK_LOOPS; i++) {                                   // Repeat the test as requested
retry:
        printf("Get shared lock (currently at: %i)\r\n", ((*ALock) & 0x00FFFFFF));
        if ( (Result = ATGetShare(ALock)) != ATERR_SUCCESS ) {                  // Get a shared read lock
            printf("GetShareLock failed!  Test failure!\r\n");
            return 0;
        }
        usleep(250000);                                                         // Sleep a bit to stagger w/other proc
        printf("Bounce a second instance of the shared lock (currently at: %i)\r\n", ((*ALock) & 0x00FFFFFF));
        if ( (Result = ATBounceShare(ALock)) != ATERR_SUCCESS ) {               // Get a 2nd instance to inc the read count
            printf("Bounce failed!  To avoid deadlock, I'll start over!\r\n");
            ATFreeShare(ALock);
            usleep(500000);
            goto retry;
        }
        usleep(250000);                                                         // Sleep a bit to stagger w/other proc
        printf("Bounce a third instance of the shared lock (currently at: %i)\r\n", ((*ALock) & 0x00FFFFFF));
        if ( (Result = ATBounceShare(ALock)) != ATERR_SUCCESS ) {               // Get a 3rd instance to inc the read count
            printf("Bounce failed!  To avoid deadlock, I'll start over!\r\n");
            ATFreeShare(ALock);
            ATFreeShare(ALock);
            usleep(500000);
            goto retry;
        }
        usleep(250000);                                                         // Sleep a bit to stagger w/other proc
        printf("Free one share instance (currently at: %i)\r\n", ((*ALock) & 0x00FFFFFF));
        if ( (Result = ATFreeShare(ALock)) != ATERR_SUCCESS ) {                 // Free just one instance
            printf("FreeShareLock failed!  Test failure!\r\n");
            return 0;
        }
        usleep(250000);                                                         // Sleep a bit to stagger w/other proc
        printf("Free second share instance (currently at: %i)\r\n", ((*ALock) & 0x00FFFFFF));
        if ( (Result = ATFreeShare(ALock)) != ATERR_SUCCESS ) {                 // Free a 2nd instance
            printf("FreeShareLock failed!  Test failure!\r\n");
            return 0;
        }
        usleep(250000);                                                         // Sleep a bit to stagger w/other proc
        printf("Free last share instance (currently at: %i)\r\n", ((*ALock) & 0x00FFFFFF));
        if ( (Result = ATFreeShare(ALock)) != ATERR_SUCCESS ) {                 // Free the 3rd (last) instance
            printf("FreeShareLock failed!  Test failure!\r\n");
            return 0;
        }
        sleep(1);                                                               // Sleep a bit to stagger w/other proc
        printf("Bounce exclusive (currently at: %i)\r\n", ((*ALock) & 0x00FFFFFF));
        if ( (Result = ATBounceShareExclusive(ALock)) != ATERR_SUCCESS ) {      // Now try to bounce an exclusive
            printf("Bounced... didn't get it.\r\n");
            sleep(1);
        }
        else {
            printf("Got it... Sleeping for a second\r\n");
            sleep(1);                                                           // If I got it, sleep to stagger w/other proc
            printf("Now freeing it...\r\n");
            if ( (Result = ATFreeShareExclusive(ALock)) != ATERR_SUCCESS ) {    // Now free it
                printf("FreeShareLockExclusive failed!  Test failure!\r\n");
                return 0;
            }
        }
        sleep(1);                                                               // Sleep a bit to stagger w/other proc
        printf("Get exclusive (currently at: %i)\r\n", ((*ALock) & 0x00FFFFFF));
        if ( (Result = ATGetShareExclusive(ALock)) != ATERR_SUCCESS ) {         // Get an exclusive lock
            printf("GetShareLockExclusive failed!  Test failure!\r\n");
            return 0;
        }
        else {
            printf("Got it... Sleeping for a second\r\n");
            sleep(1);                                                           // If I got it, sleep to stagger w/other proc
            printf("Now freeing it...\r\n");
            if ( (Result = ATFreeShareExclusive(ALock)) != ATERR_SUCCESS ) {    // Now free it
                printf("FreeShareLockExclusive failed!  Test failure!\r\n");
                return 0;
            }
        }
    }

    printf("\r\nAll ShareLock tests successful!\r\n");
    return 1;
}
// **************************************************************************** SpinLocks
int     SpinLocks() {                                                           // Tests the spin locks
    volatile void *Data;
    int     i, Result, Count = 0;
    ATLOCK  *ALock;
    long    Kilroy;

    printf("Testing Spin Locks...\r\n");
    printf("This test requires Shared Memory, so make you have tested it first!\r\n");
    printf("You should run another atlas_test process at the same time to\r\n");
    printf("verify operation. (preferably in side by side windows so you can\r\n");
    printf("see the volley back and forth).\r\n\r\n");

    printf("Create a shared memory object..\r\n");                              // In order to test the spin locks between processes, we must place them in a shared memory segment so both processes can see them.
    Result = Mem.CreateSharedMem(32132, 4096);                                  // Create a 4k shared memory object
    if ( Result != ATERR_SUCCESS ) {
        printf("Create() failed- trying Attach() in case it already exists...\r\n");
        Result = Mem.AttachSharedMem(32132);                                    // If it failed, maybe I am the second guy up, so try attaching instead
        if ( Result != ATERR_SUCCESS ) {
            printf("Attach() also failed- test failure.\r\n");
            return 0;
        }
        printf("Attach() successful!\r\n");
        Kilroy = 2;                                                             // If I am the second guy up, make my kilroy 2
    }
    else {
        printf("Create() successful!\r\n");
        Kilroy = 1;                                                             // If I am the first guy up, make my kilroy 1
    }
    Data = Mem.GetBasePointer();                                                // Get a ptr to the shared memory
    ALock = (ATLOCK *)(Data);                                                   // Make the first int in the shared memory my lock

    printf("Initializing the spin lock...\r\n");
    if ( Kilroy == 1) *ALock = 0;                                               // Init the spin lock to zero if I am the first one up

    printf("Initializing the sems...\r\n");
    Result = ATInitSems();                                                      // Init the semaphore libray
    if ( Result != ATERR_SUCCESS ) {
        printf("Failed to init the sems!  Test failure!\r\n");
        return 0;
    }

    printf("Test the following %i times...\r\n", SPIN_LOCK_LOOPS);
    for ( i = 0; i < SPIN_LOCK_LOOPS; i++) {                                    // Repeat this loop as specified
        printf("Try to GetSpinLock()...\r\n");
        if ( (Result = ATGetSpinLock(Kilroy, ALock)) != ATERR_SUCCESS ) {       // Try to get the lock
            printf("Failed to GetSpinLock()!\r\nTest failure.\r\n");            // Get should never fail...
            return 0;
        }
        printf("Got it!\r\n");
        sleep(3);                                                               // Sleep a bit (to stagger w/ the other process)
        printf("Now freeing it...\r\n");
        if ( (Result = ATFreeSpinLock(Kilroy, ALock)) != ATERR_SUCCESS ) {      // Free the lock
            printf("Failed to FreeLock()!\r\nTest failure.\r\n");               // Free should never fail
            return 0;
        }
        printf("Freed!\r\n");
        sleep(1);                                                               // Sleep a bit (to stagger w/ the other process)
        printf("Now bouncing it...\r\n");
        while ( (Result = ATBounceSpinLock(Kilroy, ALock)) != ATERR_SUCCESS ) { // Now try bouncing the lock
            printf("Bounce...\r\n");
            sleep(1);                                                           // Sleep a bit (to stagger w/ the other process)
            Count++;
            if ( Count > 12 ) {                                                 // If I have retried too many times there might be a problem
                printf("Possible problem with BounceSpinLock()! (MIGHT just be bad timing...)\r\nTest failure.\r\n");
                return 0;
            }
        }
        Count = 0;
        printf("Got it!\r\n");
        sleep(3);                                                               // Sleep a bit (to stagger w/ the other process)
        printf("Now freeing it...\r\n");
        if ( (Result = ATFreeSpinLock(Kilroy, ALock)) != ATERR_SUCCESS ) {      // Now free the lock again
            printf("Failed to FreeSpinLock()!\r\nTest failure.\r\n");
            return 0;
        }
        printf("Freed!\r\n");
        sleep(1);
    }

    printf("\r\nAll SpinLock tests successful!\r\n");
    return 1;
}
// **************************************************************************** ScratchMemory
int     ScratchMemory() {                                                       // Tests the scratch memory pool object

    ATScratchMem Mem(SCRATCH_MEM_SIZE);                                         // Allocate a 1 meg scratch memory pool

    volatile void *Data;
    int     i, o, Result;

    printf("Testing Scratch Memory...\r\n\r\n");
    printf("Created a scratch memory object 256k in size...\r\n");

    printf("Make %i 1k allocations, write to it, and then reset; %i times...\r\n",
        SCRATCH_MEM_ALLOCS, SCRATCH_MEM_RESETS);
    printf("(%i basic operations, %i bytes written)\r\n",
        SCRATCH_MEM_RESETS * SCRATCH_MEM_ALLOCS,
        ((SCRATCH_MEM_RESETS * SCRATCH_MEM_ALLOCS) * SCRATCH_MEM_GET));

    for ( o = 0; o < SCRATCH_MEM_RESETS; o++) {                                 // Set up outer loop for requested resets
        for ( i = 0; i < SCRATCH_MEM_ALLOCS; i++) {                             // Set up inner loop for request allocs
            Data = Mem.GetScratchMem(SCRATCH_MEM_GET);                          // Allocate mem
            memset((void*)Data, 7, SCRATCH_MEM_GET);                            // Write to it
        }
        Mem.ResetScratchMem();                                                  // Now reset the object and start over
    }

    printf("Success!\r\nCheck alignment (%i allocs, %i resets, as above)...\r\n",
        SCRATCH_MEM_ALLOCS, SCRATCH_MEM_RESETS);
    printf("(%i basic operations, %i bytes written)\r\n",
        SCRATCH_MEM_RESETS * SCRATCH_MEM_ALLOCS,
        ((SCRATCH_MEM_RESETS * SCRATCH_MEM_ALLOCS) * 7));

    for ( o = 0; o < SCRATCH_MEM_RESETS; o++) {                                 // Set up outer loop for requested resets
        for ( i = 0; i < SCRATCH_MEM_ALLOCS; i++) {                             // Set up inner loop for request allocs
            Data = Mem.GetScratchMem(7);                                        // Allocate a nice odd number of bytes
            Result = (((int)Data) % AT_MEM_ALIGN);                              // Check that it is properly aligned
            if ( Result ) {
                printf("Align Failed!\r\nTest Failure...\r\n");
                return 0;
            }
        }
        Mem.ResetScratchMem();                                                  // Now reset the object and start over
    }
    printf("Success!\r\nHighWater = %i\r\n", Mem.GetHighWater());               // Show our high water mark
    printf("\r\nAll Scratch Memory tests successful!\r\n");
    return 0;
}

// **************************************************************************** SharedMemory
int     SharedMemory() {                                                        // Tests the shared memory
    char    *Data;
    int     i, Me = 0, Stagger = 0, Result;

    printf("Testing Shared Memory...\r\n");
    printf("You should run another atlas_test process at the same time to\r\n");
    printf("verify operation. (preferably in side by side windows so you can\r\n");
    printf("see the volley back and forth).\r\n\r\n");

    printf("Create a shared memory object 256k in size...\r\n");

    Result = Mem.CreateSharedMem(55555, 256000);                                // Create a 256k shared mem block- the 55555 is just an arbitrary key, just needs to be unique
    if ( Result != ATERR_SUCCESS ) {                                            // If this fails maybe someone has created it already
        printf("Create() failed- trying Attach() in case it already exists...\r\n");
        Result = Mem.AttachSharedMem(55555);                                    // So let's try to attach to it instead
        if ( Result != ATERR_SUCCESS ) {                                        // If this also fails, just give up
            printf("Attach() also failed- test failure.\r\n");
            return 0;
        }
        printf("Attach() successful!\r\n");
        Me = 1;                                                                 // If I was not the first one up, set my id to 1 (instead of 0 default)
    }
    else {
        printf("Create() successful!\r\n");
    }

    Data = (char*)Mem.GetBasePointer();                                         // Get my base ptr to the memory

    printf("Test the following %i times...\r\n", SHARED_MEMORY_REPS);
    for ( i = 0; i < SHARED_MEMORY_REPS; ++i ) {                                // Repeat a loop for a while, to allow user to see bounce back and forth in process windows
        printf("Looking at what's in the block...\r\n");

        if ( !strncmp(Data,"AAAAA", 5) ) {                                      // If the data in the block is full of A's
            if ( !Me ) {                                                        // And I am the first guy up
                printf("HAH!  It is full of what I wrote to it! (A's)\r\n");
                Stagger= 1;
            }
            else {                                                              // And I am NOT the first guy up
                printf("HEY!  Some dirty so-and-so filled it with A's!!!\r\n");
                Stagger= 0;
            }
        }
        else if ( !strncmp(Data,"BBBBB", 5) ) {                                 // If the data in the block is full of B's
            if ( Me ) {                                                         // And I am the first guy up
                printf("HAH!  It is full of what I wrote to it! (B's)\r\n");
                Stagger= 1;
            }
            else {                                                              // And I am NOT the first guy up
                printf("HEY!  Some dirty so-and-so filled it with B's!!!\r\n");
                Stagger= 0;
            }
        }
        else {                                                                  // Random junk in the block
            printf("Hmmm... Just full of junk!!\r\n");
        }

        if ( !Me )                                                              // If I am the first guy up
            memset(Data, 'A', 256000);                                          // Then I fill the block with A's
        else                                                                    // If I am NOT the first guy up
            memset(Data, 'B', 256000);                                          // Then I fill the block with B's

        if ( Stagger )                                                          // Stagger, so the procs take turns
            sleep(2);
        else
            sleep(0);

    }

    printf("\r\nAll Shared Memory tests successful!\r\n");
    printf("If you ran two processes, you should have seen them\n");
    printf("each get turns at reading & writing.\n");
    printf("You should run ipcs at the command line to make sure\r\n");
    printf("the memory is gone now (to verify the destructor & Close()).\r\n");

    return 1;

}
// **************************************************************************** KernelSemaphores
int     KernelSemaphores() {                                                    // Tests the kernel semaphores
    int Result, i, Count = 0;

    printf("Testing Kernel Semaphores...\r\n");
    printf("You should run another atlas_test process at the same time to\r\n");
    printf("verify operation. (preferably in side by side windows so you can\r\n");
    printf("see the volley back and forth).\r\n\r\n");

    printf("Creating a kernel semaphore...\r\n");
    Result = Sem.Create(12345);                                                 // Create a kernel semaphore- the key is arbitrary, just needs to be unique
    if ( Result != ATERR_SUCCESS ) {                                            // If the create failed, maybe it already exists
        printf("Create() failed- trying Open() in case it already exists...\r\n");
        Result = Sem.Open(12345);                                               // So let's try to open it instead
        if ( Result != ATERR_SUCCESS ) {                                        // If that also fails give up
            printf("Open() also failed- test failure.\r\n");
            return 0;
        }
        printf("Open() successful!\r\n");
    }
    else {
        printf("Create() successful!\r\n");
    }

    printf("Test the following %i times...\r\n", KERNEL_SEM_LOOPS);             // Now let's run thru the requested repetitions & test the main operations
    for ( i = 0; i < KERNEL_SEM_LOOPS; i++) {                                   // The printf's show what's going on here- run two procs to really get it though
        printf("Try to GetLock() for 3 seconds...\r\n");                        // GetLock
        if ( (Result = Sem.GetLock()) != ATERR_SUCCESS ) {
            printf("Failed to GetLock()!\r\nTest failure.\r\n");
            return 0;
        }
        printf("Got it!\r\n");
        sleep(3);
        printf("Now freeing it...\r\n");
        if ( (Result = Sem.FreeLock()) != ATERR_SUCCESS ) {                     // FreeLock
            printf("Failed to FreeLock()!\r\nTest failure.\r\n");
            return 0;
        }
        printf("Freed!\r\n");
        sleep(1);
        printf("Now bouncing it...\r\n");
        while ( (Result = Sem.BounceLock()) != ATERR_SUCCESS ) {                // BounceLock
            printf("Bounce...\r\n");
            sleep(1);
            Count++;
            if ( Count > 12 ) {
                printf("Possible problem with BounceLock()! (MIGHT just be bad timing...)\r\nTest failure.\r\n");
                return 0;
            }
        }
        Count = 0;
        printf("Got it!\r\n");
        sleep(3);
        printf("Now freeing it...\r\n");
        if ( (Result = Sem.FreeLock()) != ATERR_SUCCESS ) {                     // FreeLock
            printf("Failed to FreeLock()!\r\nTest failure.\r\n");
            return 0;
        }
        printf("Freed!\r\n");
        sleep(1);
    }

    printf("\r\nAll Kernel Semaphore tests successful!\r\n");
    printf("You should run ipcs at the command line to make sure\r\n");
    printf("the sems are gone now (to verify the destructor & Close()).\r\n");

    return 1;
}
// **************************************************************************** Arithmetic
int     Arithmetic() {                                                          // Test the Arithmetic
    char        B1[40], B2[40], B3[40];
    ATCurrency  Curr, Curr2, R;
    long        Long;

    printf("Testing Arithmetic...\r\n");
    printf("Testing ATCurrencyToString...\r\n");
    Curr = 9006543210123456999;
    ATCurrencyToString(B1, &Curr, AT_CURR_FPLAIN, 4);                           // Routine to map a currency type into a C string
    printf("\t9006543210123456999, precision 4, plain -> %s\r\n", B1);          // Let's make lots of tests, displaying the use & behavior of various settings
    ATCurrencyToString(B1, &Curr, AT_CURR_FPLAIN, 3);
    printf("\t9006543210123456999, precision 3, plain -> %s\r\n", B1);
    ATCurrencyToString(B1, &Curr, AT_CURR_FPLAIN, 2);
    printf("\t9006543210123456999, precision 2, plain -> %s\r\n", B1);
    ATCurrencyToString(B1, &Curr, AT_CURR_FPLAIN, 1);
    printf("\t9006543210123456999, precision 1, plain -> %s\r\n", B1);
    ATCurrencyToString(B1, &Curr, AT_CURR_FPLAIN, 0);
    printf("\t9006543210123456999, precision 0, plain -> %s\r\n", B1);
    Curr = 9006543210123456389;
    ATCurrencyToString(B1, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t9006543210123456389, precision 4, plain -> %s\r\n", B1);
    ATCurrencyToString(B1, &Curr, AT_CURR_FPLAIN, 3);
    printf("\t9006543210123456389, precision 3, plain -> %s\r\n", B1);
    ATCurrencyToString(B1, &Curr, AT_CURR_FPLAIN, 2);
    printf("\t9006543210123456389, precision 2, plain -> %s\r\n", B1);
    ATCurrencyToString(B1, &Curr, AT_CURR_FPLAIN, 1);
    printf("\t9006543210123456389, precision 1, plain -> %s\r\n", B1);
    ATCurrencyToString(B1, &Curr, AT_CURR_FPLAIN, 0);
    printf("\t9006543210123456389, precision 0, plain -> %s\r\n", B1);
    ATCurrencyToString(B1, &Curr, AT_CURR_FCOMMA, 2);
    printf("\t9006543210123456389, precision 2, comma -> %s\r\n", B1);
    ATCurrencyToString(B1, &Curr, AT_CURR_FCOMMA, 0);
    printf("\t9006543210123456389, precision 0, comma -> %s\r\n", B1);
    ATCurrencyToString(B1, &Curr, AT_CURR_FDOLLAR, 2);
    printf("\t9006543210123456389, precision 2, dollar -> %s\r\n", B1);
    ATCurrencyToString(B1, &Curr, AT_CURR_FDOLLAR, 0);
    printf("\t9006543210123456389, precision 0, dollar -> %s\r\n", B1);
    ATCurrencyToString(B1, &Curr, AT_CURR_FCURR, 2);
    printf("\t9006543210123456389, precision 2, currency -> %s\r\n", B1);
    Curr = Curr * ((ATCurrency)-1);
    ATCurrencyToString(B1, &Curr, AT_CURR_FCURR, 2);
    printf("\t-9006543210123456389, precision 2, currency -> %s\r\n", B1);
    Curr = 0;
    ATCurrencyToString(B1, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t0, precision 4, plain -> %s\r\n", B1);
    ATCurrencyToString(B1, &Curr, AT_CURR_FPLAIN, 3);
    printf("\t0, precision 3, plain -> %s\r\n", B1);
    ATCurrencyToString(B1, &Curr, AT_CURR_FPLAIN, 2);
    printf("\t0, precision 2, plain -> %s\r\n", B1);
    ATCurrencyToString(B1, &Curr, AT_CURR_FPLAIN, 1);
    printf("\t0, precision 1, plain -> %s\r\n", B1);
    ATCurrencyToString(B1, &Curr, AT_CURR_FPLAIN, 0);
    printf("\t0, precision 0, plain -> %s\r\n", B1);
    Curr = 7777;
    ATCurrencyToString(B1, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t.7777, precision 4, plain -> %s\r\n", B1);
    ATCurrencyToString(B1, &Curr, AT_CURR_FPLAIN, 3);
    printf("\t.7777, precision 3, plain -> %s\r\n", B1);
    ATCurrencyToString(B1, &Curr, AT_CURR_FPLAIN, 2);
    printf("\t.7777, precision 2, plain -> %s\r\n", B1);
    ATCurrencyToString(B1, &Curr, AT_CURR_FPLAIN, 1);
    printf("\t.7777, precision 1, plain -> %s\r\n", B1);
    ATCurrencyToString(B1, &Curr, AT_CURR_FPLAIN, 0);
    printf("\t.7777, precision 0, plain -> %s\r\n", B1);

    printf("\r\nTesting ATLongToCurrency...\r\n");                              // Show long to currency
    ATLongToCurrency(Curr2, 1);
    Curr += Curr2;
    ATCurrencyToString(B1, &Curr, AT_CURR_FPLAIN, 0);
    printf("\t1 + 0 -> %s\r\n", B1);
    ATLongToCurrency(Curr2, 123);
    Curr += Curr2;
    ATCurrencyToString(B1, &Curr, AT_CURR_FPLAIN, 0);
    printf("\t1 + 123 -> %s\r\n", B1);

    printf("\r\nTesting ATCurrencyToLong...\r\n");                              // Show currency to long
    Curr = 1234567;
    ATCurrencyToLong(Long, Curr);
    printf("\t123.4567 -> %i\r\n", Long);

    printf("\r\nTesting ATStringToCurrency...\r\n");                            // Show string to currency
    strcpy(B1, "12345.67995"); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t%s -> %s\r\n", B1, B2);
    strcpy(B1, "12345.99995"); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t%s -> %s\r\n", B1, B2);
    strcpy(B1, "12345.67895"); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t%s -> %s\r\n", B1, B2);
    strcpy(B1, "12345.67894"); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t%s -> %s\r\n", B1, B2);
    strcpy(B1, "12345.6789"); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t%s -> %s\r\n", B1, B2);
    strcpy(B1, "12345.678"); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t%s -> %s\r\n", B1, B2);
    strcpy(B1, "12345.67"); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t%s -> %s\r\n", B1, B2);
    strcpy(B1, "12345.6"); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t%s -> %s\r\n", B1, B2);
    strcpy(B1, "12345."); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t%s -> %s\r\n", B1, B2);
    strcpy(B1, "12345"); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t%s -> %s\r\n", B1, B2);
    strcpy(B1, ".1"); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 3);
    printf("\t%s -> %s\r\n", B1, B2);
    strcpy(B1, ".1255"); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 2);
    printf("\t%s -> %s\r\n", B1, B2);
    strcpy(B1, "   12345.67 blah"); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t%s -> %s\r\n", B1, B2);
    strcpy(B1, ",12345.67 blah"); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t%s -> %s\r\n", B1, B2);
    strcpy(B1, " 123,433,345.67 blah"); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t%s -> %s\r\n", B1, B2);
    strcpy(B1, "-12345.67,blah"); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t%s -> %s\r\n", B1, B2);
    strcpy(B1, "-12,345.6722blah"); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t%s -> %s\r\n", B1, B2);
    strcpy(B1, "-12345blah"); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t%s -> %s\r\n", B1, B2);
    strcpy(B1, "+12345blah"); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t%s -> %s\r\n", B1, B2);
    strcpy(B1, ""); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t%s -> %s\r\n", B1, B2);
    strcpy(B1, "x"); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t%s -> %s\r\n", B1, B2);
    strcpy(B1, "900654321012345.6389"); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t%s -> %s\r\n", B1, B2);
    strcpy(B1, "    -$900,654,321,012,345.6389"); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t%s -> %s\r\n", B1, B2);
    strcpy(B1, "+900,654,321,012,345.6389"); ATStringToCurrency(&Curr, B1); ATCurrencyToString(B2, &Curr, AT_CURR_FPLAIN, 4);
    printf("\t%s -> %s\r\n", B1, B2);


    printf("Testing ATDivideCurrency & ATMultCurrency...\r\n");                 // Show division
    ATLongToCurrency(Curr, 500);
    ATLongToCurrency(Curr2, 2);
    ATDivideCurrency(Curr, Curr2, &R);
    ATCurrencyToString(B1, &R, AT_CURR_FCOMMA, 4);
    printf("\t500 / 2 -> %s\r\n", B1);

    ATMultiplyCurrency(Curr2, R, &Curr);                                        // Show multiplication
    ATCurrencyToString(B1, &Curr, AT_CURR_FCOMMA, 4);
    printf("\t2 * (result) -> %s\r\n", B1);

    ATStringToCurrency(&Curr, "950,400.4444");
    ATLongToCurrency(Curr2, 2);
    ATDivideCurrency(Curr, Curr2, &R);
    ATCurrencyToString(B1, &R, AT_CURR_FCOMMA, 4);
    printf("\t950,400.4444 / 2 -> %s\r\n", B1);

    ATMultiplyCurrency(Curr2, R, &Curr);
    ATCurrencyToString(B1, &Curr, AT_CURR_FCOMMA, 4);
    printf("\t2 * (result) -> %s\r\n", B1);

    ATStringToCurrency(&Curr, "379214.2993");
    ATStringToCurrency(&Curr2, "70.7239");
    ATDivideCurrency(Curr, Curr2, &R);
    ATCurrencyToString(B1, &R, AT_CURR_FCOMMA, 4);
    printf("\t379214.2993 / 70.7239 -> %s\r\n", B1);

    ATMultiplyCurrency(Curr2, R, &Curr);
    ATCurrencyToString(B1, &Curr, AT_CURR_FCOMMA, 4);
    printf("\t70.7239 * (result) -> %s\r\n", B1);

    ATStringToCurrency(&Curr, "7");
    ATStringToCurrency(&Curr2, "3");
    ATDivideCurrency(Curr, Curr2, &R);
    ATCurrencyToString(B1, &R, AT_CURR_FCOMMA, 4);
    printf("\t7 / 3 -> %s\r\n", B1);

    ATMultiplyCurrency(Curr2, R, &Curr);
    ATCurrencyToString(B1, &Curr, AT_CURR_FCOMMA, 4);
    printf("\t3 * (result) -> %s\r\n", B1);

    ATStringToCurrency(&Curr, "71313.3333");
    ATStringToCurrency(&Curr2, "112.0011");
    ATDivideCurrency(Curr, Curr2, &R);
    ATCurrencyToString(B1, &R, AT_CURR_FCOMMA, 4);
    printf("\t71313.3333 / 112.0011 -> %s\r\n", B1);

    ATMultiplyCurrency(Curr2, R, &Curr);
    ATCurrencyToString(B1, &Curr, AT_CURR_FCOMMA, 4);
    printf("\t112.0011 * (result) -> %s\r\n", B1);

    printf("Testing ATAddCurrency & ATSubtractCurrency...\r\n");                // Show addition & subtraction
    ATStringToCurrency(&Curr, "71313.3333");
    ATStringToCurrency(&Curr2, "112.0011");
    ATAddCurrency(Curr, Curr2, R);
    ATCurrencyToString(B1, &R, AT_CURR_FCOMMA, 4);
    printf("\t71313.3333 + 112.0011 -> %s\r\n", B1);

    ATStringToCurrency(&Curr, "71313.3333");
    ATStringToCurrency(&Curr2, "112.0011");
    ATSubtractCurrency(Curr, Curr2, R);
    ATCurrencyToString(B1, &R, AT_CURR_FCOMMA, 4);
    printf("\t71313.3333 - 112.0011 -> %s\r\n", B1);

    printf("\r\nAll arithmetic tests appear successful!\r\n(double check output accuracy above)\r\n\r\n");
    return 1;
}
#ifdef  AT_USE_BKDB
// **************************************************************************** VTables
int     VTables() {                                                             // Test the VTables
    VTT     T, *Ret;
    long    Result, i;
    ULONG   Kilroy;
    char    Scratch[AT_MAX_PATH];

    if ( !(strlen(VTTESTDATAPATH)) ) {                                          // Make sure we are configured properly
        printf("Not properly configured to run this test.  You probably need to\r\n");
        printf("run the configure script in the base directory, then re-make.\r\n");
        return 0;
    }

    printf("Testing VTables...\r\n");
    printf("Note that for full testing of VTables, you should probably\r\n");
    printf("run the tests that come with your Berkeley DB distribution.\r\n");
    printf("You should run another atlas_test process at the same time to\r\n");
    printf("verify operation. (preferably in side by side windows so you can\r\n");
    printf("see the volley back and forth).\r\n\r\n");

    Result = ATInitSems();                                                      // To run more than one process we'll need a semaphore to coordinate
    if ( Result != ATERR_SUCCESS ) {
        printf("Failed to init the sems!  Test failure!\r\n");
        return 0;
    }
    Result = Sem.Create(11132 + BTINC);                                         // Create a kernel sem
    if ( Result != ATERR_SUCCESS ) {
        printf("Create() failed- trying Open() in case it already exists...\r\n");
        Result = Sem.Open(11132 + BTINC);                                       // If it failed try an open, since I might not be the first guy up
        if ( Result != ATERR_SUCCESS ) {
            printf("Open() also failed- test failure.\r\n");
            return 0;
        }
        printf("Open() successful!\r\n");
        Kilroy = 2;                                                             // Since I am second up set my kilroy to 2
        printf("I am the second proc up, so I will wait for the signal to start...\r\n");
        Sem.GetLock();                                                          // Wait for the signal to start
    }
    else {
        Sem.GetLock();                                                          // Keep the next guy from starting until we are ready
        printf("Create() successful!\r\n");
        printf("You should also run another process to validate concurrency.\r\n");
        Kilroy = 1;                                                             // Since I am first up set my kilroy to 1
        sprintf(Scratch,"rm -f %s/*",VTTESTDATAPATH);
        system(Scratch);                                                        // Remove any old test table stuff
    }

    if ( Kilroy == 1) {
        printf("Creating VTable...\r\n");                                       // Create the table since I am first up



        if ( (Result = VTable.Create(  VTTESTDATAPATH,                          // Base path for the database this table belongs to
                                "vttest.vtb",                                   // Filename for the virtual table
                                VTTEST,                                         // Systemwide unique IPC key
                                1024000,                                        // Cache size to use in bytes
                                65536,                                          // Page size (may not be larger than 64k)
                                VTMakeKey,                                      // Routine to make a key from a tuple
                                sizeof(T.Key)                                   // Length of the key, in bytes
                                )) != ATERR_SUCCESS ) {
            printf("Failed to open VTable!  Test Failure!\r\n"); return 0;}


        printf("Testing AddTuple...\r\n");                                      // Let's add a couple of tuples

        printf("Adding Apple...\r\n");
        memset(T.Key, 0, sizeof(T.Key)); strcpy(T.Key, "Apple");                // Note how I COMPLETELY clear the key
                strcpy(T.Data, "A red, juicy, delicious fruit, sometimes covered with caramel.");
        if ( (Result =  VTable.AddTuple((void*)&T, sizeof(T.Key) + strlen(T.Data) + 1)) != ATERR_SUCCESS ) {
            printf("AddTuple failed!  Test Failure!\r\n"); return 0;}

        printf("Adding Peach...\r\n");
        memset(T.Key, 0, sizeof(T.Key)); strcpy(T.Key, "Peach");                // Note how I COMPLETELY clear the key
        strcpy(T.Data, "A plump, delicious fruit, which Georgia has become famous for.");
        if ( (Result =  VTable.AddTuple((void*)&T, sizeof(T.Key) + strlen(T.Data) + 1)) != ATERR_SUCCESS ) {
            printf("AddTuple failed!  Test Failure!\r\n"); return 0;}

        printf("Giving the other proc 3 seconds to start...\r\n");              // Wait for the 2nd guy to start
        sleep(3);
        Sem.FreeLock();                                                         // Now let the 2nd guy run
        printf("Now waiting on other proc...\r\n");
        sleep(2);
        Sem.GetLock();                                                          // Wait for my signal
        Sem.FreeLock();                                                         // Then release the lock
    }
    else {
        printf("Opening VTable...\r\n");                                        // Just open the table since I am second up
        if ( (Result = VTable.Open(VTTESTDATAPATH,                              // Base path for the database this table belongs to
                                "vttest.vtb",                                   // Filename for the virtual table
                                VTMakeKey,                                      // Routine to make a key from a tuple
                                sizeof(T.Key)                                   // Length of the key, in bytes
                                )) != ATERR_SUCCESS ) {
            printf("Failed to open VTable!  Test Failure!\r\n"); return 0;}

        printf("Testing AddTuple...\r\n");                                      // Now let's add a few tuple of my own

        printf("Adding Pear...\r\n");
        memset(T.Key, 0, sizeof(T.Key)); strcpy(T.Key, "Pear");                 // Note how I COMPLETELY clear the key
        strcpy(T.Data, "A yummy, though oddly gritty fruit, shaped like the clerk in Monsters Inc.");
        if ( (Result =  VTable.AddTuple((void*)&T, sizeof(T.Key) + strlen(T.Data) + 1)) != ATERR_SUCCESS ) {
            printf("AddTuple failed!  Test Failure!\r\n"); return 0;}

        printf("Adding Watermelon...\r\n");
        memset(T.Key, 0, sizeof(T.Key)); strcpy(T.Key, "Watermelon");           // Note how I COMPLETELY clear the key
        strcpy(T.Data, "A huge fruit, green stripes on the outside, red juicy pulp on the inside.");
        if ( (Result =  VTable.AddTuple((void*)&T, sizeof(T.Key) + strlen(T.Data) + 1)) != ATERR_SUCCESS ) {
            printf("AddTuple failed!  Test Failure!\r\n"); return 0;}

    }


    printf("Testing FindTuple...\r\n");

    memset(T.Key, 0, sizeof(T.Key)); strcpy(T.Key, "Apple");                    // Now for both, let's make sure we can find all the tuples
    if ( !(Ret = (VTT*)VTable.FindTuple((void*)&T.Key, &Result)) )
        printf("\r\nCould not find Apple! (is the other process running?)\r\n");
    else printf("\r\nKey: %s \r\nData: %s\r\n", Ret->Key, Ret->Data);

    memset(T.Key, 0, sizeof(T.Key)); strcpy(T.Key, "Peach");
    if ( !(Ret = (VTT*)VTable.FindTuple((void*)&T.Key, &Result)) )
        printf("\r\nCould not find Peach! (is the other process running?)\r\n");
    else printf("\r\nKey: %s \r\nData: %s\r\n", Ret->Key, Ret->Data);

    memset(T.Key, 0, sizeof(T.Key)); strcpy(T.Key, "Pear");
    if ( !(Ret = (VTT*)VTable.FindTuple((void*)&T.Key, &Result)) )
        printf("\r\nCould not find Pear! (is the other process running?)\r\n");
    else printf("\r\nKey: %s \r\nData: %s\r\n", Ret->Key, Ret->Data);

    memset(T.Key, 0, sizeof(T.Key)); strcpy(T.Key, "Watermelon");
    if ( !(Ret = (VTT*)VTable.FindTuple((void*)&T.Key, &Result)) )
        printf("\r\nCould not find Watermelon! (is the other process running?)\r\n");
    else printf("\r\nKey: %s \r\nData: %s\r\n", Ret->Key, Ret->Data);

    if ( Kilroy != 1 ) {                                                        // If I was the 2nd guy up I am now done
        printf("\r\nClosing...\r\n");
        if ( (Result = VTable.Close()) ) {
            printf("Close failed!  Test Failure!\r\n"); return 0;}

        printf("This process is now done.  If you see all four fruits above, then\r\n");
        printf("the multi-process part of the test is a success.  If not- it failed.\r\n");
        printf("All VTable tests appear successful, but check results above to be sure.\r\n\r\n");

        Sem.FreeLock();
        return 0;
    }

    printf("\r\nIf you have two procs running, you should see four fruits found\r\nabove, otherwise you should see only two.\r\nMoving on to cursor tests...\r\n");


    memset(T.Key, 0, sizeof(T.Key)); strcpy(T.Key, "Guava1");                   // Let's add some more tuples
    strcpy(T.Data, "The first of five scrumptious guavas.");
    if ( (Result =  VTable.AddTuple((void*)&T, sizeof(T.Key) + strlen(T.Data) + 1)) != ATERR_SUCCESS ) {
        printf("AddTuple failed (G1)!  Test Failure!\r\n"); return 0;}

    memset(T.Key, 0, sizeof(T.Key)); strcpy(T.Key, "Guava2");
    strcpy(T.Data, "The second of five scrumptious guavas.");
    if ( (Result =  VTable.AddTuple((void*)&T, sizeof(T.Key) + strlen(T.Data) + 1)) != ATERR_SUCCESS ) {
        printf("AddTuple failed (G2)!  Test Failure!\r\n"); return 0;}

    memset(T.Key, 0, sizeof(T.Key)); strcpy(T.Key, "Guava3");
    strcpy(T.Data, "The third of five scrumptious guavas.");
    if ( (Result =  VTable.AddTuple((void*)&T, sizeof(T.Key) + strlen(T.Data) + 1)) != ATERR_SUCCESS ) {
        printf("AddTuple failed (G3)!  Test Failure!\r\n"); return 0;}

    memset(T.Key, 0, sizeof(T.Key)); strcpy(T.Key, "Guava4");
    strcpy(T.Data, "The fourth of five scrumptious guavas.");
    if ( (Result =  VTable.AddTuple((void*)&T, sizeof(T.Key) + strlen(T.Data) + 1)) != ATERR_SUCCESS ) {
        printf("AddTuple failed (G4)!  Test Failure!\r\n"); return 0;}

    memset(T.Key, 0, sizeof(T.Key)); strcpy(T.Key, "Guava5");
    strcpy(T.Data, "The fifth of five scrumptious guavas.");
    if ( (Result =  VTable.AddTuple((void*)&T, sizeof(T.Key) + strlen(T.Data) + 1)) != ATERR_SUCCESS ) {
        printf("AddTuple failed (G5)!  Test Failure!\r\n"); return 0;}

    printf("\r\nHere are all the fruits added, in forward order.\r\n(Should be nine fruits total)\r\n");
    if ( !(Ret = (VTT*)VTable.SetCursorToStart(&Result)) ) {                    // Test SetCursorToStart
        printf("SetCursorToStart Failed!  Test Failure!\r\n"); return 0;}
    else printf("\r\nKey: %s \r\nData: %s\r\n", Ret->Key, Ret->Data);

    for ( i = 0; i < 8; ++i ) {                                                 // Scroll through all the tuples forwards
        if ( !(Ret = (VTT*)VTable.CursorNext(&Result)) )
            printf("CursorNext Failed (%i)!  \r\nUnless there is only one proc running, this is a Test Failure!\r\n", i);
        else printf("\r\nKey: %s \r\nData: %s\r\n", Ret->Key, Ret->Data);
    }
    if ( (Ret = (VTT*)VTable.CursorNext(&Result)) ) {
        printf("CursorNext found a non-existent tuple!  Test Failure!\r\n"); return 0;}

    printf("\r\nHere are all the fruits added, in reverse order.\r\n(Should be nine fruits total)\r\n");
    if ( !(Ret = (VTT*)VTable.SetCursorToEnd(&Result)) ) {                      // Test SetCursorToEnd
        printf("SetCursorToEnd Failed!  Test Failure!\r\n"); return 0;}
    else printf("\r\nKey: %s \r\nData: %s\r\n", Ret->Key, Ret->Data);

    for ( i = 0; i < 8; ++i ) {
        if ( !(Ret = (VTT*)VTable.CursorPrev(&Result)) )                        // Scroll through all the tuples backwards
            printf("CursorPrev Failed (%i)!  \r\nUnless there is only one proc running, this is a Test Failure!\r\n", i);
        else printf("\r\nKey: %s \r\nData: %s\r\n", Ret->Key, Ret->Data);
    }
    if ( (Ret = (VTT*)VTable.CursorPrev(&Result)) ) {
        printf("CursorPrev found a non-existent tuple!  Test Failure!\r\n"); return 0;}

    printf("\r\nTesting DeleteTuple...\r\n");                                   // Test tuple deletions
    memset(T.Key, 0, sizeof(T.Key)); strcpy(T.Key, "Guava2");
    if ( (Result = VTable.DeleteTuple((void*)&T.Key)) ) {
        printf("\r\nCould not delete Guava2! Test Failure!\r\n"); return 0;}

    memset(T.Key, 0, sizeof(T.Key)); strcpy(T.Key, "Guava4");
    if ( (Result = VTable.DeleteTuple((void*)&T.Key)) ) {
        printf("\r\nCould not delete Guava4! Test Failure!\r\n"); return 0;}


    printf("\r\nHere are Guava1, Guava3, & Guava5 in forward order:\r\n");
    memset(T.Key, 0, sizeof(T.Key)); strcpy(T.Key, "Guava");
    if ( !(Ret = (VTT*)VTable.SetCursor(T.Key, &Result, 5)) ) {                 // Test plain old SetCursor on a partial match
        printf("\r\nSetCursor failed! Test Failure!\r\n"); return 0;}
    else printf("\r\nKey: %s \r\nData: %s\r\n", Ret->Key, Ret->Data);
    if ( !(Ret = (VTT*)VTable.CursorNext(&Result)) ) {
        printf("\r\nCursorNext failed (1)! Test Failure!\r\n"); return 0;}
    else printf("\r\nKey: %s \r\nData: %s\r\n", Ret->Key, Ret->Data);
    if ( !(Ret = (VTT*)VTable.CursorNext(&Result)) ) {
        printf("\r\nCursorNext failed (2)! Test Failure!\r\n"); return 0;}
    else printf("\r\nKey: %s \r\nData: %s\r\n", Ret->Key, Ret->Data);


    printf("\r\nClosing...\r\n");
    if ( (Result = VTable.Close()) ) {                                          // Close the table
        printf("Close failed!  Test Failure!\r\n"); return 0;}

    printf("\r\nAll VTable tests appear successful, but check results above to be sure.\r\n\r\n");
    return 1;
}
void *VTMakeKey(void *inKey) {                                                  // This is our routine to make a key from the tuple
    VTT     *D = (VTT*)inKey;
    return D->Key;
}
#endif
// **************************************************************************** Sessions
int     Sessions() {                                                            // Test the Sessions
    long            Result, i;
    ULONG           Kilroy;
    ATSU            User;
    volatile ATST   *SPtr[5], *Test;

    printf("Testing Sessions...\r\n");
    printf("This is a high level class, so make sure the rest of Atlas is solid first!\r\n");
    printf("You should run another atlas_test process at the same time to\r\n");
    printf("verify operation. (preferably in side by side windows so you can\r\n");
    printf("see the volley back and forth).\r\n\r\n");

    printf("Initializing the sems...\r\n");
    Result = ATInitSems();                                                      // For more than one process we'll need to create a semaphore to coordinate
    if ( Result != ATERR_SUCCESS ) {
        printf("Failed to init the sems!  Test failure!\r\n");
        return 0;
    }
    Result = Sem.Create(78432 + BTINC);                                         // Create a kernel sem
    if ( Result != ATERR_SUCCESS ) {
        printf("Create() failed- trying Open() in case it already exists...\r\n");
        Result = Sem.Open(78432 + BTINC);                                       // If it failed I may not be first up, so try open instead
        if ( Result != ATERR_SUCCESS ) {
            printf("Open() also failed- test failure.\r\n");
            return 0;
        }
        printf("Open() successful!\r\n");
        Kilroy = 2;                                                             // If I was the second guy up, set my kilroy to 2
        printf("I am the second proc up, so I will wait for the signal to start...\r\n");
        Sem.GetLock();                                                          // Wait for my turn to start
    }
    else {
        printf("Create() successful!\r\n");
        Sem.GetLock();                                                          // Keep the next guy up from starting until I'm ready
        printf("You should also run another process to validate concurrency.\r\n");
        Kilroy = 1;                                                             // Set my kilroy to 1 since I was the first guy up
    }

    if ( Kilroy == 1 ) {                                                        // If I'm first up, I'll create the session manager object
        printf("Creating Session class...\r\n");
        if ( (Result = Session.Create(
                            STKEYLOW,                           // Systemwide unique IPC ID for the class- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                            STKEYHIGH,                          // The high value of the range of IPC keys allowed to the session class
                            100,                                // Number of concurrent users to allocate at start- also how will be allocated at once to allow for growth
                            1000,                               // Maximum concurrent users to allow
                            1000,                               // Approximately how may users in total are actually allowed to log on to system
                            6,                                  // Scaling variable- go with at least number of processors + 5 for a good default- too high can be a downer as well as too low, so don't get carried away
                            10 * sizeof(long),                  // Session data size- user specifiable chunk of data associated with each session
                            900,                                // Timeout value, in seconds, for the sessions
                            NULL,                               // Address of a function to call when a timeout occurs- may be null if no callback is desired
                            Kilroy                              // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                            )) != ATERR_SUCCESS ) {
            printf("Failed create!  Test failure!\r\n"); return 0;}
    }
    else {                                                                      // If I'm the second guy up I'll just open the session object
        printf("Opening Session class...\r\n");
        if ( (Result = Session.Open(
                            STKEYLOW,                           // Systemwide unique IPC ID for the class- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                            STKEYHIGH,                          // The high value of the range of IPC keys allowed to the session class
                            NULL,                               // Address of a function to call when a timeout occurs- may be null if no callback is desired
                            Kilroy                              // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                            )) != ATERR_SUCCESS ) {
            printf("Failed open!  Test failure!\r\n"); return 0;}
    }

    if ( Kilroy == 1 ) {                                                        // If I'm the first guy up I'll need to populate the user list
        printf("Testing AddUser...\r\n");
        User.UserID =       100;
        User.Authorize =    7;
        strcpy((char*)User.UserName,"Franchesca");
        strcpy((char*)User.Password,"Sabatini");
        if ( (Result = Session.AddUser(&User)) != ATERR_SUCCESS ) {             // So let's add a few users
            printf("AddUser failed! (1)  Test failure!\r\n"); return 0;}
        User.UserID =       101;
        User.Authorize =    8;
        strcpy((char*)User.UserName,"Arthur");
        strcpy((char*)User.Password,"Clark");
        if ( (Result = Session.AddUser(&User)) != ATERR_SUCCESS ) {
            printf("AddUser failed! (2)  Test failure!\r\n"); return 0;}
        User.UserID =       102;
        User.Authorize =    9;
        strcpy((char*)User.UserName,"Arnold");
        strcpy((char*)User.Password,"Schwartz");
        if ( (Result = Session.AddUser(&User)) != ATERR_SUCCESS ) {
            printf("AddUser failed! (3)  Test failure!\r\n"); return 0;}
        User.UserID =       103;
        User.Authorize =    10;
        strcpy((char*)User.UserName,"Frodo");
        strcpy((char*)User.Password,"Baggins");
        if ( (Result = Session.AddUser(&User)) != ATERR_SUCCESS ) {
            printf("AddUser failed! (4)  Test failure!\r\n"); return 0;}
        User.UserID =       104;
        User.Authorize =    11;
        strcpy((char*)User.UserName,"Julia");
        strcpy((char*)User.Password,"Fairchild");
        if ( (Result = Session.AddUser(&User)) != ATERR_SUCCESS ) {
            printf("AddUser failed! (5)  Test failure!\r\n"); return 0;}

        printf("Testing DeleteUser...\r\n");                                    // Let's delete a user just to try to trip up the session manager
        if ( (Result = Session.DeleteUser("Arnold")) != ATERR_SUCCESS ) {
            printf("DeleteUser failed! (1)  Test failure!\r\n"); return 0;}
    }

    // !!REMEMBER!!: ERROR RETURNS WILL SLEEP A WHILE TO HELP THWART HACKING ATTEMPTS- so this test can easily take a very long time if you get carried away...

    printf("Testing (this can take a moment...)\r\n");
    for ( i = 0; i < 2; ++i ) {                                               // Let's run thru the whole process a few times
        if ( !(SPtr[0] = Session.Login("Franchesca", "Sabatini")) ) {           // Do lots of login & logout attempts, checking for correct responses
            printf("Valid Login failed! (1)  Test failure!\r\n"); return 0;}
        Session.FreeSession();
        
        if ( (SPtr[1] = Session.Login("Arthur", "Dent")) ) {
            printf("Invalid password succeeded! (1)  Test failure!\r\n"); return 0;}
        if ( !(SPtr[1] = Session.Login("Arthur", "Clark")) ) {
            printf("Valid Login failed! (2)  Test failure!\r\n"); return 0;}
        Session.FreeSession();
        
        if ( (SPtr[2] = Session.Login("Arnold", "Schwartz")) ) {
            printf("Deleted user login succeeded! (1)  Test failure!\r\n"); return 0;}
        Session.FreeSession();
        
        if ( !(SPtr[2] = Session.Login("Frodo", "Baggins")) ) {
            printf("Valid Login failed! (3)  Test failure!\r\n"); return 0;}
        Session.FreeSession();

        if ( !(SPtr[3] = Session.Login("Julia", "Fairchild")) ) {
            printf("Valid Login failed! (4)  Test failure!\r\n"); return 0;}
        Session.FreeSession();

        if( !(Test = Session.GetSession(SPtr[0]->SessionID, SPtr[0]->Token))) { // Do lots of GetSession tests, checking for correct responses
            printf("Failed GetSession!  Test failure!\r\n"); return 0;}
        if ( memcmp((void*)Test,(void*)(SPtr[0]), sizeof(ATST)) ) {
            printf("Returned invalid session!  Test failure!\r\n"); return 0;}
        Session.FreeSession();

        if( !(Test = Session.GetSession(SPtr[1]->SessionID, SPtr[1]->Token))) {
            printf("Failed GetSession!  Test failure!\r\n"); return 0;}
        if ( memcmp((void*)Test, (void*)(SPtr[1]), sizeof(ATST)) ) {
            printf("Returned invalid session!  Test failure!\r\n"); return 0;}
        Session.FreeSession();

        if( !(Test = Session.GetSession(SPtr[2]->SessionID, SPtr[2]->Token))) {
            printf("Failed GetSession!  Test failure!\r\n"); return 0;}
        if ( memcmp((void*)Test, (void*)(SPtr[2]), sizeof(ATST)) ) {
            printf("Returned invalid session!  Test failure!\r\n"); return 0;}
        Session.FreeSession();

        if( !(Test = Session.GetSession(SPtr[3]->SessionID, SPtr[3]->Token))) {
            printf("Failed GetSession!  Test failure!\r\n"); return 0;}
        if ( memcmp((void*)Test, (void*)(SPtr[3]), sizeof(ATST)) ) {
            printf("Returned invalid session!  Test failure!\r\n"); return 0;}
        Session.FreeSession();

        if( (Test = Session.GetSession(SPtr[0]->SessionID, -1))) {
            printf("GetSession succeeded w/bogus token!  Test failure!\r\n"); return 0;}
        if( (Test = Session.GetSession(-1, SPtr[0]->Token))) {
            printf("GetSession succeeded w/bogus session ID!  Test failure!\r\n"); return 0;}

                                                                                // Finally, do lots of logouts to check for correct responses
        if ( (Result = Session.Logout(SPtr[0]->SessionID, SPtr[0]->Token)) != ATERR_SUCCESS ) {
            printf("Valid Logout failed! (1)  Test failure!\r\n"); return 0;}
        if ( (Result = Session.Logout(SPtr[0]->SessionID, SPtr[0]->Token)) == ATERR_SUCCESS ) {
            printf("Invalid Logout succeeded! (1)  Test failure!\r\n"); return 0;}
        if ( (Result = Session.Logout(SPtr[1]->SessionID, SPtr[1]->Token)) != ATERR_SUCCESS ) {
            printf("Valid Logout failed! (2)  Test failure!\r\n"); return 0;}
        if ( (Result = Session.Logout(SPtr[2]->SessionID, SPtr[2]->Token)) != ATERR_SUCCESS ) {
            printf("Valid Logout failed! (3)  Test failure!\r\n"); return 0;}
        if ( (Result = Session.Logout(SPtr[3]->SessionID, SPtr[3]->Token)) != ATERR_SUCCESS ) {
            printf("Valid Logout failed! (4)  Test failure!\r\n"); return 0;}
    }
    printf("Passed.\r\n");

    printf("Testing CheckTimeouts...\r\n");
    if ( (Result = Session.CheckTimeouts()) != ATERR_SUCCESS ) {                // Let's run a timeouts check for good measure
        printf("Failed CheckTimeout! Test failure!\r\n"); return 0;}

    printf("All Session tests successful.\r\n");

    if ( Kilroy == 1 ) {
        sleep(3);           // Let the 2nd proc start
        Sem.FreeLock();
        sleep(3);           // Let the 2nd proc finish
        Sem.GetLock();
        Sem.FreeLock();
    }
    else {
        Sem.FreeLock();
    }
    return 1;
}
// **************************************************************************** Templates
int     Templates() {                                                           // Test the Templates
    ATScratchMem    Scratch(1000000);                                           // Create a nice big scratch mem object
    long    Result, i, Count = 0, Count2 = 0, Kilroy;
    char    Buff[20], Banana[] = {"Banana"}, Guava[] = {"Guava"};

    printf("Testing Templates...\r\n");
    printf("This is a high level class, so make sure the rest of Atlas is solid first!\r\n");
    printf("You should run another atlas_test process at the same time to\r\n");
    printf("verify operation. (preferably in side by side windows so you can\r\n");
    printf("see the volley back and forth).\r\n\r\n");

    printf("Initializing the sems...\r\n");
    Result = ATInitSems();                                                      // To test more than one process at a time we'll need a semaphoe set up
    if ( Result != ATERR_SUCCESS ) {
        printf("Failed to init the sems!  Test failure!\r\n");
        return 0;
    }
    Result = Sem.Create(78462 + BTINC);                                         // Create a kernel sem
    if ( Result != ATERR_SUCCESS ) {
        printf("Create() failed- trying Open() in case it already exists...\r\n");
        Result = Sem.Open(78462 + BTINC);                                       // If it fails I may not be first up, so try open instead
        if ( Result != ATERR_SUCCESS ) {
            printf("Open() also failed- test failure.\r\n");
            return 0;
        }
        printf("Open() successful!\r\n");
        Kilroy = 2;                                                             // Since I am 2nd up, set kilroy to 2
        printf("I am the second proc up, so I will wait for the signal to start...\r\n");
        Sem.GetLock();                                                          // Wait to start until told to
    }
    else {
        printf("Create() successful!\r\n");
        Sem.GetLock();                                                          // Keep the 2nd process from starting until we are ready
        printf("You should also run another process to validate concurrency.\r\n");
        Kilroy = 1;                                                             // I am first up, so set kilroy to 1
    }

    if ( Kilroy == 1 ) {
        printf("Creating Template...\r\n");
        if ( (Result = Template.Create(                 // Call to create the template
                                1200,                   // Your best guess at the average page size (probably 50k - 80k)
                                1,                      // Number of pages to allocate for at once (probably 50 - 100)
                                1,                      // Number of blocks to alloc each time
                                &Scratch,               // Scratch memory class to use
                                TTKEYLOW,               // Systemwide unique IPC low entry for the template cache to use- EXPECT ALL NUMBERS IN RANGE TO BE USED
                                TTKEYHIGH,              // Systemwide unique IPC high entry for the template cache to use- EXPECT ALL NUMBERS IN RANGE TO BE USED
                                                        // LEAVE PLENTY OF ROOM BETWEEN LOW & HIGH- At least your expected max number of blocks the cache will allocate divided by three- but BE GENEROUS
                                Kilroy                  // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                                )) != ATERR_SUCCESS) {
            printf("Create failed!  Test failure!\r\n"); return 0;}
    }
    else {
        printf("Opening Template...\r\n");
        if ( (Result = Template.Open(                   // Call to open an already created template
                                &Scratch,               // Scratch memory class to use
                                TTKEYLOW,               // Systemwide unique IPC low entry for the template cache to use- EXPECT ALL NUMBERS IN RANGE TO BE USED
                                TTKEYHIGH,              // Systemwide unique IPC high entry for the template cache to use- EXPECT ALL NUMBERS IN RANGE TO BE USED
                                Kilroy                  // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                                )) != ATERR_SUCCESS) {
            printf("Open failed!  Test failure!\r\n"); return 0;}
    }

repeat:                                                                             // Lets start the tests
    printf("Loading a template...\r\n");                                            // Load a template
    if ( (Result = Template.GetTemplate("../testdata/templatetest1.html")) != ATERR_SUCCESS) {
        printf("GetTemplate failed!  Test failure!\r\n"); return 0;}

    printf("Testing ReplaceLabel...\r\n");                                          // Try replacing various labels
    if ( (Result = Template.ReplaceLabel("Marker1", "Value1, ")) != ATERR_SUCCESS) {
        printf("ReplaceLabel failed!  Test failure!\r\n"); return 0;}
    if ( (Result = Template.ReplaceLabel("Marker2", "Value2, ")) != ATERR_SUCCESS) {
        printf("ReplaceLabel failed!  Test failure!\r\n"); return 0;}
    if ( (Result = Template.ReplaceLabel("Marker3", "Value3,")) != ATERR_SUCCESS) {
        printf("ReplaceLabel failed!  Test failure!\r\n"); return 0;}
    if ( (Result = Template.ReplaceLabel("Marker4", "Value4.")) != ATERR_SUCCESS) {
        printf("ReplaceLabel failed!  Test failure!\r\n"); return 0;}

    if ( (Result = Template.ReplaceLabel("bogus", "bogus")) == ATERR_SUCCESS) {
        printf("ReplaceLabel found bogus label!  Test failure!\r\n"); return 0;}

    if ( (Result = Template.ReplaceLabel("Marker6", "hill.")) != ATERR_SUCCESS) {
        printf("ReplaceLabel failed!  Test failure!\r\n"); return 0;}

    printf("Testing PointLabelTo...\r\n");                                          // Try PointLabelTo
    if ( (Result = Template.PointLabelTo("Fruit", Banana, strlen(Banana))) != ATERR_SUCCESS) {
        printf("ReplaceLabel failed!  Test failure!\r\n"); return 0;}

    printf("Testing AppendToLabel...\r\n");                                         // Try AppendToLabel
    for ( i = 1; i < 11; ++i) {
        sprintf(Buff, "%i\r\n", i);
        if ( (Result = Template.AppendToLabel("Marker5", Buff)) != ATERR_SUCCESS) {
            printf("ReplaceLabel failed!  Test failure!\r\n"); return 0;}
    }

    printf("The authorize value for page 1 is: %i\r\n", Template.GetAuthorize());   // Try GetAuthorize
    printf("Printing page 1 for inspection:\r\n");
    if ( (Result = Template.WriteTemplate()) != ATERR_SUCCESS) {                    // Print the first page for inspection
        printf("WriteTemplate failed!  Test failure!\r\n"); return 0;}

    if ( (Result = Template.FreeTemplate()) != ATERR_SUCCESS ) {                    // Free up the template
        printf("FreeTemplate failed!  Test failure!\r\n"); return 0; }

    printf("Page 1 passed, loading page 2...\r\n");                                 // Load up another page
    if ( (Result = Template.GetTemplate("../testdata/templatetest3.html")) != ATERR_SUCCESS) {
        printf("GetTemplate failed!  Test failure!\r\n"); return 0;}

    printf("Testing ReplaceLabel...\r\n");                                          // Try replacing various labels
    if ( (Result = Template.ReplaceLabel("Marker1", "Value1")) != ATERR_SUCCESS) {
        printf("ReplaceLabel failed!  Test failure!\r\n"); return 0;}
    if ( (Result = Template.ReplaceLabel("Marker2", "Value2")) != ATERR_SUCCESS) {
        printf("ReplaceLabel failed!  Test failure!\r\n"); return 0;}
    if ( (Result = Template.ReplaceLabel("Marker3", "Value3")) != ATERR_SUCCESS) {
        printf("ReplaceLabel failed!  Test failure!\r\n"); return 0;}
    if ( (Result = Template.ReplaceLabel("Marker4", "Value4")) != ATERR_SUCCESS) {
        printf("ReplaceLabel failed!  Test failure!\r\n"); return 0;}

    if ( (Result = Template.ReplaceLabel("Marker6", "Jack and Jill")) != ATERR_SUCCESS) {
        printf("ReplaceLabel failed!  Test failure!\r\n"); return 0;}

    printf("Testing AppendToLabel...\r\n");                                         // Test AppendToLabel
    for ( i = 1; i < 11; ++i) {
        sprintf(Buff, "%i\r\n", i);
        if ( (Result = Template.AppendToLabel("Marker5", Buff)) != ATERR_SUCCESS) {
            printf("ReplaceLabel failed!  Test failure!\r\n"); return 0;}
    }

    printf("Testing PointLabelTo...\r\n");                                          // Test PointLabelTo
    if ( (Result = Template.PointLabelTo("Fruit", Guava, strlen(Guava))) != ATERR_SUCCESS) {
        printf("ReplaceLabel failed!  Test failure!\r\n"); return 0;}

    printf("The authorize value for page 2 is: %i\r\n", Template.GetAuthorize());   // Test GetAuthorize
    printf("Printing page 2 for inspection:\r\n");
    if ( (Result = Template.WriteTemplate()) != ATERR_SUCCESS) {                    // Test WriteTemplate
        printf("WriteTemplate failed!  Test failure!\r\n"); return 0;}

    if ( (Result = Template.FreeTemplate()) != ATERR_SUCCESS ) {                    // Free up the template
        printf("FreeTemplate failed!  Test failure!\r\n"); return 0; }

    if ( Count < 1 ) {                                                              // Let's repeat the test (so we can trace into a cached call as well)
        printf("Page 2 passed.\r\nNow the tests will repeat (to test caching).\r\n");
        Count++;
        goto repeat;
    }

    if ( Count2 < 250 ) {                                                           // Your basic resource leak test.... let's just keep repeating the test
        Count2++;
        Count = 0;
        printf("Now resetting the cache and repeating tests.\r\n");
        Template.EmptyCache();
        Scratch.ResetScratchMem();                                                  // Be sure & rewind our scratch mem
        goto repeat;
    }
    printf("All Template tests successful.\r\n");
    if ( Kilroy == 1 ) {
        sleep(3);                                                                   // Let the other guy start
        Sem.FreeLock();
        sleep(3);                                                                   // Let the other guy finish
        Sem.GetLock();
        Sem.FreeLock();
    }
    else {
        Sem.FreeLock();
    }
    return 1;
}
// **************************************************************************** ExitHandler
void     ExitHandler(int Signal) {                                              // Simple signal handler
    printf("Oops!  Shutting down!\r\n");
    switch ( Signal) {
        case    SIGFPE:     printf("Processing SIGFPE\r\n"); break;
        case    SIGILL:     printf("Processing SIGILL\r\n"); break;
        case    SIGQUIT:    printf("Processing SIGQUIT\r\n"); break;
        case    SIGSEGV:    printf("Processing SIGSEGV\r\n"); break;
        case    SIGSYS:     printf("Processing SIGSYS\r\n"); break;
        case    SIGTERM:    printf("Processing SIGTERM\r\n"); break;
        default:            printf("Undefined Signal!\r\n");break;
    };

    Cleanup();                                                                  // Clean up after ourselves

    exit(0);
}

// **************************************************************************** Main
int main(int argc, char *argv[ ]) {
    int Return = 0;                                                             // Our return value

    signal( SIGFPE, ExitHandler);                                               // Register our signal handler for various signals
    signal( SIGILL, ExitHandler);
    signal( SIGQUIT, ExitHandler);
    signal( SIGSEGV, ExitHandler);
    signal( SIGSYS, ExitHandler);
    signal( SIGTERM, ExitHandler);

    if ( argc != 2 ) {                                                          // Check input
        printf("\nUsage:\r\natlas_test <section>\r\n\r\nWhere section is one of:\r\n\r\n");
        printf("KernelSemaphores\r\n");
        printf("SharedMemory\r\n");
        printf("ScratchMemory\r\n");
        printf("SpinLocks\r\n");
        printf("Atomics\r\n");
        printf("ShareLocks\r\n");
        printf("Timing\r\n");
        printf("Logs\r\n");
        printf("Tables\r\n");
        printf("BTrees\r\n");
        printf("Templates\r\n");
        printf("Sessions\r\n");
        printf("Arithmetic\r\n");
#ifdef  AT_USE_BKDB
        printf("VTables\r\n");
#endif
        printf("\r\n");
        return 0;
    }

    if ( !stricmp(argv[1], "KernelSemaphores") )                                // Try to match the user's request
        Return = KernelSemaphores();
    else if ( !stricmp(argv[1], "SharedMemory") )
        Return = SharedMemory();
    else if ( !stricmp(argv[1], "ScratchMemory") )
        Return = ScratchMemory();
    else if ( !stricmp(argv[1], "SpinLocks") )
        Return = SpinLocks();
    else if ( !stricmp(argv[1], "Atomics") )
        Return = Atomics();
    else if ( !stricmp(argv[1], "ShareLocks") )
        Return = ShareLocks();
    else if ( !stricmp(argv[1], "Timing") )
        Return = Timing();
    else if ( !stricmp(argv[1], "Logs") )
        Return = Logs();
    else if ( !stricmp(argv[1], "Tables") )
        Return = Tables();
    else if ( !stricmp(argv[1], "BTrees") )
        Return = BTrees();
    else if ( !stricmp(argv[1], "Templates") )
        Return = Templates();
    else if ( !stricmp(argv[1], "Sessions") )
        Return = Sessions();
    else if ( !stricmp(argv[1], "Arithmetic") )
        Return = Arithmetic();
#ifdef  AT_USE_BKDB
    else if ( !stricmp(argv[1], "VTables") )
        Return = VTables();
#endif
    else {
        printf("Sorry, %s is not a recognized segment.\r\n", argv[1]);
    }

    Cleanup();
    return Return;
}
// **************************************************************************** Cleanup
void    Cleanup() {                                                             // This function is called at every shutdown or signal trap

    /* Note that all Atlas objects have a clean up function that can be safely called
    repeatedly, necessary or not. */

#ifdef  AT_USE_BKDB
    VTable.Close();
#endif
    Session.Close();
    Template.Close();
    BTree.Close();
    Email.Close();
    Table.CloseTable();
    Sem.Close();
    Mem.FreeSharedMem();
}
