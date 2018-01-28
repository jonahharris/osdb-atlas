#ifndef BTREE_H
#define BTREE_H
// ****************************************************************************
// * btree.h - The BTree code for Atlas.                                      *
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

                                                                                // The declaration for a BTree comparison routine
typedef     long (ATBTreeComp)(void *, void*, long);
                                                                                // The declaration for a BTree make key routine
typedef     void *(ATBTreeMakeKey)(void *);

typedef struct ATBTreeInformationBlock  ATBTInfo;
struct ATBTreeKeyCB {                                                           // Control block for a BTree key
    volatile long       BBlock;                                                 // BTree block ID
    volatile long       BTuple;                                                 // BTree tuple ID
    volatile long       TBlock;                                                 // Table block ID
    volatile long       TTuple;                                                 // Table tuple ID
};
typedef struct ATBTreeKeyCB ATBTCB;
struct ATBTreeKeyShortCB {                                                      // Short control block for a BTree key
    volatile long       BBlock;                                                 // BTree block ID
    volatile long       BTuple;                                                 // BTree tuple ID
};
typedef struct ATBTreeKeyShortCB ATBTSHCB;

struct ATBTreeKeyTKCB {                                                         // Lock tracking control block for a BTree page
    ATLOCK              *ALock;                                                 // The lock held
    volatile long       Block;                                                  // Part 1 of 2 32bit ints that make up the ID
    volatile long       Tuple;                                                  // Part 2 of 2 32bit ints that make up the ID
};
typedef struct ATBTreeKeyTKCB ATBTTKCB;
typedef struct ATBTreePageHeader ATBTPH;

// Find mode search flags
// Find the first value in a range
#define     AT_BTREE_FINDFIRST          (-1)
// Find any exact match- note this will fail in a user call on a secondary key- use FIRST or LAST instead for secondaries
#define     AT_BTREE_FINDDIRECT         (0)
// Find the last value in a range
#define     AT_BTREE_FINDLAST           (1)

// Lock mode search flags
// Be very careful with READ_OPTIMISTIC.  It is certainly the fastest & lightest protocol, but
// ONLY if the items being requested are typically found... If they aren't found, it has to
// automatically re-search with CRABLOCK to make sure it didn't miss it, making it more than
// twice as slow in a not found.  MOST IMPORTANTLY, a concurrent delete COULD blow up an optimistic
// read by briefly exposing garbage, so only use it where some other means keeps deletes from
// happening concurrently.  For example, it works great in the XTemplate cache, where 99% of the time
// a page will be found, and deletes do not happen without a lock of the entire table.
#define     AT_BTREE_READ_OPTIMISTIC    (0)
#define     AT_BTREE_READ_CRABLOCK      (1)
#define     AT_BTREE_WRITE_OPTIMISTIC   (2)
#define     AT_BTREE_WRITE_HOLDLOCK     (3)
#define     AT_BTREE_DELETE             (4)

// Maximum depth of a BTree
#define     AT_BTREE_MAXDEPTH           (20)

