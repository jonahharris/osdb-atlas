#ifndef TABLE_H
#define TABLE_H
// ****************************************************************************
// * table.h - The table code for Atlas.                                      *
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

#include "general.h"
#include "sem.h"
#include "memory.h"

typedef struct ATTupleControlBlock      ATTupleCB;
typedef struct ATTableAllocHeader       ATTBAH;
struct ATTableAllocHeaderGlobal {                                               // Header for each alloc in SHARED MEMORY
                                                                                // Contains the actual data for this block
    volatile long   NumberTuples;                                               // Number of tuples located in this block
    volatile long   TuplesAllocated;                                            // The number of tuple allocated for in this block
    volatile long   ThisBlock;                                                  // Which block is this, anyway?  Zero based.
    ATLOCK          ALock;                                                      // The lock for this header
    volatile long   SHMID;                                                      // The shared memory ID for this block
    volatile long   NextSHMID;                                                  // The shared memory ID for the NEXT block in the chain
};
typedef struct ATTableAllocHeaderGlobal ATTBAHG;
struct ATTableInformation {
    volatile long   TrueTupleSize;                                              // Tuple size adjusted to include the overhead the ATTable class add to each tuple
    volatile long   TupleSize;                                                  // Size of the tuples in bytes
    volatile long   NumberBlocks;                                               // The number of blocks allocated for this table
    volatile long   GrowthAlloc;                                                // Chunks of records to alloc as the table grows
    volatile long   InitialAlloc;                                               // Number of records to alloc initially
    volatile long   SoftWrites;                                                 // Set to true means that changes made to the table are queued until flushed, false means always flush changes to disk
//    ATLOCK          ALock;                                                      // Lock for this structure
    volatile long   Key;                                                        // Systemwide unique ID for this table
    volatile long   InstanceCount;                                              // Number of open instances to table
    volatile long   NumDelLists;                                                // Number of segments used for delete lists
    volatile long   NumAddLists;                                                // Number of segments used for add lists
};
typedef struct ATTableInformation       ATTableInfo;
typedef struct ATTableListSegments      ATTListSegs;

// The maximum number of BTrees allowed per table- if you need more, simply make this higher & recompile all
#define     AT_MAX_BTREES               (20)
// Type of index (primary = unique, secondary = non-unique)
#define     AT_BTREE_PRIMARY            (1)
#define     AT_BTREE_SECONDARY          (2)


// ****************************************************************************
// ****************************************************************************
//                           SHARED TABLE METHODS
// ****************************************************************************
// ****************************************************************************

// **************************************************************************** ATSharedTable
// NOTES:  Each thread should have its OWN open version of this class, then it will
// be threadsafe.  All runtime methods within the class are safe, however you should
// ALWAYS protect the actual creation/open process with a kernel lock or other method.
// I didn't think creating my own kernel locks were the friendliest choice here- so I wait
// until we are up in shared memory to start atomicity.  USE A VALID KILROY or you
// will toss the protection out the window.
//
// These tables grow infinitely, allocating in the chunk size passed in the init.
// They will reuse deleted record space, but they will not actually free the memory.
// If there is ever sufficient need for it, a runtime compaction routine would be pretty
// straightforward.  In my experience, it would be a very rare need in real world use.
// Note that occasional compaction can be done now by exporting/importing the table.

