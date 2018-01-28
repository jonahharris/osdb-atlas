// ****************************************************************************
// * table.cpp - The table code for Atlas.                                    *
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

/*
Low priority:  I'd like to implement an allocate-ahead strategy for the new
table page creation, since that should be the only possible high-contention
point left- right as the last page is just a couple of tuples short of full...
I haven't quite figured out what the threshold should be, or the cheapest way
to ensure the previous page.  Someday.
*/
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


// Maximum number of indexes per table
#define     AT_MAX_TABLE_INDEXES        10
// Maximum number of soft writes
#define     AT_MAX_SOFT_WRITES          64
// Allocation block chunk size for the table block headers
#define     AT_TH_ALLOC                 20
// Alloc size for the tracking list for the table block header allocations
#define     AT_TH_TRACKING_ALLOC        5
// Cursor status
#define     AT_CURSOR_BOT               0
#define     AT_CURSOR_NORMAL            1
#define     AT_CURSOR_EOT               2


struct ATTupleControlBlock {                                                    // A structure maintained by tuple to allow for efficient parallelism
    ATLOCK          ALock;                                                      // The tuple lock
    volatile long   Block;                                                      // Next block in the chain & status of tuple
    volatile long   Tuple;                                                      // Next tuple in the chain & status of tuple
};

struct ATTableAllocHeader {                                                     // Header for each alloc in LOCAL MEMORY
                                                                                // Contains only the local pointers we need
    volatile char        *Data;                                                 // Start of data in this block
    volatile ATTBAHG     *SharedHeader;                                         // Ptr to the shared header
    volatile ATTListSegs *AddSegs;                                              // Ptr to the add segments for the block
    volatile ATTBAH      *PrevHeader;                                           // Previous header in the list of blocks
    volatile ATTBAH      *NextHeader;                                           // Next header in the list of blocks
    volatile char        *Base;                                                 // Ptr to this block's allocation in process local memory
};

struct ATTableListSegments {                                                    // Struct used to house the list chains for the tables
    ATLOCK                  ALock;                                              // Lock for the segment
    volatile    long        Block;                                              // Last block in this list
    volatile    long        Tuple;                                              // Last tuple in this list
};

// Sentinel for a deleted tuple
#define     AT_DELETED_TUPLE    ((unsigned long)0xFFFFFFFF)
// Sentinel for a normal tuple
#define     AT_NORMAL_TUPLE     (-1)
// Sentinel for the end of a delete chain
#define     AT_CHAIN_END        (-2)
// Marker for a pristine, virgin, unspoiled touple
#define     AT_VIRGIN_TUPLE     (-3)


// ****************************************************************************
// ****************************************************************************
//                           SHAREDTABLE METHODS
// ****************************************************************************
// ****************************************************************************

