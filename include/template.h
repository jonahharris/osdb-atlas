#ifndef TEMPLATE_H
#define TEMPLATE_H
// ****************************************************************************
// * template.h - The XHTML template code for Atlas.                          *
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

typedef struct ATXTemplateInfoBlock ATXTInfo;
typedef struct ATXTemplateCacheEntry ATXCE;
typedef struct ATXTemplateBlockHeader ATXTBH;
typedef struct ATXParseFormHeader ATXPFH;
typedef struct ATXPFormItem ATXPFI;

// ****************************************************************************
// ****************************************************************************
//                                ATXParse
// ****************************************************************************
// ****************************************************************************
class ATXTemplate;
class ATXParse {                                                                // XHTML Parser class
private:
    char            *Input;                                                     // Ptr to the input file
    long            InputOffset;                                                // Byte offset within the input
    long            ParseState;                                                 // State of the parser
    ATScratchMem    *Scratch;                                                   // Scratch memory object to use
    ATXTemplate     *Template;
    void            Reset();                                                    // Reset class members
    char            *ProcessInclude(                                            // Called to process the include command
                                ATXPFI      *Item                               // Ptr to the item to be set
                                );
    char            *ProcessAuthorize(                                          // Called to process the authorize command
                                ATXPFH      *Header                             // Ptr to the header
                                );
    long            GetNextItem(                                                // Call to parse the next item from the input
                            long        *Stop                                   // The end of the current item being returned
                            );
public:
    ATXParse();
    ~ATXParse();
    int             Initialize(                                                 // Initialize the class
                            ATXTemplate     *inTemplate,                        // Ptr to the template that is calling me
                            ATScratchMem    *inScratch                          // Ptr to a scratch memory object to use
                            );
    ATXPFH          *ParseForm(                                                 // Return a ptr to a parsed form from raw XHTML input
                            char            *Raw,                               // Ptr to the raw HTML input
                            long            *OutLength,                         // Address of a long that will be set to the parsed form length
                            long            *Block,                             // The block where the header is stored
                            long            *Offset                             // The offset of the header within the block
                            );
};

// ****************************************************************************
// ****************************************************************************
//                                ATXTemplate
// ****************************************************************************
// ****************************************************************************

// **************************************************************************** ATXTemplate
class ATXTemplate {                                                             // Class to manage XHTML templates
private:
    volatile ATXPFH *CurrForm;                                                  // Current form we are working with
    volatile ATXTInfo *Info;                                                    // Ptr to the shared info struct for the cache
    volatile ATXTBH **Blocks;                                                   // Ptr to an array of blocks allocated
    long            MyBlocksAllocated;                                          // My number of cache blocks allocated
    ATBTree         Index;                                                      // BTree used as our index
    ATSharedTable   EntryTable;                                                 // Table used to track all page entries
    ATScratchMem    *Scratch;                                                   // Ptr to the scratch memory class to use
    ATXParse        Parser;                                                     // The XHTML parser
    char            IncludePath[AT_MAX_PATH];                                   // Base include path
    long            IncludeLength;                                              // Length of the base include path
    long            TrackingBlocks;                                             // Number of tracking blocks we have allocated
    long            BlockSize;                                                  // The size of each block allocated
    long            BlocksPerAlloc;                                             // Blocks to alloc each time
    ATSharedTable   CacheTable;                                                 // Table used as the template cache
    long            CacheTableKey;                                              // The cache table IPC key
    long            IndexKey;                                                   // The index IPC key
    long            EntryTableKey;                                              // The entry table IPC key

