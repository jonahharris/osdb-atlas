// ****************************************************************************
// * template.cpp - The XHTML template code for Atlas.                        *
// * (c) 2002,2003 Shawn Houser, All Rights Reserved                          *
// * This property and it's ancillary properties are completely and solely    *
// * owned by Shawn Houser, and no part of it is a work for hire or the work  *
// * of any other.                                                            *
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

// Have to use different IO libraries for FASTCGI
#ifndef AT_USE_FCGI
    #include <stdio.h>
    #include <stdlib.h>
#else
    #include "fcgi_config.h"
    #include <stdlib.h>
    #ifdef HAVE_UNISTD_H
        #include <unistd.h>
    #endif
    #ifdef _WIN32
        #include <process.h>
    #else
        extern char **environ;
    #endif
    #include "fcgi_stdio.h"
#endif
#include <string.h>
#include <memory.h>
#include <assert.h>
#include <ctype.h>

#include "general.h"
#include "support.h"
#include "memory.h"
#include "table.h"
#include "btree.h"
#include "template.h"


// Number of keys per BTree page in the index
#define     AT_XT_INDEX_KEYS        150
// Number of blocks to alloc in the tracking list
#define     AT_XT_TRACK_ALLOC       5

struct  ATXTemplateInfoBlock {                                                  // Shared Info block for the cache
    volatile long   CurrentBlock;                                               // Current block in use
    ATLOCK          ALock;                                                      // Lock on the cache
    volatile long   BlocksAllocated;                                            // Number of cache blocks allocated
    volatile long   BlockSize;                                                  // The size of each block allocated
    volatile long   BlocksPerAlloc;                                             // Blocks to alloc each time
    volatile long   CacheTableKey;                                              // IPC key for the cache table
    volatile long   EntryTableKey;                                              // IPC key for the entry table
    volatile long   IndexKey;                                                   // IPC key for the index
};
struct  ATXTemplateCacheEntry {                                                 // Entry for a cached template
    volatile char   FileName[AT_MAX_PATH];                                      // Name of the entry
    volatile long   Block;                                                      // Block this entry is stored in
    volatile long   Offset;                                                     // Offset this entry is stored in within the block
};
struct  ATXTemplateBlockHeader {                                                // Control struct for each cache block
    volatile long   Offset;                                                     // Offset within the block that is in use
    volatile long   ID;                                                         // The block ID
};
struct  ATXParseFormHeader {                                                    // Header of a parsed form
    volatile long   NumberItems;                                                // Number of items in the form
    volatile long   Authorize;                                                  // Authorize value of the form
};
struct ATXPFormItem {                                                           // A parsed form item
    volatile long   Type;                                                       // The type of item
    volatile char   *Location;                                                  // Ptr to the location of the item in local memory- only not null if the item is stored in local memory
    volatile long   Length;                                                     // Length of the data stored
    volatile long   Block;                                                      // Block where the item is stored- only > -1 when there is actually something stored in global memory for the item
    volatile long   Offset;                                                     // Offset of the item within the block
    volatile long   CommandType;                                                // Type of the command, if appropriate
    volatile long   NameBlock;                                                  // Name of the item, if appropriate
    volatile long   NameOffset;
};

// Parsed item types
#define     AT_XP_COMMAND       (1)
#define     AT_XP_MARKER        (2)
#define     AT_XP_BLOCK         (3)

// Token types
#define     AT_XP_EOF           (-2)
#define     AT_XP_INVALID       (-1)
#define     AT_XP_START         (0)
#define     AT_XP_COMMAND_START (1)
#define     AT_XP_COMMAND_STOP  (2)
#define     AT_XP_MARKER_START  (3)
#define     AT_XP_MARKER_STOP   (4)
#define     AT_XP_BLOCK_START   (5)
#define     AT_XP_BLOCK_STOP    (6)
#define     AT_XP_INQUOTES      (7)
#define     AT_XP_ACTIVE        (8)
#define     AT_XP_INACTIVE      (9)

// Command Types
#define     AT_XP_CM_INCLUDE    (1)


// **************************************************************************** CacheKeyCompare
long    CacheKeyCompare(void *P1, void *P2, long Size) {                        // Routine to compare the index entries
    return strnicmp((char*)P1, (char*)P2, Size);
}
// **************************************************************************** CacheMakeKey
void    *CacheMakeKey(void *Tuple) {                                            // Routine to make a key for the cache index entries
    return (void*)(((ATXCE*)Tuple)->FileName);
}



// ****************************************************************************
// ****************************************************************************
//                                ATXTemplate
// ****************************************************************************
// ****************************************************************************

// **************************************************************************** EmptyCache
void ATXTemplate::EmptyCache() {                                                // Forces all templates out of cache (mostly development use)
    ATGetShareExclusive(&(Info->ALock));                                        // Get an exclusive lock
    Info->CurrentBlock = 0;                                                     // Set ourselves back to the first block
    (Blocks[0])->Offset = sizeof(ATXTBH);                                       // Move the offset to just past the header
    (Blocks[0])->Offset += sizeof(ATXTInfo);                                    // Since this is the first block, also move it past our info block

    EntryTable.ResetCursor();                                                   // Start at the beginning
    while( EntryTable.NextTuple() ) {                                           // Go through all of our entries
        EntryTable.LockTuple();
        EntryTable.DeleteTuple();                                               // And delete them
    }
    ATFreeShareExclusive(&(Info->ALock));                                       // Free our lock
}
// **************************************************************************** WriteTemplate
int ATXTemplate::WriteTemplate() {                                              // Writes the template to STDOUT via stdio
    ATXPFI      *Items = (ATXPFI*)(CurrForm + 1);                               // Get a ptr to the items
    char        *Stored;

    if ( !CurrForm ) return ATERR_NOT_FOUND;                                    // Make sure they have opened up the darn thing first

    for ( int i = 0; i < CurrForm->NumberItems; ++i) {                          // Run through all of the items
        if ( Items[i].Block > -1 ) {                                            // If there is anything in shared memory
            Stored =    ((char*)(Blocks[Items[i].Block])) +                     // Get a pointer to the stored data for this block
                        Items[i].Offset;
            fwrite(Stored, Items[i].Length, 1, stdout);                         // And write it out
        }
        else if ( Items[i].Location ) {                                         // If there is anything in local memory
            fwrite((void*)Items[i].Location, Items[i].Length, 1, stdout);       // Write it out
        }
    }
    return ATERR_SUCCESS;
}