class   ATBTree;
class   ATSharedTable {                                                         // A shared memory table class
private:
    ULONG           Kilroy;                                                     // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
    volatile ATTupleCB *CursorCB;                                               // Ptr to the cursor's tuple CB
    long            CursorStatus;                                               // Cursor status indicator
    long            CursorBlock;                                                // The current block I am scrolling through
    long            CursorTupleNumber;                                          // My tuple position within this block
    volatile ATTBAH *CursorTBAH;                                                // Ptr to the cursor's block header
    volatile ATTableInfo *TB;                                                   // Pointer to this table info structure (located in shared memory)
    volatile ATTListSegs *DelSegs;                                              // Pointer to the delete list segments
    long            NumDelLists;                                                // Locally cached number of delete lists in the table
    long            NumAddLists;                                                // Locally cached number of add lists per block
    long            LastDelSegment;                                             // Last delete segment I used (just round robins things a bit)
    long            LastAddSegment;                                             // Last add segment I used (just round robins things a bit)
    long            MyNumberBlocks;                                             // The number of blocks that I have mapped access for- VERY IMPORTANT
    volatile ATTBAH *LastTBAH;                                                  // A ptr to the last TBAH allocated
    long            NumberBTrees;                                               // The number of BTrees that are registered with us
    ATBTree         *BTrees[AT_MAX_BTREES];                                     // All BTrees that are registered with us
    ATBTree         *PrimaryBTree;                                              // Pointer to the primary BTree
    long            NumberTBAHs;                                                // The number of TBAHs I have allocated in the current block
    long            NumberTBAHBlocks;                                           // The number of table allocation header BLOCKS I have mapped- not the headers, but the blocks that contain the headers
    long            AllocatedTBAHBlocks;                                        // The number of table allocation header BLOCKS I have allocated for- not the headers, but the blocks that contain the headers
    volatile ATTBAH **TBAHBlocks;                                               // The blocks containing individual TBAHs
    ATSharedMem     Mem;                                                        // If I created this, then it is the shared memory object for the first block.  Otherwise it is nothing.
    long            IAmCreator;                                                 // Flag to save whether or not I am the one who created the table

    ATTBAH          *GetNewTBAH();                                              // Routine to allocate and initialize the chain portion of a new TBAH
    void            ResetVariables();                                           // Internal routine to reset the variables
    volatile ATTBAH *GetHeaderPointer(                                          // Internal routine to get a given header pointer, and make sure we have access rights to it
                                                                                // We do it this way to help make sure we ALWAYS use locked access- never access the list directly!
                            int         Header                                  // Header to get
                            );
    void            SynchBlockAccess();                                         // Internal routine to synch the local process memory allocs to global memory allocs
    int             FirstDeterminePointers(                                     // An internal function to make sure everyone looks for structures in the same place when a table is first created/opened
                            int         Create                                  // Set to true if calling from Create(), false if calling from Open()
                            );
    int             DeterminePointers(                                          // An internal function to make sure everyone looks for structures in the same place
                            ATTuple     *Base,                                  // Ptr to the base of the block
                            ATTBAH      *TBAH                                   // Block header to initialize
                            );
    ATTBAH          *AddBlock();                                                // Internal call to add a block to the table- complete with header & pointers for the calling process, which it returns
                                                                                // CALLER SHOULD HOLD ANY NEEDED LOCKS
    ATTuple         *GetDeletedRecord();                                        // Internal call to try to reclaim deleted records- returns NULL if none found, otherwise a ptr to a reclaimed tuple
    volatile ATTupleCB *MakeCBPointer(                                          // Internal routine to return a CB pointer from a block/tuple combo
                            long        Block,                                  // Block to use
                            long        Tuple                                   // Tuple to use
                            );
    void            InitBlock(                                                  // Internal routine to init the structures in a new block
                            volatile ATTBAH  *TBAH                              // Block to init
                            );
public:
    ATSharedTable();
    ~ATSharedTable();