    void            Reset();                                                    // Reset the class's members
    volatile ATXTBH *GetNewBlock();                                             // Get a new block for the cache
                                                                                // DON'T CALL THIS UNLESS YOU HAVE THE CACHE EXCLUSIVELY LOCKED
    long            FindLabel(                                                  // Call to find a label within an item list
                            char        *Label                                  // Label to find
                            );
    int             SynchBlocks();                                              // Call to synchronize the local blocks with the global blocks
    int             AllocTracking(                                              // Call to make sure we have enough tracking space available
                            int         Needed                                  // Number needed to store (zero based)
                            );
    char            *GetCacheMemory(                                            // Call to get a block of cache memory
                            long        Bytes,                                  // Bytes to get
                            long        *Block,                                 // To be set to the block where the item is stored
                            long        *Offset                                 // To be set to the offest where the item is stored
                            );                                                  // DON'T CALL THIS UNLESS YOU HAVE CACHE EXCLUSIVE LOCKED
public:
    friend class ATXParse;
    // ****************************************************************************
    //                          SYSTEM CALLS
    // ****************************************************************************
    ATXTemplate();
    ~ATXTemplate();
    void            EmptyCache();                                               // Forces all templates out of cache (mostly development use)
    // ****************************************************************************
    //                          USER CALLS
    // ****************************************************************************
    int             Create(                                                     // Call to create
                            long            inAvgPageSize,                      // Your best guess at the average page size (probably 50k - 80k)
                            long            inPagesPerBlock,                    // Number of pages to allocate for at once (probably 50 - 100)
                            long            inBlocksPerAlloc,                   // Number of blocks to alloc each time
                            ATScratchMem    *inScratch,                         // Scratch memory class to use
                            int             inKeyRangeLow,                      // Systemwide unique IPC low entry for the template cache to use- EXPECT ALL NUMBERS IN RANGE TO BE USED
                            int             inKeyRangeHigh,                     // Systemwide unique IPC high entry for the template cache to use- EXPECT ALL NUMBERS IN RANGE TO BE USED
                                                                                // LEAVE PLENTY OF ROOM BETWEEN LOW & HIGH- At least your expected max number of blocks the cache will allocate multiplied by three- but BE GENEROUS
                            ULONG           inKilroy                            // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                            );
    int             Open(                                                       // Call to open an already created template
                            ATScratchMem    *inScratch,                         // Scratch memory class to use
                            int             inKeyRangeLow,                      // Systemwide unique IPC low entry for the template cache to use- EXPECT ALL NUMBERS IN RANGE TO BE USED
                            int             inKeyRangeHigh,                     // Systemwide unique IPC high entry for the template cache to use- EXPECT ALL NUMBERS IN RANGE TO BE USED
                            ULONG           inKilroy                            // My kilroy- a unique ID for the caller, like maybe the proc ID & the thread ID combined....
                            );
    int             GetTemplate(                                                // Call to get a template form into the template class
                            char        *Name                                   // Name of the template
                            );
    int             FreeTemplate();                                             // Call to free up the template form- ALWAYS CALL ASAP TO FREE LOCKS
    int             ReplaceLabel(                                               // Replaces a given label with a string (copy takes place)
                            char        *Label,                                 // Label to replace
                            char        *Value                                  // Value to replace label with
                            );
    int             AppendToLabel(                                              // Inserts a requested value immediately after the specified label- (copy takes place)
                                                                                //   multiple insertions allowed- they are inserted in order they are requested (first = top, last = bottom)
                                                                                // Note that this DOES use up your scratch memory very quickly, since every add is not actually an append (can't free scratch memory), but a new block with your line added...
                            char        *Label,                                 // Label to insert at
                            char        *Value                                  // Value to append
                            );
    int             PointLabelTo(                                               // Have label replaced with what is at the specified location (no copy takes place)
                            char        *Label,                                 // Label to have pointed to this data
                            char        *Data,                                  // Data to replace the label with
                            long        Length                                  // Length of the data to be used
                            );
    long            GetAuthorize();                                             // Returns the authorize value of the form
    int             WriteTemplate();                                            // Writes the template to STDOUT via stdio
    int             Close();                                                    // Close the class & free resources
    int             SetIncludePath(                                             // Call to set a base include path
                            char        *Path                                   // Path to set- can be called with NULL to reset
                            );
};

#endif