// ****************************************************************************
// ****************************************************************************
//                                  ATBTREE
// ****************************************************************************
// ****************************************************************************
/*  As with most Atlas objects, threadsafe AFTER Create() but not during,
    and each thread MUST have its own object instance and unique valid Kilroy.
    Is should also be noted that it is up to the caller to make sure the Compare
    and MakeKey routines supplied to the BTree are appropriately safe.  For
    example, MakeKey needs to return a ptr to a string, which obviously can't be
    on the stack, so it should be from a safe tuple, or safe static, etc. etc.

    We have four locking levels that we use:

    In read optimistic, we do no locking at all, unless we do not find the request,
    in which case we escalate our mode to read crablock and restart the descent.
    (to make sure it really isn't there).  Probably the best choice for read heavy
    tables.  Of course, ALWAYS check what you get to make sure it is what you asked
    for...

    In read crablock, we crab down with shares.  Might be a better choice if the
    table is very write heavy, since optimistic reads might do a lot of restarting.
    This is also the only mode that cursors use.

    In write optimistic, we use shares to crab down, just enough to ensure the
    integrity of the path, only getting an exclusive at the leaf level to do the
    insert.  However, in the unlikely case that a split will be needed, we escalate
    to a write hold lock and restart the descent.

    In write hold, we will hold an exclusive lock on every node where it
    looks like the next level will need a split.  Now, this means that rarely we
    might hold a lock we don't need, since we did not actually track them all on the
    previous descent.  It is my admittedly untested belief though that this
    case is so rare as to be an acceptable alternative to more pessimistic approaches.
    The worst case performance would probably be a tree 4 levels deep, since we
    might sometimes hold the root in that case where we don't need to, though even
    then it would be exceedingly rare. (a split (unlikely to start with), combined
    with a perfectly full second level but not full third level node, and finally of
    course, a perfectly full fourth node- you do the math- it's reasonably rare).
    Note that with our queued exclusive ability, we can actually traverse the tree &
    get a lot of work done on a split before we have to verify all other readers are
    out of the access path.

    The backwards scroll should not be considered isolated by itself- it might miss
    records in the right sleep/split interaction.  You could easily fix it with a
    separate lock just for that value, if desired.  Maybe when I add transactions... ;^)

    Basically, locks are acquired top to bottom.  Shares always give way to exclusives.
*/
// **************************************************************************** ATBTree
class ATBTree {                                                                 // BTree class for Atlas
private:
    long                TrueKeyLength;                                          // Actual space taken up by stored keys
    // Search values I set this way to keep from having to pass them umpteen times on the stack
    long                SearchResult;                                           // The result of the last search comparison- used differently by different methods
    volatile char       *SearchVal;                                             // The base of the last val compared
    long                SearchCompareLength;                                    // Length to compare by
    ATBTreeComp         *Compare;                                               // Pointer to the function to use for comparisons of the keys
    long                SearchContainedIn;                                      // Ptr to variable to set with the key number (or contained in value) within the page
    long                SearchMode;                                             // Find mode (AT_BTREE_FINDFIRST, AT_BTREE_FINDDIRECT, AT_BTREE_FINDLAST)
    long                SearchLockMode;                                         // Locking mode (AT_BTREE_OPTIMISTIC, AT_BTREE_CRABLOCK)
    volatile ATBTPH     *SearchFoundPage;                                       // Ptr to set to the page where key was found

    volatile ATBTPH     *CursorPage;                                            // Page the cursor was last on
    long                CursorTuple;                                            // Tuple the cursor was last on
    long                CursorStatus;                                           // Status of the cursor
    long                CursorOp;                                               // Flag to other class methods that a cursor operation is taking place

    volatile ATBTPH     *Root;                                                  // Pointer to the root of the BTree
    long                LockAlert;                                              // Special case flag for locking in FINDDIRECT mode

    long                KeyLength;                                              // Length of the keys
    long                IndexType;                                              // Either AT_BTREE_PRIMARY or AT_BTREE_SECONDARY
    ATSharedTable       PageMan;                                                // Our "page manager", which is really simply a shared table
    ATBTreeMakeKey      *MakeKey;                                               // Pointer to the function to use to make a key from a given tuple

    ATBTTKCB            LockedPath[AT_BTREE_MAXDEPTH];                          // The LOCKED path that I have descended through the tree-i.e., all the nodes above me that I have left locked
    long                PathDepth;                                              // The depth to which I have descended the locked path-i.e., the number of nodes above me which are locked
    long                EscalationFailures;                                     // Keeps track of the escalation failures I have had

    volatile ATBTPH     *ShiftLeft;                                             // The shift left page, a special case for find first
    long                KeysPerPage;                                            // Number of keys stored in each BTree page
    ULONG               Kilroy;                                                 // My kilroy ID
    ATSharedTable       *Table;                                                 // Table that this BTree is associated with
    long                PagesSplit;                                             // Keeps track of the current number of pages we have split
    long                AllocSize;                                              // Number of pages allocated as a block each time we need to grow
    volatile ATBTInfo   *Info;                                                  // Pointer to the info struct in shared memory

    long                Debug;                                                  // Useful debug flag
    long                CheckLevel;                                             // Used by CheckBTree to keep track of depth traversed
    long                CheckErrors;                                            // Used by CheckBTree to keep track of errors

