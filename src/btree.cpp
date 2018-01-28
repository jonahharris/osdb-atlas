// ****************************************************************************
// * btree.cpp - The BTree code for Atlas.                                    *
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>

#ifdef      AT_WIN32
    #include	<windows.h>
#else
    #include    <unistd.h>
    #include    <sys/types.h>
    #include    <sys/ipc.h>
    #include    <sys/sem.h>
#endif

#include "general.h"
#include "table.h"
#include "btree.h"


struct ATBTreeInformationBlock {                                                // Struct that stores all the common control info for a BTree
    volatile long       KeyLength;                                              // Length of the keys
    volatile long       KeysPerPage;                                            // Number of keys stored per page
    volatile long       TrueKeyLength;                                          // Actual space taken up by stored keys
    volatile long       PageType;                                               // What type of page type is this?  DO NOT MOVE! // IMPORTANT: MUST BE IN SAME BYTE OFFSET AS THE SAME FIELD IN THE HEADER!!!!
    volatile long       AllocSize;                                              // Number of pages allocated as a block each time we need to grow
    volatile long       PageKey;                                                // IMPORTANT: MUST BE IN SAME BYTE OFFSET AS THE SAME FIELD IN THE HEADER!!!!
    volatile long       IndexType;                                              // Either AT_BTREE_PRIMARY or AT_BTREE_SECONDARY
    volatile long       SystemKey;                                              // System IPC key
    volatile long       TotalPageSize;                                          // Total size of a BTree page
};

/* BTree Page Layout
    -------------------------
    | H | K | K             |
    | E | E | E             |
    | A | Y | Y             |
    | D |   | S             |
    | E | P |               |
    | R | T |               |
    |   | R |               |
    |   | S |               |
    -------------------------
*/

// The sentinel for the end of a BTree delete chain
#define     AT_BTREE_END_CHAIN          (-1)
// Node page type
#define     AT_BTREE_NODE               (2)
#define     AT_BTREE_LEAF               (3)
#define     AT_BTREE_ROOT               (4)
#define     AT_BTREE_INFO               (5)

// Cursor status flags
#define     AT_BTREE_CSTATUS_BOB        (-1)
#define     AT_BTREE_CSTATUS_NORMAL     (0)
#define     AT_BTREE_CSTATUS_EOB        (1)

// Special FINDFIRST lock alert flags
#define     AT_BTREE_SHIFTLOW           (1)
#define     AT_BTREE_SHIFTLEFT          (2)
#define     AT_BTREE_SHIFTFAILED        (3)

struct ATBTreePageHeader {                                                      // Header structure for a BTree page header
    ATLOCK              ALock;                                                  // The page's share lock
    volatile long       AvailableChain;                                         // Free node type of available blocks chain
    volatile long       NumberKeys;                                             // Number of keys in the page
    volatile long       PageType;                                               // The page's type
    volatile long       Block;                                                  // Part 1 of 2 32bit ints that make up this page's ID
    volatile long       PageKey;                                                // IMPORTANT: MUST BE IN SAME BYTE OFFSET AS THE SAME FIELD IN THE INFO PAGE!!!!
    volatile long       Tuple;                                                  // Part 2 of 2 32bit ints that make up this page's ID
    volatile ATBTSHCB   Low;                                                    // The low CB for the page
    volatile ATBTSHCB   PrevPage;                                               // The prev page in this sequence
    volatile ATBTSHCB   NextPage;                                               // The prev page in this sequence
};

struct testkey {
    long    BBlock;
    long    BTuple;
    long    TBlock;
    long    TTuple;
    long    Key;
};
typedef struct testkey TK;
struct testph {
    ATBTPH  Header;
    long    Ptrs[50];
    TK      Keys[50];
};
typedef struct testph TPH;
TPH Tester;


// Finds the start of the key ptrs where: X = Start of Page
#define     ATBTreeKeyPtrBase(X)            ((long*)(((char*)(X)) + sizeof(ATBTPH)))
// Finds the start of the keys where: X = Key Ptr Base ptr
#define     ATBTreeKeyBase(X)               (((long*)(X)) + KeysPerPage)
// Returns a CB ptr USING THE KEY PTRS to a given key where: W = Key Ptr Base, X = Key Base Ptr, Y = Key Number)
// Don't try to use this for adds- for an add the key ptrs are not right, of course
#define     ATBTreeKey(W, X, Y)             (  (volatile ATBTCB*)                                       \
                                            (  ((char*)(X)) +                                           \
                                            (TrueKeyLength * (*(((long*)W) + (Y))))   ))
// Returns a CB ptr USING THE KEY PTRS where: X = Start of Page, Y = Key Number, T1 = long * temp, T2 = char * temp
// Don't try to use this for adds- for an add the key ptrs are not right, of course
#define     ATBTreeKeyDirect(X, Y, T1, T2)  (  (volatile ATBTCB*)                                                           \
                                            (  ((char*)(T2 = (char*)ATBTreeKeyBase((char*)(T1 = ATBTreeKeyPtrBase(X))))) +  \
                                            (TrueKeyLength * ( *(((long*)(T1 = ATBTreeKeyPtrBase(X))) + (Y)))) ))
// Returns a CB ptr NOT USING THE KEY PTRS to a given key where: X = Key Base Ptr, Y = Key Number)
// Use this for adds
#define     ATBTreeKeyRaw(X, Y)             (  (volatile ATBTCB*)                                       \
                                            (  ((char*)(X)) +                                           \
                                            (TrueKeyLength * (Y))   ))

// ****************************************************************************
// ****************************************************************************
//                                ATBTREE METHODS
// ****************************************************************************
// ****************************************************************************

