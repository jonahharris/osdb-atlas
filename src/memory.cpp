// ****************************************************************************
// * memory.cpp - The memory code for Atlas.                                  *
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

#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <assert.h>
#include <ctype.h>

#ifdef      AT_WIN32
    #include	<windows.h>
#else
    #include    <unistd.h>
    #include    <sys/types.h>
    #include    <sys/ipc.h>
    #include    <sys/shm.h>
#endif

#include "memory.h"
#include "general.h"


// ****************************************************************************
// ****************************************************************************
//                             SHARED MEMORY METHODS
// ****************************************************************************
// ****************************************************************************

// **************************************************************************** Constructor
ATSharedMem::ATSharedMem() {
    Reset();                                                                    // Clean up the object
}

// **************************************************************************** Destructor
ATSharedMem::~ATSharedMem() {
    if ( IAmCreator && Base ) FreeSharedMem();                                  // If I created this, try to clean it up
    Reset();                                                                    // Clean up the object
}

// **************************************************************************** CreateSharedMem
int    ATSharedMem::CreateSharedMem(                                            // Create a shared memory object- returns a handle to the object & automatically attaches to it
                            int             Key,                                // Key ID for the object
                            int             Size                                // Size in bytes to be allocated
                            ) {
    if ( Base != NULL ) return ATERR_OBJECT_IN_USE;                             // Don't allow this object to be screwed up

    SysID = shmget((key_t)Key, Size + sizeof(int), IPC_CREAT | IPC_EXCL | 0666 );// Try to alloc the memory ( with a little extra for us )

    if ( SysID > -1 ) {                                                         // If we could allocate the memory...
        Base = (char *)shmat(SysID, 0, 0);                                      // Now attach to it
        if ( (int)Base == -1 ) return ATERR_OUT_OF_MEMORY;
        Allocated = Size;
        UserBase = Base + sizeof(long);                                         // Save a ptr to the user memory
        *((long *)Base) = Allocated;                                            // We write out the size of the block as the first int
        IAmCreator = 1;                                                         // Save the fact that I am the creator of the memory
        return ATERR_SUCCESS;
    }
    Reset();                                                                    // Clean up the object
    return ATERR_OUT_OF_MEMORY;
}

// **************************************************************************** AttachSharedMem
int     ATSharedMem::AttachSharedMem(                                           // Attach to an already created shared memory object- returns a handle
                            int             Key                                 // Key ID for the object
                            ) {
    if ( Base != NULL ) return ATERR_OBJECT_IN_USE;                             // Don't allow this object to be screwed up

    SysID = shmget((key_t)Key, 0, 0);                                           // Try to access the memory
    if ( SysID > -1 ) {                                                         // If we could access the memory...
        Base = (char *)shmat(SysID, 0, 0);                                      // Now attach to it
        if ( (int)Base == -1 ) return ATERR_OUT_OF_MEMORY;
        Allocated = *((long *)Base);                                            // Get the size from the first int
        UserBase = Base + sizeof(long);                                         // Save a ptr to the user memory
        return ATERR_SUCCESS;
    }
    Reset();                                                                    // Clean up the object
    return ATERR_OUT_OF_MEMORY;
}

// **************************************************************************** GetBasePointer
volatile void *ATSharedMem::GetBasePointer() {                                  // Returns a pointer to the base of the allocated memory
    return UserBase;
}

// **************************************************************************** GetTrueBasePointer
volatile void *ATSharedMem::GetTrueBasePointer() {                              // Returns a pointer to the TRUE base of the allocated memory
                                                                                // USE THIS ONLY IF YOU ABSOLUTEY NEED THE OS ADDRESS- YOU CAN REALLY MESS THINGS UP!
    return Base;
}

// **************************************************************************** DetachSharedMem
int     ATSharedMem::DetachSharedMem(                                           // Detach from a shared memory object
                            ){
    if ( Base ) shmdt(Base);                                                    // If a valid attach, detach from it
    Reset();                                                                    // Clean up the object
    return ATERR_SUCCESS;
}

// **************************************************************************** FreeSharedMem
int     ATSharedMem::FreeSharedMem() {                                          // Frees/destroys shared mem object after detaching from it
                                                                                // Only the owner, creator, or superuser can call.
    if ( !SysID ) return ATERR_SUCCESS;

    if ( !IAmCreator )                                                          // If I did NOT create it
        DetachSharedMem();                                                      // Just detach from it
    else {                                                                      // If I DID create it, then destroy it
        struct shmid_ds Dummy;
        shmctl(SysID, IPC_RMID, &Dummy);                                        // Ask system to remove it
    }
    Reset();                                                                    // Clean up the object
    return ATERR_SUCCESS;
}