    void                Reset();                                                // Reset the class members
    int                 FindInPage(                                             // Find a key in the page- most parameters are set in the class members- check there for info.
                                volatile ATBTPH *Page,                          // Page to search
                                void            *inKey,                         // Key to search for
                                long            Block,                          // Block to search for
                                long            Tuple                           // Tuple to search for
                                );
    int                 PagedFindKey(                                           // Routine to find the page a key belong in- page is appropriately locked on return
                                void            *inKey,                         // Key to find
                                long            Block,                          // Block & tuple for the key
                                long            Tuple
                                );                                              // Many parameters are set in the class members- check there for info.
    int                 GetCrabDownWriteLock(                                   // Internal routine for crabbing down for an insert op
                                volatile ATBTPH *CPage,                         // Ptr to the page we are on now
                                volatile ATBTPH *DPage                          // Ptr to the page we want to crab to
                                );
    int                 GetCrabDownReadLock(                                    // Internal routine for getting a BTree crab down share lock
                                ATLOCK          *HeldLock,                      // Ptr to the lock you currently hold
                                ATLOCK          *DesiredLock                    // Ptr to lock caller's wants to crab to
                                );
    int                 SplitPage(                                              // Possibly recursive method to split a page- returns a ptr to
                                volatile ATBTPH *Page,                          // Page to split
                                long            Split,                          // Where to split the page (passing it this way just makes SURE we picked the same spot)
                                volatile ATBTPH **RightPage,                    // Ptr set to to first new page (new right page ALWAYS)
                                volatile ATBTPH **LeftPage                      // Ptr to set to second new page (will be null unless the root splits, in which case it will be the new left page)
                                );
    volatile ATBTPH     *MakePageCopy(                                          // Routine to copy part of an existing page into a newly created page- returns a ptr to the new page
                                                                                // Does NOT change key ptrs or NumberKeys in old page
                                volatile ATBTPH *Page,                          // The original page
                                long            Low,                            // The low tuple to move over
                                long            High                            // The high tuple to move over
                                );
    int                 InsertKeyIntoPage(                                      // By the time we get here, we have our insert page ID'd and we have an exlclusive lock at least queued on it... possibly recursive call
                                volatile ATBTPH *Page,                          // Page to insert into
                                void            *inKey,                         // Ptr to key to insert
                                long            inBBlock,                       // BBlock value to use for compare
                                long            inBTuple,                       // BTuple value to use for compare
                                long            inTBlock,                       // BBlock value to use for compare
                                long            inTTuple,                       // BTuple value to use for compare
                                long            Mode                            // Set to 0 for leaf inserts, 1 for node split
                                );
    void                InitNewPage(                                            // Call to initialize a newly allocated BTree page data members (but not header)
                                volatile ATBTPH *Page                           // Page to init
                                );
    int                 UpgradeShareToExclusiveQueuedRB(                        // Routine to upgrade a share lock to a queued exclusive lock, and track it if not the leaf page- MUST HAVE A SHARELOCK ON THE LOCK!
                                                                                // Failure to upgrade releases all locks returns an error
                                volatile ATBTPH *Page                           // Page to lock
                                );
    int                 UpgradeShareToExclusiveQueuedSP(                        // Routine to upgrade a share lock to a queued exclusive lock, and track it if not the leaf page- MUST HAVE A SHARELOCK ON THE LOCK!
                                                                                // On failure we just spin until we get it
                                volatile ATBTPH *Page                           // Page to lock
                                );
    int                 GetQueuedExclusive(                                     // Routine to just spin until I get an exclusive lock, then track it if needed
                                volatile ATBTPH *Page                           // Page to lock
                                );
    int                 DeleteKeyFromPage(                                      // Called to delete a tuple from a specific page
                                void            *inKey,                         // Ptr to key to delete
                                long            inBlock,                        // Block value to check
                                long            inTuple                         // Tuple value to check
                                );
    ATTuple             *FindKey(                                               // Request to find a tuple based on the specified key- MAY NOT be the first key in a non-unique index- there are other calls for that
                                                                                // Note this also positions the table's cursor
                                void            *inKey,                         // Key to find tuple for
                                long            LockMode,                       // Locking mode- if this isn't a primary key, this MUST be set to CRABLOCK
                                long            FindMode,                       // Set to AT_BTREE_FINDFIRST, AT_BTREE_DIRECT, or AT_BTREE_FINDLAST
                                long            inMatchLength,                  // How long to test the match for (this is passed into the user routine, so there might be *some* use for values greater than keylength... I DON'T CHECK IT!)
                                long            Block,                          // Block & tuple to find
                                long            Tuple
                                );
    int                 Recurse(                                                // Called by CheckBTree to recurse the tree
                                volatile ATBTPH *Page,                          // Page to check
                                volatile ATBTPH *Parent,                        // Parent page
                                void            *CheckLowBounds,                // Lowest key value allowed in the page
                                void            *CheckHighBounds,               // Highest key value allowed in the page
                                long            CheckLowBlock,                  // Lowest block & tuple allowed in the page
                                long            CheckLowTuple,
                                long            CheckHighBlock,                 // Highest block & tuple allowed in the page
                                long            CheckHighTuple
                                );
    int                 TestCrabLock(                                           // Internal routine for getting a BTree crab share lock
                                ATLOCK          *HeldLock,                      // Ptr to the lock you currently hold
                                ATLOCK          *DesiredLock                    // Ptr to lock caller's wants to crab to
                                );
    int                 CrabShareToExclusive(                                   // Crab from a shared to an exclusive lock
                                volatile ATBTPH *HeldPage,                      // Page you hold a share lock on
                                volatile ATBTPH *DesiredPage                    // Page you want an exclusive lock on
                                );

public:
    ATBTree();
    ~ATBTree();