    // ****************************************************************************
    //                          CREATION/INITIALIZATION
    // ****************************************************************************
   int             CreateTable(                                                // Create a table
                            int         inKey,                                  // Systemwide unique IPC ID for this table- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                                                                                // !!IMPORTANT!! The key that you specify here will be incremented by 1 each time a new section needs to be allocated,
                                                                                // So be sure to leave adequate room to grow between your IPC ID's!!
                            int         inTupleSize,                            // Size of the tuples in bytes
                            int         inInitialAlloc,                         // Number of records to alloc initially
                            int         inGrowthAlloc,                          // Chunks of records to alloc as the table grows
                            int         inSoftWrites,                           // Set to true means that changes made to the table are queued until flushed, false means always flush changes to disk
                            int         inDelLists,                             // Number of delete lists to maintain for entire table- good default might be around 20- systems w/many procs might want even more
                            int         inAddLists,                             // Number of add lists to maintain for each block- good default might be around 5- systems w/many procs might want even more
                            ULONG       inKilroy                                // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                            );
    int             OpenTable(                                                  // Open a table that already exists
                            int         inKey,                                  // Systemwide unique IPC ID for this table
                            ULONG       inKilroy                                // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                            );
    int             CloseTable();                                               // Close the table- CHANGES WILL BE LOST IF NOT WRITTEN OUT!!!!
    int             ImportTable(                                                // Import a table from a disk file- this should be a binary file of fixed length records- NOT COMPATIBLE WITH INDICES!!!!!
                                                                                // Function may be called any number of times to add more files sequentially into the table
                            char        *inFileName,                            // Filename to load the table from
                            char        *inBuffer,                              // A buffer that may be used for file I/O
                            long        inBufferSize                            // The size of the I/O buffer provided (recommend at least 32-64k)
                            );
    int             ExportTable(                                                // Writes the table out as a binary file of fixed length records- NOT COMPATIBLE WITH INDICES!!!!!
                                                                                // This also compresses the table- removing any empty space.  THE STATE OF THE TABLE (changing data, etc.) IS UP TO THE CALLERS.
                            char        *FileName,                              // Filename to write the table to
                            char        *inBuffer,                              // A buffer that may be used for file I/O
                            long        inBufferSize                            // The size of the I/O buffer provided (recommend at least 32-64k)
                            );
    int             CreateFromFile(                                             // Load and Create a table from a disk file- this must be a file written previously by WriteTable
                            int         inKey,                                  // Systemwide unique IPC ID for this table- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                                                                                // !!IMPORTANT!! The key that you specify here will be incremented by 1 each time a new section needs to be allocated,
                                                                                // So be sure to leave adequate room to grow between your IPC ID's!!
                            ULONG       inKilroy,                               // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                            char        *inFileName,                            // Filename to load the table from
                            char        *inBuffer,                              // A buffer that may be used for file I/O
                            long        inBufferSize                            // The size of the I/O buffer provided (recommend at least 32-64k)
                            );
    int             LoadTable(                                                  // Load a table from a disk file- this must be a file written previously by WriteTable
                                                                                // THIS CALL SHOULD BE MADE RIGHT AFTER CREATE WITH MATCHING PARMS TO THE TABLE TO BE LOADED.  LOADING MISMATCHED TABLES COULD BE DISASTROUS (though it tries not to do it at all), AND FAILURES MAY LEAVE THE TABLE IN UNKNOWN STATE.
                            char        *inFileName,                            // Filename to load the table from
                            char        *inBuffer,                              // A buffer that may be used for file I/O
                            long        inBufferSize                            // The size of the I/O buffer provided (recommend at least 32-64k)
                            );
    int             WriteTable(                                                 // Write a table to a disk file
                            char        *inFileName                             // Filename to write the table to
                            );
    // ****************************************************************************
    //                          GENERAL USE
    // ****************************************************************************
    ATTuple         *AddTuple(                                                  // Add a tuple to the table & return a ptr to its location- this call automatically inserts the tuple into all associated BTrees
                                                                                // IMPORTANT: TUPLE IS ALWAYS RETURNED LOCKED- CALLER MUST FREE.  Returning them unlocked is just silly, really.  WAY too error prone.
                            void        *Tuple                                  // Ptr to the tuple to add to the table
                            );
    int             DeleteTuple();                                              // Delete the tuple at the current record position- this call automatically removes the tuple from all associated BTrees
                                                                                // WILL REFUSE TO WORK IF YOU DO NOT HAVE A LOCK ON THE TUPLE!
    ATTuple         *NextTuple();                                               // Return a ptr to the next tuple, no locking
                                                                                // NOTE: This operation refers to the table as a linear list, not a chronological one.  For example, a new tuple insert that reclaims a deleted spot may NOT be at the end of the list...
                                                                                // In other words- do NOT expect tuples back in the order you added them.  If you MUST have that for some reason, set all your allocs to 1 at create and then never delete any tuples.
    ATTuple         *PrevTuple();                                               // Return a ptr to the prev tuple, no locking
                                                                                // NOTE: This operation refers to the table as a linear list, not a chronological one.  For example, a new tuple insert that reclaims a deleted spot may NOT be at the end of the list...
    ATTuple         *GetTuple();                                                // Return the current tuple ptr w/no lock- a null return means the current tuple is not valid (may have been deleted)...
    int             ResetCursor();                                              // Resets the cursor to its "pristine" unused state
    ATTuple         *LockTuple();                                               // Locks the current tuple for changes, and return a ptr to it-will return null if tuple not valid
    ATTuple         *BounceLockTuple();                                         // Locks the current tuple for changes but returns immediately if lock already held, and return a ptr to it-will return null if tuple not valid
    int             UnlockTuple();                                              // Unlocks the current tuple
    ATTuple         *LockedGetTuple();                                          // Return the current tuple ptr locked- a null return means the current tuple is not valid (may have been deleted)...
    ATTuple         *LockedNextTuple();                                         // Return a ptr to the next tuple- however this call ALWAYS locks the call for safety before returning, so DON'T forget to free it
                                                                                // NOTE: This operation refers to the table as a linear list, not a chronological one.  For example, a new tuple insert that reclaims a deleted spot may NOT be at the end of the list...
                                                                                // In other words- do NOT expect tuples back in the order you added them.  If you MUST have that for some reason, set all your allocs to 1 at create and then never delete any tuples.
    ATTuple         *LockedPrevTuple();                                         // Return a ptr to the prev tuple- however this call ALWAYS locks the call for safety before returning, so DON'T forget to free it
                                                                                // NOTE: This operation refers to the table as a linear list, not a chronological one.  For example, a new tuple insert that reclaims a deleted spot may NOT be at the end of the list...

    // ****************************************************************************
    //                          EXPERT LEVEL
    // ****************************************************************************
    // ************ Likely that the following calls are only made by the system's indexes, but they *could* be used otherwise....
    ATTuple         *AllocateTuple();                                           // Reserve a tuple in the table & return a ptr to its location- exactly like AddTuple() except it does not copy the new tuple over for the caller
                                                                                // IMPORTANT: TUPLE IS ALWAYS RETURNED LOCKED- CALLER MUST FREE.  Returning them unlocked is just silly, really.  WAY too error prone.
                                                                                // UNLIKE AddTuple(), this call obviously cannot add the tuple to the BTrees automatically for you
    ATTuple         *SetTuple(                                                  // Set the cursor to a given spot and return the tuple ptr- returns null if setting not valid
                            long        Block,                                  // Specific block
                            long        Tuple                                   // Specific tuple
                            );
    ATTuple         *LocateTuple(                                               // Does NOT set the cursor, but just returns a ptr to the requested tuple w/no validity checking
                            long        Block,                                  // Specific block
                            long        Tuple                                   // Specific tuple
                            );
    ATTuple         *GetTupleLong(                                              // Returns long version of tuple information, including the tuple ptr itself
                            long        *Block,                                 // Address of long value to be set to the tuple's block location
                            long        *Tuple                                  // Address of long value to be set to the tuple's number intrablock
                            );
    int             RegisterBTree(                                              // Call to register a new BTree with the table
                            ATBTree     *inBTree,                               // Ptr to the BTree being registered
                            long        inIndexType                             // Either AT_BTREE_PRIMARY or AT_BTREE_SECONDARY
                            );
    int             UnRegisterBTree(                                            // Call to UNregister a BTree with the table
                            ATBTree     *inBTree,                               // Ptr to the BTree being unregistered
                            long        inIndexType                             // Either AT_BTREE_PRIMARY or AT_BTREE_SECONDARY
                            );
};

#endif