// **************************************************************************** FreeThisInstanceOnly
int     ATSharedMem::FreeThisInstanceOnly() {                                   // Destroys the object in the caller's object memory only- leaves it up in everyone elses memory.
    Reset();                                                                    // Clean up the object
    return ATERR_SUCCESS;
}

// **************************************************************************** Reset
void     ATSharedMem::Reset() {                                                 // Reset the object members
    Base = UserBase = NULL;
    Allocated = SysID = IAmCreator = 0;
}

// **************************************************************************** GetSystemID
int     ATSharedMem::GetSystemID() {                                            // Return the system ID for this block
    return SysID;
}


// ****************************************************************************
// ****************************************************************************
//                             SCRATCH MEMORY METHODS
// ****************************************************************************
// ****************************************************************************

// **************************************************************************** Constructor
ATScratchMem::ATScratchMem(                                                     // Construct the object with a given size in bytes
                            int             Size
                            ) {
    if ( (Base = new char[Size]) ) {                                            // If I can alloc the mem....
        Allocated = Size;                                                       // Initialize the data members
        Top = Base + Size;
        CurrentOffset = HighWater = Base;
    }
    else                                                                        // Couldn't do it
        Reset();                                                                // Clean up the object

}

// **************************************************************************** Destructor
ATScratchMem::~ATScratchMem() {
    if ( Base )     delete Base;                                                // If the allocation exists, delete it
}

// **************************************************************************** GetScratchMem
void    *ATScratchMem::GetScratchMem(                                           // Get a block of the scratch area- returns a ptr to the block, will be processor aligned
                            int             Size                                // Size in bytes to get
                            ) {
    char            *Original = CurrentOffset;                                  // Save existing ptr

    CurrentOffset += Size + AT_MEM_ALIGN;                                       // Add the size & alignment space to the current position
    CurrentOffset = ATAlignPtr(CurrentOffset);                                  // Align the ptr

    if ( CurrentOffset > HighWater ) HighWater = CurrentOffset;                 // Check against the high water mark

    if ( CurrentOffset < Top ) {                                                // Make sure valid & then return the spot
        return Original;
    }
    CurrentOffset = Original;                                                   // Reset & return NULL
    return NULL;
}

// **************************************************************************** ResetScratchMem
void ATScratchMem::ResetScratchMem() {                                          // Reset the scratch memory object (does not reset high water mark)
    CurrentOffset = Base;                                                       // Just adjust the offset ptr back
}

// **************************************************************************** GetHighWater
int ATScratchMem::GetHighWater() {                                              // Returns the high water mark in bytes for the object (highest point allocated for the object)
    return HighWater - Base;
}

// **************************************************************************** Reset
void     ATScratchMem::Reset() {                                                // Reset the object members
    Allocated = 0;
    Base = Top = CurrentOffset = HighWater = NULL;
}


// ****************************************************************************
// ****************************************************************************
//                             HELPER ROUTINES
// ****************************************************************************
// ****************************************************************************

// **************************************************************************** ATAlignPtr
char    *ATAlignPtr(                                                            // Function to align a ptr to AT_MEM_ALIGN- always forwards!
                    char        *Ptr                                            // Ptr to align
                    ) {
    unsigned int    Int = (unsigned int)Ptr;
    Int = ((Int + (AT_MEM_ALIGN - 1)) & ~(AT_MEM_ALIGN - 1));                   // Do the align
    return (char *)Int;
}

// **************************************************************************** ATDestroySharedMem
int     ATDestroySharedMem(                                                     // Sometimes you need to kill a shared memory block that you do not have the control object for- here is how to do that.
                                                                                // Only the owner, creator, or superuser can call.
                    int         SID                                             // System ID for the object
                    ) {
    struct shmid_ds Dummy;
    shmctl(SID, IPC_RMID, &Dummy);                                              // Ask OS to remove the object
    return ATERR_SUCCESS;
}

// **************************************************************************** ATDetachSharedMem
int     ATDetachSharedMem(                                                      // Detach from a shared memory alloc
                    volatile void *Base                                         // Base ptr of the alloc to detach from
                    ) {
    if ( Base ) shmdt((void*)Base);                                             // If not null, ask the OS to detach me from the object
    return ATERR_SUCCESS;
}