// **************************************************************************** WriteBTree
int ATBTree::WriteBTree(                                                        // Write a BTree to a disk file
                            char        *inFileName                             // Filename to write the btree to
                            ) {
    return PageMan.WriteTable(inFileName);                                      // Save our data & structures
}
// **************************************************************************** LoadBTree
int ATBTree::LoadBTree(                                                         // Load a BTree from a disk file
                                                                                // THIS CALL SHOULD BE MADE RIGHT AFTER CREATE WITH MATCHING PARMS TO THE BTREE TO BE LOADED.  LOADING MISMATCHED BTREES COULD BE DISASTROUS (though it tries not to do it at all), AND FAILURES MAY LEAVE THE BTREE IN UNKNOWN STATE.
                            char        *inFileName                             // Filename to load the btree from
                            ) {
    ATSharedTable   NewTable;                                                   // Scratch table to use to load from
    ATBTInfo        *Test;                                                      // Old info block
    char            Buffer[sizeof(ATTableInfo) + sizeof(ATTBAHG)];              // Buffer
    long            Result, inKey;
    volatile        ATBTPH  *PH;

    if ( !inFileName )  return ATERR_BAD_PARAMETERS;
    if ( Root->NumberKeys > 0 ) return ATERR_UNSAFE_OPERATION;                  // Don't try to load a BTree into a used BTree- unsafe


    if ( (Result = NewTable.CreateTable((Info->SystemKey) + 1, Info->TotalPageSize,// Create a table to load our old table into
        Info->AllocSize + 1, Info->AllocSize, 1, 1, 12, Kilroy)) != ATERR_SUCCESS )
        goto error_exit;

    if ( (Result = NewTable.LoadTable(inFileName, Buffer,sizeof(ATTableInfo)    // Now load it up
            + sizeof(ATTBAHG))) != ATERR_SUCCESS )
        goto error_exit;

    NewTable.ResetCursor();                                                     // Let's find the info page in the new table
    do {                                                                        // This has to be checked before we do anything to see if this will even work
        if ( !(Test = (ATBTInfo*)NewTable.NextTuple()) ) {                      // Grab pages from the new table
            Result = ATERR_OPERATION_FAILED;
            goto error_exit;
        }
    }
    while ( Test->PageKey != AT_BTREE_INFO );                                   // Until we find our info page (will be one of the first few pages)

    if (    Info->KeyLength != Test->KeyLength ||                               // Let's do a few comparisons to make sure that these BTrees are compatible
            Info->KeysPerPage != Test->KeysPerPage ||
            Info->AllocSize != Test->AllocSize ||
            Info->IndexType != Test->IndexType ||
            Info->TotalPageSize != Test->TotalPageSize )
        goto mismatch_exit;

    NewTable.CloseTable();                                                      // We can close our temp table now
    inKey = Info->SystemKey;                                                    // Save the ipc key
    PageMan.CloseTable();                                                       // Close the old table

                                                                                // Now that we have confirmed that the BTrees are compatible...
    if ( (Result = PageMan.CreateFromFile(                                      // Now let's just recreate ourselves from the disk file
                            inKey,                                              // Systemwide unique IPC ID for this table- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                            Kilroy,                                             // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                            inFileName,                                         // Filename to load the table from
                            Buffer,                                             // A buffer that may be used for file I/O
                            sizeof(ATTableInfo) + sizeof(ATTBAHG)               // The size of the I/O buffer provided (recommend at least 32-64k)
                            )) != ATERR_SUCCESS ) {
            return Result;}

    PageMan.ResetCursor();                                                      // Get access to our info page (will be one of the first few pages- no guarantees on placement)
    do {
        if ( !(Info = (volatile ATBTInfo*)PageMan.NextTuple()) )                // Keep grabbing tuples
            return ATERR_OPERATION_FAILED;
    }
    while ( Info->PageKey != AT_BTREE_INFO );                                   // Until we find the Info page

    PageMan.ResetCursor();                                                      // Get access to our root page (will be one of the first few pages- no guarantees on placment)
    do {
        if ( !(Root = (volatile ATBTPH*)PageMan.NextTuple()) )                  // Keep grabbing tuples
            return ATERR_OPERATION_FAILED;
    }
    while ( Root->PageKey != AT_BTREE_ROOT );                                   // Until we find the root

    PageMan.ResetCursor();                                                      // Let's make sure we won't have any open pagelocks in here
    while ( PH = (volatile ATBTPH*)PageMan.NextTuple() ) {                      // Keep grabbing tuples
        if ( PH->PageKey != AT_BTREE_INFO ) {                                   // For everyone except the info page
            PH->ALock = 0;                                                      // Make sure the locks are zeroed
        }
    }

    return ATERR_SUCCESS;

error_exit:
    NewTable.CloseTable();
    return Result;
mismatch_exit:
    NewTable.CloseTable();
    return ATERR_UNSAFE_OPERATION;
}
// **************************************************************************** CreateFromFile
int ATBTree::CreateFromFile(                                                    // Create a BTree from a disk file
                                char        *inFileName,                        // Filename to load the btree from
                                int         inKey,                              // Systemwide unique IPC ID for this BTree- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                                                                                // !!IMPORTANT!! The key that you specify here will be incremented by 1 each time a new section needs to be allocated,
                                                                                // So be sure to leave adequate room to grow between your IPC ID's!!
                                ATSharedTable   *inTable,                       // Which table to create it for (pointer to a valid created ATSharedTable
                                                                                // MUST BE AN OBJECT INSTANCE THAT BELONGS TO THE SAME THREAD THAT IS CALLING
                                ATBTreeComp     *inComp,                        // Pointer to the function that will be called to compare the key values- should behave just like memcmp(), returning <0,0, or >0 integer.
                                ATBTreeMakeKey  *inMakeKey,                     // Pointer to a function to make a key from a tuple- i.e., when given the pointer to a tuple, it returns a ptr to a valid key for the tuple
                                ULONG           inKilroy                        // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                                ) {
    long        Result, StartAlloc;
    char        Buffer[sizeof(ATTableInfo) + sizeof(ATTBAHG)];                  // Buffer
    volatile    ATBTPH  *PH;

    if ( !inFileName || !inKey || !inTable || !inComp || !inKilroy )
        return ATERR_BAD_PARAMETERS;

    if ( (Result = PageMan.CreateFromFile(                                      // Now let's just recreate ourselves from the disk file
                            inKey,                                              // Systemwide unique IPC ID for this table- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                            inKilroy,                                           // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                            inFileName,                                         // Filename to load the table from
                            Buffer,                                             // A buffer that may be used for file I/O
                            sizeof(ATTableInfo) + sizeof(ATTBAHG)               // The size of the I/O buffer provided (recommend at least 32-64k)
                            )) != ATERR_SUCCESS ) {
            return Result;}

    Table =     inTable;                                                        // Init class members
    Compare =   inComp;
    MakeKey =   inMakeKey;

    PageMan.ResetCursor();                                                      // Get access to our info page (will be one of the first few pages- no guarantees on placment)
    do {
        if ( !(Info = (volatile ATBTInfo*)PageMan.NextTuple()) )
            return ATERR_OUT_OF_MEMORY;
    }
    while ( Info->PageKey != AT_BTREE_INFO );

    KeysPerPage =   Info->KeysPerPage;                                          // Fill in our info from the global page
    KeyLength =     Info->KeyLength;
    TrueKeyLength = Info->TrueKeyLength;
    AllocSize =     Info->AllocSize;
    Kilroy =        inKilroy;
    IndexType =     Info->IndexType;

    PageMan.ResetCursor();                                                      // Get access to our root page (will be one of the first few pages- no guarantees on placment)
    do {
        if ( !(Root = (volatile ATBTPH*)PageMan.NextTuple()) )
            return ATERR_OUT_OF_MEMORY;
    }
    while ( Root->PageKey != AT_BTREE_ROOT );

    PageMan.ResetCursor();                                                      // Let's make sure we won't have any open pagelocks in here
    while ( PH = (volatile ATBTPH*)PageMan.NextTuple() ) {                      // Keep grabbing tuples
        if ( PH->PageKey != AT_BTREE_INFO ) {                                   // For everyone except the info page
            PH->ALock = 0;                                                      // Make sure the locks are zeroed
        }
    }

    if ( (Result = Table->RegisterBTree(this, IndexType)) != ATERR_SUCCESS)     // Register the BTree with the table
        return Result;

    return ATERR_SUCCESS;
}
// **************************************************************************** TestCrabLock
int ATBTree::TestCrabLock(                                                      // Internal routine for getting a BTree crab share lock
                                ATLOCK          *HeldLock,                      // Ptr to the lock you currently hold
                                ATLOCK          *DesiredLock                    // Ptr to lock caller's wants to crab to
                            ) {
    long    Result, Attempts = 0;
retry:
    if ( (Result = ATBounceShare(DesiredLock)) == ATERR_SUCCESS)                // Try to get the lock below this page
        return ATERR_SUCCESS;                                                   // Return if I get it
    else {
        if ( !(*HeldLock & AT_SHARE_EXC) )  {                                   // As long as nobody has gotten an exclusive on our page
            ATSpinLockArbitrate(Attempts);                                      // We'll just keep trying
            Attempts++;
            goto retry;
        }
        else                                                                    // Crap- someone has an exclusive on our page
            return ATERR_OBJECT_IN_USE;                                         // So give it up- we don't want a deadlock
    }
}
// **************************************************************************** FindInPage
int ATBTree::FindInPage(                                                        // Find a key in the page- most parameters are set in the class members- check there for info.
                        volatile ATBTPH     *Page,                              // Page to search
                        void                *inKey,                             // Key to search for
                        long                Block,                              // TBlock to search for
                        long                Tuple                               // TTuple to search for
                        ) {
    long    *KeyPtrs = ATBTreeKeyPtrBase(Page);
    long    *KeysBase = ATBTreeKeyBase(KeyPtrs);
    long    Left = 0, Right = Page->NumberKeys - 1, CurrKey;
    long    Found = 0, *T1, Count = 0;
    volatile char   *TestVal, *T2;
    volatile ATBTCB *CB;
    volatile ATBTPH *PH, *PH2;
    long    Result, *TKeyPtrs, *TKeysBase;

    if ( Page->NumberKeys ) {                                                   // As long as the page has keys in it
        switch ( SearchMode ) {                                                 // We customize our seach based on the search mode
        // ***************************************** FIND DIRECT
        case AT_BTREE_FINDDIRECT:
retry_dr:                                                                       // This part is a pretty typical binary search
            CurrKey = (Left + Right);                                           // Take the range of records left
            CurrKey >>= 1;                                                      // Divide it by two

            SearchVal = (volatile char*)((CB = ATBTreeKey(KeyPtrs, KeysBase, CurrKey)) + 1);// Get the CB for the tuple to compare, and a string pointer as well (attempting to let the optimizer know it's okay to skip one read/write here... who knows)
            if ( (SearchResult = (Compare)(inKey, (void*)SearchVal, SearchCompareLength)) < 0 )// If the key is smaller...
                Right = CurrKey - 1;                                            // Move to the bottom half
            else if ( SearchResult )                                            // If it was larger...
                Left = CurrKey + 1;                                             // Move to the top half
            else {                                                              // At this point we have a match on the key itself...
                if ( IndexType == AT_BTREE_PRIMARY ) {                          // As long as this is the primary key
                    SearchContainedIn = CurrKey;                                // Set the contained in value
                    return 1;                                                   // We're outta here
                }                                                               // We MUST order secondary keys by tuple ID as well- otherwise it becomes arbitrary, and that IS VARY BAD when it comes to page splits- higher pages in front of lower, etc.
                                                                                // It also makes deletes on secondaries very fast, as opposed to the alternative
                SearchResult = Block - CB->TBlock;                              // Compare the blocks
                if ( SearchResult < 0 )
                    Right = CurrKey - 1;
                else if ( SearchResult )
                    Left = CurrKey + 1;
                else {                                                          // The blocks are equal, now lets compare the tuples
                    SearchResult = Tuple - CB->TTuple;
                    if ( SearchResult < 0 )
                        Right = CurrKey - 1;
                    else if ( SearchResult )
                        Left = CurrKey + 1;
                    else {                                                      // We finally have a match
                        SearchContainedIn = CurrKey;                            // Set the contained in value
                        return 1;                                               // We're outta here
                    }
                }
            }
            if ( Left <= Right ) goto retry_dr;                                 // Keep looking til we run out of records
            SearchContainedIn = CurrKey;                                        // Set the contained in value
            return 0;                                                           // Return a false for the find

        // ***************************************** FIND FIRST
        case AT_BTREE_FINDFIRST:
retry_ff:                                                                       // This is a binary search for a first occurring value
            CurrKey = (Left + Right);                                           // Take the range of records left
            CurrKey >>= 1;                                                      // Divide it by two
            SearchVal = ((volatile char*)((ATBTreeKey(KeyPtrs, KeysBase, CurrKey)) + 1));// Figure out where the compare key is
            if ( (SearchResult = (Compare)(inKey, (void*)SearchVal, SearchCompareLength)) < 0 )// If the key is smaller...
                Right = CurrKey - 1;                                            // Move to the bottom half
            else if ( SearchResult )                                            // If it was larger...
                Left = CurrKey + 1;                                             // Move to the top half
            else {
                Found = 1;                                                      // Found a match!
                ( Right != CurrKey ) ? Right = CurrKey:Right--;                 // Note we DON'T exclude this record from the next search, just move down the top
            }
            if ( Left < Right )                                                 // Keep looking til we run out of records
                goto retry_ff;
            else if ( Left == Right && !Count ) {                               // Make sure we try the last case
                Count++;
                goto retry_ff;
            }

            // There is a special case with a Found = True, where we just aren't sure if this is the FIRST one unless we peek at the previous bucket, since ranges can span buckets
            // This could be one of 3 cases- entry to my left, the lowpage entry, or a child of the page to my left.  Yes, in fact, it is a pain.
            if ( Found && (Page->PageType != AT_BTREE_LEAF) ) {                 // If we found a match and this is not a leaf page
                if ( CurrKey > 0 ) {                                            // As long as we aren't at the very start
                    CB = ATBTreeKeyDirect(Page, (CurrKey - 1), T1, T2);         // Get the block for the lesser page
                    PH = (volatile ATBTPH*)PageMan.LocateTuple(CB->BBlock, CB->BTuple);//Get a ptr to that page
                    LockAlert = AT_BTREE_SHIFTLOW;                              // Set the lock alert
                    if ( (Count = TestCrabLock(&(Page->ALock), &(PH->ALock))) != ATERR_SUCCESS) {// Try to crab to that page
                        LockAlert = AT_BTREE_SHIFTFAILED; return 0;}
                    TKeyPtrs = ATBTreeKeyPtrBase(PH);                           // Figure out where everything is
                    TKeysBase = ATBTreeKeyBase(TKeyPtrs);
                }
                else {                                                          // We are at the very start, so get the lowpage if there is one
                    if ( Page->Low.BBlock != AT_BTREE_END_CHAIN ) {             // Is there a low page?
                        PH = (volatile ATBTPH*)PageMan.LocateTuple(Page->Low.BBlock, Page->Low.BTuple);// Get a ptr to that page
                        LockAlert = AT_BTREE_SHIFTLOW;                          // Set the lock alert
                        if ( (Count = TestCrabLock(&(Page->ALock), &(PH->ALock))) != ATERR_SUCCESS) {
                            LockAlert = AT_BTREE_SHIFTFAILED; return 0;}
                        TKeyPtrs = ATBTreeKeyPtrBase(PH);                       // Figure out where everything is
                        TKeysBase = ATBTreeKeyBase(TKeyPtrs);
                    }
                    else if  ( Page->PrevPage.BBlock != AT_BTREE_END_CHAIN ) {  // Is there a page to the left?
                        PH = (volatile ATBTPH*)PageMan.LocateTuple(Page->PrevPage.BBlock, Page->PrevPage.BTuple);// First, get the left hand page
                        if ( (Count = TestCrabLock(&(Page->ALock), &(PH->ALock))) != ATERR_SUCCESS) {
                            LockAlert = AT_BTREE_SHIFTFAILED; return 0;}
                        TKeyPtrs = ATBTreeKeyPtrBase(PH);                       // Figure out where everything is
                        TKeysBase = ATBTreeKeyBase(TKeyPtrs);                   // Note that we'll always have at least one record in this page
                        CB = ATBTreeKey(TKeyPtrs, TKeysBase, PH->NumberKeys - 1);// Get a CB to the last entry
                        PH2 = (volatile ATBTPH*)PageMan.LocateTuple(CB->BBlock, CB->BTuple);// Now load the page it points to
                        if ( (Count = TestCrabLock(&(PH->ALock), &(PH2->ALock))) != ATERR_SUCCESS) { // Not worried about contention between Page->Alock & PH locks- they are different paths
                            ATFreeShare(&(PH->ALock));                              // Free its parent
                            LockAlert = AT_BTREE_SHIFTFAILED; return 0;}
                        ATFreeShare(&(PH->ALock));                              // Free its parent
                        PH = PH2;                                               // Now we are back to having only one page open
                        TKeyPtrs = ATBTreeKeyPtrBase(PH);                       // Figure out where everything is
                        TKeysBase = ATBTreeKeyBase(TKeyPtrs);                   // Note that we'll always have at least one record in this page
                        LockAlert = AT_BTREE_SHIFTLEFT;
                    }
                    else {                                                      // Only in error
                        PH = NULL;
                    }
                }
                if ( PH ) {                                                     // As long as we have a page to check
                    // Note, this page CANNOT be going to split if I can lock it- that would break the protocol...
                    if ( Page->NumberKeys > 0 ) {                               // If there are any records in here
                        TestVal = ((volatile char*)((ATBTreeKey(TKeyPtrs, TKeysBase, PH->NumberKeys - 1)) + 1));//Get a ptr to the last key
                        if ( !((Compare)(inKey, (void*)TestVal, SearchCompareLength)) ) {// If the last guy matches what I am looking for
                            SearchResult = 0;                                   // Just making sure
                            if ( LockAlert != AT_BTREE_SHIFTLEFT )
                                SearchContainedIn = CurrKey - 1;                // Just point at the previous bucket if this is not a SHIFTLEFT
                            else                                                // Otherwise we literally have to tell the routine where the new page went
                                ShiftLeft = PH;
                            return Found;
                        }
                    }
                    ATFreeShare(&(PH->ALock));                                  // Not a fit! Free the share and return
                    LockAlert = 0;
                }
            }
            SearchContainedIn = CurrKey;                                        // Set the contained in value
            return Found;                                                       // Return the find

        // ***************************************** FIND LAST
        case AT_BTREE_FINDLAST:
retry_fl:                                                                       // This is a binary search for a last occurring value

            CurrKey = (Left + Right);                                           // Take the range of records left
            CurrKey >>= 1;                                                      // Divide it by two

            SearchVal = ((volatile char*)((ATBTreeKey(KeyPtrs, KeysBase, CurrKey)) + 1));// Figure out where the compare key is
            if ( (SearchResult = (Compare)(inKey, (void*)SearchVal, SearchCompareLength)) < 0 )// If the key is smaller...
                Right = CurrKey - 1;                                            // Move to the bottom half
            else if ( SearchResult )                                            // If it was larger...
                Left = CurrKey + 1;                                             // Move to the top half
            else {
                Found = 1;                                                      // Found a match!
                ( Left != CurrKey ) ? Left = CurrKey:Left++;                    // Note we DON'T exclude this record from the next search, just move up the bottom
            }
            if ( Left < Right )                                                 // Keep looking til we run out of records
                goto retry_fl;
            else if ( Left == Right && !Count ) {                               // Make sure we try the last case
                Count++;
                goto retry_fl;
            }
            while ( Found && (SearchResult < 0) && (CurrKey > -1) ) {           // This is a sporadic thing that executes usually once- one of these days I want to figure out why...
                SearchVal = ((volatile char*)((ATBTreeKey(KeyPtrs, KeysBase, CurrKey)) + 1));
                SearchResult = (Compare)(inKey, (void*)SearchVal, SearchCompareLength);
                if ( SearchResult == 0 ) break;
                CurrKey -= 1;
            }
            SearchContainedIn = CurrKey;                                        // Set the contained in value
            return Found;                                                       // Return the find
        }
    }
    else {                                                                      // The page is empty
        SearchContainedIn = 0;                                                  // Set the contained in value
        SearchResult = 0;                                                       // Neutral compare
        return 0;                                                               // Was not a match...
    }
}
// **************************************************************************** PagedFindKey
int ATBTree::PagedFindKey(                                                      // Routine to find the page a key belong in- page is appropriately locked on return
                            void        *inKey,                                 // Key to find
                            long        Block,                                  // TBlock & TTuple to find
                            long        Tuple
                            ) {
    volatile    ATBTPH      *NextPage;                                          // Many parameters are set in the class members- check there for info.
    long        Result, Found, *T1, WhichPage;
    volatile    ATBTCB      *CB;
    char        *T2;

    EscalationFailures = 0;                                                     // Make sure this starts with a clean counter every time

start_over:
    SearchFoundPage = Root;                                                     // Start at the root
    if ( SearchLockMode )                                                       // For any actual locking mode at all
        ATGetShare(&(Root->ALock));                                             // Start with a share lock on the root

    while ( SearchFoundPage->PageType != AT_BTREE_LEAF ) {                      // Until we get to the leaf page...
        Found = FindInPage(SearchFoundPage,inKey, Block, Tuple);                // Try to find it in this page
        if  ( (LockAlert != AT_BTREE_SHIFTLEFT) && (LockAlert != AT_BTREE_SHIFTFAILED)) {// A special and very annoying case due to FINDFIRST
            WhichPage = SearchContainedIn;                                      // We MUST NOT modify SearchContainedIn directly because sometimes the locking routines need to know WHY we are here
            if (SearchResult < 0) WhichPage -= 1;                               // Make sure I am IN the bucket, not past it
            (WhichPage > -1) ?                                                  // Get a ptr to the closest tuple found
                CB = ATBTreeKeyDirect(SearchFoundPage, WhichPage, T1, T2):CB = (volatile ATBTCB*)&(SearchFoundPage->Low);
            NextPage = (volatile ATBTPH*)PageMan.LocateTuple(CB->BBlock, CB->BTuple); // Get the next page for the search
        }
        else {                                                                  // We are in the special FINDFIRST case
            if ( LockAlert == AT_BTREE_SHIFTLEFT ) {                            // If we have shifted to the left
                LockAlert = AT_BTREE_SHIFTLOW;                                  // De-escalate the alert for the lock routines
                NextPage = ShiftLeft;                                           // Pick up our queue as instructed on the next page
            }
            else {                                                              // Crap- shift left must have failed- we need to roll back locks
                LockAlert = 0;                                                  // Clear the flag
                ATFreeShare(&(SearchFoundPage->ALock));                         // Free our lock
                goto start_over;                                                // And restart
            }
        }

        switch ( SearchLockMode ) {                                             // Depending on what the lock mode is
            case    AT_BTREE_READ_CRABLOCK:
                if ( (Result = GetCrabDownReadLock(&(SearchFoundPage->ALock), &(NextPage->ALock))) // Get a crabbed read lock
                        != ATERR_SUCCESS )
                    goto start_over;
                break;
            case    AT_BTREE_WRITE_OPTIMISTIC:
                if ( (Result = GetCrabDownWriteLock(SearchFoundPage, NextPage)) != ATERR_SUCCESS) {// Get a crabbed write lock
                    SearchLockMode = AT_BTREE_WRITE_HOLDLOCK;                   // If this failed, we have a split, so let's escalate and restart
                    goto start_over;
                }
                break;
            case    AT_BTREE_WRITE_HOLDLOCK:
            case    AT_BTREE_DELETE:
                if ( (Result = GetCrabDownWriteLock(SearchFoundPage, NextPage)) != ATERR_SUCCESS)// Get a crabbed write lock
                    goto start_over;                                            // If this failed, we could not escalate a lock, so we have to just start over
                break;
        }
        SearchFoundPage = NextPage;                                             // Set everything to look at this new page
    }
    if ( SearchFoundPage != Root )                                              // As long as it isn't the root page
            return Found;                                                       // Then just return
                                                                                // It IS the root page!
    switch ( SearchLockMode ) {                                                 // Depending on our lock mode
        case    AT_BTREE_WRITE_OPTIMISTIC:
        case    AT_BTREE_WRITE_HOLDLOCK:
        case    AT_BTREE_DELETE:
            if ( (Result = UpgradeShareToExclusiveQueuedRB(Root)) != ATERR_SUCCESS ) {// Try to upgrade my share lock
                goto start_over;                                                // Have to restart if I failed
                }
    }
    return Found;
}
// **************************************************************************** GetCrabDownExcLock
/*  The basic idea: If the node below me is full, I need to maintain an exc lock on the current node because
I am going to be doing an insert, which will split it.  I also need to get one regardless in the
case that the node below is really a leaf, since I will be inserting into it.  If neither is true,
then a share lock on the next node is just fine. Yeah, sometimes it is wrong about the split, but rarely.
*/
int ATBTree::GetCrabDownWriteLock(                                              // Internal routine for crabbing down for an insert op
                                volatile ATBTPH          *CPage,                // Ptr to the page we are on now
                                volatile ATBTPH          *DPage                 // Ptr to the page we want to crab to
                            ) {
    long    Result;
                                                                                // The first question is, which locking mode am I in?
    // ************************************************** AT_BTREE_WRITE_OPTIMISTIC
    if ( SearchLockMode == AT_BTREE_WRITE_OPTIMISTIC ) {                        // Am I in optimistic mode (most likely)
        if ( DPage->PageType != AT_BTREE_LEAF )                                 // If it is not a leaf page
            return GetCrabDownReadLock(&(CPage->ALock), &(DPage->ALock));       // We just a crab down lock

        if ( DPage->AvailableChain != AT_BTREE_END_CHAIN) {                     // Since this is a leaf, first make sure it isn't full
            if ( (Result = CrabShareToExclusive(CPage, DPage)) != ATERR_SUCCESS )// Get an exclusive queued up on it
                return Result;
            if ( DPage->AvailableChain != AT_BTREE_END_CHAIN) {                 // Now that we actually have it locked, we need to make SURE it has free space
                ATFreeShare(&(CPage->ALock));
                return ATERR_SUCCESS;                                           // And return
            }
            // Rats!  Someone inserted a record before I could get it locked! Before doing anything drastic, let's look around...
            if ( CPage->AvailableChain != AT_BTREE_END_CHAIN ) {                // If there is room in this page to insert a split key
                if ( (Result = UpgradeShareToExclusiveQueuedRB(CPage)) != ATERR_SUCCESS){// Then let's try to upgrade my lock
                    ATRemoveQueueShareExclusive(&(DPage->ALock));               // Didn't work- I have to start over anyway
                    return Result;
                }
                return ATERR_SUCCESS;                                           // Great!  Got them both!
            }
            ATFreeShare(&(CPage->ALock));
            ATRemoveQueueShareExclusive(&(DPage->ALock));                       // No use- the page above the leaf is full too- just need to start over
            return ATERR_UNSAFE_OPERATION;
        }
        // Before doing anything drastic, let's look around
        if ( CPage->AvailableChain != AT_BTREE_END_CHAIN ) {                    // If there is room in this page to insert a split key
            if ( (Result = UpgradeShareToExclusiveQueuedRB(CPage)) != ATERR_SUCCESS){// Then let's try to upgrade my lock
                return Result;                                                  // Didn't work- I have to start over anyway
            }
            GetQueuedExclusive(DPage);                                          // I managed to get an upgrade, so now let's just get the next page locked too...
            return ATERR_SUCCESS;
        }
        ATFreeShare(&(CPage->ALock));                                           // The page is already full- we are going to have to split it AND the page beneath it.
        return ATERR_UNSAFE_OPERATION;                                          // So we will have to escalate and start all over
    }

    // ************************************************** AT_BTREE_WRITE_HOLDLOCK
    else if ( SearchLockMode == AT_BTREE_WRITE_HOLDLOCK ) {                     // Am I in WRITE_HOLDLOCK mode?
        if ( !PathDepth ) {                                                     // If I haven't yet started into lock accrual mode (when that starts I just lock everything exc on the way down)
            if ( (Result = TestCrabLock(&(CPage->ALock), &DPage->ALock)) != ATERR_SUCCESS) {
                ATFreeShare(&(CPage->ALock));
                return Result;
            }
            if ( DPage->AvailableChain != AT_BTREE_END_CHAIN ) {                // If I can enter here, I DO NOT need an exclusive on the current page.
                                                                                // So now the question is, what about the next page?
                if ( DPage->PageType != AT_BTREE_LEAF ) {                       // If it ISN'T the leaf, we are free to go
                    ATFreeShare(&(CPage->ALock));
                    return ATERR_SUCCESS;                                       // And return
                }
                Result = UpgradeShareToExclusiveQueuedRB(DPage);                // Upgrade my lock
                ATFreeShare(&(CPage->ALock));                                   // Free prior lock regardless
                return Result;
            }
            else {// Since I got here, I know that I have to lock the CURRENT page, because the next page is full, and this kicks off lock accrual mode
                if ( (Result = UpgradeShareToExclusiveQueuedRB(CPage)) != ATERR_SUCCESS) {// If I can't upgrade, I have to start over
                    ATFreeShare(&(DPage->ALock));                               // Have to free this too
                    return Result;
                }
                // Now that I have checked the exc lock on the current page, I need to exc lock the next page as well, but it is much easier!
                UpgradeShareToExclusiveQueuedSP(DPage);                         // Upgrade my lock
            }
        }
        else                                                                    // I'm in lock accrual mode.  Lock it all, baby!
            GetQueuedExclusive(DPage);                                          // Get an exclusive queued up on it
        return ATERR_SUCCESS;
    }

    // ************************************************** AT_BTREE_DELETE
    else {                                                                      // Hmmm... must be in AT_BTREE_DELETE mode
        if ( DPage->PageType != AT_BTREE_LEAF )                                 // If the next page is not the leaf
            return GetCrabDownReadLock(&(CPage->ALock), &(DPage->ALock));       // Then I just want to crab down with share locks
        else {                                                                  // I have arrived at the leaf page
            CrabShareToExclusive(CPage, DPage);                                 // Crab from a share to an exclusive
            ATFreeShare(&(CPage->ALock));
        }
        return ATERR_SUCCESS;                                                   // And return
    }
}
// **************************************************************************** CrabShareToExclusive
// THIS ROUTINE DOES NOT FREE THE SHARE LOCK UNLESS ERROR OCCURS
int ATBTree::CrabShareToExclusive(                                              // Crab from a shared to an exclusive lock
                                volatile ATBTPH *HeldPage,                      // Page you hold a share lock on
                                volatile ATBTPH *DesiredPage                    // Page you want an exclusive lock on
                                ) {
    long    Attempts = 0, Result;
    while ( (Result = ATQueueShareExclusive(&(DesiredPage->ALock))) != ATERR_SUCCESS ){// Try to queue up an exclusive lock
        if ( !( (HeldPage->ALock) & AT_SHARE_EXC) ) {                           // AS long a nobody has gotten our lock
            ATSpinLockArbitrate(Attempts);                                      // We just retry
            Attempts++;
        }
        else {
            ATFreeShare(&(HeldPage->ALock));                                     // Someone else got an exc on our page- we have to restart
            return ATERR_OBJECT_IN_USE;
        }
    }
    if ( DesiredPage->PageType != AT_BTREE_LEAF ) {                             // As long as it isn't a leaf page
        if ( PathDepth < AT_BTREE_MAXDEPTH ) {
            LockedPath[PathDepth].ALock = &(DesiredPage->ALock);                // Save tracking info for the lock
            LockedPath[PathDepth].Block = DesiredPage->Block;
            LockedPath[PathDepth].Tuple = DesiredPage->Tuple;
            PathDepth++;
        }
        else {
            printf("BTree exceeded AT_BTREE_MAXDEPTH!  Either change it or for heaven's\r\nsake go to bigger pages!\r\n");
            fflush(stdout); char *T = NULL;*T = 0;
        }
    }
    return ATERR_SUCCESS;
}
// **************************************************************************** GetQueueExclusive
// ONLY USE THIS IF YOU HAVE AN EXLUSIVE ABOVE IT ALREADY
int ATBTree::GetQueuedExclusive(                                                // Routine to just spin until I get an exclusive lock, then track it if needed
                                volatile ATBTPH *Page                           // Page to lock
                                ) {
    long    Attempts = 0, Result;
    while ( (Result = ATQueueShareExclusive(&(Page->ALock))) ) {                // Try to queue up an exclusive lock
        ATSpinLockArbitrate(Attempts);
        Attempts++;
    }
    if ( Page->PageType != AT_BTREE_LEAF ) {                                    // As long as it isn't a leaf page
        if ( PathDepth < AT_BTREE_MAXDEPTH ) {
            LockedPath[PathDepth].ALock = &(Page->ALock);                       // Save tracking info for the lock
            LockedPath[PathDepth].Block = Page->Block;
            LockedPath[PathDepth].Tuple = Page->Tuple;
            PathDepth++;
        }
        else {
            printf("BTree exceeded AT_BTREE_MAXDEPTH!  Either change it or for heaven's\r\nsake go to bigger pages!\r\n");
            fflush(stdout); char *T = NULL;*T = 0;
        }
    }
    return ATERR_SUCCESS;
}
// **************************************************************************** UpgradeShareToExclusiveQueuedRB
int ATBTree::UpgradeShareToExclusiveQueuedRB(                                   // Routine to upgrade a share lock to a queued exclusive lock, and track it if not the leaf page- MUST HAVE A SHARELOCK ON THE LOCK!
                                                                                // Failure to upgrade releases all locks returns an error
                                volatile ATBTPH *Page                           // Page to lock
                                ) {
    long    Result;
    Result = ATQueueShareExclusive(&(Page->ALock));                             // Try to queue up an exclusive lock
    ATFreeShare(&(Page->ALock));                                                // Whether I got it or not, I can't hold the share anymore
    if ( Result != ATERR_SUCCESS ) {                                            // Unfortunately, if I did not get the exc, I MUST free my share, which means the world could have changed, so I have to start over
        if ( PathDepth ) {                                                      // Did I accrue any locks?  Unusual, if not rare.
            for ( int i = 0; i < PathDepth; ++i)                                // Loop thru any locks we may need to free up
                ATRemoveQueueShareExclusive((LockedPath[i].ALock));             // Remove the queue on our lock
            PathDepth = 0;                                                      // Reset the PathDepth counter
        }
        ATSpinLockArbitrate(EscalationFailures);                                // This is, after all, a lock we are fighting for, so let's not behave badly
        EscalationFailures++;                                                   // This could happen over & over, so let's respond appropriately
        return ATERR_OBJECT_IN_USE;
    }

    // Phew!! I successfully upgraded!
    if ( Page->PageType != AT_BTREE_LEAF ) {                                    // As long as it isn't a leaf page
        if ( PathDepth < AT_BTREE_MAXDEPTH ) {
            LockedPath[PathDepth].ALock = &(Page->ALock);                       // Save tracking info for the lock
            LockedPath[PathDepth].Block = Page->Block;
            LockedPath[PathDepth].Tuple = Page->Tuple;
            PathDepth++;
        }
        else {
            printf("BTree exceeded AT_BTREE_MAXDEPTH!  Either change it or for heaven's\r\n");
            printf("sake go to bigger pages!\r\n");
            fflush(stdout); char *T = NULL;*T = 0;
        }
    }
    return ATERR_SUCCESS;
}
// **************************************************************************** UpgradeShareToExclusiveQueuedSP
// ONLY USE THIS IF YOU HAVE AN EXLUSIVE ABOVE IT ALREADY
int ATBTree::UpgradeShareToExclusiveQueuedSP(                                   // Routine to upgrade a share lock to a queued exclusive lock, and track it if not the leaf page- MUST HAVE A SHARELOCK ON THE LOCK!
                                                                                // On failure we just spin until we get it
                                volatile ATBTPH *Page                           // Page to lock
                                ) {
    long    Result, Attempts = 0;
    Result = ATQueueShareExclusive(&(Page->ALock));                             // Try to queue up an exclusive lock
    ATFreeShare(&(Page->ALock));                                                // Whether I got it or not, I can't hold the share anymore, since a fail would mean someone else is trying to write
    if ( Result != ATERR_SUCCESS ) {
        while ( (Result = ATQueueShareExclusive(&(Page->ALock))) ) {            // Try to queue up an exclusive lock
            ATSpinLockArbitrate(Attempts);
            Attempts++;
        }
    }
    if ( Page->PageType != AT_BTREE_LEAF ) {                                    // As long as it isn't a leaf page
        if ( PathDepth < AT_BTREE_MAXDEPTH ) {
            LockedPath[PathDepth].ALock = &(Page->ALock);                       // Save tracking info for the lock
            LockedPath[PathDepth].Block = Page->Block;
            LockedPath[PathDepth].Tuple = Page->Tuple;
            PathDepth++;
        }
        else {
            printf("BTree exceeded AT_BTREE_MAXDEPTH!  Either change it or for heaven's\r\n");
            printf("sake go to bigger pages!\r\n");
            fflush(stdout); char *T = NULL;*T = 0;
        }
    }
    return ATERR_SUCCESS;
}
// **************************************************************************** GetCrabDownShareLock
int ATBTree::GetCrabDownReadLock(                                               // Internal routine for getting a BTree crab down share lock
                                ATLOCK          *HeldLock,                      // Ptr to the lock you currently hold
                                ATLOCK          *DesiredLock                    // Ptr to lock caller's wants to crab to
                            ) {
    long    Result, Attempts = 0;

    if ( SearchMode != AT_BTREE_FINDFIRST ) {                                   // As long as we are in any other find mode...
retry:
        if ( (Result = ATBounceShare(DesiredLock)) == ATERR_SUCCESS) {          // Try to get the lock below
            ATFreeShare(HeldLock);                                              // Just free our old lock & head out
            return ATERR_SUCCESS;
        }
        else {
            if ( !(*HeldLock & AT_SHARE_EXC) )  {                               // As long as nobody has gotten an exclusive on our page
                ATSpinLockArbitrate(Attempts);                                  // We'll just keep trying
                Attempts++;
                goto retry;
            }
            else {                                                              // Crap- someone has an exclusive on our page
                ATFreeShare(HeldLock);                                          // Free our old lock & return an error
                return ATERR_OBJECT_IN_USE;
            }
        }
    }
    if ( LockAlert ) {                                                          // We must be in a FINDFIRST mode
                                                                                // Further, we are going down the low page, which means we already have a read lock acquired on the new page
        ATFreeShare(HeldLock);                                                  // So just release the old lock
        LockAlert = 0;
        return ATERR_SUCCESS;
    }
    goto retry;                                                                 // Just a false alarm
}
// **************************************************************************** SplitPage
int ATBTree::SplitPage(                                                         // Possibly recursive method to split a page- returns a ptr to
                                volatile ATBTPH *Page,                          // Page to split
                                long            Split,                          // Where to split the page (passing it this way just makes SURE we picked the same spot)
                                volatile ATBTPH **RightPage,                    // Ptr set to to first new page (new right page ALWAYS)
                                volatile ATBTPH **LeftPage                      // Ptr to set to second new page (will be null unless the root splits, in which case it will be the new left page)
                                ) {
    volatile ATBTPH  *NewRight, *NewLeft, *IPage, *NextPage;
    volatile ATBTCB  *CB;
    long    Which, *T1;
    char    *T2;

    // Note that in MakePageCopy is where we wait for our queued exlcusive to free up....
    if ( !(NewRight = MakePageCopy(Page, Split, Page->NumberKeys)) )            // Split off the right side into a new page
        return ATERR_OUT_OF_MEMORY;
    CB = ATBTreeKeyDirect(NewRight, 0, T1, T2);                                 // This is the dividing key to be propagated upwards
    *RightPage = NewRight;                                                      // Pass back what the new page is

    if ( Page != Root ) {                                                       // As long as it isn't the root we are splitting
        *LeftPage = NULL;
        Which = ((PathDepth - 1) - PagesSplit);                                 // Figure out where I am in the stack of pages being held for splitting
        PagesSplit++;                                                           // MUST DO THIS BEFORE YOU RECURSE, but AFTER you use it! Inc the number of pages split so far in this operation
        IPage = (volatile ATBTPH*)PageMan.LocateTuple(LockedPath[Which].Block, LockedPath[Which].Tuple);// Figure out which page I am supposed to insert the split key into

        if ( Page->NextPage.BBlock != AT_BTREE_END_CHAIN ) {                    // If the page we are splitting has a next page, we need to fix up the page chain
            NextPage = (volatile ATBTPH*)PageMan.LocateTuple(Page->NextPage.BBlock, Page->NextPage.BTuple);// Get ptr to the next page
            NextPage->PrevPage.BBlock = NewRight->Block;                        // Let him know it is a new page behind him now
            NextPage->PrevPage.BTuple = NewRight->Tuple;
            NewRight->NextPage.BBlock = Page->NextPage.BBlock;                  // Point the new page to the next guy
            NewRight->NextPage.BTuple = Page->NextPage.BTuple;
        }
        else {
            NewRight->NextPage.BBlock = AT_BTREE_END_CHAIN;
            NewRight->NextPage.BTuple = AT_BTREE_END_CHAIN;
        }
        Page->NextPage.BBlock = NewRight->Block;                                // Point the original page to the new page
        Page->NextPage.BTuple = NewRight->Tuple;
        NewRight->PrevPage.BBlock = Page->Block;                                // Point the new page back to the original page
        NewRight->PrevPage.BTuple = Page->Tuple;

        Page->NumberKeys -= Page->NumberKeys - Split;                           // Set the old page to show we have cleared out the tuples
        InsertKeyIntoPage(IPage, (void*)(CB + 1), NewRight->Block, NewRight->Tuple,// Insert the dividing key into the page above us
                        CB->TBlock, CB->TTuple, 1);
    }
    else {                                                                      // We are splitting the root, so we need another page as well
        if ( !(NewLeft = MakePageCopy(Page, 0, Split)) )                        // Split off the left side into a new page
            return ATERR_OUT_OF_MEMORY;
        *LeftPage = NewLeft;                                                    // Pass back what the new page is

        // Let's put in place the start of the prev & next page chains
        NewLeft->PrevPage.BBlock = AT_BTREE_END_CHAIN;                          // Terminate the start of the chain
        NewLeft->PrevPage.BTuple = AT_BTREE_END_CHAIN;
        NewLeft->NextPage.BBlock = NewRight->Block;                             // Point left to right
        NewLeft->NextPage.BTuple = NewRight->Tuple;
        NewRight->PrevPage.BBlock = NewLeft->Block;                             // Point right to left
        NewRight->PrevPage.BTuple = NewLeft->Tuple;
        NewRight->NextPage.BBlock = AT_BTREE_END_CHAIN;                         // Terminate the end of the chain
        NewRight->NextPage.BTuple = AT_BTREE_END_CHAIN;

        Page->NumberKeys = 0;                                                   // Set the root page to show we have cleared out the tuples
        NewLeft->Low.BBlock = Page->Low.BBlock;                                 // Pass my low page to guy below me on left
        NewLeft->Low.BTuple = Page->Low.BTuple;
        Page->Low.BBlock = NewLeft->Block;                                      // Then make him my new low page
        Page->Low.BTuple = NewLeft->Tuple;
        PagesSplit++;                                                           // Inc the number of pages split so far in this operation
        InsertKeyIntoPage(Root, (void*)(CB + 1), NewRight->Block, NewRight->Tuple,// Insert the dividing key into the root- looks like a leaf split to the insert routine
                        CB->TBlock, CB->TTuple, 0);
        Root->PageType = AT_BTREE_NODE;                                         // Once it has split, it will from then on always be a node page
    }
    return ATERR_SUCCESS;                                                       // We're done!
}
// **************************************************************************** MakePageCopy
volatile ATBTPH  *ATBTree::MakePageCopy(                                        // Routine to copy part of an existing page into a newly created page- returns a ptr to the new page
                                                                                // Does NOT change key ptrs or NumberKeys in old page
                                volatile ATBTPH *Page,                          // The original page
                                long            Low,                            // The low tuple to move over
                                long            High                            // The high tuple to move over
                                ) {
    volatile ATBTPH  *NewPage;
    volatile ATBTCB  *NewCB, *OldCB;
    long    i, *NewKeyPtrs, *NewKeysBase, FreeSpot, *OldKeyPtrs, *OldKeysBase;
    long    ToMove = High - Low, OldTuple;                                      // How many records am I moving?

    if ( !(NewPage = (volatile ATBTPH*)PageMan.AllocateTuple()) )               // Allocate ourselves a nice new page
        return NULL;
    PageMan.UnlockTuple();                                                      // Unlock it before we forget about it
    PageMan.GetTupleLong((long int*)&(NewPage->Block), (long int*)&(NewPage->Tuple));// Ask the table where this tuple is
    NewPage->ALock =        0;                                                  // Init the lock (to exclusive)
    NewPage->PageType =     Page->PageType;                                     // Our page type stays the same
    NewPage->Low.BBlock =   AT_BTREE_END_CHAIN;
    NewPage->Low.BTuple =   AT_BTREE_END_CHAIN;
    NewPage->PageKey =      AT_BTREE_NODE;
    InitNewPage(NewPage);                                                       // Set up the tuple chains, etc.

    OldKeyPtrs = ATBTreeKeyPtrBase(Page);                                       // Figure out where the key ptrs are in the old page
    OldKeysBase = ATBTreeKeyBase(OldKeyPtrs);                                   // and where the keys themselves are
    NewKeyPtrs = ATBTreeKeyPtrBase(NewPage);                                    // Figure out where the key ptrs are in the new page
    NewKeysBase = ATBTreeKeyBase(NewKeyPtrs);                                   // and where the keys themselves are

    ATWaitQueueShareExclusive(&(Page->ALock));                                  // We now need to change something other readers might see, so make sure everyone is now out
                                                                                // The TTuple field can be read by any readers...
    for ( i = 0; i < ToMove; ++i) {                                             // Loop through all the keys we have to move
        FreeSpot = NewPage->AvailableChain;                                     // Get a free spot in the new page
        NewCB = ATBTreeKeyRaw(NewKeysBase, FreeSpot);                           // Get a ptr to the free key spot in the new page
        NewPage->AvailableChain = NewCB->TTuple;                                // Pass on the next in the chain (if any) to the header of the new page
        OldTuple = OldKeyPtrs[(Low + i)];                                       // Save a copy of the old tuple's physical location
        OldCB = ATBTreeKey(OldKeyPtrs, OldKeysBase, (Low + i));                 // Figure out which record we are copying over
        memcpy((void*)NewCB, (void*)OldCB, KeyLength + sizeof(ATBTCB));         // Copy the key and CB into their new home
        OldCB->TTuple = Page->AvailableChain;                                   // Make the old tuple part of the available list
        Page->AvailableChain = OldTuple;                                        // And have the list now point to it
        NewKeyPtrs[i] = FreeSpot;                                               // Keep track of what we copy over
    }
    NewPage->NumberKeys = ToMove;                                               // Set the number of keys now present in this page

    return NewPage;
}
// **************************************************************************** InsertKeyIntoPage
int ATBTree::InsertKeyIntoPage(                                                 // By the time we get here, we have our insert page ID'd and we have an exlclusive lock at least queued on it... possibly recursive call
                                volatile ATBTPH *Page,                          // Page to insert into
                                void            *inKey,                         // Ptr to key to insert
                                long            inBBlock,                       // BBlock value to use for compare
                                long            inBTuple,                       // BTuple value to use for compare
                                long            inTBlock,                       // BBlock value to use for compare
                                long            inTTuple,                       // BTuple value to use for compare
                                long            Mode                            // Set to 0 for leaf inserts, 1 for node split
                                ) {

    long        Result, *KeyPtrs, *KeysBase, FreeSpot, Split, InsertPoint, i, IsSplitPage = 0;
    volatile ATBTPH  *RightPage, *LeftPage;
    volatile ATBTCB  *CB;

    Result = FindInPage(Page, inKey, inTBlock, inTTuple);                       // For leaf inserts, just use parms as passed
    // Now, a search always wants to find the right bucket that the key fits in, but we are inserting a new bucket, so we want just past the smaller bucket, not in it...
    ( SearchResult <= 0 ) ? InsertPoint = SearchContainedIn:InsertPoint = SearchContainedIn + 1;// MUST save the value locally, since we are recursive
    if ( Result ) {                                                             // If we actually found this key
        if ( IndexType == AT_BTREE_PRIMARY && Page->PageType == AT_BTREE_LEAF ) // AND If this is a primary key index AND a leaf page (DON'T check nodes, because we don't delete entries in the nodes when we delete records)
            return ATERR_OBJECT_IN_USE;                                         // Don't allow a non-unique insert
    }

retry_insert:
    FreeSpot = Page->AvailableChain;                                            // Get a free spot in the page
    if ( FreeSpot != AT_BTREE_END_CHAIN ) {                                     // As long as we have room in this page to insert the new guy...
        // Note that since no reader can be accessing a free key spot or the free chain, I will go ahead and start
        // work here while I give the queued up lock(s) time to clear out...
        KeyPtrs = ATBTreeKeyPtrBase(Page);                                      // Figure out where the key ptrs are
        KeysBase = ATBTreeKeyBase(KeyPtrs);                                     // and where the keys themselves are
        CB = ATBTreeKeyRaw(KeysBase, FreeSpot);                                 // Get a ptr to the free key spot
        Page->AvailableChain = CB->TTuple;                                      // Pass on the next in the chain (if any) to the header
        memcpy((void*)(CB + 1), inKey, KeyLength);                              // Copy the key into its new home
        CB->TBlock = inTBlock;                                                   // Set the block & tuple references from the tuple's home table
        CB->TTuple = inTTuple;
        CB->BBlock = inBBlock;                                                   // Set the block & tuple references from the tuple's home table
        CB->BTuple = inBTuple;

        if ( !IsSplitPage ) ATWaitQueueShareExclusive(&(Page->ALock));          // Unless this is a newly created split, we now need to wait to make sure the share locks are all cleared before we modify the metadata
        for ( i = Page->NumberKeys; i > InsertPoint; i--)                       // Loop thru all ptrs above our insert point (backwards)
            KeyPtrs[i] = KeyPtrs[i - 1];                                        // Move them up to make a spot available
        KeyPtrs[InsertPoint] = FreeSpot;                                        // Stick our value into the ptrs list
        Page->NumberKeys++;                                                     // Inc the # of keys stored in the page
        return ATERR_SUCCESS;
    }
                                                                                // Huh oh.  Have to split a page.
    Split = Page->NumberKeys >> 1;                                              // Figure out where the split will be
    if ( (Result = SplitPage(Page, Split, &RightPage, &LeftPage)) != ATERR_SUCCESS)// Split this page in half
        return Result;
    if ( !LeftPage ) {                                                          // As long as the root wasn't split
        if ( InsertPoint > Split ) {                                            // If our record was above the split, we need to switch pages
            IsSplitPage++;                                                      // Make note that we are going into a split created page
            InsertPoint -= Split;                                               // Adjust our insert point
            Page = RightPage;                                                   // Set ourselves to the new page on the right
        }
    }
    else {                                                                      // The root must have been split, so that moves things around a little
        IsSplitPage++;                                                          // Make note that we are going into a split created page
        if ( InsertPoint <= Split ) {                                           // If this record belongs on the left...
            Page = LeftPage;                                                    // Set ourselves to the new page on the left
        }
        else {                                                                  // Must belong on the right...
            InsertPoint -= Split;                                               // Adjust our insert point
            Page = RightPage;                                                   // Set ourselves to the new page on the right
        }
    }
    goto retry_insert;                                                          // Okay, now that we have room, let's try again
}
// **************************************************************************** InsertKey
int ATBTree::InsertKey(                                                         // Call to insert a tuple
                                                                                // NOTE: GENERALLY NOT USER CALLED!  CALL THE TABLE's INSERT FUNCTION
                                void            *inKey,                         // Key of tuple to insert
                                long            inBlock,                        // The unique block & tuple ID
                                long            inTuple
                                ) {
    long            Result, i;
    // NEVER EVER EVER DO AN INSERT WITH ANYTHING OTHER THAN FINDDIRECT- for example, FINDFIRST could really hose the lock tracking.
    SearchCompareLength =   KeyLength;      SearchMode =    AT_BTREE_FINDDIRECT;// SEE NOTE ABOVE
    SearchLockMode =        AT_BTREE_WRITE_OPTIMISTIC;
    PagedFindKey((void*)inKey, inBlock, inTuple);                               // Find the location for insertion

    PagesSplit = 0;                                                             // Make sure our page split ctr is starting from zero
    Result = InsertKeyIntoPage(SearchFoundPage, (void*)inKey, SearchFoundPage->Block,// Insert the key into the page we located
                SearchFoundPage->Tuple, inBlock, inTuple, 0);

    if ( PathDepth ) {                                                          // Did I accrue any locks?  Unusual, if not rare.
        for ( i = 0; i < PathDepth; ++i)                                        // Loop thru any locks we may need to free up
            ATRemoveQueueShareExclusive((LockedPath[i].ALock));                 // Note remove and not free in case of an error condition where I did not wait
        PathDepth = 0;                                                          // Reset the PathDepth counter
    }
    ATRemoveQueueShareExclusive(&(SearchFoundPage->ALock));                     // Release the lock on the leaf page- cautiously in case of an error where I did not wait
    return Result;
}
// **************************************************************************** InsertTuple
int ATBTree::InsertTuple(                                                       // Called when a tuple has been inserted into the associated table
                                                                                // NOTE: GENERALLY NOT USER CALLED!  CALL THE TABLE's INSERT FUNCTION
                                ATTuple         *Tuple,                         // Key of tuple to insert
                                long            inBlock,                        // The unique block & tuple ID
                                long            inTuple
                                ) {
    void *Key = ((MakeKey)((void*)Tuple));                                      // Get a key made from the tuple
    return InsertKey(Key, inBlock, inTuple);                                    // And insert it
}
// **************************************************************************** Constructor
ATBTree::ATBTree() {
    Reset();
}
// **************************************************************************** Destructor
ATBTree::~ATBTree() {
    if ( Table ) Close();
}
// **************************************************************************** Reset
void    ATBTree::Reset() {                                                      // Reset the class members
    PageMan.CloseTable();
    Table = NULL;
    Compare = NULL;
    MakeKey = NULL;
    Info = NULL;
    Root = NULL;
    KeyLength = 0;
    TrueKeyLength = 0;
    KeysPerPage = 0;
    AllocSize = 0;
    Kilroy = 0;
    PathDepth = 0;
    LockAlert = 0;
    PagesSplit = 0;
    EscalationFailures = 0;
    CursorPage = NULL;
    CursorTuple = 0;
    CursorStatus = AT_BTREE_CSTATUS_NORMAL;
    CursorOp = 0;
    SearchFoundPage = NULL;
    SearchContainedIn = -5;
    SearchResult = 0;
    SearchVal = NULL;
    SearchCompareLength = -1;
    SearchMode = -5;
    SearchLockMode = -1;
    IndexType = -1;
    CheckLevel = CheckErrors = 0;

    memset(LockedPath, 0, AT_BTREE_MAXDEPTH * sizeof(ATBTTKCB));

}
// **************************************************************************** InitNewPage
void ATBTree::InitNewPage(                                                      // Call to initialize a newly allocated BTree page data members (but not header)
                                volatile ATBTPH *Page                           // Page to init
                                ) {
    long    *KeyPtrs = ATBTreeKeyPtrBase(Page);
    long    *KeysBase = ATBTreeKeyBase(KeyPtrs);
    volatile ATBTCB  *CB = (volatile ATBTCB*)KeysBase;
    long    i;

    Page->AvailableChain = 0;                                                   // Initialize the available chain (at the start, ALL keys are actually free, of course)
    for ( i = 0; i < KeysPerPage; ++i) {                                        // Loop thru all the keys
        CB->TTuple = i + 1;                                                     // Use the CB's tuple to store a ptr to the next guy in the chain
        CB = (volatile ATBTCB*)(((char *)CB) + TrueKeyLength);                  // Move ahead one tuple
    }
    CB = (volatile ATBTCB*)(((char *)CB) - TrueKeyLength);                      // Go back one
    CB->TTuple = AT_BTREE_END_CHAIN;                                            // Mark the last guy as the end of the chain
}