// **************************************************************************** AppendToLabel
int ATXTemplate::AppendToLabel(                                                 // Inserts a requested line at the line immediately after the specified label-
                                                                                //   multiple insertions allowed- they are inserted in order they are requested (first = top, last = bottom)
                                                                                // Note that this DOES use up your scratch memory very quickly, since every add is not actually an append (can't free scratch memory), but a new block with your line added...
                            char        *Label,                                 // Label to insert at
                            char        *Value                                  // Value to append
                            ) {
    ATXPFI      *Items = (ATXPFI*)(CurrForm + 1);                               // Get a ptr to the items
    long        Which;
    void        *Tmp;

    if ( !CurrForm ) return ATERR_NOT_FOUND;                                    // Make sure they have opened up the darn thing first

    if ( (Which = FindLabel(Label)) < 0 )                                       // Get the item index
        return ATERR_NOT_FOUND;

    Items[Which].Length = Items[Which].Length + strlen(Value);                  // Get length of new value PLUS the old value
    if ( !(Tmp = Scratch->GetScratchMem(Items[Which].Length + 1)) )             // Allocate the memory to store the value
        return ATERR_OUT_OF_MEMORY;

    if ( Items[Which].Location ) {                                              // As long as there was indeed a value there before....
        strcpy((char*)Tmp, (char*)Items[Which].Location);                       // First copy over the old value
        strcat((char*)Tmp, Value);
    }
    else
        strcpy((char*)Tmp, Value);
    Items[Which].Location = (char*)Tmp;                                         // Set to the new pointer

    return ATERR_SUCCESS;
}
// **************************************************************************** ReplaceLabel
int ATXTemplate::ReplaceLabel(                                                  // Replaces a given label with a string
                            char        *Label,                                 // Label to replace
                            char        *Value                                  // Value to replace label with
                            ) {
    ATXPFI      *Items = (ATXPFI*)(CurrForm + 1);                               // Get a ptr to the items
    long        Which;

    if ( !CurrForm ) return ATERR_NOT_FOUND;                                    // Make sure they have opened up the darn thing first

    if ( (Which = FindLabel(Label)) < 0 )                                       // Get the item index
        return ATERR_NOT_FOUND;

    Items[Which].Length = strlen(Value);                                        // Get length of new value
    if ( !((Items[Which].Location) =                                            // Allocate the memory to store the value
        (char*)Scratch->GetScratchMem(Items[Which].Length + 1)) )
        return ATERR_OUT_OF_MEMORY;

    strcpy((char*)Items[Which].Location, Value);
    return ATERR_SUCCESS;
}
// **************************************************************************** PointLabelTo
int ATXTemplate::PointLabelTo(                                                  // Have label replaced with what is at the specified location (no copy takes place)
                            char        *Label,                                 // Label to have pointed to this data
                            char        *Data,                                  // Data to replace the label with
                            long        Length                                  // Length of the data to be used
                            ) {
    ATXPFI      *Items = (ATXPFI*)(CurrForm + 1);                               // Get a ptr to the items
    long        Which;

    if ( !CurrForm ) return ATERR_NOT_FOUND;                                    // Make sure they have opened up the darn thing first

    if ( (Which = FindLabel(Label)) < 0 )                                       // Get the item index
        return ATERR_NOT_FOUND;
    Items[Which].Length = Length;                                               // Store length of new value
    Items[Which].Location = Data;                                               // Point to new data
    return ATERR_SUCCESS;
}
// **************************************************************************** FindLabel
long ATXTemplate::FindLabel(                                                    // Call to find a label within an item list
                            char        *Label                                  // Label to find
                            ) {
    ATXPFI      *Items = (ATXPFI*)(CurrForm + 1);                               // Get a ptr to the items
    char        *Stored;

    for ( int i = 0; i < CurrForm->NumberItems; ++i ) {                         // Look thru all the items
        if ( Items[i].NameBlock != -1 ) {                                       // If it has a label associated with it
            Stored =    ((char*)(Blocks[Items[i].NameBlock])) +                 // Get a pointer to the stored name for this block
                        Items[i].NameOffset;
            if ( !strcmp(Stored, Label) )                                       // If they match
                return i;                                                       // Return the item number
        }
    }
    return -1;                                                                  // Didn't find it
}
// **************************************************************************** GetAuthorize
long ATXTemplate::GetAuthorize() {                                              // Returns the authorize value of the form
    if ( !CurrForm ) return -1;                                                 // Make sure they have opened up the darn thing first
    return CurrForm->Authorize;                                                 // Return the authorize value
}
// **************************************************************************** FreeTemplate
int ATXTemplate::FreeTemplate() {                                               // Call to free up the template form- ALWAYS CALL ASAP TO FREE LOCKS
    if ( CurrForm )
        ATFreeShare(&(Info->ALock));                                            // Release the share lock
    CurrForm = NULL;
    return ATERR_SUCCESS;
}
// **************************************************************************** GetTemplate
int ATXTemplate::GetTemplate(                                                   // Returns a handle to a given template
                            char        *Name                                   // Name of the template
                            ) {
    FILE    *Input;
    long    FileLength, Result, Attempts = 0, FormLength, CurrBlock;
    char    *Raw, *Global;
    ATXPFH  *NewForm;
    ATXCE   NewEntry, *Found;

    if ( !Name ) return ATERR_BAD_PARAMETERS;

    if ( CurrForm ) {                                                           // If they forgot to free the last one
        ATFreeShare(&(Info->ALock));                                            // Release the share lock
        CurrForm = NULL;
    }
    if ( MyBlocksAllocated < Info->BlocksAllocated )                            // Make sure I am up to date
        SynchBlocks();

start_over:
    ATGetShare(&(Info->ALock));                                                 // Get a read lock- no, I don't want to hold an old one if they had it- someone might need to write something
    if ( (Found = (ATXCE*)Index.FindTuple((void*)Name,                          // If we already have it loaded
            AT_BTREE_READ_OPTIMISTIC, AT_BTREE_FINDDIRECT, AT_MAX_PATH)) ) {
        CurrForm = (ATXPFH*)(((char*)(Blocks[Found->Block])) + Found->Offset);  // Get a pointer to the header
        FormLength = (CurrForm->NumberItems * sizeof(ATXPFI)) + sizeof(ATXPFH); // Determine form length
        if ( !(Raw = (char*)Scratch->GetScratchMem(FormLength)) ) {             // Get space in local memory for the item list
            ATFreeShare(&(Info->ALock));
            return ATERR_OUT_OF_MEMORY;
        }
        memcpy((void*)Raw, (void*)CurrForm, FormLength);                        // Copy it into local space
        CurrForm = (volatile ATXPFH*)Raw;                                       // Now point to the local copy
        return ATERR_SUCCESS;
    }
    ATFreeShare(&(Info->ALock));                                                // Free up our share

    // Okay, we are going to assume that nobody else is going to try to load this at the same time, and we'll start to work with no lock
    // so that we don't hold up the readers.  At the last moment possible, then, we'll get our exclusive.

    if ( !(Input = fopen(Name, "r")) )                                          // Open up the file
        return ATERR_NOT_FOUND;
    fseek(Input, 0, SEEK_END);                                                  // Get the length of the file
    FileLength = ftell(Input);
    fseek(Input, 0, SEEK_SET);
    if ( FileLength < 1 ) {                                                     // Must be at least 1 byte in length
        fclose(Input);
        return ATERR_FILE_ERROR;
    }
    if ( !(Raw = (char*)Scratch->GetScratchMem(FileLength + 1)) ) {             // Get the memory to load it
        fclose(Input);
        return ATERR_OUT_OF_MEMORY;
    }
    Result = fread(Raw, FileLength, 1, Input);                                  // Load it in
    fclose(Input);
    if ( Result != 1)
        return ATERR_FILE_ERROR;
    *(Raw + FileLength) = '\0';                                                 // Null term it

    ATGetShareExclusive(&(Info->ALock));                                        // The Parser will now require us to get the exclusive lock so it can allocate memory safely from the cache
    if ( (Found = (ATXCE*)Index.FindTuple((void*)Name,                          // If we already have it loaded
            AT_BTREE_READ_OPTIMISTIC, AT_BTREE_FINDDIRECT, AT_MAX_PATH)) ) {
        CurrForm = (ATXPFH*)(((char*)(Blocks[Found->Block])) + Found->Offset);  // Get a pointer to the header
        FormLength = (CurrForm->NumberItems * sizeof(ATXPFI)) + sizeof(ATXPFH); // Determine form length
        if ( !(Raw = (char*)Scratch->GetScratchMem(FormLength)) ) {             // Get space in local memory for the item list
            ATFreeShareExclusive(&(Info->ALock));                               // Oh well.  Free our lock and quit.
            return ATERR_OUT_OF_MEMORY;
        }
        memcpy((void*)Raw, (void*)CurrForm, FormLength);                        // Copy it into local space
        CurrForm = (volatile ATXPFH*)Raw;                                       // Now point to the local copy
        ATFreeShareExclusive(&(Info->ALock));                                   // Oh well.  Free our lock and restart.
        return ATERR_SUCCESS;
    }
    if ( !(NewForm = Parser.ParseForm(Raw, &FormLength,                         // Parse it- it isn't loaded
            (long*)&(NewEntry.Block), (long*)&(NewEntry.Offset))) ) {
        ATFreeShareExclusive(&(Info->ALock));
        return ATERR_OPERATION_FAILED;
    }
    strncpy((char*)NewEntry.FileName, Name, AT_MAX_PATH);                       // Copy the name into the entry
    NewEntry.FileName[AT_MAX_PATH - 1] = '\0';

    Raw = (char*)EntryTable.AddTuple((char*)&NewEntry);                         // Add it to our entry table
    EntryTable.UnlockTuple();
    ATFreeShareExclusive(&(Info->ALock));                                       // Free the lock
    if ( Raw )                                                                  // As long as the entry was successful
        goto start_over;                                                        // Retry- I could play some hokey games with the locks and go from here- but then I violate a few ordering concepts.
    else
        return ATERR_OUT_OF_MEMORY;
}
// **************************************************************************** GetCacheMemory
char *ATXTemplate::GetCacheMemory(                                              // Call to get a block of cache memory
                            long        Bytes,                                  // Bytes to get
                            long        *Block,                                 // To be set to the block where the item is stored
                            long        *Offset                                 // To be set to the offest where the item is stored
                            ) {                                                 // DON'T CALL THIS UNLESS YOU HAVE CACHE EXCLUSIVE LOCKED
    char    *Available, *Raw;
    long    CurrBlock;

    Bytes += AT_MEM_ALIGN;                                                      // Leave enough room for alignment
retry:
    CurrBlock = Info->CurrentBlock;                                             // Let it be cached
    if ( (BlockSize - (Blocks[CurrBlock])->Offset) >= Bytes ) {                 // As long as there is room in this block
        Available =
        (((char*)(Blocks[CurrBlock])) + (Blocks[CurrBlock])->Offset);           // Get a ptr to the available space
        (Blocks[CurrBlock])->Offset += Bytes;                                   // Adjust the offset in the block
        Available = ATAlignPtr(Available);                                      // Align the ptr
        *Block = CurrBlock;
        *Offset = (Available - ((char*)(Blocks[CurrBlock])));
        return Available;
    }
    else {
        if ( Bytes < (BlockSize - sizeof(ATXTBH)) ) {                           // Make sure this will FIT in a block
            if ( !(Raw = (char*)GetNewBlock()) )                                // Get a new block to put it in
                return NULL;
            goto retry;
        }
        else                                                                    // Uh oh- can't help ya pal- too big to fit in the blocks I was told to allocate
            return NULL;
    }
}
// **************************************************************************** Constructor
ATXTemplate::ATXTemplate() {
    Reset();
}
// **************************************************************************** Destructor
ATXTemplate::~ATXTemplate() {
    Close();
}
// **************************************************************************** Create
int ATXTemplate::Create(                                                        // Call to create
                            long            inAvgPageSize,                      // Your best guess at the average page size (probably 50k - 80k)
                            long            inPagesPerBlock,                    // Number of pages to allocate for at once (probably 50 - 100)
                            long            inBlocksPerAlloc,                   // Number of blocks to alloc each time
                            ATScratchMem    *inScratch,                         // Scratch memory class to use
                            int             inKeyRangeLow,                      // Systemwide unique IPC low entry for the template cache to use- EXPECT ALL NUMBERS IN RANGE TO BE USED
                            int             inKeyRangeHigh,                     // Systemwide unique IPC high entry for the template cache to use- EXPECT ALL NUMBERS IN RANGE TO BE USED
                                                                                // LEAVE PLENTY OF ROOM BETWEEN LOW & HIGH- At least your expected max number of blocks the cache will allocate multiplied by three- but BE GENEROUS
                            ULONG           inKilroy                            // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                            ) {
    long        Result;
    ATXTInfo    TempInfo;
    volatile ATXTBH *BH;

    if ( !inScratch || inPagesPerBlock < 1 || inBlocksPerAlloc < 1 ||
        (inKeyRangeHigh - inKeyRangeLow < 3 ) || !inKilroy )
        return ATERR_BAD_PARAMETERS;

    Scratch = inScratch;                                                        // Save our ptr to the scratch memory object
    BlockSize = (inAvgPageSize * inPagesPerBlock) + sizeof(ATXTBH) +            // Determine our block size
                sizeof(ATXTInfo) +
                + (sizeof(AT_MEM_ALIGN) * (inPagesPerBlock + 2));
    BlocksPerAlloc = inBlocksPerAlloc;

    Result = (inKeyRangeHigh - inKeyRangeLow) / 3;                              // Determine our keys
    CacheTableKey = inKeyRangeLow;
    EntryTableKey = inKeyRangeLow + Result;
    IndexKey = EntryTableKey + Result;

    if ( (Result = CacheTable.CreateTable(CacheTableKey, BlockSize, inBlocksPerAlloc,// Create the cache table
            inBlocksPerAlloc, 1, 1, 1, inKilroy)) != ATERR_SUCCESS )
        return Result;

    if ( (Result = EntryTable.CreateTable(EntryTableKey, sizeof(ATXCE), inPagesPerBlock,// Create the entry table
            inPagesPerBlock, 1, 1, 1, inKilroy)) != ATERR_SUCCESS ) {
        CacheTable.CloseTable();
        return Result;
    }

    if ( (Result = Index.Create(IndexKey, &EntryTable, &CacheKeyCompare,        // Create the index BTree
            &CacheMakeKey, AT_MAX_PATH, AT_XT_INDEX_KEYS, 1, AT_BTREE_PRIMARY,
            inKilroy)) != ATERR_SUCCESS ) {
        CacheTable.CloseTable();
        EntryTable.CloseTable();
        return Result;
    }

    Info = &TempInfo;                                                           // Create our initial Info structure
    Info->BlocksAllocated =     0;
    Info->BlockSize =           BlockSize;
    Info->BlocksPerAlloc =      inBlocksPerAlloc;
    Info->CacheTableKey =       CacheTableKey;
    Info->EntryTableKey =       EntryTableKey;
    Info->IndexKey =            IndexKey;
    Info->CurrentBlock =        -1;
    Info->ALock =               0;

    if ( !(BH = GetNewBlock()) ) {                                              // Add our first block to the cache
        CacheTable.CloseTable();
        EntryTable.CloseTable();
        Index.Close();
        return ATERR_OUT_OF_MEMORY;
    }
    memcpy((char*)(BH+1), (void*)Info, sizeof(ATXTInfo));                       // Copy it out to global memory
    Info = (volatile ATXTInfo*)(BH+1);                                          // Now make ourselves look at the global copy

    Parser.Initialize(this, Scratch);                                           // Initialize the parser

    return ATERR_SUCCESS;
}
// **************************************************************************** Open
int ATXTemplate::Open(                                                          // Call to open an already created template
                            ATScratchMem    *inScratch,                         // Scratch memory class to use
                            int             inKeyRangeLow,                      // Systemwide unique IPC low entry for the template cache to use- EXPECT ALL NUMBERS IN RANGE TO BE USED
                            int             inKeyRangeHigh,                     // Systemwide unique IPC high entry for the template cache to use- EXPECT ALL NUMBERS IN RANGE TO BE USED
                            ULONG           inKilroy                            // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                            ) {
    long        Result;
    ATXTInfo    TempInfo;
    volatile ATXTBH *BH;

    if ( !inScratch || (inKeyRangeHigh - inKeyRangeLow < 3 ) || !inKilroy )
        return ATERR_BAD_PARAMETERS;

    Scratch = inScratch;                                                        // Save our ptr to the scratch memory object

    Result = (inKeyRangeHigh - inKeyRangeLow) / 3;                              // Determine our keys
    CacheTableKey = inKeyRangeLow;
    EntryTableKey = inKeyRangeLow + Result;
    IndexKey = EntryTableKey + Result;

    if ( (Result = CacheTable.OpenTable(CacheTableKey,inKilroy)) != ATERR_SUCCESS )// Open the cache table
        return Result;
    if ( (Result = EntryTable.OpenTable(EntryTableKey, inKilroy)) != ATERR_SUCCESS ) {// Open the entry table
        CacheTable.CloseTable();
        return Result;
    }
    if ( (Result = Index.Open(IndexKey, &EntryTable, &CacheKeyCompare,          // Open the index BTree
            &CacheMakeKey, inKilroy)) != ATERR_SUCCESS ) {
        CacheTable.CloseTable();
        EntryTable.CloseTable();
        return Result;
    }

    CacheTable.ResetCursor();                                                   // We have to find our Info struct
    while ( (BH = (volatile ATXTBH*)CacheTable.NextTuple()) ) {                 // Search through the cache blocks
        if ( BH->ID == 0 ) {                                                    // If this is ID 0, it has our struct in it
            Info = (volatile ATXTInfo*)(BH + 1);
            break;
        }
    }
    if ( !BH ) {                                                                // If we can't find it
        CacheTable.CloseTable();
        EntryTable.CloseTable();
        Index.Close();
        return ATERR_OPERATION_FAILED;
    }

    BlockSize =     Info->BlockSize;
    BlocksPerAlloc= Info->BlocksPerAlloc;
    Parser.Initialize(this, Scratch);                                           // Initialize the parser

    return ATERR_SUCCESS;
}
// **************************************************************************** AddBlock
volatile ATXTBH *ATXTemplate::GetNewBlock() {                                   // Get a new block for the cache
                                                                                // DON'T CALL THIS UNLESS YOU HAVE THE CACHE EXCLUSIVELY LOCKED
    long Result;
    volatile ATXTBH *NewHeader;

start_over:
    if ( Info->CurrentBlock < Info->BlocksAllocated - 1 ) {                     // If we have blocks allocated, but we just need to increment the current block indicator
        if ( (Result = SynchBlocks()) != ATERR_SUCCESS )                        // Make sure we are synched with the global blocks
            return NULL;
        Info->CurrentBlock++;                                                   // Move to the next one
        (Blocks[Info->CurrentBlock])->Offset = sizeof(ATXTBH);                  // Move the offset to just past the header
        if ( (Blocks[Info->CurrentBlock])->ID == 0 )
            (Blocks[Info->CurrentBlock])->Offset += sizeof(ATXTInfo);           // In the case of the first block, also move it past our info block
        return ((Blocks[Info->CurrentBlock]));                                  // And return the pointer
    }

    if ( !(NewHeader = (volatile ATXTBH*)CacheTable.AllocateTuple()) )          // Get a new block allocated
        return NULL;
    Info->CurrentBlock++;                                                       // Let everyone know to move ahead
    NewHeader->Offset = sizeof(ATXTBH);                                         // Initialize the header
    NewHeader->ID = Info->CurrentBlock;
    if ( NewHeader->ID == 0 )
        NewHeader->Offset += sizeof(ATXTInfo);                                  // In the case of the first block, also move it past our info block
    Info->BlocksAllocated++;                                                    // Let everybody know about the new block
    if ( (Result = SynchBlocks()) != ATERR_SUCCESS )                            // Make sure we are synched with the global blocks
        return NULL;
    return NewHeader;                                                           // Return a ptr to the new header
}
// **************************************************************************** SynchBlocks
int ATXTemplate::SynchBlocks() {                                                // Call to synchronize the local blocks with the global blocks
    volatile ATXTBH *NewBlock;
    long    Result, i;

    while ( MyBlocksAllocated < Info->BlocksAllocated ) {                       // As long as I am behind the times
        CacheTable.ResetCursor();                                               // Start at the beginning
        while ( (NewBlock = (volatile ATXTBH *)CacheTable.NextTuple()) ) {      // For as many blocks as there are in the cache table
            for ( i = 0; i < MyBlocksAllocated; ++i ) {                         // Look thru all of my records
                if ( Blocks[i] == NewBlock )                                    // If this is not a new block
                    break;
            }
            if ( i == MyBlocksAllocated ) {                                     // If this is indeed a new block
                if ( (Result = AllocTracking(i)) != ATERR_SUCCESS )             // Make sure we have enough tracking allocated
                    return Result;
                Blocks[i] = NewBlock;                                           // Save the new block
                ++MyBlocksAllocated;                                            // Increment my number of blocks
            }
        }
    }
    return ATERR_SUCCESS;
}
// **************************************************************************** AllocTracking
int ATXTemplate::AllocTracking(                                                 // Call to make sure we have enough tracking space available
                            int         Needed                                  // Number needed to store (zero based)
                            ) {
    ATXTBH  **NewTracking;

retry:
    if ( Needed < TrackingBlocks )                                              // As long as we have enough...
        return ATERR_SUCCESS;
    NewTracking = (ATXTBH**)new char[sizeof(ATXTBH*) * (TrackingBlocks +        // Allocate a new list
                    AT_XT_TRACK_ALLOC)];
    if ( !NewTracking ) return ATERR_OUT_OF_MEMORY;

    if ( Blocks ) {                                                             // As long as there was an old list
        memcpy((void*)NewTracking, (void*)Blocks, TrackingBlocks * sizeof(ATXTBH*));// Copy the old list to the new spot
        delete Blocks;                                                          // Free the old list
    }
    Blocks = (volatile ATXTBH**)NewTracking;                                    // Point to the new spot
    TrackingBlocks += AT_XT_TRACK_ALLOC;                                        // Adjust the number of blocks we have allocated
    goto retry;                                                                 // Just in case someone called us in bad order (which they shouldn't)
}
// **************************************************************************** Reset
void ATXTemplate::Reset() {                                                     // Reset the class's members
    Scratch = NULL;
    CacheTableKey = 0;
    IndexKey = 0;
    EntryTableKey = BlockSize = 0;
    MyBlocksAllocated = 0;
    BlocksPerAlloc = 0;
    Scratch = NULL;
    Info = NULL;
    Blocks = NULL;
    TrackingBlocks = 0;
    CurrForm = NULL;
    IncludePath[0] = '\0';
    IncludeLength = 0;
}
// **************************************************************************** Close
int ATXTemplate::Close() {                                                      // Close the class & free resources
    if ( CurrForm )                                                             // If they forgot to free the last template
        ATFreeShare(&(Info->ALock));                                            // Release the share lock
    if ( Blocks )                                                               // Clear any tracking blocks if any exist
        delete Blocks;
    Index.Close();                                                              // Close the index btree
    EntryTable.CloseTable();                                                    // Close our entry table
    CacheTable.CloseTable();                                                    // Close the cache table

    Reset();                                                                    // Reset the object
    CurrForm = NULL;
    Blocks = NULL;
    return ATERR_SUCCESS;
}
// **************************************************************************** IncludePath
int ATXTemplate::SetIncludePath(                                                // Call to set a base include path
                            char        *Path                                   // Path to set- can be called with NULL to reset
                            ) {
    if ( Path ) {                                                               // If a path was passed
        strncpy(IncludePath, Path, AT_MAX_PATH - 1);                            // Copy it in
        IncludePath[AT_MAX_PATH - 1] = '\0';                                    // Safely...
        IncludeLength = strlen(IncludePath);
    }
    else {                                                                      // They probably just want to clear it, so let's do that
        IncludePath[0] = '\0';
        IncludeLength = 0;
    }
    return ATERR_SUCCESS;
}