    // *************************************************************************
    //                          UTILITY ROUTINES
    // *************************************************************************
    int                 Create(                                                 // Create a BTree
                                int              inKey,                         // Systemwide unique IPC ID for this BTree- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
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
                                );
    int                 Open(                                                   // Open up an existing BTree
                                int             inKey,                          // Systemwide unique IPC ID for this BTree- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                                                                                // !!IMPORTANT!! The key that you specify here will be incremented by 1 each time a new section needs to be allocated,
                                                                                // So be sure to leave adequate room to grow between your IPC ID's!!
                                ATSharedTable   *inTable,                       // Which table to create it for (pointer to a valid created ATSharedTable
                                                                                // MUST BE AN OBJECT INSTANCE THAT BELONGS TO THE SAME THREAD THAT IS CALLING
                                ATBTreeComp     *inComp,                        // Pointer to the function that will be called to compare the key values- should behave just like memcmp(), returning <0,0, or >0 integer.
                                ATBTreeMakeKey  *inMakeKey,                     // Pointer to a function to make a key from a tuple- i.e., when given the pointer to a tuple, it returns a ptr to a valid key for the tuple
                                ULONG           inKilroy                        // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                                );
    int                 Close();                                                // Close the BTree & free all resources
    int                 WriteBTree(                                             // Write a BTree to a disk file
                                char            *inFileName                     // Filename to write the btree to
                                );
    int                 LoadBTree(                                              // Load a BTree from a disk file
                                                                                // THIS CALL SHOULD BE MADE RIGHT AFTER CREATE WITH MATCHING PARMS TO THE BTREE TO BE LOADED.  LOADING MISMATCHED BTREES COULD BE DISASTROUS (though it tries not to do it at all), AND FAILURES MAY LEAVE THE BTREE IN UNKNOWN STATE.
                                char            *inFileName                     // Filename to load the btree from
                                );
    int                 CreateFromFile(                                         // Create a BTree from a disk file
                                char            *inFileName,                    // Filename to load the btree from
                                int             inKey,                          // Systemwide unique IPC ID for this BTree- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                                                                                // !!IMPORTANT!! The key that you specify here will be incremented by 1 each time a new section needs to be allocated,
                                                                                // So be sure to leave adequate room to grow between your IPC ID's!!
                                ATSharedTable   *inTable,                       // Which table to create it for (pointer to a valid created ATSharedTable
                                                                                // MUST BE AN OBJECT INSTANCE THAT BELONGS TO THE SAME THREAD THAT IS CALLING
                                ATBTreeComp     *inComp,                        // Pointer to the function that will be called to compare the key values- should behave just like memcmp(), returning <0,0, or >0 integer.
                                ATBTreeMakeKey  *inMakeKey,                     // Pointer to a function to make a key from a tuple- i.e., when given the pointer to a tuple, it returns a ptr to a valid key for the tuple
                                ULONG           inKilroy                        // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                                );
    int                 PopulateFromTable();                                    // Use to populate a new BTree ONLY from an existing table

    // *************************************************************************
    //                              FIND
    // *************************************************************************
    ATTuple             *FindTuple(                                             // Request to find a tuple based on the specified key
                                                                                // Note this also positions the table's cursor
                                void            *inKey,                         // Key to find tuple for
                                long            LockMode,                       // Locking mode (AT_BTREE_READ_OPTIMISTIC or AT_BTREE_READ_CRABLOCK) - if this isn't a primary key, this MUST be set to CRABLOCK
                                long            FindMode,                       // Set to AT_BTREE_FINDFIRST, AT_BTREE_DIRECT, or AT_BTREE_FINDLAST
                                long            inMatchLength                   // How long to test the match for (this is passed into the user routine, so there might be *some* use for values greater than keylength... I DON'T CHECK IT!)
                                );

    // *************************************************************************
    //                          CURSOR ROUTINES
    // *************************************************************************
    ATTuple             *SetCursor(                                             // Locate the cursor to a given key- returns a ptr to the tuple
                                                                                // CURSORS ALWAYS USE CRAB LOCKS!  ALWAYS USE FREECURSOR ASAP!
                                                                                // Note this also positions the table's cursor
                                void            *inKey,                         // Key to find tuple for
                                long            FindMode,                       // Set to AT_BTREE_FINDFIRST, AT_BTREE_DIRECT, or AT_BTREE_FINDLAST
                                long            inMatchLength                   // How long to test the match for (this is passed into the user routine, so there might be *some* use for values greater than keylength... I DON'T CHECK IT!)
                                );
    ATTuple             *CursorNext();                                          // Move the cursor to the next tuple
                                                                                // Note this also positions the table's cursor
    ATTuple             *CursorPrev();                                          // Move the cursor to the prev tuple
                                                                                // Note this also positions the table's cursor
    int                 FreeCursor();                                           // Call to release any locks the cursor holds, and reset it
    ATTuple             *SetCursorToStart();                                    // Locate the cursor to the start of the index
                                                                                // CURSORS ALWAYS USE CRAB LOCKS!  ALWAYS USE FREECURSOR ASAP!
                                                                                // Note this also positions the table's cursor
    ATTuple             *SetCursorToEnd();                                      // Locate the cursor to the end of the index
                                                                                // CURSORS ALWAYS USE CRAB LOCKS!  ALWAYS USE FREECURSOR ASAP!
                                                                                // Note this also positions the table's cursor

    // *************************************************************************
    //                         LOW LEVEL SYSTEM ROUTINES
    // *************************************************************************
    // ***** NOTE: THE FOLLOWING GROUP OF CALLS ARE NOT GENERALLY CALLED BY USERS, BUT INSTEAD BY OTHER CLASSES- but I have left them exposed since they might be useful...
    int                 DeleteKey(                                              // Request to delete the specified tuple
                                                                                // NOTE: GENERALLY NOT USER CALLED!  CALL THE TABLE's DELETE FUNCTION
                                void            *inKey,                         // Key of tuple to delete
                                long            inBlock,                        // The unique block & tuple ID (ensures a tuple match in non-unique indexes)
                                long            inTuple
                                );
    int                 InsertKey(                                              // Call to insert a key
                                                                                // NOTE: GENERALLY NOT USER CALLED!  CALL THE TABLE's INSERT FUNCTION TO ADD A TUPLE!!
                                void            *inKey,                         // Key of tuple to insert
                                long            inBlock,                        // The unique block & tuple ID
                                long            inTuple
                                );
    int                 InsertTuple(                                            // Called when a tuple has been inserted into the associated table
                                                                                // NOTE: GENERALLY NOT USER CALLED!  CALL THE TABLE's INSERT FUNCTION TO ADD A TUPLE!!
                                ATTuple         *Tuple,                         // Key of tuple to insert
                                long            inBlock,                        // The unique block & tuple ID
                                long            inTuple
                                );
    int                 DeleteTuple(                                            // Called when a tuple is deleted
                                                                                // USUALLY NOT CALLED BY USER- USE THE TABLE'S DeleteTuple() INSTEAD
                                ATTuple         *Tuple,                         // Key of tuple to delete
                                long            inBlock,                        // The unique block & tuple ID
                                long            inTuple
                                );
    int                 CheckBTree();                                           // Routine to rigorously test the integrity of the BTree
    int                 SetDebug(long V) {Debug = V;}                           // Debug method that sets a flag when called
};



#endif