// **************************************************************************** Create
int ATBTree::Create(                                                            // Create a BTree
                                int         inKey,                              // Systemwide unique IPC ID for this BTree- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                                                                                // !!IMPORTANT!! The key that you specify here will be incremented by 1 each time a new section needs to be allocated,
                                                                                // So be sure to leave adequate room to grow between your IPC ID's!!
                                ATSharedTable   *inTable,                       // Which table to create it for (pointer to a valid created ATSharedTable)
                                                                                // MUST BE AN OBJECT INSTANCE THAT BELONGS TO THE SAME THREAD THAT IS CALLING
                                ATBTreeComp     *inComp,                        // Pointer to the function that will be called to compare the key values- should behave just like memcmp(), returning <0,0, or >0 integer.
                                ATBTreeMakeKey  *inMakeKey,                     // Pointer to a function to make a key from a tuple- i.e., when given the pointer to a tuple, it returns a ptr to a valid key for the tuple
                                long            inKeyLength,                    // Length of the keys
                                long            inKeysPerPage,                  // Number of keys to store in BTree page.  Very important factor in balancing parallel performance.  Is tree read or write heavy?  Etc.
                                long            inBlockAllocSize,               // Number of BTree pages to allocate in a block each time growth is needed
                                long            inIndexType,                    // Either AT_BTREE_PRIMARY (unique entries only) or AT_BTREE_SECONDARY (non-unique allowed)
                                ULONG           inKilroy                        // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                                ) {
    long     Result, StartAlloc;

    if ( !inKey || !inTable || !inComp || inKeyLength < 1  || inKeysPerPage < 2 ||
        !inBlockAllocSize || !inKilroy ||
        ((inIndexType != AT_BTREE_PRIMARY) && (inIndexType != AT_BTREE_SECONDARY) ) )
        return ATERR_BAD_PARAMETERS;

    TrueKeyLength = inKeyLength + sizeof(ATBTCB);
    TrueKeyLength = ((TrueKeyLength + (AT_MEM_ALIGN - 1)) & ~(AT_MEM_ALIGN - 1));

    StartAlloc = (TrueKeyLength * inKeysPerPage) + sizeof(ATBTPH) +             // How big should the pages be?
                    (inKeysPerPage * sizeof(long)) + (AT_MEM_ALIGN * 3);        // The 3 is a struct + list + data align
    if ( StartAlloc < sizeof(ATBTInfo) )                                        // Make sure enough room to fit our info block in a page
        StartAlloc = sizeof(ATBTInfo);

    if ( (Result = PageMan.CreateTable(                                         // Create the table to use as out page manager
                                        inKey,                                  // Unique key
                                        StartAlloc,                             // Size of our tuples ( one page )
                                        inBlockAllocSize + 1,                   // Number of pages per block, +1 in our first group for the info page
                                        inBlockAllocSize,                       // Subsequent growth alloc
                                        1,
                                        1,                                      // At this time, I have no near plans to merge pages, which is all this would be used for
                                        12,                                     // Should be a fine number typically
                                        inKilroy                                // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                            )) != ATERR_SUCCESS )
        return Result;

    Table =         inTable;                                                    // Init class members
    Compare =       inComp;
    MakeKey =       inMakeKey;
    KeyLength =     inKeyLength;
    KeysPerPage =   inKeysPerPage;
    AllocSize =     inBlockAllocSize;
    IndexType =     inIndexType;
    Kilroy =        inKilroy;

    if ( !(Info = (volatile ATBTInfo*)PageMan.AllocateTuple()) )                // Set up our info block in the first page
        return ATERR_OUT_OF_MEMORY;
    PageMan.UnlockTuple();
    Info->PageKey =         AT_BTREE_INFO;                                      // Init our info struct
    Info->PageType =        AT_BTREE_INFO;
    Info->KeysPerPage =     KeysPerPage;
    Info->KeyLength =       inKeyLength;
    Info->AllocSize =       inBlockAllocSize;
    Info->TrueKeyLength =   TrueKeyLength;
    Info->IndexType =       IndexType;
    Info->SystemKey =       inKey;
    Info->TotalPageSize =   StartAlloc;

    if ( !(Root = (volatile ATBTPH*)PageMan.AllocateTuple()) )                  // Set up our root page
        return ATERR_OUT_OF_MEMORY;
    PageMan.UnlockTuple();
    PageMan.GetTupleLong((long int*)&(Root->Block), (long int*)&(Root->Tuple));// Ask the table where this tuple is
    Root->PageType =        AT_BTREE_LEAF;
    Root->PageKey =         AT_BTREE_ROOT;
    Root->ALock =           0;
    Root->NumberKeys =      0;
    Root->Low.BBlock =      AT_BTREE_END_CHAIN;
    Root->Low.BTuple =      AT_BTREE_END_CHAIN;
    Root->PrevPage.BBlock = AT_BTREE_END_CHAIN;
    Root->NextPage.BBlock = AT_BTREE_END_CHAIN;

    InitNewPage(Root);                                                          // Init the page's data structures

    if ( (Result = Table->RegisterBTree(this, IndexType)) != ATERR_SUCCESS)     // Register the BTree with the table
        return Result;

    return ATERR_SUCCESS;
}
// **************************************************************************** Open
int ATBTree::Open(                                                              // Open up an existing BTree
                                int         inKey,                              // Systemwide unique IPC ID for this BTree- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                                                                                // !!IMPORTANT!! The key that you specify here will be incremented by 1 each time a new section needs to be allocated,
                                                                                // So be sure to leave adequate room to grow between your IPC ID's!!
                                ATSharedTable   *inTable,                       // Which table to create it for (pointer to a valid created ATSharedTable
                                                                                // MUST BE AN OBJECT INSTANCE THAT BELONGS TO THE SAME THREAD THAT IS CALLING
                                ATBTreeComp     *inComp,                        // Pointer to the function that will be called to compare the key values- should behave just like memcmp(), returning <0,0, or >0 integer.
                                ATBTreeMakeKey  *inMakeKey,                     // Pointer to a function to make a key from a tuple- i.e., when given the pointer to a tuple, it returns a ptr to a valid key for the tuple
                                ULONG           inKilroy                        // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                                ) {
    long     Result, StartAlloc;

    if ( !inKey || !inTable || !inComp || !inKilroy )
        return ATERR_BAD_PARAMETERS;

    if ( (Result = PageMan.OpenTable(                                           // Create the table to use as out page manager
                                        inKey,                                  // Unique key
                                        inKilroy                                // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                            )) != ATERR_SUCCESS )
        return Result;

    Table =     inTable;                                                        // Init class members
    Compare =   inComp;
    MakeKey =   inMakeKey;

    PageMan.ResetCursor();                                                      // Get access to our info page (will be one of the first few pages- no guarantees on placment)
    do {
        if ( !(Info = (volatile ATBTInfo*)PageMan.NextTuple()) )
            return ATERR_OUT_OF_MEMORY;
    }
    while ( Info->PageKey != AT_BTREE_INFO );

    KeysPerPage =   Info->KeysPerPage;                                          // Fill in our info from the global page
    KeyLength =     Info->KeyLength;
    TrueKeyLength = Info->TrueKeyLength;
    AllocSize =     Info->AllocSize;
    Kilroy =        inKilroy;
    IndexType =     Info->IndexType;

    PageMan.ResetCursor();                                                      // Get access to our root page (will be one of the first few pages- no guarantees on placment)
    do {
        if ( !(Root = (volatile ATBTPH*)PageMan.NextTuple()) )
            return ATERR_OUT_OF_MEMORY;
    }
    while ( Root->PageKey != AT_BTREE_ROOT );

    if ( (Result = Table->RegisterBTree(this, IndexType)) != ATERR_SUCCESS)     // Register the BTree with the table
        return Result;

    return ATERR_SUCCESS;
}
// **************************************************************************** Close
int ATBTree::Close() {                                                          // Close the BTree & free all resources
    if ( CursorPage )                                                           // Clean up any open cursor locks
        ATFreeShare(&(CursorPage->ALock));
    CursorPage = NULL;
    PageMan.CloseTable();                                                       // Close the BTree table
    if ( Table )
        Table->UnRegisterBTree(this, IndexType);                                // Unregister ourselves from the table
    Table = NULL;
    Reset();                                                                    // Init all the vars
    return ATERR_SUCCESS;
}
// **************************************************************************** FindTuple
ATTuple *ATBTree::FindTuple(                                                    // Request to find a tuple based on the specified key- MAY NOT be the first key in a non-unique index- there are other calls for that
                                                                                // Note this also positions the table's cursor
                                void            *inKey,                         // Key to find tuple for
                                long            LockMode,                       // Locking mode- this MUST be set to CRABLOCK for anything other than FINDDIRECT
                                long            FindMode,                       // Set to AT_BTREE_FINDFIRST, AT_BTREE_DIRECT, or AT_BTREE_FINDLAST
                                long            inMatchLength                   // How long to test the match for (this is passed into the user routine, so there might be *some* use for values greater than keylength... I DON'T CHECK IT!)
                                ) {
    if ( IndexType == AT_BTREE_PRIMARY )                                        // As long as this is the primary
        return FindKey(inKey, LockMode, FindMode, inMatchLength, 0, 0);         // This is really find key, except we are a user and we don't know what the block and tuple are
    else {                                                                      // If this is a secondary
        if ( FindMode == AT_BTREE_FINDDIRECT )                                  // We don't atually allow finddirect on secondaries (unless you specify the full ID)... but we'll let them look
            FindMode = AT_BTREE_FINDLAST;                                       // FindLast is faster (sometimes), and this is probably just an "exists" query anyhow
        return FindKey(inKey, LockMode, FindMode, inMatchLength, 0, 0);         // This is really find key, except we are a user and we don't know what the block and tuple are
    }
}
// **************************************************************************** FindKey
ATTuple *ATBTree::FindKey(                                                      // Request to find a tuple based on the specified key
                                                                                // Note this also positions the table's cursor
                                void            *inKey,                         // Key to find tuple for
                                long            LockMode,                       // Locking mode- if this isn't a primary key, this MUST be set to CRABLOCK
                                long            FindMode,                       // Set to AT_BTREE_FINDFIRST, AT_BTREE_DIRECT, or AT_BTREE_FINDLAST
                                long            inMatchLength,                  // How long to test the match for (this is passed into the user routine, so there might be *some* use for values greater than keylength... I DON'T CHECK IT!)
                                long            inBlock,                        // Block & tuple to find
                                long            inTuple
                                ) {
    long    Found;
    volatile ATBTCB  *CB;
    ATTuple  *Tuple;

    if ( !LockMode ) {                                                          // If the no locking mode is set
        if (IndexType != AT_BTREE_PRIMARY)                                      // Only allow this with a primary key...
            return NULL;
        if ( FindMode != AT_BTREE_FINDDIRECT )                                  // If they are not calling a finddirect
            LockMode = AT_BTREE_READ_CRABLOCK;                                  // Then just escalate the lock for them
    }

    SearchCompareLength =   inMatchLength;  SearchMode =    FindMode;           // Set the search parameters
    SearchLockMode =        LockMode;

retry:
    PagedFindKey(inKey, inBlock, inTuple);                                      // Find the right page
    if ( (Found = FindInPage(SearchFoundPage, inKey, inBlock, inTuple)) ) {     // Try to find it in this page
        CB = (((volatile ATBTCB*)SearchVal) - 1);
        Tuple = Table->SetTuple(CB->TBlock, CB->TTuple);                        // Set the table's cursor to the tuple
        if ( LockMode && !CursorOp )                                            // If this in not a cursor op and we did locking, we can now free our lock(s)
            ATFreeShare(&(SearchFoundPage->ALock));
        return Tuple;
    }
    if ( !LockMode ) {                                                          // If I'm not in locking mode
        SearchLockMode = LockMode = AT_BTREE_READ_CRABLOCK;                     // Then we need to retry before returning NULL
        goto retry;
    }
    if ( LockMode && !CursorOp )                                                // If this in not a cursor op and we did locking, we can now free our read lock
        ATFreeShare(&(SearchFoundPage->ALock));
    return NULL;
}
// **************************************************************************** SetCursor
ATTuple *ATBTree::SetCursor(                                                    // Locate the cursor to a given key- returns a ptr to the tuple
                                                                                // CURSORS ALWAYS USE CRAB LOCKS!  ALWAYS USE FREECURSOR ASAP!
                                void            *inKey,                         // Key to find tuple for
                                long            FindMode,                       // Set to AT_BTREE_FINDFIRST, AT_BTREE_DIRECT, or AT_BTREE_FINDLAST
                                long            inMatchLength                   // How long to test the match for (this is passed into the user routine, so there might be *some* use for values greater than keylength... I DON'T CHECK IT!)
                                ) {                                             // Note this also positions the table's cursor
    volatile ATBTPH *PH;
    volatile ATBTCB *CB;
    long    Found;
    ATTuple *Return;

    if ( CursorPage )                                                           // In case we forgot we have an open lock
        ATFreeShare(&(CursorPage->ALock));
    // BTW, we ALWAYS have to use crab locks with cursors because of the FINDFIRST thing- see FindInPage to see why
    CursorOp = 1;                                                               // Let class know this is a cursor op
    if ( (Return = FindTuple(inKey, AT_BTREE_READ_CRABLOCK, FindMode, inMatchLength)) ) {// Try to find it
found_it:
        CursorPage = SearchFoundPage;                                           // If found, save the info
        CursorTuple = SearchContainedIn;
        if ( CursorTuple < 0 ) CursorTuple = 0;                                 // Rare occasions where we are wanting to start at first
        CursorStatus = AT_BTREE_CSTATUS_NORMAL;
        CursorOp = 0;
        SearchMode = AT_BTREE_FINDDIRECT;                                       // Don't leave this in funny state
        return Return;                                                          // And return the tuple ptr
    }

    if ( FindMode == AT_BTREE_FINDDIRECT ) {                                    // If this was a direct locate we are done- we did not find it
        CursorPage = SearchFoundPage;                                           // Make sure to tell FreeCursor where we were last
        FreeCursor();                                                           // Reset the cursor & return
        return NULL;
    }
    // Unfortunately, since they got here via the nodes, and we don't delete entries from the nodes when we delete records, they may have gotten to this page under false pretenses...
    else if ( FindMode == AT_BTREE_FINDFIRST ) {                                // Let's look further forward to make sure
        while ( SearchFoundPage->NextPage.BBlock != AT_BTREE_END_CHAIN ) {      // As long as there are more pages left
            PH = (volatile ATBTPH*)PageMan.LocateTuple(                         // Find that next page
                    SearchFoundPage->NextPage.BBlock, SearchFoundPage->NextPage.BTuple);
            ATGetShare(&(PH->ALock));                                           // Get a shared lock on it
            ATFreeShare(&(SearchFoundPage->ALock));                             // Free up our old lock
            CursorPage = SearchFoundPage = PH;
            Found = FindInPage(SearchFoundPage, inKey, 0, 0);                   // Is it in this page?
            if ( Found ) {                                                      // If we find it
                CB = (((volatile ATBTCB*)SearchVal) - 1);                       // Get the CB
                Return = Table->SetTuple(CB->TBlock, CB->TTuple);               // Set the table's cursor to the tuple
                goto found_it;                                                  // And go take care of our other cursor stuff
            }
            if ( SearchResult < 0 ) {                                           // No point in going further forward when the records are too big
                FreeCursor();
                return NULL;
            }
        }
        CursorPage = SearchFoundPage;                                           // Make sure to tell FreeCursor where we were last
        FreeCursor();                                                           // Went through all the pages and didn't find it
        return NULL;
    }
    else  {                                                                     // Let's look further backward to make sure
        while ( SearchFoundPage->PrevPage.BBlock != AT_BTREE_END_CHAIN ) {      // As long as there are more pages left
            PH = (volatile ATBTPH*)PageMan.LocateTuple(                         // Find that next page
                    SearchFoundPage->PrevPage.BBlock, SearchFoundPage->PrevPage.BTuple);
            ATGetShare(&(PH->ALock));                                           // Get a shared lock on it
            ATFreeShare(&(SearchFoundPage->ALock));                             // Free up our old lock
            CursorPage = SearchFoundPage = PH;
            Found = FindInPage(SearchFoundPage, inKey, 0, 0);                   // Is it in this page?
            if ( Found ) {                                                      // If we find it
                CB = (((volatile ATBTCB*)SearchVal) - 1);                       // Get the CB
                Return = Table->SetTuple(CB->TBlock, CB->TTuple);               // Set the table's cursor to the tuple
                goto found_it;                                                  // And go take care of our other cursor stuff
            }
            if ( SearchResult > 0 ) {                                           // No point in going further back if we get smaller records
                FreeCursor();
                return NULL;
            }
        }
        CursorPage = SearchFoundPage;                                           // Make sure to tell FreeCursor where we were last
        FreeCursor();                                                           // Went through all the pages and didn't find it
        return NULL;
    }
}
// **************************************************************************** SetCursorToStart
ATTuple *ATBTree::SetCursorToStart() {                                          // Locate the cursor to the start of the index
                                                                                // Note this also positions the table's cursor
    volatile ATBTPH *NextPage;
    long            Result;

    if ( CursorPage )                                                           // In case we forgot we have an open lock
        ATFreeShare(&(CursorPage->ALock));
    SearchLockMode = AT_BTREE_READ_CRABLOCK;                                    // Set our locking mode

restart:
    SearchFoundPage = Root;                                                     // Start at the root
    ATGetShare(&(Root->ALock));                                                 // Start with a share lock on the root

    while ( SearchFoundPage->PageType != AT_BTREE_LEAF ) {                      // Until we get to the leaf page...
        NextPage = (volatile ATBTPH*)PageMan.LocateTuple(SearchFoundPage->Low.BBlock,// Get the first page in the tree
                                                        SearchFoundPage->Low.BTuple);
        if ( (Result = GetCrabDownReadLock(&(SearchFoundPage->ALock), &(NextPage->ALock)))// Get a crabbed read lock
                    != ATERR_SUCCESS )
            goto restart;
        SearchFoundPage = NextPage;
    }
    CursorPage = SearchFoundPage;                                               // Set the cursor stuff
    CursorTuple = 0;
    CursorStatus = AT_BTREE_CSTATUS_BOB;
    return CursorNext();
}
// **************************************************************************** SetCursorToEnd
ATTuple *ATBTree::SetCursorToEnd() {                                            // Locate the cursor to the end of the index
                                                                                // Note this also positions the table's cursor
    volatile ATBTPH *NextPage;
    volatile ATBTCB *CB;
    long    *T1;
    char    *T2;
    long            Result;

    if ( CursorPage )                                                           // In case we forgot we have an open lock
        ATFreeShare(&(CursorPage->ALock));
    SearchLockMode = AT_BTREE_READ_CRABLOCK;                                    // Set our locking mode

restart:
    SearchFoundPage = Root;                                                     // Start at the root
    ATGetShare(&(Root->ALock));                                                 // Start with a share lock on the root

    while ( SearchFoundPage->PageType != AT_BTREE_LEAF ) {                      // Until we get to the leaf page...
        CB = ATBTreeKeyDirect(SearchFoundPage, SearchFoundPage->NumberKeys - 1, T1, T2);// Find the last record in the page
        NextPage = (volatile ATBTPH*)PageMan.LocateTuple(CB->BBlock, CB->BTuple); // Load that page
        if ( (Result = GetCrabDownReadLock(&(SearchFoundPage->ALock), &(NextPage->ALock)))// Get a crabbed read lock
                    != ATERR_SUCCESS )
            goto restart;
        SearchFoundPage = NextPage;
    }
    CursorPage = SearchFoundPage;                                               // Set the cursor stuff
    CursorTuple = SearchFoundPage->NumberKeys - 2;                              // Note we go 1 less than the end so that CursorNext will return the last record
    CursorStatus = AT_BTREE_CSTATUS_NORMAL;
    return CursorNext();
}

// **************************************************************************** CursorNext
ATTuple *ATBTree::CursorNext(                                                   // Move the cursor to the next tuple
                                ) {                                             // Note this also positions the table's cursor
    long    *T1;
    char    *T2;
    volatile ATBTCB     *CB;

    if ( CursorPage ) {                                                         // Make sure cursor has been set up
        if ( CursorStatus == AT_BTREE_CSTATUS_NORMAL )                          // In normal mode...
            CursorTuple++;                                                      // Just inc the cursor #
        else {
            if ( CursorStatus == AT_BTREE_CSTATUS_BOB )                         // At start of tree
                CursorStatus = AT_BTREE_CSTATUS_NORMAL;
            else                                                                // At end of tree
                return NULL;
        }
retry:
        if ( CursorTuple < CursorPage->NumberKeys ) {                           // As long as there is room left in this page
            CB = ATBTreeKeyDirect(CursorPage, CursorTuple, T1, T2);             // Get the key location
            return Table->SetTuple(CB->TBlock, CB->TTuple);                     // Set the table's cursor to the tuple
        }
        else {                                                                  // Need to go to the next page- out of records here
            if ( CursorPage->NextPage.BBlock != AT_BTREE_END_CHAIN ) {          // If there is a next page
                volatile ATBTPH *OldPH = CursorPage;                            // Save the old page ptr
                CursorPage = (volatile ATBTPH*)PageMan.LocateTuple(CursorPage->NextPage.BBlock,// Move to the next page
                                               CursorPage->NextPage.BTuple);
                ATGetShare(&(CursorPage->ALock));                               // Get lock on the next page
                ATFreeShare(&(OldPH->ALock));                                   // Free up the old lock
                CursorTuple = 0;                                                // Start at zero
                goto retry;                                                     // And give 'er another go
            }
            else {
                CursorStatus = AT_BTREE_CSTATUS_EOB;                            // We are just out of records
                return NULL;
            }
        }
    }
    return NULL;
}
// **************************************************************************** CursorPrev
ATTuple *ATBTree::CursorPrev(                                                   // Move the cursor to the prev tuple
                                ) {                                             // Note this also positions the table's cursor
    long    *T1;
    char    *T2;
    volatile ATBTCB  *CB;

    if ( CursorPage ) {                                                         // Make sure cursor has been set up
        if ( CursorStatus == AT_BTREE_CSTATUS_NORMAL )                          // In normal mode...
            CursorTuple--;                                                      // Just dec the cursor #
        else {
            if ( CursorStatus == AT_BTREE_CSTATUS_EOB )                         // At end of tree
                CursorStatus = AT_BTREE_CSTATUS_NORMAL;
            else                                                                // At start of tree
                return NULL;
        }
retry:
        if ( CursorTuple >= 0 ) {                                               // As long as there is room left in this page
            CB = ATBTreeKeyDirect(CursorPage, CursorTuple, T1, T2);             // Get the key location
            return Table->SetTuple(CB->TBlock, CB->TTuple);                     // Set the table's cursor to the tuple
        }
        else {                                                                  // Need to go to the prev page- out of records here
            if ( CursorPage->PrevPage.BBlock != AT_BTREE_END_CHAIN ) {          // If there is a prev page
                volatile ATBTPH *OldPH = CursorPage;                            // Save the old page ptr
                CursorPage = (volatile ATBTPH*)PageMan.LocateTuple(CursorPage->PrevPage.BBlock,// Move to the prev page
                                                CursorPage->PrevPage.BTuple);
                ATGetShare(&(CursorPage->ALock));                               // Get lock on the next page
                ATFreeShare(&(OldPH->ALock));                                   // Free up the old lock
                CursorTuple = CursorPage->NumberKeys - 1;                       // Start at the end
                goto retry;                                                     // And give 'er another go
            }
            else {
                CursorStatus = AT_BTREE_CSTATUS_BOB;                            // We are just out of records
                return NULL;
            }
        }
    }
    return NULL;
}
// **************************************************************************** FreeCursor
int ATBTree::FreeCursor() {                                                     // Call to release any locks the cursor holds, and reset it
    if ( CursorPage )                                                           // If it was in use
        ATFreeShare(&(CursorPage->ALock));                                      // Free the last page lock held

    CursorPage = NULL;                                                          // Then reset the cursor
    CursorTuple = 0;
    CursorStatus = AT_BTREE_CSTATUS_NORMAL;
    CursorOp = 0;
}
// **************************************************************************** InsertTuple
int ATBTree::DeleteTuple(                                                       // Called when a tuple has been inserted into the associated table
                                                                                // NOTE: GENERALLY NOT USER CALLED!  CALL THE TABLE's INSERT FUNCTION
                                ATTuple         *Tuple,                         // Key of tuple to insert
                                long            inBlock,                        // The unique block & tuple ID
                                long            inTuple
                                ) {
    void *Key = (MakeKey)((void*)Tuple);                                       // Get a key made from the tuple
    return DeleteKey(Key, inBlock, inTuple);                                   // And delete it
}

// **************************************************************************** DeleteKey
int ATBTree::DeleteKey(                                                         // Called when a tuple is deleted
                                                                                // USUALLY NOT CALLED BY USER- USE THE TABLE'S DeleteTuple() INSTEAD
                                void            *inKey,                         // Key of tuple to delete
                                long            inBlock,                        // The unique block & tuple ID
                                long            inTuple
                                ) {
    long            Result, i;
    SearchCompareLength = KeyLength;                                            // Set the search parameters
    // NEVER EVER EVER DO A DELETE WITH ANYTHING OTHER THAN FINDDIRECT- for example, FINDFIRST could really hose the lock tracking.
    SearchLockMode =      AT_BTREE_DELETE;  SearchMode = AT_BTREE_FINDDIRECT;   // SEE NOTE ABOVE
    PagedFindKey(inKey, inBlock, inTuple);                                      // Find the location for insertion

    PagesSplit = 0;                                                             // Make sure our page split ctr is starting from zero
    Result = DeleteKeyFromPage(inKey, inBlock, inTuple);                        // Delete the key from the page we located

    ATRemoveQueueShareExclusive(&(SearchFoundPage->ALock));                     // Release the lock on the leaf page- cautiously in case of an error where I did not wait
    return Result;
}
// **************************************************************************** DeleteKeyFromPage
int ATBTree::DeleteKeyFromPage(                                                 // Called to delete a tuple from a specific page
                                void            *inKey,                         // Ptr to key to delete
                                long            inBlock,                        // Block value to check
                                long            inTuple                         // Tuple value to check
                                ) {

    long        Result, *KeyPtrs, *KeysBase, FreeSpot, i, NKeys;
    volatile ATBTCB *CB;

    if ( !(Result = FindInPage(SearchFoundPage, inKey, inBlock, inTuple)) )     // Find where to delete the key in this page
        return ATERR_NOT_FOUND;

    KeyPtrs = ATBTreeKeyPtrBase(SearchFoundPage);                               // Figure out where the key ptrs are
    KeysBase = ATBTreeKeyBase(KeyPtrs);                                         // and where the keys themselves are
    CB = ATBTreeKey(KeyPtrs, KeysBase, SearchContainedIn);                      // Get a CB ptr to the key
    if ( CB->TTuple != inTuple || CB->TBlock != inBlock )                       // But we MUST double check, because it might be that someone tried to insert a dupe, and we are now in rollback mode
        return ATERR_NOT_FOUND;
    ATWaitQueueShareExclusive(&(SearchFoundPage->ALock));                       // Now we need to wait for the readers to leave
    CB->TTuple = SearchFoundPage->AvailableChain;                               // Make myself part of the chain
    SearchFoundPage->AvailableChain = KeyPtrs[SearchContainedIn];               // Point the chain to me

    NKeys = SearchFoundPage->NumberKeys - 1;                                    // Go ahead and let the optimizer cache this for the loop- we have it locked
    for ( i = SearchContainedIn; i < NKeys; i++)                                // Loop thru all ptrs above our delete point
        KeyPtrs[i] = KeyPtrs[i + 1];                                            // Move them down to remove our deleted key hole
    SearchFoundPage->NumberKeys--;                                              // Dec the # of keys stored in the page
    return ATERR_SUCCESS;

}
// **************************************************************************** PopulateFromTable
int ATBTree::PopulateFromTable() {                                              // Use to populate a new BTree ONLY from an existing table
    ATTuple *Tuple;
    long    Block, TupleNumber, Result;

    Table->ResetCursor();
    while ( (Tuple = Table->NextTuple()) ) {                                    // Go thru all the tuples
        if ( !(Table->GetTupleLong(&Block, &TupleNumber)) )                     // Get the stored block & tuple number
            return ATERR_OPERATION_FAILED;
        if ( (Result = InsertTuple(Tuple, Block, TupleNumber)) != ATERR_SUCCESS )// Insert the tuple
            return ATERR_OPERATION_FAILED;
    }
    return ATERR_SUCCESS;
}
// **************************************************************************** CheckBTree
int ATBTree::CheckBTree() {                                                     // Routine to test integrity of BTree
    char    *Boom;
    CheckLevel = CheckErrors = 0;

    if ( Debug) printf("**************************************************************************\r\n");
    Recurse(Root, Root, NULL, NULL, 0,0,0,0);                                   // Start with the root....
    if ( !CheckErrors )                                                         // If there were no errors
        return ATERR_SUCCESS;
    else {
        Boom = NULL; *((long*)Boom) = 1;                                        // Kablooie.
    }
}
// **************************************************************************** Recurse
int ATBTree::Recurse(                                                           // Called by CheckBTree to recurse the tree
                                volatile ATBTPH *Page,                          // Page to check
                                volatile ATBTPH *Parent,                        // Parent page
                                void            *CheckLowBounds,                // Lowest key value allowed in the page
                                void            *CheckHighBounds,               // Highest key value allowed in the page
                                long            CheckLowBlock,                  // Lowest block & tuple allowed in the page
                                long            CheckLowTuple,
                                long            CheckHighBlock,                 // Highest block & tuple allowed in the page
                                long            CheckHighTuple
                                ) {
    long    Result, *KeyPtrs = ATBTreeKeyPtrBase(Page);
    long    *KeysBase = ATBTreeKeyBase(KeyPtrs), i, OldLockMode;
    volatile ATBTCB *CB1, *CB2;
    volatile ATBTPH *PH;
    volatile char   *TestVal, *TestVal2;
    void    *OCheckLowBounds, *OCheckHighBounds;
    long    OCheckLowBlock, OCheckHighBlock, OCheckLowTuple, OCheckHighTuple;

    ++CheckLevel;
    if ( Page->PageType != AT_BTREE_LEAF ) {                                    // Keep going til the bottom (a leaf page)
        if ( Page->Low.BBlock != AT_BTREE_END_CHAIN ) {                         // If there is a low page, recurse it first
            PH = (volatile ATBTPH*)PageMan.LocateTuple(Page->Low.BBlock, Page->Low.BTuple);// Find it
            OCheckLowBounds = NULL;                                             // Set the bounds
            CB1 = ATBTreeKey(KeyPtrs, KeysBase, 0);
            OCheckHighBounds = (char*)(CB1 + 1);                                // What is the highest possible entry for the page?
            OCheckHighBlock = CB1->TBlock;
            OCheckHighTuple = CB1->TTuple;
            Recurse(PH, Page, OCheckLowBounds, OCheckHighBounds, OCheckLowBlock,// Check it (recurse into it)
                    OCheckLowTuple, OCheckHighBlock, OCheckHighTuple);
        }
        for ( i = 0; i < Page->NumberKeys; ++i ) {                              // Go thru all the other entries
            CB1 = ATBTreeKey(KeyPtrs, KeysBase, i);                             // Get the entry
            PH = (volatile ATBTPH*)PageMan.LocateTuple(CB1->BBlock, CB1->BTuple);
            OCheckLowBounds = (char*)(CB1 + 1);                                 // Set the bounds
            OCheckLowBlock = CB1->TBlock;
            OCheckLowTuple = CB1->TTuple;
            if ( i < Page->NumberKeys - 1) {
                CB1 = ATBTreeKey(KeyPtrs, KeysBase, i + 1);
                OCheckHighBounds = (char*)(CB1 + 1);                            // What is the highest possible entry for the page?
                OCheckHighBlock = CB1->TBlock;
                OCheckHighTuple = CB1->TTuple;
            }
            else
                OCheckHighBounds = NULL;
            Recurse(PH, Page, OCheckLowBounds, OCheckHighBounds, OCheckLowBlock,// Check it (recurse into it)
                    OCheckLowTuple, OCheckHighBlock, OCheckHighTuple);
        }
    }

    if ( Debug) printf("Checking page: %p (%i, %i), level %i, parent is %p.\r\n", Page, Page->Block, Page->Tuple, CheckLevel, Parent);
    if ( CheckLowBounds ) {if ( Debug) printf("Low Bounds: (%i, %i) \r\n", CheckLowBlock, CheckLowTuple);}
    if ( CheckHighBounds ) {if ( Debug) printf("High Bounds: (%i, %i) \r\n", CheckHighBlock, CheckHighTuple);}
//    if ( CheckLowBounds ) {if ( Debug) printf("Low Bounds: (%i, %i) %s\r\n", CheckLowBlock, CheckLowTuple, CheckLowBounds);}
//    if ( CheckHighBounds ) {if ( Debug) printf("High Bounds: (%i, %i) %s\r\n", CheckHighBlock, CheckHighTuple, CheckHighBounds);}

    if ( Page->ALock != 0 ) {
        if ( Page->PageType != AT_BTREE_LEAF )
            printf("NODE PAGE:");
        else
            printf("LEAF PAGE:");
        printf("THERE ARE LOCKS ACTIVE (%i) ON THIS PAGE! (level %i)\r\n", Page->ALock, CheckLevel);
    }

    for ( i = 0; i < Page->NumberKeys; ++i ) {                                  // Check all entries
        TestVal = ((volatile char*)((ATBTreeKey(KeyPtrs, KeysBase, i)) + 1));
        CB1 = (((volatile ATBTCB*)TestVal) - 1);
        if ( Debug) printf("Entry %i: B(%i, %i) T(%i, %i) \r\n", i, CB1->BBlock, CB1->BTuple, CB1->TBlock, CB1->TTuple);
//        if ( Debug) printf("Entry %i: B(%i, %i) T(%i, %i) %s\r\n", i, CB1->BBlock, CB1->BTuple, CB1->TBlock, CB1->TTuple, TestVal);
        if ( i < Page->NumberKeys - 1 ) {
            TestVal2 = ((volatile char*)((ATBTreeKey(KeyPtrs, KeysBase, i + 1)) + 1));
            Result = ((Compare)((void*)TestVal, (void*)TestVal2, KeyLength));
            if ( Result == 0 ) {
                if ( IndexType != AT_BTREE_PRIMARY ) {
                    CB1 = (((volatile ATBTCB*)TestVal) - 1);
                    CB2 = (((volatile ATBTCB*)TestVal2) - 1);
                    if ( (CB1->TBlock >= CB2->TBlock) ) {
                        if ( (CB1->TTuple >= CB2->TTuple) ) {
                            printf("Keys out of order by tuple ID at %i! (%i, %i)\r\n", i, CB2->TBlock, CB2->TTuple);
//                            printf("Keys out of order by tuple ID at %i! (%s, %i, %i)\r\n", i, TestVal2, CB2->TBlock, CB2->TTuple);
                            ++CheckErrors;
                        }
                    }
                }
                else {
                    printf("Duplicate entries in primary index at %i! (%i, %i)\r\n", i, CB2->TBlock, CB2->TTuple);
//                    printf("Duplicate entries in primary index at %i! (%s, %i, %i)\r\n", i, TestVal2, CB2->TBlock, CB2->TTuple);
                    ++CheckErrors;
                }
            }
            else if ( Result > 0 ) {
                printf("Entries out of order by key at %i! (%i, %i)\r\n", i, CB2->TBlock, CB2->TTuple);
//                printf("Entries out of order by key at %i! (%s, %i, %i)\r\n", i, TestVal2, CB2->TBlock, CB2->TTuple);
                ++CheckErrors;
            }
        }

        if ( CheckLowBounds ) {
            Result = ((Compare)((void*)TestVal, (void*)CheckLowBounds, KeyLength));
            if ( !Result ) {
                if ( IndexType != AT_BTREE_PRIMARY ) {
                    CB1 = (((volatile ATBTCB*)TestVal) - 1);
                    if ( (CB1->TBlock == CheckLowBlock) ) {
                        if ( (CB1->TTuple < CheckLowTuple) ) {
                            printf("Entry lower than low bounds at %i!\r\n", i);
                            ++CheckErrors;
                        }
                    }
                    else if ( (CB1->TBlock < CheckLowBlock) ) {
                        printf("Entry lower than low bounds at %i!\r\n", i);
                        ++CheckErrors;
                    }
                }
            }
            else if ( Result < 0 ) {
                printf("Entry key lower than low bounds at %i!\r\n", i);
                ++CheckErrors;
            }
        }
        if ( CheckHighBounds ) {
            Result = ((Compare)((void*)TestVal, (void*)CheckHighBounds, KeyLength));
            if ( !Result ) {
                if ( IndexType != AT_BTREE_PRIMARY ) {
                    CB1 = (((volatile ATBTCB*)TestVal) - 1);
                    if ( (CB1->TBlock == CheckHighBlock) ) {
                        if ( (CB1->TTuple >= CheckHighTuple) ) {
                            printf("Entry higher than high bounds at %i!\r\n", i);
                            ++CheckErrors;
                        }
                    }
                    else if ( (CB1->TBlock > CheckHighBlock) ) {
                        printf("Entry higher than higher bounds at %i!\r\n", i);
                        ++CheckErrors;
                    }
                }
            }
            else if ( Result >= 0 ) {
                printf("Entry key GE than high bounds at %i!\r\n", i);
                ++CheckErrors;
            }
        }

        if ( Page->PageType == AT_BTREE_LEAF ) {
            OldLockMode = SearchLockMode;
            SearchCompareLength =   KeyLength;
            SearchMode =    AT_BTREE_FINDDIRECT;
            SearchLockMode = 0;
            CB1 = (((volatile ATBTCB*)TestVal) - 1);
            PagedFindKey((void*)TestVal, CB1->TBlock, CB1->TTuple);
            if  ( SearchFoundPage != Page ) {
                printf("PagedFindKey() did not find this tuple here at %i!\r\n", i);
                ++CheckErrors;
            }
            Result = FindInPage(Page, (void*)TestVal, CB1->TBlock, CB1->TTuple);
            if ( !Result || (SearchContainedIn != i) ) {
                printf("FindInPage() did not find this tuple in the page correctly at %i!\r\n", i);
            }
            SearchLockMode = OldLockMode;
        }
    }

    if ( !CheckErrors )
        if ( Debug) printf("Page okay- leaving.\r\n");
    else
        if ( Debug) printf("!!This BTree sucks harder than a Hoover!! %i Error(s)!\r\n", CheckErrors);
    --CheckLevel;
    CheckLowBounds = CheckHighBounds = NULL;
    return ATERR_SUCCESS;
}