// ****************************************************************************
// ****************************************************************************
//                                ATXParse
// ****************************************************************************
// ****************************************************************************

// **************************************************************************** ParseForm
ATXPFH *ATXParse::ParseForm(                                                    // Return a ptr to a parsed form from raw XHTML input
                            char            *Raw,                               // Ptr to the raw HTML input
                            long            *OutLength,                         // Address of a long that will be set to the parsed form length
                            long            *Block,                             // The block where the header is stored
                            long            *Offset                             // The offset of the header within the block
                            ) {
    long    NumberItems, Item, Start, Length, Stop, SBlock, SOffset;
    ATXPFH  *Header;
    ATXPFI  *TempItems;
    char    *Where;

    if ( !Raw || !OutLength )
        return NULL;

    // First, let's find out how many items there are in the file
    Start = NumberItems = InputOffset = 0;                                      // Clear the state machine
    ParseState = AT_XP_START;
    Input = Raw;
    while ( (Item = GetNextItem(&Stop)) != AT_XP_EOF ) {                        // Search through the entire file
        switch( Item ) {                                                        // Depending on the type of the item
            case    AT_XP_INVALID:
                return NULL;
            case    AT_XP_COMMAND_START:                                        // Increment the count of items
            case    AT_XP_MARKER_START:
            case    AT_XP_BLOCK_START:
                ++NumberItems;
        }
    }
    if ( !( Header = (ATXPFH*)Template->GetCacheMemory(                         // Now that we know the number of items, we can allocate the item list
        (NumberItems * sizeof(ATXPFI)) + sizeof(ATXPFH), &SBlock, &SOffset )) ) // This is space for the header, plus a spot for each item
        return NULL;
    TempItems = (ATXPFI*)(Header + 1);                                          // Set the location of the items list
    Header->Authorize = 0;                                                      // Clear the authorize field
    *Block = SBlock;                                                            // Pass back where the header is stored
    *Offset = SOffset;

    for ( int i = 0; i < NumberItems; ++i ) {                                   // Clear the item list
        TempItems[i].Type = 0;
        TempItems[i].Location = NULL;
        TempItems[i].Block = -1;
        TempItems[i].NameBlock = -1;
    }

    // Now let's parse it for real
    Start = NumberItems = InputOffset = 0;                                      // Clear the state machine
    ParseState = AT_XP_START;
    Input = Raw;
    while ( (Item = GetNextItem(&Stop)) != AT_XP_EOF ) {                        // Search through the entire file
        switch( Item ) {                                                        // Depending on the type of the item
            case    AT_XP_INVALID:
                return NULL;
            case    AT_XP_COMMAND_START:                                        // Track where an item starts
            case    AT_XP_MARKER_START:
            case    AT_XP_BLOCK_START:
                Start = InputOffset;
                if ( Start == 1 ) Start = 0;
                break;
            case    AT_XP_COMMAND_STOP:
                Length = (Stop - Start) + 1;                                    // Get the length of the command
                                                                                // Figure out which command it is
                if ( !strnicmp(((Input + InputOffset) - Length) - 1, "!include", 8) ){// If it is an include request
                    if ( !(ProcessInclude(&(TempItems[NumberItems]))) )         // Process the include
                        return NULL;
                }
                else if ( !strnicmp(((Input + InputOffset) - Length) - 1, "!authorize", 10) ){// If it is an authorize request
                    if ( !(ProcessAuthorize(Header)) )                          // Process the authorize
                        return NULL;
                }
                else                                                            // Not a command we understand
                    return NULL;
                ++NumberItems;
                break;
            case    AT_XP_MARKER_STOP:
                Length = (Stop - Start) + 1;                                    // Get the length of the marker
                if ( !(Where = Template->GetCacheMemory(Length + 1, &SBlock,    // Get a spot to store the name of the marker
                    &SOffset)) )
                    return NULL;
                memcpy(Where, (Input + Start) - 1, Length);                     // Copy the name over
                *(Where + Length) = '\0';                                       // Null term it
                TempItems[NumberItems].Type = AT_XP_MARKER;                     // Set the rest of the item parameters
                TempItems[NumberItems].Length = 0;
                TempItems[NumberItems].Location = NULL;
                TempItems[NumberItems].Block = -1;
                TempItems[NumberItems].NameBlock = SBlock;
                TempItems[NumberItems].NameOffset = SOffset;
                ++NumberItems;
                break;
            case    AT_XP_BLOCK_STOP:
                Length = (Stop - Start) + 1;                                    // Get the length of the block
                if ( !(Where = Template->GetCacheMemory(Length + 1, &SBlock,    // Get a spot to store the block
                    &SOffset)) )
                    return NULL;
                memcpy(Where, (Input + Start) - 1, Length);                     // Copy the name over
                *(Where + Length) = '\0';                                       // Null term it
                TempItems[NumberItems].Type = AT_XP_BLOCK;                      // Set the rest of the item parameters
                TempItems[NumberItems].Length = Length;
                TempItems[NumberItems].Location = NULL;
                TempItems[NumberItems].Block = SBlock;
                TempItems[NumberItems].Offset = SOffset;
                TempItems[NumberItems].NameBlock =   -1;
                ++NumberItems;
                break;
            default:
                return NULL;
        }
    }
    *OutLength = (NumberItems * sizeof(ATXPFI)) + sizeof(ATXPFH);               // Pass back the total lsit length (each item plust the header)
    Header->NumberItems = NumberItems;                                          // Set the number of items in the list in the header
    return Header;
}
// **************************************************************************** ProcessInclude
// Includes are not parsed for Atlas requests.  Maybe in the future I can make
// a pass (or multiple passes) first to load all includes in to make a contiguous
// file and then do the follow on parsing...
char *ATXParse::ProcessInclude(                                                 // Called to process the include command
                                ATXPFI      *Item                               // Ptr to the item to be set
                                ) {
    FILE    *Include;
    char    FileName[AT_MAX_PATH], Scratch[AT_MAX_PATH], *Where;
    long    Stop, Start, FileLength, SBlock, SOffset, Tmp, Length;

    if ( (Tmp = GetNextItem(&Stop)) != AT_XP_MARKER_START )                     // Get the file name start
        return NULL;
    Start = Stop;
    if ( (Tmp = GetNextItem(&Stop)) != AT_XP_MARKER_STOP )                      // Get the file name end
        return NULL;
    Length = (Stop - Start);

    if ( Template->IncludeLength ) {                                            // If there is an include path
        strcpy(FileName, Template->IncludePath);
        if ( (Length + Template->IncludeLength) <= AT_MAX_PATH - 1 ) {          // Make sure name fits in bounds
            strncpy(Scratch, (Input + Start), Length);                          // Turn name into C string
            Scratch[Length] = '\0';
            strcat(FileName, Scratch);
        }
        else
            return NULL;
    }
    else {
        if ( Length <= AT_MAX_PATH - 1 ) {                                      // Make sure name fits in bounds
            strncpy(FileName, (Input + Start), Length);                         // Turn name into C string
            FileName[Length] = '\0';
        }
        else
            return NULL;
    }
    if ( !(Include = fopen(FileName,"r")) )                                     // Open the file
        return NULL;
    fseek(Include, 0, SEEK_END);                                                // Get the length of the file
    FileLength = ftell(Include);
    fseek(Include, 0, SEEK_SET);

    if ( !(Where = Template->GetCacheMemory(FileLength + 1, &SBlock, &SOffset)) ){// Get room in the cache to store this
        fclose(Include);
        return NULL;
    }
    Stop = fread(Where, FileLength, 1, Include);                                // Read it in
    fclose(Include);
    if ( Stop != 1) return NULL;
    *(Where + FileLength) = '\0';                                               // Null terminate it

    Item->Type =        AT_XP_COMMAND;                                          // Set the item parameters
    Item->CommandType = AT_XP_CM_INCLUDE;
    Item->Length =      FileLength;
    Item->Location =    NULL;
    Item->Block =       SBlock;
    Item->Offset =      SOffset;
    Item->NameBlock =   -1;

    return Where;
}
// **************************************************************************** ProcessAuthorize
char *ATXParse::ProcessAuthorize(                                               // Called to process the authorize command
                                ATXPFH      *Header                             // Ptr to the header
                                ) {
    long    Stop, Start, SBlock, SOffset, Tmp, Level, Length;
    char    String[11];

    if ( (Tmp = GetNextItem(&Stop)) != AT_XP_MARKER_START )                     // Get the authorize level start
        return NULL;
    Start = Stop;
    if ( (Tmp = GetNextItem(&Stop)) != AT_XP_MARKER_STOP )                      // Get the authorize level end
        return NULL;
    Length = (Stop - Start);
    if ( Length < 1 || Length > 10) return NULL;
    strncpy(String, (Input + Start), Length);                                   // Turn it into a C string
    String[Length] = '\0';
    Level = atol(String);
    Header->Authorize = Level;
    return (char*)(Header->Authorize);
}
// **************************************************************************** GetNextItem
long ATXParse::GetNextItem(                                                     // Call to parse the next item from the input
                            long        *Stop                                   // The end of the current item being returned
                            ) {
    char    *C = Input + InputOffset;                                           // Get the character where we are

    if ( !InputOffset ) {                                                       // Special case for the very first character
        if ( *C != '<' ) {                                                      // If it isn't a command, pass back the start of a block
            InputOffset++;
            *Stop = 0;
            return (ParseState = AT_XP_BLOCK_START);
        }
        else if ( *(C + 1) != '@' ) {
            InputOffset++;
            *Stop = 0;
            return (ParseState = AT_XP_BLOCK_START);
        }
    }
    if ( ParseState == AT_XP_COMMAND_STOP || ParseState == AT_XP_MARKER_STOP )  // Special case modes- we allow multiple tokens in an Atlas request
        ParseState = AT_XP_ACTIVE;

    while ( *C ) {                                                              // Until we reach the end of the file
        switch ( *C ) {
            case '<':                                                           // Start of Atlas request?
                if ( ParseState != AT_XP_INQUOTES ) {                           // Ignore almost everything in quotes mode
                    if ( *(C + 1) != '@' ) {                                    // Nope, keep going
                        if ( ParseState != AT_XP_BLOCK_START ) {
                            *Stop = InputOffset;
                            InputOffset++;
                            return (ParseState = AT_XP_BLOCK_START);
                        }
                        InputOffset++;
                        break;
                    }
                    if ( ParseState == AT_XP_BLOCK_START ) {
                        *Stop = InputOffset;
                        return (ParseState = AT_XP_BLOCK_STOP);
                    }
                    *Stop = InputOffset;
                    InputOffset += 2;                                           // Yep, we have an Atlas command, so move past it
                    ParseState = AT_XP_ACTIVE;
                }
                else
                    InputOffset++;                                              // Skip past it 'cause we are in qutemode
                break;
            case '@':                                                           // End of Atlas request?
                if ( ParseState != AT_XP_INQUOTES ) {                           // Ignore almost everything in quotes mode
                    if ( *(C + 1) != '>' ) {                                    // Not for us- move on
                        if ( ParseState != AT_XP_BLOCK_START ) {
                            *Stop = InputOffset;
                            InputOffset++;
                            return (ParseState = AT_XP_BLOCK_START);
                        }
                        InputOffset++;
                        break;
                    }
                    if ( ParseState == AT_XP_MARKER_START ) {                   // End of a marker?
                        *Stop = InputOffset;
                        return (ParseState = AT_XP_MARKER_STOP);
                    }
                    if ( ParseState == AT_XP_COMMAND_START ) {                  // End of a command?
                        *Stop = InputOffset;
                        return (ParseState = AT_XP_COMMAND_STOP);
                    }
                    InputOffset += 2;                                           // Yep, we finished an Atlas command, so move past it
                    if ( ParseState == AT_XP_ACTIVE )
                        ParseState = AT_XP_INACTIVE;
                }
                else
                    InputOffset++;                                              // Skip past it 'cause we are in qutemode
                break;
            case '"':                                                           // Quotes...
                if ( ParseState != AT_XP_BLOCK_START && ParseState != AT_XP_INQUOTES) {
                    *Stop = InputOffset;
                    InputOffset++;
                    return (ParseState = AT_XP_BLOCK_START);
                }
                else {
                    if ( ParseState != AT_XP_INQUOTES )
                        ParseState = AT_XP_INQUOTES;
                    else
                        ParseState = AT_XP_BLOCK_START;
                }
                InputOffset++;
                break;
            case '!':                                                           // Atlas command?
                if ( ParseState == AT_XP_ACTIVE ) {                             // If we are in active mode
                    *Stop = InputOffset;
                    InputOffset++;
                    return (ParseState = AT_XP_COMMAND_START);
                }
                else if ( ParseState != AT_XP_BLOCK_START && ParseState != AT_XP_INQUOTES ) {
                    *Stop = InputOffset;
                    InputOffset++;
                    return (ParseState = AT_XP_BLOCK_START);
                }
                InputOffset++;
                break;
            case ' ':                                                           // The end of a word we care about?
                if ( ParseState == AT_XP_MARKER_START ) {
                    *Stop = InputOffset;
                    InputOffset++;
                    return (ParseState = AT_XP_MARKER_STOP);
                }
                else if ( ParseState == AT_XP_COMMAND_START ) {
                    *Stop = InputOffset;
                    InputOffset++;
                    return (ParseState = AT_XP_COMMAND_STOP);
                }
                else if ( ParseState != AT_XP_BLOCK_START && ParseState != AT_XP_ACTIVE &&
                    ParseState != AT_XP_INQUOTES ) {
                    *Stop = InputOffset;
                    InputOffset++;
                    return (ParseState = AT_XP_BLOCK_START);
                }
                InputOffset++;
                break;
            default:
                switch ( ParseState ) {
                    case AT_XP_ACTIVE:
                        *Stop = InputOffset;
                        InputOffset++;
                        return ( ParseState = AT_XP_MARKER_START );
                    case AT_XP_INACTIVE:
                        *Stop = InputOffset;
                        InputOffset++;
                        return ( ParseState = AT_XP_BLOCK_START );
                    default:
                        InputOffset++;
                }
        }
        C = Input + InputOffset;
    }
    if ( ParseState == AT_XP_BLOCK_START || ParseState == AT_XP_INQUOTES ) {
        *Stop = InputOffset;
        return (ParseState = AT_XP_BLOCK_STOP);
    }
    return AT_XP_EOF;
}
// **************************************************************************** Initialize
int ATXParse::Initialize(                                                       // Initialize the class
                            ATXTemplate     *inTemplate,                        // Ptr to the template that is calling me
                            ATScratchMem    *inScratch                          // Ptr to a scratch memory object to use
                            ) {
    if ( !inTemplate || !inScratch ) return ATERR_BAD_PARAMETERS;

    Template = inTemplate;
    Scratch = inScratch;
    return ATERR_SUCCESS;
}
// **************************************************************************** Constructor
ATXParse::ATXParse() {
    Reset();
}
// **************************************************************************** Destructor
ATXParse::~ATXParse() {
}
// **************************************************************************** Reset
void ATXParse::Reset() {                                                        // Reset class members
    Scratch = NULL;
    Input = NULL;
    InputOffset = 0;
    ParseState = 0;
    Template = NULL;
}