// **************************************************************************** WriteTable
int ATSharedTable::WriteTable(                                                  // Write a table to a disk file
                            char        *inFileName                             // Filename to write the table to
                            ) {
    FILE    *Output;
    long    Written, i, NumberBlocks, SHMID;
    volatile ATTBAH     *TBAH;
    volatile ATTBAHG    *GTBAH;

    if ( !inFileName || !TB )                                                   // Simple checks
        return ATERR_BAD_PARAMETERS;

    if ( !(Output = fopen(inFileName, "wb") ) )                                 // Open up the file
        return ATERR_FILE_ERROR;

    if ( !(Written = fwrite(AT_ATLAS_VERSION, strlen(AT_ATLAS_VERSION), 1,      // Write out the table version
        Output) ) )
        goto file_error;

    if ( !(Written = fwrite((void *)TB, sizeof(ATTableInfo), 1, Output) ) )     // Write out the table information block
        goto file_error;

    if ( !(Written = fwrite((void *)DelSegs, (sizeof(ATTListSegs) * TB->NumDelLists), // Write out the delete tracking lists
        1, Output) ) )
        goto file_error;

    TBAH = GetHeaderPointer(0);                                                 // The first blocks are a special case, so let's do them first...
    GTBAH = TBAH->SharedHeader;
    if ( !(Written = fwrite((void*)GTBAH, sizeof(ATTBAHG), 1, Output) ) )       // Write out the global header for this block
        goto file_error;

    if ( !(Written = fwrite((void*)TBAH->AddSegs,                               // Now write out the add lists
        (sizeof(ATTListSegs) * TB->NumAddLists), 1, Output) ) )
        goto file_error;

    if ( !(Written = fwrite((void*)TBAH->Data,                                  // Now write out the tuples for this block
        (GTBAH->TuplesAllocated * TB->TrueTupleSize), 1, Output) ) )
        goto file_error;

    for ( i = 1; i < TB->NumberBlocks; ++i ) {                                  // Now let's write out all the remaining blocks
        TBAH = GetHeaderPointer(i);                                             // Get pointers to the block
        GTBAH = TBAH->SharedHeader;

        if ( !(Written = fwrite((void*)GTBAH, sizeof(ATTBAHG), 1, Output) ) )   // Write out the global header for this block
            goto file_error;

        if ( !(Written = fwrite((void*)TBAH->AddSegs,                           // Now write out the add lists
            (sizeof(ATTListSegs) * TB->NumAddLists), 1, Output) ) )
            goto file_error;

        if ( !(Written = fwrite((void*)TBAH->Data,                              // Now write out the tuples for this block
            (GTBAH->TuplesAllocated * TB->TrueTupleSize), 1, Output) ) )
            goto file_error;
    }

    fclose(Output);
    return ATERR_SUCCESS;

file_error:
    fclose(Output);
    return ATERR_FILE_ERROR;
}
// **************************************************************************** LoadTable
int ATSharedTable::LoadTable(                                                   // Load a table from a disk file- this must be a file written previously by WriteTable
                                                                                // THIS CALL SHOULD BE MADE RIGHT AFTER CREATE WITH MATCHING PARMS TO THE TABLE TO BE LOADED.  LOADING MISMATCHED TABLES COULD BE DISASTROUS.
                            char        *inFileName,                            // Filename to load the table from
                            char        *inBuffer,                              // A buffer that may be used for file I/O
                            long        inBufferSize                            // The size of the I/O buffer provided (recommend at least 32-64k)
                            ) {
    FILE    *Input;
    long    Read, i, NumberBlocks, SHMID, ac, cz, OldKey, OldInstances;
    volatile ATTBAH     *TBAH;
    volatile ATTBAHG    *GTBAH, OrigGTBAH;
    char    Scratch[25];
    ATTupleCB *CB;
    ATTableInfo NTB;

    if ( !inFileName || !inBuffer || inBufferSize < sizeof(ATTableInfo) ||      // Basic checks
        inBufferSize < sizeof(ATTBAHG) || !TB )
        return ATERR_BAD_PARAMETERS;

    if ( !(Input = fopen(inFileName, "rb") ) )                                  // Open up the file
        return ATERR_NOT_FOUND;

    if ( !(Read = fread(Scratch, strlen(AT_ATLAS_VERSION), 1, Input) ) )        // Read in the table version
        goto file_error;

    if ( strncmp(Scratch, AT_ATLAS_VERSION, strlen(AT_ATLAS_VERSION)) )         // If we can't safely read in this file version
        goto mismatch_error;

    if ( !(Read = fread((void*)&NTB, sizeof(ATTableInfo), 1, Input) ) )         // Read in the table information block
        goto file_error;

    if (    NTB.TrueTupleSize != TB->TrueTupleSize ||                           // Let's make a few comparisons to ensure that this will be a safe match
            NTB.InitialAlloc != TB->InitialAlloc ||
            NTB.NumAddLists != TB->NumAddLists ||
            NTB.NumDelLists != TB->NumDelLists )
        goto mismatch_error;

    OldKey = TB->Key;                                                           // Save the existing key...
    OldInstances = TB->InstanceCount;                                           // Save the old instance count
    memcpy((void*)TB, (void*)&NTB, sizeof(ATTableInfo));                        // Copy over the new block
    TB->Key = OldKey;                                                           // Restore the key ID
    TB->InstanceCount = OldInstances;                                           // Restore the key ID

    if ( !(Read = fread((void *)DelSegs, (sizeof(ATTListSegs) * TB->NumDelLists), 1, Input) ) )// Read in the delete tracking lists
        goto file_error;

    cz = TB->NumAddLists;                                                       // Let the optimizer cache the value
    for ( ac = 0; ac < cz; ++ac )                                               // Loop thru all of the del segments
        DelSegs[ac].ALock = 0;                                                  // And make sure the locks are cleared

    NumberBlocks = TB->NumberBlocks;                                            // We need to fool the AddBlock routine...
    TB->NumberBlocks = 1;

    TBAH = GetHeaderPointer(0);                                                 // The first header will already be allocated
    GTBAH = TBAH->SharedHeader;
    memcpy((void*)&OrigGTBAH, (void*)GTBAH, sizeof(ATTBAHG));                   // Save a copy of the original global header
    if ( !(Read = fread((void*)GTBAH, sizeof(ATTBAHG), 1, Input) ) )            // Read in the global header for this block
        goto file_error;
    if ( GTBAH->TuplesAllocated != TB->InitialAlloc ) {                         // Make sure this is a matching table
        fclose(Input);
        return ATERR_BAD_PARAMETERS;
    }
    GTBAH->SHMID = OrigGTBAH.SHMID;                                             // Save some stuff from the original block...

    if ( !(Read = fread((void*)TBAH->AddSegs,                                   // Now read in the add lists
        (sizeof(ATTListSegs) * TB->NumAddLists), 1, Input) ) )
        goto file_error;

    cz = TB->NumAddLists;                                                       // Let the optimizer cache the value
    for ( ac = 0; ac < TB->NumAddLists; ++ac )                                  // Loop thru all of the add segments
        TBAH->AddSegs[ac].ALock = 0;                                            // And make sure the locks are cleared

    if ( !(Read = fread((void*)TBAH->Data,                                      // Now read in the tuples for this block
        (GTBAH->TuplesAllocated * TB->TrueTupleSize), 1, Input) ) )
        goto file_error;

    CB = (ATTupleCB *)TBAH->Data;                                               // Set a ptr to the start of the data in the block
    cz = GTBAH->TuplesAllocated;                                                // Let the optimizer cache the value
    for ( ac = 0; ac < cz; ++ac ) {                                             // Loop thru all of the tuples
        if (CB->ALock > 0) {                                                    // As long as it could be a valid kilroy
            if (CB->ALock != AT_DELETED_TUPLE) CB->ALock = 0;                   // And it isn't the delete flag, then make sure it is clear
        }
        CB = (ATTupleCB *)(((char*)CB) + TB->TrueTupleSize);                    // Move to the next tuple
    }

    for ( i = 1; i < NumberBlocks; ++i ) {                                      // Now let's read in all the remaining blocks
        if ( !(TBAH = AddBlock() ) )                                             // Add another block to the table
            goto mem_error;
        GTBAH = TBAH->SharedHeader;
        SHMID = GTBAH->SHMID;                                                   // Save real SHMID

        if ( !(Read = fread((void*)GTBAH, sizeof(ATTBAHG), 1, Input) ) )        // Read in the global header for this block
            goto file_error;
        if ( GTBAH->TuplesAllocated != TB->GrowthAlloc ) {                      // Make sure this is a matching table
            fclose(Input);
            return ATERR_BAD_PARAMETERS;
        }
        GTBAH->SHMID = SHMID;                                                   // Write real SHMID

        if ( !(Read = fread((void*)TBAH->AddSegs,                               // Now read in the add lists
            (sizeof(ATTListSegs) * TB->NumAddLists), 1, Input) ) )
            goto file_error;

        cz = TB->NumAddLists;                                                   // Let the optimizer cache the value
        for ( ac = 0; ac < TB->NumAddLists; ++ac )                              // Loop thru all of the add segments
            TBAH->AddSegs[ac].ALock = 0;                                        // And make sure the locks are cleared

        if ( !(Read = fread((void*)TBAH->Data,                                  // Now read in the tuples for this block
            (GTBAH->TuplesAllocated * TB->TrueTupleSize), 1, Input) ) )
            goto file_error;

        CB = (ATTupleCB *)TBAH->Data;                                           // Set a ptr to the start of the data in the block
        cz = GTBAH->TuplesAllocated;                                            // Let the optimizer cache the value
        for ( ac = 0; ac < cz; ++ac ) {                                         // Loop thru all of the tuples
            if (CB->ALock > 0) {                                                // As long as it could be a valid kilroy
                if (CB->ALock != AT_DELETED_TUPLE) CB->ALock = 0;               // And it isn't the delete flag, then make sure it is clear
            }
            CB = (ATTupleCB *)(((char*)CB) + TB->TrueTupleSize);                // Move to the next tuple
        }
    }

    fclose(Input);
    return ATERR_SUCCESS;

file_error:
    fclose(Input);
    return ATERR_FILE_ERROR;
mem_error:
    fclose(Input);
    return ATERR_OUT_OF_MEMORY;
mismatch_error:
    fclose(Input);
    return ATERR_UNSAFE_OPERATION;
}
// **************************************************************************** CreateFromFile
int ATSharedTable::CreateFromFile(                                              // Load and Create a table from a disk file- this must be a file written previously by WriteTable
                            int         inKey,                                  // Systemwide unique IPC ID for this table- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                                                                                // !!IMPORTANT!! The key that you specify here will be incremented by 1 each time a new section needs to be allocated,
                                                                                // So be sure to leave adequate room to grow between your IPC ID's!!
                            ULONG       inKilroy,                               // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                            char        *inFileName,                            // Filename to load the table from
                            char        *inBuffer,                              // A buffer that may be used for file I/O
                            long        inBufferSize                            // The size of the I/O buffer provided (recommend at least 32-64k)
                            ) {
    FILE                *Input;
    long                Read, i;
    char                Scratch[25];
    ATTableInfo         Info;

    if ( !inFileName || !inBuffer || inBufferSize < sizeof(ATTableInfo) ||
        inBufferSize < sizeof(ATTBAHG) )
        return ATERR_BAD_PARAMETERS;

    if ( !(Input = fopen(inFileName, "rb") ) )                                  // Open up the file
        return ATERR_NOT_FOUND;

    if ( !(Read = fread(Scratch, strlen(AT_ATLAS_VERSION), 1, Input) ) )        // Read in the table version
        goto file_error;

    if ( strncmp(Scratch, AT_ATLAS_VERSION, strlen(AT_ATLAS_VERSION)) )         // If we can't safely read in this file version
        goto file_error;

    if ( !(Read = fread((void*)&Info, sizeof(ATTableInfo), 1, Input) ) )        // Read in the table information block
        goto file_error;

    fclose(Input);                                                              // Done with this now

    if ( (i = CreateTable(                                                      // Create the table from the stored info block
                            inKey,                                              // Systemwide unique IPC ID for this table- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                            Info.TupleSize,                                     // Size of the tuples in bytes
                            Info.InitialAlloc,                                  // Number of records to alloc initially
                            Info.GrowthAlloc,                                   // Chunks of records to alloc as the table grows
                            Info.SoftWrites,                                    // Set to true means that changes made to the table are queued until flushed, false means always flush changes to disk
                            Info.NumDelLists,                                   // Number of delete lists to maintain for entire table- good default might be around 20- systems w/many procs might want even more
                            Info.NumAddLists,                                   // Number of add lists to maintain for each block- good default might be around 5- systems w/many procs might want even more
                            inKilroy                                            // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                            )) != ATERR_SUCCESS )
        return i;

    return LoadTable(inFileName, inBuffer, inBufferSize);                       // Now load it up

file_error:
    fclose(Input);
    return ATERR_FILE_ERROR;
}
// **************************************************************************** ExportTable
int ATSharedTable::ExportTable(                                                 // Writes the table out as a binary file of fixed length records- NOT COMPATIBLE WITH INDICES!!!
                                                                                // This also compresses the table- removing any empty space
                            char        *inFileName,                            // Filename to write the table to
                            char        *inBuffer,                              // A buffer that may be used for file I/O
                            long        inBufferSize                            // The size of the I/O buffer provided (recommend at least 32-64k)
                            ) {
    FILE    *Output;
    int     Loaded = 0;
    long    NumberRecords, FileLength, BufferRecords, i;
    ATTuple *Tuple;

    if ( !inFileName || !inBuffer || inBufferSize < TB->TupleSize)
        return ATERR_BAD_PARAMETERS;

    BufferRecords = inBufferSize / TB->TupleSize;                               // How many records can we buffer
    if ( !(Output = fopen(inFileName, "wb")) )                                  // Open the file
        return ATERR_NOT_FOUND;

    ResetCursor();                                                              // Set the cursor to the start of the table...
    while ( (Tuple = NextTuple()) ) {                                           // Loop thru all the tuples
        memcpy( (inBuffer + (TB->TupleSize * Loaded)), (void*)Tuple, TB->TupleSize);// Copy tuples into the buffer one by one
        ++Loaded;
        if ( Loaded == BufferRecords ) {                                        // When we have a buffer full
            fwrite(inBuffer, (TB->TupleSize * Loaded), 1, Output);              // Write out the buffer
            Loaded = 0;
        }
    }
    if ( Loaded ) {
        fwrite(inBuffer, (TB->TupleSize * Loaded), 1, Output);                  // Write out the last partial buffer load
        Loaded = 0;
    }
    fclose(Output);

    return ATERR_SUCCESS;
}
// **************************************************************************** ResetCursor
int  ATSharedTable::ResetCursor() {                                             // Resets the cursor to its "pristine" unused state
    CursorBlock = 0;
    CursorTupleNumber = 0;
    CursorCB = NULL;
    CursorTBAH = NULL;
    CursorStatus = AT_CURSOR_BOT;
    return ATERR_SUCCESS;
}
// **************************************************************************** GetTupleLong
ATTuple *ATSharedTable::GetTupleLong(                                           // Returns long version of tuple information, including the tuple ptr itself
                            long        *Block,                                 // Long value to set to the tuple's block location
                            long        *Tuple                                  // Long value to set to the tuple's number intrablock
                            ) {
    *Block = CursorBlock;
    *Tuple = CursorTupleNumber;
    return (ATTuple*)(CursorCB + 1);
}
// **************************************************************************** SetTuple
ATTuple *ATSharedTable::SetTuple(                                               // Set the cursor to a given spot and return the tuple ptr- returns null if setting not valid
                            long        Block,                                  // Specific block
                            long        Tuple                                   // Specific tuple
                            ) {
    if ( Block < 0 || Tuple < 0 ) return NULL;                                  // Hallmark of non locking access gone bad
    CursorTBAH = GetHeaderPointer(Block);                                       // Adjust the cursor
    CursorTupleNumber = Tuple;
    CursorBlock = Block;
    CursorCB = (ATTupleCB*)(CursorTBAH->Data +                                  // Figure out where the tuple is
            (TB->TrueTupleSize * CursorTupleNumber) );
    CursorStatus = AT_CURSOR_NORMAL;

    if ( CursorCB->ALock != AT_DELETED_TUPLE && CursorCB->Block == AT_NORMAL_TUPLE)// As long as this is a safe tuple
        return (ATTuple *)(CursorCB + 1);
    else
        return NULL;
}
// **************************************************************************** LocateTuple
ATTuple *ATSharedTable::LocateTuple(                                             // Does NOT set the cursor, but just returns a ptr to the requested tuple w/no validity checking
                            long        Block,                                  // Specific block
                            long        Tuple                                   // Specific tuple
                            ) {
    if ( Block < 0 || Tuple < 0 ) return NULL;                                  // Hallmark of non locking access gone bad
    volatile    ATTBAH  *TBAH = GetHeaderPointer(Block);                        // Get a ptr to the block
    volatile    ATTupleCB   *CB;
    CB = (ATTupleCB*)(TBAH->Data +                                              // Figure out where the tuple is
                (TB->TrueTupleSize * Tuple) );
    return (ATTuple *)(CB + 1);
}
// **************************************************************************** UnlockTuple
int ATSharedTable::UnlockTuple() {                                              // Unlocks the current tuple
    if ( CursorCB && CursorCB->ALock == Kilroy )
        return ATFreeSpinLock(Kilroy, &(CursorCB->ALock));
}
// **************************************************************************** LockTuple
ATTuple *ATSharedTable::LockTuple() {                                           // Locks the current tuple for changes, and return a ptr to it-will return null if tuple not valid
    long    NumberAttempts = 0;                                                 // Number of attempts made to get the lock
    ATTuple *Return;

    // Unfortunately, since the ALock field is also used as a flag, I can't simply call GetLock- it might block forever on a deleted record, etc...
retry:
    if ( (Return = BounceLockTuple()) ) {                                       // Try to bounce the lock
        return Return;                                                          // Just return on success
    }
    if ( CursorCB && CursorCB->ALock != AT_DELETED_TUPLE &&                     // As long as the tuple continues to look normal
            CursorCB->Block == AT_NORMAL_TUPLE) {
        ATSpinLockArbitrate(NumberAttempts);                                    // Behave intelligently (hopefully) while spinning
        NumberAttempts++;                                                       // Increment our number of attempts
        goto retry;                                                             // And just keep trying
    }
    return NULL;                                                                // The tuple must no longer look normal
}
// **************************************************************************** BounceLockTuple
ATTuple *ATSharedTable::BounceLockTuple() {                                     // Locks the current tuple for changes, and return a ptr to it-will return null if tuple not valid
    int Result;
    if ( CursorCB && CursorCB->ALock != AT_DELETED_TUPLE &&
            CursorCB->Block == AT_NORMAL_TUPLE) {
        if ( (Result = ATBounceSpinLock(Kilroy, &(CursorCB->ALock)) ==          // Try to lock it
            ATERR_SUCCESS) ) {
            return (ATTuple *)(CursorCB + 1);                                   // Return the ptr
        }
        else
            return NULL;                                                        // Could not get a lock
    }
    return NULL;                                                                // No cursor setup has taken place
}
// **************************************************************************** GetTuple
ATTuple *ATSharedTable::GetTuple() {                                            // Return the current tuple ptr w/no lock- a null return means the current tuple is not valid (may have been deleted)...
    if ( CursorCB && CursorCB->ALock != AT_DELETED_TUPLE &&                     // As long as this is a safe tuple
            CursorCB->Block == AT_NORMAL_TUPLE) {
        return (ATTuple *)(CursorCB + 1);
    }
    return NULL;                                                                // No cursor setup has taken place
}
// **************************************************************************** LockedGetTuple
ATTuple *ATSharedTable::LockedGetTuple() {                                      // Return the current tuple ptr locked- a null return means the current tuple is not valid (may have been deleted)...
    return LockTuple();                                                         // This is really another way to call LockTuple
}
// **************************************************************************** PrevTuple
// In general, the Prev & Next tuples will never be speed demons on sparse tables- that is what indexes are for
ATTuple *ATSharedTable::PrevTuple() {                                           // Return a ptr to the previous tuple
    volatile    ATTupleCB   *CB;
    long        OrigTuple = CursorTupleNumber;                                  // Save for restore
    long        OrigCursor = CursorBlock;
    volatile    ATTBAH      *OrigTBAH;

    ( CursorTBAH ) ? OrigTBAH = CursorTBAH:CursorTBAH = GetHeaderPointer(CursorBlock);

    if ( CursorStatus == AT_CURSOR_NORMAL )                                     // In normal mode...
        CursorTupleNumber--;                                                    // Just dec the cursor #
    else {
        if ( CursorStatus == AT_CURSOR_EOT )                                    // At end of table...
            CursorStatus = AT_CURSOR_NORMAL;
        else                                                                    // At beginning of table
            return NULL;
    }

retry:
    if ( CursorTupleNumber >= 0 ) {                                             // As long as there are still tuples in this block
        CB =    (ATTupleCB*)(CursorTBAH->Data +                                 // Figure out where the tuple is
                (TB->TrueTupleSize * CursorTupleNumber) );
        if ( CB->ALock != AT_DELETED_TUPLE && CB->Block == AT_NORMAL_TUPLE) {   // As long as this is a safe tuple
            CursorCB = CB;                                                      // Save this for speed later
            return (ATTuple*)(CB + 1);                                          // Return the ptr
        }
        CursorTupleNumber--;                                                    // Was a delete, so let's just move to the next one
        goto retry;
    }
    if ( CursorBlock >= 1 ) {                                                   // Are there any more block to check?
        CursorBlock--;                                                          // Move to the next block
        CursorTBAH = GetHeaderPointer(CursorBlock);                             // Get ptr to current block
        CursorTupleNumber = CursorTBAH->SharedHeader->TuplesAllocated - 1;      // Start at the last tuple (previous pages will be packed before additional adds...)
        goto retry;
    }
    CursorTupleNumber = OrigTuple;                                              // We have reached the start of the table
    CursorBlock = OrigCursor;
    CursorTBAH = OrigTBAH;
    return NULL;
}
// **************************************************************************** LockedPrevTuple
ATTuple *ATSharedTable::LockedPrevTuple() {                                     // Return a ptr to the previous tuple, but lock before returning to caller
    ATTuple *Return;

    while ( PrevTuple() ) {                                                     // As long as I am getting prev tuples returned
        if ( Return = LockTuple() ) {                                           // If I can get a lock on it
            return Return;                                                      // Then I am done
        }
    }
    return NULL;                                                                // Could not find any prev tuples
}
// **************************************************************************** NextTuple
// In general, the Prev & Next tuples will never be speed demons on sparse tables- that is what indexes are for
ATTuple *ATSharedTable::NextTuple() {                                           // Return a ptr to the next tuple
                                                                                // NOTE: This operation refers to the table as a linear list, not a chronological one.  For example, a new tuple insert that reclaims a deleted spot may NOT be at the end of the list...
    volatile    ATTupleCB   *CB;
    long        OrigTuple = CursorTupleNumber;                                  // Save for restore
    long        OrigCursor = CursorBlock;
    volatile    ATTBAH      *OrigTBAH;
    long        i, MaxTuples;

    ( CursorTBAH ) ? OrigTBAH = CursorTBAH:CursorTBAH = GetHeaderPointer(CursorBlock);

    if ( CursorStatus == AT_CURSOR_NORMAL )                                     // In normal mode...
        CursorTupleNumber++;                                                    // Just inc the cursor #
    else {
        if ( CursorStatus == AT_CURSOR_BOT )                                    // At beginning of table...
            CursorStatus = AT_CURSOR_NORMAL;
        else                                                                    // At end of table
            return NULL;
    }

retry:
    if ( CursorTBAH->NextHeader )                                               // If there is a next page, this is easy
        MaxTuples = CursorTBAH->SharedHeader->TuplesAllocated;
    else {                                                                      // We are in the last page, so number allocated is unknown
        MaxTuples = 0;                                                          // This gives poor, but fairly consistent performance (imagine a page with 1,000,000 tuples as a bad case...)
        for ( i = 0; i < NumAddLists; ++i) {                                    // The top tuple can be no more than the highest number found in these lists
            if (CursorTBAH->AddSegs[i].Tuple > MaxTuples)
                MaxTuples = CursorTBAH->AddSegs[i].Tuple;
            if (CursorTBAH->AddSegs[i].Tuple == AT_NORMAL_TUPLE ) {
                MaxTuples = CursorTBAH->SharedHeader->TuplesAllocated;
                break;
            }
        }
    }

    if ( CursorTupleNumber < MaxTuples ) {                                      // As long as there are still tuples in this block
        CB =    (ATTupleCB*)(CursorTBAH->Data +                                 // Figure out where the tuple is
                (TB->TrueTupleSize * CursorTupleNumber) );
        if ( CB->ALock != AT_DELETED_TUPLE && CB->Block == AT_NORMAL_TUPLE) {   // As long as this is a safe tuple
            CursorCB = CB;                                                      // Save this for speed later
            return (ATTuple*)(CB + 1);                                          // Return the ptr
        }
        CursorTupleNumber++;                                                    // Was a delete, so let's just move to the next one
        goto retry;
    }
    if ( CursorBlock < (TB->NumberBlocks - 1) ) {                               // Are there any more block to check?
        CursorBlock++;                                                          // Move to the next block
        CursorTupleNumber = 0;                                                  // Start at first tuple
        CursorTBAH = GetHeaderPointer(CursorBlock);                             // Get ptr to current block
        goto retry;
    }
    CursorTupleNumber = OrigTuple;                                              // We have reached the end of the table
    CursorBlock = OrigCursor;
    CursorTBAH = OrigTBAH;
    CursorStatus = AT_CURSOR_EOT;
    return NULL;
}
// **************************************************************************** LockedNextTuple
ATTuple *ATSharedTable::LockedNextTuple() {                                     // Return a ptr to the previous tuple, but lock before returning to caller
    ATTuple *Return;

    while ( NextTuple() ) {                                                     // As long as I am getting next tuples returned
        if ( Return = LockTuple() ) {                                           // If I can get a lock on it
            return Return;                                                      // Then I am done
        }
    }
    return NULL;                                                                // Could not find any next tuples
}
// **************************************************************************** ImportTable
int ATSharedTable::ImportTable(                                                 // Import a table from a disk file- this should be a binary file of fixed length records
                                                                                // Function may be called any number of times to add more files sequentially into the table
                            char        *inFileName,                            // Filename to load the table from
                            char        *inBuffer,                              // A buffer that may be used for file I/O
                            long        inBufferSize                            // The size of the I/O buffer provided (recommend at least 32-64k)
                            ) {
    int     Loaded;
    FILE    *Input;
    long    NumberRecords, FileLength, BufferRecords, i;

    if ( !inFileName || !inBuffer || inBufferSize < TB->TupleSize )
        return ATERR_BAD_PARAMETERS;

    BufferRecords = inBufferSize / TB->TupleSize;                               // How many records can we buffer
    if ( !(Input = fopen(inFileName, "rb")) )                                   // Open the file
        return ATERR_NOT_FOUND;

    while ( (Loaded = fread(inBuffer, TB->TupleSize, BufferRecords, Input)) ) { // Read the file in, a buffer load at a time
        for ( i = 0; i < Loaded; ++i) {                                         // Loop thru all the records
            if( !AddTuple((inBuffer + (i * TB->TupleSize))) )                   // Load the tuple
                return ATERR_OUT_OF_MEMORY;
            UnlockTuple();                                                      // Unlock it
        }
    }
    fclose(Input);

    return ATERR_SUCCESS;
}
// **************************************************************************** Constructor
ATSharedTable::ATSharedTable() {
    ResetVariables();
}
// **************************************************************************** ResetVariables
void    ATSharedTable::ResetVariables() {                                       // Internal routine to reset the variables
    TB = NULL;
    IAmCreator = Kilroy = 0;
    TBAHBlocks = NULL;
    LastTBAH = NULL;
    NumberTBAHs = 0;

    CursorTupleNumber = 0;
    CursorBlock = 0;
    CursorCB = NULL;
    CursorTBAH = NULL;
    CursorStatus = AT_CURSOR_BOT;
    LastDelSegment = LastAddSegment = 0;
    DelSegs = NULL;
    NumDelLists = 0;
    NumAddLists = 0;

    MyNumberBlocks= 0;

    NumberBTrees = 0;
    for ( int i = 0; i < AT_MAX_BTREES; ++i) {
        BTrees[i] = NULL;
    }
    PrimaryBTree = NULL;

    NumberTBAHBlocks = AllocatedTBAHBlocks = 0;
}

// **************************************************************************** Destructor
ATSharedTable::~ATSharedTable() {
    if ( TB ) CloseTable();
}
// **************************************************************************** CreateTable
int ATSharedTable::CreateTable(                                                 // Create a table
                            int         inKey,                                  // Systemwide unique IPC ID for this table- BECOMES A SHARED MEMORY KEY as well, so, again, it must be system wide IPC unique
                                                                                // !!IMPORTANT!! The key that you specify here will be incremented by 1 each time a new section needs to be allocated,
                                                                                // So be sure to leave adequate room to grow between your IPC ID's!!
                            int         inTupleSize,                            // Size of the tuples in bytes
                            int         inInitialAlloc,                         // Number of records to alloc initially
                            int         inGrowthAlloc,                          // Chunks of records to alloc as the table grows
                            int         inSoftWrites,                           // Set to true means that changes made to the table are queued until flushed, false means always flush changes to disk
                            int         inDelLists,                             // Number of delete lists to maintain for entire table- good default might be around 20- systems w/many procs might want even more
                            int         inAddLists,                             // Number of add lists to maintain for each page- good default might be around 5- systems w/many procs might want even more
                            ULONG       inKilroy                                // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                            ) {
    long            StartAlloc, Result, i, Count, Seg, Add;
    unsigned long   TrueTupleSize;
    volatile ATTBAH *FirstHeader;
    ATTupleCB       *CB;

    if ( !inKey || !inTupleSize || !inInitialAlloc || !inGrowthAlloc || !inKilroy ||// Simple error checks
        inKilroy == AT_DELETED_TUPLE || (inTupleSize < 1) || !inDelLists || !inAddLists )
        return ATERR_BAD_PARAMETERS;

    TrueTupleSize = inTupleSize + sizeof(ATTupleCB);                            // Figure out my true tuple size
    TrueTupleSize = ((TrueTupleSize + (AT_MEM_ALIGN - 1)) & ~(AT_MEM_ALIGN - 1));

    StartAlloc =    (TrueTupleSize * inInitialAlloc) + sizeof(ATTableInfo) +    // Figure out how much memory we need to allocate
                    sizeof(ATTBAHG) + (sizeof(ATTListSegs) * inDelLists) +
                    (sizeof(ATTListSegs) * inAddLists) +
                    (AT_MEM_ALIGN * 5);                                         // The 5 is four structs + data align

    if ( (Result = Mem.CreateSharedMem(inKey, StartAlloc)) != ATERR_SUCCESS)    // Try to create the shared memory
        return ATERR_OUT_OF_MEMORY;
    NumDelLists = inDelLists;                                                   // Need to set this before calling FirstDeterminePointers
    NumAddLists = inAddLists;                                                   // Need to set this before calling FirstDeterminePointers
    if ( (Result = FirstDeterminePointers(1)) != ATERR_SUCCESS )                // Set up my local space pointers
        return ATERR_OUT_OF_MEMORY;

    TB->Key =           inKey;                                                  // Init the table info struct
    TB->TupleSize =     inTupleSize;
    TB->TrueTupleSize = TrueTupleSize;
    TB->InitialAlloc =  inInitialAlloc;
    TB->GrowthAlloc =   inGrowthAlloc;
//    TB->SoftWrites =    inSoftWrites;
    TB->InstanceCount = 1;
    TB->NumberBlocks =  1;
//    TB->ALock =         0;
    TB->NumDelLists =   inDelLists;
    TB->NumAddLists =   inAddLists;

    FirstHeader = TBAHBlocks[0];                                                // Init the first header struct in local memory

    FirstHeader->SharedHeader->TuplesAllocated =    inInitialAlloc;
    FirstHeader->SharedHeader->NumberTuples =       0;
    FirstHeader->SharedHeader->ALock =              0;
    FirstHeader->SharedHeader->SHMID =              Mem.GetSystemID();
    Mem.FreeThisInstanceOnly();                                                 // Then tell my object not to track it anymore (only the creator needs to track it)
    FirstHeader->SharedHeader->NextSHMID =          0;
    FirstHeader->SharedHeader->ThisBlock =          0;

    Kilroy = inKilroy;
    MyNumberBlocks = 1;                                                         // I have local access to the first block
    IAmCreator = 1;                                                             // Remember that I created this guy

    for ( i = 0; i < NumDelLists; ++i) {                                        // Init the segments for delete tracking
        DelSegs[i].ALock =  0;
        DelSegs[i].Block =  AT_NORMAL_TUPLE;
        DelSegs[i].Tuple =  AT_NORMAL_TUPLE;
    }

    InitBlock(FirstHeader);                                                     // Init the structures in the new block

    return ATERR_SUCCESS;
}
// **************************************************************************** OpenTable
int ATSharedTable::OpenTable(                                                   // Open a table that already exists
                            int         inKey,                                  // Systemwide unique IPC ID for this table
                            ULONG       inKilroy                                // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                            ) {
    int     Result;

    if ( !inKilroy || inKilroy == AT_DELETED_TUPLE)
        return ATERR_BAD_PARAMETERS;

    // When we create a table, our key is also our shared memory key, so we just need to access that block of shared memory
    if ( (Result = Mem.AttachSharedMem(inKey)) != ATERR_SUCCESS )
        return ATERR_NOT_FOUND;
    if ( (Result = FirstDeterminePointers(0)) != ATERR_SUCCESS )                // Set up my local space pointers
        return ATERR_OUT_OF_MEMORY;

    ATAtomicInc(&(TB->InstanceCount));                                          // Inc the instance count for the table
    Kilroy = inKilroy;
    NumDelLists = TB->NumDelLists;
    NumAddLists = TB->NumAddLists;
    MyNumberBlocks = 1;                                                         // I have local access to the first block
    Mem.FreeThisInstanceOnly();                                                 // Then tell my object not to track it anymore (only the creator needs to track it)

    return ATERR_SUCCESS;
}
// **************************************************************************** AddTuple
ATTuple *ATSharedTable::AddTuple(                                               // Add a tuple to the table & return a ptr to its location
                            void        *Tuple                                  // Ptr to the tuple to add to the table
                            ) {
    ATTuple *Insert;
    int     Result, i;

    if ( (Insert = AllocateTuple()) ) {                                         // As long as the allocation goes okay...
        memcpy((void*)Insert, (void*)Tuple, TB->TupleSize);                     // Copy in the tuple
        if ( PrimaryBTree ) {                                                   // If there is a primary BTree on the table
            if ( (Result = PrimaryBTree->InsertTuple((void*)Tuple,              // Try to insert the key
                    CursorBlock, CursorTupleNumber)) != ATERR_SUCCESS) {
                DeleteTuple();                                                  // A failure here means we have to delete this tuple
                return NULL;
            }
        }
        if ( NumberBTrees ) {                                                   // If there are any secondary keys
            for ( i = 0; i < NumberBTrees; ++i ) {                              // For any and all BTrees associated
                if ( (Result = (BTrees[i])->InsertTuple((void *)Tuple,          // Insert the tuple into the BTree
                    CursorBlock, CursorTupleNumber)) != ATERR_SUCCESS) {
                DeleteTuple();                                                  // A failure here means we have to delete this tuple
                return NULL;
                }
            }
        }
    }
    return Insert;
}
// **************************************************************************** AllocateTuple
/*  The add lists are stored by page.  At some point I would like to come back and improve
the behavior as the page is getting nearly empty, since contention might be high.  Maybe go
ahead and create a new page, and then sporadically (maybe each time seg wraps?) look
in the prev page to make sure we cleaned out all the adds- or maybe store a flag- or something...
*/
ATTuple *ATSharedTable::AllocateTuple() {                                       // Reserve a tuple in the table & return a ptr to its location- exactly like AddTuple() except it does not copy the new tuple over for the caller
                                                                                // IMPORTANT: TUPLE IS ALWAYS RETURNED LOCKED- CALLER MUST FREE.  Returning them unlocked is just silly, really.  WAY too error prone.
    ATTuple     *Insert;
    if ( (Insert = GetDeletedRecord()) != NULL )                                // First try to reclaim a deleted record
        return Insert;

    long        Result, Tries = 0, StillFree = 0, VeryBad = 0;
    long        Seg = LastAddSegment + 1;                                       // Start at the list after the last one I used
    if ( Seg >= NumAddLists ) Seg = 0;
    volatile    ATTBAH      *EndTBAH, *NewTBAH;

new_retry:
    EndTBAH = GetHeaderPointer((TB->NumberBlocks) - 1);                         // Get a pointer to the last block
retry:                                                                          // Keep going until I get one (or space runs out)

    if ( EndTBAH->AddSegs[Seg].Tuple != AT_NORMAL_TUPLE ) {                     // Is there anything here?
        StillFree++;
        if ( (Result = ATBounceSpinLock(Kilroy, &(EndTBAH->AddSegs[Seg].ALock))) == ATERR_SUCCESS) {//Can we get a lock on it w/o fighting?
            if ( EndTBAH->AddSegs[Seg].Tuple != AT_NORMAL_TUPLE ) {             // Make sure someone didn't swipe it before we could get it locked....
                CursorTupleNumber = EndTBAH->AddSegs[Seg].Tuple;                // Get BEFORE you release the lock
                CursorCB = (ATTupleCB*)((EndTBAH->Data) + (TB->TrueTupleSize *
                    CursorTupleNumber));
                if ( CursorCB->Tuple != AT_CHAIN_END )                          // If this is not the end of the chain
                    EndTBAH->AddSegs[Seg].Tuple = CursorCB->Tuple;              // Set the list to point to the next guy
                else                                                            // This is the end...
                    EndTBAH->AddSegs[Seg].Tuple = AT_NORMAL_TUPLE;              // Clear the list header
                ATFreeSpinLock(Kilroy, &(EndTBAH->AddSegs[Seg].ALock));         // Free the segment lock
                CursorCB->ALock = Kilroy;                                       // Get it locked to this caller

                CursorBlock = EndTBAH->SharedHeader->ThisBlock;                 // Set up remaining cursor stuff
                CursorTBAH = EndTBAH;
                LastAddSegment = Seg;                                           // Save the last seg we used
                CursorStatus = AT_CURSOR_NORMAL;

                Insert = (ATTuple*)(CursorCB + 1);
                CursorCB->Block = CursorCB->Tuple = AT_NORMAL_TUPLE;            // MAKE SURE YOU HAVE TUPLE LOCKED BEFORE CLEARING THESE
                return Insert;                                                  // Return the tuple
            }
            ATFreeSpinLock(Kilroy, &(EndTBAH->AddSegs[Seg].ALock));             // Free the segment lock- someone stole it from us!
        }
    }
    ++Tries;
    if ( Tries >= NumAddLists ) {                                               // If we have made a complete loop
        if ( !StillFree )   goto new_block;                                     // Exit if nothing is free
        StillFree = Tries = 0;                                                  // Otherwise, let's make another loop
        VeryBad++;
        if ( VeryBad > 25 ) {                                                   // This is just in case we have a big problem- let's try not to take the system down...
            usleep(1000);                                                       // Sleep for a millisecond
        }
    }
    Seg++;
    if ( Seg >= NumAddLists ) Seg = 0;
    goto retry;

new_block:                                                                      // I only hit here when the page is full
    ATGetSpinLock(Kilroy, &(EndTBAH->SharedHeader->ALock));                     // Lock the block
    if ( MyNumberBlocks != TB->NumberBlocks ) {                                 // If someone managed to add a block before we got the lock... just restart
        ATFreeSpinLock(Kilroy, &(EndTBAH->SharedHeader->ALock));                // Free it and retry
        goto new_retry;
    }

    if ( !(NewTBAH = AddBlock()) ) {                                            // Add a block to the table
        ATFreeSpinLock(Kilroy, &(EndTBAH->SharedHeader->ALock));                // Hmmm... bad error...
        return NULL;
    }
    ATFreeSpinLock(Kilroy, &(EndTBAH->SharedHeader->ALock));                    // Hmmm... bad error...
    EndTBAH = NewTBAH;
    goto retry;
}
// **************************************************************************** AddBlock
ATTBAH *ATSharedTable::AddBlock() {                                             // Internal call to add a block to the table- complete with header & pointers for the calling process, which it returns
                                                                                // CALLER SHOULD HOLD ANY NEEDED LOCKS
    ATTuple     *Base, *Tuple;
    volatile    ATTBAH      *EndTBAH, *NewTBAH;
    long        StartAlloc, Result, Count, Add, Seg;

    EndTBAH = GetHeaderPointer((TB->NumberBlocks) - 1);                         // Get a pointer to the last block

    StartAlloc =    (TB->TrueTupleSize * TB->GrowthAlloc) +                     // Figure out how much memory we need to allocate
                    (sizeof(ATTListSegs) * NumAddLists) +
                    sizeof(ATTBAHG) + (AT_MEM_ALIGN * 3);                       // The 3 is two structs plus data align

    if ( (Result = Mem.CreateSharedMem((TB->Key + TB->NumberBlocks), StartAlloc))// Try to create the shared memory
            != ATERR_SUCCESS)
        return NULL;
    Base = (char*)Mem.GetBasePointer();                                         // Get the base pointer
    NewTBAH = GetNewTBAH();                                                     // Now that we have allocated the block globally, now let's allocate it locally
    DeterminePointers(Base, (ATTBAH*)NewTBAH);                                  // Initialize the shared ptrs in our local TBAH

    NewTBAH->SharedHeader->TuplesAllocated =    TB->GrowthAlloc;                // Init the global block header
    NewTBAH->SharedHeader->NumberTuples =       0;
    NewTBAH->SharedHeader->ALock =              0;
    NewTBAH->SharedHeader->SHMID =              Mem.GetSystemID();
    NewTBAH->SharedHeader->NextSHMID =          0;
    NewTBAH->SharedHeader->ThisBlock =          TB->NumberBlocks;

    InitBlock(NewTBAH);                                                         // Init the structures in the new block

    EndTBAH->SharedHeader->NextSHMID = NewTBAH->SharedHeader->SHMID;            // Add this to our global chain
    TB->NumberBlocks += 1;                                                      // Inc number of blocks in table
    // MAKE SURE NEVER TO RELOCATE THE FOLLOWING CALL PRIOR TO THE DETERMINEPOINTERS()
    Mem.FreeThisInstanceOnly();                                                 // Then tell my object not to track it anymore (only the creator needs to track it)
    MyNumberBlocks += 1;                                                        // I have local access to this block since I created it

    return (ATTBAH*)NewTBAH;
}

// **************************************************************************** DeleteTuple
int ATSharedTable::DeleteTuple() {                                              // Delete the tuple at the current record position
                                                                                // WILL REFUSE TO WORK IF YOU DO NOT HAVE A LOCK ON THE TUPLE!
    long        Result, Interval = 0;
    long        Seg = LastDelSegment + 1;                                       // Start at the list after the last one I used
    long        OrigBlock = CursorBlock, OrigTuple = CursorTupleNumber;         // Save these values to use for deleting the keys
    if ( Seg >= NumDelLists ) Seg = 0;
    if ( Kilroy == CursorCB->ALock ) {                                          // Make SURE they have a tuple lock and it is a valid tuple.  Multiple deletes could put the same tuple in different lists- and that would be very bad.
        CursorCB->ALock = AT_DELETED_TUPLE;                                     // IMMEDIATELY set this tuple to invalid, BEFORE it gets added to delete list
        while( (Result = ATBounceSpinLock(Kilroy, &(DelSegs[Seg].ALock))) !=ATERR_SUCCESS) { // Loop until I get a segment I can write to
            Seg++;
            if ( Seg >= NumDelLists ) Seg = 0;
            ++Interval;
            if ( Interval > 100 ) {                                             // This is entirely just in case someone made a mess of the startup settings!
                Interval = 0;
                usleep(1000);}                                                  // Help make sure we don't take the system down due to bad config...
        }
        if ( DelSegs[Seg].Block > AT_NORMAL_TUPLE ) {                           // If there is already an entry in the list...
            CursorCB->Block = DelSegs[Seg].Block;                               // Point to the next guy in the list
            CursorCB->Tuple = DelSegs[Seg].Tuple;
            DelSegs[Seg].Block = CursorBlock;                                   // Make the header point to me
            DelSegs[Seg].Tuple = CursorTupleNumber;
        }
        else {                                                                  // Nobody in the list, so add myself in
            DelSegs[Seg].Block = CursorBlock;
            DelSegs[Seg].Tuple = CursorTupleNumber;
            CursorCB->Block = AT_CHAIN_END;                                     // Mark myself as the end of the chain
            CursorCB->Tuple = AT_CHAIN_END;
        }
        LastDelSegment = Seg;

        if ( PrimaryBTree )                                                     // If there is a primary key
            PrimaryBTree->DeleteTuple((void*)(CursorCB + 1), OrigBlock, OrigTuple);// Delete this key
        if ( NumberBTrees ) {                                                   // If there are any secondary keys
            for ( int i = 0; i < NumberBTrees; ++i)                             // Loop though them all
                BTrees[i]->DeleteTuple((void*)(CursorCB + 1), OrigBlock, OrigTuple);// And remove the key for this tuple
        }
        // Note I wait until this point to release the segment lock.  Kinda long, but I HAVE to get the keys deleted first, which require a valid tuple ptr to create the keys from.
        // So either I would have to make a copy of the tuple (certainly possible), or make sure that nobody else reclaims this tuple just yet.  For now, I choose not to make an arbitrarily large copy. ;^)
        ATFreeSpinLock(Kilroy, &(DelSegs[Seg].ALock));                          // Free the segment lock
        return ATERR_SUCCESS;
    }
    return ATERR_UNSAFE_OPERATION;
}
// **************************************************************************** MakeCBPointer
volatile ATTupleCB *ATSharedTable::MakeCBPointer(                               // Internal routine to return a CB pointer from a block/tuple combo
                            long        Block,                                  // Block to use
                            long        Tuple                                   // Tuple to use
                            ) {
    volatile    ATTBAH  *TBAH = GetHeaderPointer(Block);
    return  (ATTupleCB*)((TBAH->Data) + (TB->TrueTupleSize * Tuple));
}
// **************************************************************************** GetDeletedRecord
// In certain fairly unusual instances, this routine will not return a free tuple even
// if there is one, rather than fight over it.  Bad configs could make this not so unusual!
ATTuple *ATSharedTable::GetDeletedRecord() {                                    // Internal call to try to reclaim deleted records- returns NULL if none found, otherwise a ptr to a reclaimed tuple
    long        Result, Tests = 0, Tries = NumDelLists, Harder = NumDelLists * 2;
    long        Seg = LastDelSegment + 1;                                       // Start at the list after the last one I used
    if ( Seg >= NumDelLists ) Seg = 0;

    while ( Tests < Tries ) {                                                   // Go thru the segments, but only once
        if ( DelSegs[Seg].Block != AT_NORMAL_TUPLE ) {                          // Is there anything here?
            if ( (Result = ATBounceSpinLock(Kilroy, &(DelSegs[Seg].ALock))) == ATERR_SUCCESS) {//Can we get a lock on it w/o fighting?
                if ( DelSegs[Seg].Block != AT_NORMAL_TUPLE ) {                  // Make sure someone didn't swipe it before we could get it locked....
                    CursorBlock = DelSegs[Seg].Block;                           // Get these BEFORE you release the lock
                    CursorTupleNumber = DelSegs[Seg].Tuple;
                    CursorTBAH = GetHeaderPointer(CursorBlock);
                    CursorCB = (ATTupleCB*)((CursorTBAH->Data) + (TB->TrueTupleSize * // Get a ptr to the existing entry
                        CursorTupleNumber));
                    if ( CursorCB->Block == AT_CHAIN_END ) {                    // If this is the end of the chain
                        DelSegs[Seg].Block = DelSegs[Seg].Tuple = AT_NORMAL_TUPLE;// Clear the list header
                    }
                    else {                                                      // There is another tuple in the chain
                        DelSegs[Seg].Block = CursorCB->Block;                   // Set the list to point to the next guy
                        DelSegs[Seg].Tuple = CursorCB->Tuple;                   // Set the list to point to the next guy
                    }
                    ATFreeSpinLock(Kilroy, &(DelSegs[Seg].ALock));              // Free the segment lock
                    CursorCB->Block = CursorCB->Tuple = AT_NORMAL_TUPLE;        // Clear the tuple
                    CursorCB->ALock = Kilroy;                                   // Get it locked to this caller
                    LastDelSegment = Seg;                                       // Save the last seg we used
                    CursorStatus = AT_CURSOR_NORMAL;
                    return (ATTuple*)(CursorCB + 1);                            // Return the tuple
                }
                ATFreeSpinLock(Kilroy, &(DelSegs[Seg].ALock));                  // Free the segment lock- someone stole it from us!
            }
            Tries = Harder;                                                     // If there is actually free space there, try harder
        }
        Tests++;
        Seg++;
        if ( Seg >= NumDelLists ) Seg = 0;
    }
    return NULL;
}
// **************************************************************************** GetHeaderPointer
volatile ATTBAH  *ATSharedTable::GetHeaderPointer(                              // Internal routine to get a given header pointer
                                                                                // We do it this way to help make sure we ALWAYS use locked access- never access the list directly!
                            int         Header                                  // Header to get
                            ) {
    long    Block = ((long)(Header / AT_TH_ALLOC));                             // Which block are we accessing?
    long    Offset = ((long)(Header % AT_TH_ALLOC));                            // What is our offset from the start of the block?

retry:
    if ( Header < MyNumberBlocks )                                              // As long as it is in range of my existing access
        return ((TBAHBlocks[Block]) + Offset);
    if ( Header < TB->NumberBlocks ) {                                          // If it is valid
        SynchBlockAccess();                                                     // Otherwise, synchronize the access first, then return it
        goto retry;
    }
    return NULL;                                                                // Invalid request
}
// **************************************************************************** SynchBlockAccess
void    ATSharedTable::SynchBlockAccess() {                                     // Internal routine to synch the local process memory to global memory
    volatile ATTBAH      *TBAH, *NewTBAH;
    ATTuple *Base;

    TBAH = GetHeaderPointer(MyNumberBlocks - 1);                                // Start with the last block we have access to
    while ( TBAH->SharedHeader->NextSHMID ) {                                   // As long as there are more blocks to gain access to
        NewTBAH = GetNewTBAH();                                                 // Get a new TBAH allocated/init'd in our local memory
        Mem.AttachSharedMem((TB->Key + MyNumberBlocks));                        // Get access to the shared block
        Base = (char*)Mem.GetBasePointer();                                     // Get the base pointer
        DeterminePointers(Base, (ATTBAH *)NewTBAH);                             // Initialize the shared ptrs
        Mem.FreeThisInstanceOnly();                                             // Then tell my object not to track it anymore (only the creator needs to track it)
        TBAH = NewTBAH;                                                         // Move to the next block
        MyNumberBlocks++;
    }
}
// **************************************************************************** DeterminePointers
int     ATSharedTable::DeterminePointers(                                       // An internal function to make sure everyone looks for structures in the same place
                            ATTuple     *Base,                                  // Ptr to the base of the block
                            ATTBAH      *TBAH                                   // Block header to initialize
                            ) {
    unsigned long   Test;

    Test = (unsigned int)(Base);                                                // Align & set a pointer to the shared header
    Test = ((Test + (AT_MEM_ALIGN - 1)) & ~(AT_MEM_ALIGN - 1));
    TBAH->SharedHeader = (ATTBAHG*)Test;                                        // Set the pointer to the shared header

    Test += sizeof(ATTBAHG);                                                    // Now align & set a pointer to the add lists
    Test = ((Test + (AT_MEM_ALIGN - 1)) & ~(AT_MEM_ALIGN - 1));
    TBAH->AddSegs = (ATTListSegs *)Test;

    Test = (unsigned int)(TBAH->AddSegs + NumAddLists);                         // Now align & set a ptr to the start of the data
    Test = ((Test + (AT_MEM_ALIGN - 1)) & ~(AT_MEM_ALIGN - 1));
    TBAH->Data = (char *)Test;                                                  // Store the start of data ptr
    TBAH->Base = (char*)Mem.GetTrueBasePointer();                               // Save the true base ptr for cleaning up later
    return ATERR_SUCCESS;
}
// **************************************************************************** FirstDeterminePointers
int     ATSharedTable::FirstDeterminePointers(                                  // An internal function to make sure everyone looks for structures in the same place when a table is first created/opened
                            int         Create                                  // Set to true if calling from Create(), false if calling from Open()
                            ) {
    unsigned long   Test, NumberSegs;
    volatile ATTBAH *TBAH;

    TB = (ATTableInfo *)Mem.GetBasePointer();                                   // Set my ptr to the ATTableInfo struct

    Test = (unsigned int)(TB + 1);                                              // Align & set a pointer to the delete segments
    Test = ((Test + (AT_MEM_ALIGN - 1)) & ~(AT_MEM_ALIGN - 1));
    DelSegs = (ATTListSegs *)Test;

    (Create) ? NumberSegs = NumDelLists:NumberSegs = TB->NumDelLists;           // Depending on whether I am creating or opening, I will find the number of segments in different places
    Test = (unsigned int)(DelSegs + NumberSegs);                                // Now align & set a ptr to the first shared header
    Test = ((Test + (AT_MEM_ALIGN - 1)) & ~(AT_MEM_ALIGN - 1));
    TBAH = GetNewTBAH();                                                        // Get a TBAH allocated
    if ( !TBAH ) return ATERR_OUT_OF_MEMORY;
    TBAH->SharedHeader = (ATTBAHG*)Test;                                        // Set the pointer to the shared header

    Test += sizeof(ATTBAHG);                                                    // Now align & set a pointer to the add lists
    Test = ((Test + (AT_MEM_ALIGN - 1)) & ~(AT_MEM_ALIGN - 1));
    TBAH->AddSegs = (ATTListSegs *)Test;

    (Create) ? NumberSegs = NumAddLists:NumberSegs = TB->NumAddLists;           // Depending on whether I am creating or opening, I will find the number of segments in different places
    Test = (unsigned int)(TBAH->AddSegs + NumberSegs);                          // Now align & set a ptr to the start of the data
    Test = ((Test + (AT_MEM_ALIGN - 1)) & ~(AT_MEM_ALIGN - 1));
    TBAH->Data = (char *)Test;                                                  // Store the start of data ptr
    TBAH->Base = (char*)Mem.GetTrueBasePointer();                               // Save the true base ptr for cleaning up later
    return ATERR_SUCCESS;
}
// **************************************************************************** GetNewTBAH
// The table allocation headers are themselves allocated in chunks.  This routine
// keeps track of all of them, and allocates new ones for growth as needed.  It
// maintains a infinitely growing list of pointers.  Each pointer points to a chunk
// of header storage space.
ATTBAH  *ATSharedTable::GetNewTBAH() {                                           // Routine to allocate and initialize the chain portion of a new TBAH
    volatile ATTBAH  **Temp, *NewTBAH;

    if ( AllocatedTBAHBlocks ) {                                                 // As long as I have created some tracking blocks
TBAH_retry:
        if ( NumberTBAHs < AT_TH_ALLOC ) {                                      // As long as there is room in the current tracking block
            NewTBAH = ((TBAHBlocks[NumberTBAHBlocks - 1]) + NumberTBAHs);       // Get the new block
            NewTBAH->PrevHeader = LastTBAH;                                     // Init the chain
            NewTBAH->NextHeader = NULL;
            if ( LastTBAH ) LastTBAH->NextHeader = NewTBAH;                     // Keep the chain going, if there is one started
            LastTBAH = NewTBAH;                                                 // Store this pointer as being the last one allocate
            NumberTBAHs++;                                                      // Move our current ptr to the next spot
            return (ATTBAH*)NewTBAH;
        }
        else {                                                                  // I have filled up the current block- need a new one.
            if ( NumberTBAHBlocks < AllocatedTBAHBlocks ) {                     // As long as I have another block available in the currently allocated list...
                TBAHBlocks[NumberTBAHBlocks] = (volatile ATTBAH*)new char[AT_TH_ALLOC * sizeof(ATTBAH)];// Since I have room in the list, just allocate a new block of headers & store the pointer
                if ( !(TBAHBlocks[NumberTBAHBlocks]) ) return NULL;
                NumberTBAHBlocks++;                                             // Increment the current block in the list
                NumberTBAHs = 0;                                                // Set the current TBAH to the first record in the new block
                goto TBAH_retry;
            }
            else {                                                              // I don't have any storage available in the current list- I need to allocate a new list
                Temp = (volatile ATTBAH**)new char[(AT_TH_TRACKING_ALLOC + AllocatedTBAHBlocks) * sizeof(ATTBAH*)]; // Alloc a new, larger list
                if ( !Temp ) return NULL;
                memcpy(Temp, TBAHBlocks, AllocatedTBAHBlocks * sizeof(ATTBAH*));// Copy the old list into the new list
                delete TBAHBlocks;                                              // Delete the old list
                TBAHBlocks = Temp;                                              // Set the pointer to the new list
                AllocatedTBAHBlocks += AT_TH_TRACKING_ALLOC;                    // Store how many I now have created
                goto TBAH_retry;
            }
        }
    }
    else {                                                                      // Must be my first time here, so I need ot create a starting tracking list
        TBAHBlocks = (volatile ATTBAH**)new char[AT_TH_TRACKING_ALLOC * sizeof(ATTBAH*)];// Alloc the list
        if ( !TBAHBlocks ) return NULL;
        AllocatedTBAHBlocks = AT_TH_TRACKING_ALLOC;                             // Store how many I created
        TBAHBlocks[0] = (ATTBAH*)new char[AT_TH_ALLOC * sizeof(ATTBAH)];        // Now, create the room to store the actual headers, and put the pointer in my new list
        if ( !(TBAHBlocks[0]) ) return NULL;
        NumberTBAHBlocks = 1;                                                   // I am now going to be using the first block
        goto TBAH_retry;
    }

}
// **************************************************************************** CloseTable
int     ATSharedTable::CloseTable() {                                           // Close down the table
    long List, NumHeaders, Headers;
    volatile ATTBAH  *TBAH;

    if ( !TB ) return ATERR_SUCCESS;
//printf("Number table blocks: %i\r\n", TB->NumberBlocks);

    ATAtomicDec(&(TB->InstanceCount));                                          // Decrease the instance count

    SynchBlockAccess();                                                         // Make sure I can see all the blocks
    for ( List = 0; List < NumberTBAHBlocks; ++List) {                          // Run through the entire tracking list
        if ( List < NumberTBAHBlocks - 1 )
            NumHeaders = AT_TH_ALLOC;                                           // For all except the last block, this will be a full block
        else
            NumHeaders = NumberTBAHs;                                           // Otherwise it may be partially full
        for ( Headers = 0; Headers < NumHeaders; Headers++) {                   // Loop thru all the headers in this block
            TBAH = ((TBAHBlocks[List]) + Headers);                              // Determine ptr to this header
            if ( IAmCreator )                                                   // If I created this table, I will try to take it down (won't actually go until everyone else goes too...)
                ATDestroySharedMem(TBAH->SharedHeader->SHMID);                  // Free up this shared memory
            ATDetachSharedMem((volatile void *)TBAH->Base);                     // Decrement the instance count for the mem
        }
        if ( TBAHBlocks && TBAHBlocks[List] ) delete TBAHBlocks[List];          // After freeing the shared segments above, now delete the actual block itself
    }
    if ( IAmCreator ) Mem.FreeThisInstanceOnly();                               // Clear the shared memory object
    if ( TBAHBlocks ) delete TBAHBlocks;                                        // Delete the list itself

    ResetVariables();
    TB = NULL;
    return ATERR_SUCCESS;
}
// **************************************************************************** InitBlock
void    ATSharedTable::InitBlock(                                               // Internal routine to init the structures in a new block
                            volatile ATTBAH  *TBAH                              // Block to init
                            ) {
    long        i, Count, Add, Seg;
    ATTuple     *Tuple;
    ATTupleCB   *CB;// Note the NON volatile dec...

    for ( i = 0; i < NumAddLists; ++i)  {                                       // Clear the segment locks for adds tracking
        TBAH->AddSegs[i].ALock =  0;
        TBAH->AddSegs[i].Tuple = -1;
    }

    Seg = 0;                                                                    // This loop will add all of the tuples to the add lists
    Count = TBAH->SharedHeader->TuplesAllocated;                                // Since it is a safe loop (and possibly very large), let's cache that volatile value
    Add = Count - 1;                                                            // Start at the end (so tuples get used starting at the beginning- makes for a faster NextTuple())
    Tuple = (ATTuple *)(TBAH->Data + (Add * TB->TrueTupleSize));
    CB = (ATTupleCB*)Tuple;
    while ( Add > -1 ) {                                                        // Loop thru all the tuples
        if ( TBAH->AddSegs[Seg].Tuple > -1 ) {                                  // If this is an ongoing list
            CB->Tuple = TBAH->AddSegs[Seg].Tuple;                               // Make this tuple point to the previous tuple stored in the list
            TBAH->AddSegs[Seg].Tuple = Add;                                     // Then make the list point to me
        }
        else  {                                                                 // In each header...
            TBAH->AddSegs[Seg].Tuple = Add;                                     // Start it with the highest free block left
            CB->Tuple = AT_CHAIN_END;                                           // Mark this tuple as being at the end of this list
        }
        CB->ALock = 0;                                                          // As long as we are here, init the rest of the CB
        CB->Block = AT_VIRGIN_TUPLE;

        Tuple = (ATTuple*)(((char*)Tuple) - TB->TrueTupleSize);                 // Move to the prev tuple
        CB = (ATTupleCB*)Tuple;
        Add--;                                                                  // Move to the next tuple
        Seg++;
        if ( Seg == NumAddLists ) Seg = 0;                                      // This just keeps round-robining the segments...
    }
}
// **************************************************************************** RegisterBTree
int ATSharedTable::RegisterBTree(                                               // Call to register a new BTree with the table
                            ATBTree     *inBTree,                               // Ptr to the BTree being registered
                            long        inIndexType                             // Either AT_BTREE_PRIMARY or AT_BTREE_SECONDARY
                            ) {
    if ( inIndexType == AT_BTREE_PRIMARY ) {                                    // If this is a primary key
        if ( !PrimaryBTree )                                                    // Can only have one per table
            PrimaryBTree = inBTree;
        else
            return ATERR_BAD_PARAMETERS;                                        // Tried to add more than one primary
    }
    else {                                                                      // Must be a secondary BTree request
        if ( NumberBTrees < AT_MAX_BTREES - 1 ) {                               // As long as we have room
            BTrees[NumberBTrees] = inBTree;                                     // Add it to our list
            NumberBTrees++;
        }
        else
            return ATERR_OUT_OF_MEMORY;
    }
    return ATERR_SUCCESS;
};
// **************************************************************************** UnRegisterBTree
int ATSharedTable::UnRegisterBTree(                                             // Call to UNregister a BTree with the table
                            ATBTree     *inBTree,                               // Ptr to the BTree being unregistered
                            long        inIndexType                             // Either AT_BTREE_PRIMARY or AT_BTREE_SECONDARY
                            ) {
    long    Cleared = 0, i;

    if ( inIndexType == AT_BTREE_PRIMARY ) {                                    // If this is a primary key
        if ( PrimaryBTree == inBTree )                                          // If it matches
            PrimaryBTree = NULL;                                                // Clear it
        else
            return ATERR_NOT_FOUND;                                             // Didn't find it
    }
    else {                                                                      // Must be a secondary BTree request
        for ( i = 0; i < NumberBTrees; ++i ) {
            if ( BTrees[i] == inBTree ) {                                       // If it is a match
                BTrees[i] = NULL;                                               // Clear it
                Cleared++;                                                      // Note that we have cleared it
                if ( i < NumberBTrees - 1 )                                     // Move the next one down if not the last guy
                    BTrees[i] = BTrees[i + 1];
            }
            else if ( Cleared ) {                                               // Once we have deleted one
                if ( i < NumberBTrees - 1 )                                     // Up until the last one
                    BTrees[i] = BTrees[i + 1];                                  // We need to move the others down
            }
        }
        if ( Cleared == 1 )                                                     // If we did indeed remove one
            NumberBTrees--;                                                     // Dec our # of BTrees
        else if ( Cleared > 1 )                                                 // Oops- someone must have registered the same BTree more than once....
            return ATERR_UNSAFE_OPERATION;
        else
            return ATERR_NOT_FOUND;                                             // Didn't find it
    }
    return ATERR_SUCCESS;
}


