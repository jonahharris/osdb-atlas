#ifndef MEMORY_H
#define MEMORY_H

// ****************************************************************************
// * memory.h - The memory code for Atlas.                                    *
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

/* ****************************************************************************
LINUX Notes: These routines use the SysV style mem routines under Linux- seems to be
the "safest" choice.  !!IMPORTANT!!:  Generally you will need to increase
the max shared mem segment size under Linux, maybe some other settings as well.
Luckily, you don't have to recompile the Kernel anymore if it is later than 2.2.
Use ipcs at the command line to see the various IPC settings.  Use shhmax to change
the max shared mem segment size.  Example:
echo 128000000 > /proc/sys/kernel/shmmax
Changes it to 128 megs.  Might want to put that in the ol' boot scripts too...

Here is the relevant bit from the Kernel source header for RedHat 7.2:
// * SHMMAX, SHMMNI and SHMALL are upper limits are defaults which can
//* be increased by sysctl

#define SHMMAX 0x2000000		 /* max shared seg size (bytes)
#define SHMMIN 1			 /* min shared seg size (bytes)
#define SHMMNI 4096			 /* max num of segs system wide
#define SHMALL (SHMMAX/PAGE_SIZE*(SHMMNI/16)) /* max shm system wide (pages)
#define SHMSEG SHMMNI			 /* max shared segs per process

So as you can see, some of these COULD be an issue.
*/

// ****************************************************************************
//                             ATSharedMem
// ****************************************************************************
class   ATSharedMem {                                                           // Shared memory object
private:
    int             Allocated;                                                  // Size of this object
    char            *Base;                                                      // True base of this object
    char            *UserBase;                                                  // Base of the object reported externally
    int             SysID;                                                      // The system ID for the object
    int             IAmCreator;                                                 // Flag indicating whether this proc is the one that created the block

    void            Reset();                                                    // Reset the object members
public:
    ATSharedMem();
    ~ATSharedMem();
    int             CreateSharedMem(                                            // Create a shared memory object- automatically attaches to it
                            int             Key,                                // Key ID for the object
                            int             Size                                // Size in bytes to be allocated
                            );
    int             AttachSharedMem(                                            // Attach to an already created shared memory object- returns a handle
                            int             Key                                 // Key ID for the object
                            );
    volatile void   *GetBasePointer();                                          // Returns a pointer to the base of the allocated memory
    int             DetachSharedMem();                                          // Detach from a shared memory object
    int             FreeSharedMem();                                            // Frees/destroys shared mem object after detaching from it
    int             FreeThisInstanceOnly();                                     // Destroys the object in the caller's memory only- leaves it up in everyone elses memory.
    int             GetSystemID();                                              // Return the system ID for this block
    volatile void   *GetTrueBasePointer();                                      // Returns a pointer to the TRUE base of the allocated memory
                                                                                // USE THIS ONLY IF YOU ABSOLUTEY NEED THE OS ADDRESS- YOU CAN REALLY MESS THINGS UP!
};

// ****************************************************************************
//                             ATScratchMem
// ****************************************************************************
class   ATScratchMem {                                                          // Scratch memory object
private:
    int             Allocated;                                                  // Bytes allocated in this object
    char            *Base;                                                      // Ptr to the base of the alloc'd memory for the object
    char            *Top;                                                       // Top + 1 of the allocated memory
    char            *CurrentOffset;                                             // Ptr to the current offset within the block
    char            *HighWater;                                                 // Highest offset ever reached by this object

    void            Reset();                                                    // Reset the object members
public:
    ATScratchMem(                                                               // Construct the object with a given size in bytes
                            int             Size                                // Size in bytes to construct
                            );
    ~ATScratchMem();
    void            *GetScratchMem(                                             // Get a block of the scratch area- returns a ptr to the block, will be processor aligned
                            int             Size                                // Size in bytes to get
                            );
    void            ResetScratchMem();                                          // Reset the scratch memory object (does not reset high water mark)
    int             GetHighWater();                                             // Returns the high water mark in bytes for the object (highest point allocated for the object)
};


// ****************************************************************************
//                             HELPER METHODS
// ****************************************************************************
// **************************************************************************** ATAlignPtr
char    *ATAlignPtr(                                                            // Function to align a ptr to AT_MEM_ALIGN- always forwards!
                    char        *Ptr                                            // Ptr to align
                    );
// **************************************************************************** ATDestroySharedMem
int     ATDestroySharedMem(                                                     // Sometimes you need to kill a shared memory block that you do not have the control object for- here is how to do that.
                                                                                // Only the owner, creator, or superuser can call.
                    int         SID                                             // System ID for the object
                    );
// **************************************************************************** ATDetachSharedMem
int     ATDetachSharedMem(                                                      // Detach from a shared memory alloc
                    volatile void *Base                                         // Base ptr of the alloc to detach from
                    );


#endif
