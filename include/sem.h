#ifndef SEM_H
#define SEM_H
// ****************************************************************************
// * sem.h - The sempahore code for Atlas.                                    *
// * (c) 2002,2003 Shawn Houser, All Rights Reserved                          *
// * This property and it's ancillary properties are completely and solely    *
// * owned by Shawn Houser, and no part of it is a work for hire.             *
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

// Definition of a lock
#define     ATLOCK          volatile unsigned long

// Exclusive value for a sharelock
#define     AT_SHARE_EXC    ((unsigned long)0xF0000000)
// Inverse mask for exclusive
#define     AT_SHARE_EXC_I  ((unsigned long)0x0FFFFFFF)

struct ATCPUInformation {                                                       // A struct used to hold information on the host system
    int             NumberProcs;                                                // Number of processors
    int             CPUMHz;                                                     // Speed of the CPU(s)
    int             bogomips;                                                   // Horsepower of the CPU(s), as lame a benchmark as any other, but at least some general idea
};
typedef struct ATCPUInformation ATCPUInfo;


// ****************************************************************************
// ****************************************************************************
//                             KERNEL SEMAPHORES
// ****************************************************************************
// ****************************************************************************

// **************************************************************************** ATKernelSem
                                                                                // Kernel semaphores are shared/viewable among all processes
class   ATKernelSem {                                                           // Kernel semaphore object
private:
    int             ID;                                                         // ID of the semaphore
    int             SemOpBlock(                                                 // Perform a blocking sem assignment operation
                            int         SemVal                                  // Value to assign to sem
                            );
    int             SemOpBounce(                                                // Perform a non-blocking sem assignment operation
                            int         SemVal                                  // Value to assign to sem
                            );
    int             Remove();                                                   // Remove the semaphore
public:
    ATKernelSem();
    ~ATKernelSem();
    int             Create(                                                     // Create a named Kernel semaphore- fails if exists
                            int         Key                                     // Key of the semaphore to create (a unique non-zero number)
                            );
    int             Open(                                                       // Open access to an existing Kernel semaphore- fails if DOESN'T exist
                            int         Key                                     // Key of the semaphore to open (a unique non-zero number)
                            );
    int             GetLock();                                                  // Lock the semaphore, blocking until successful
    int             BounceLock();                                               // Try to lock, but return if busy
    int             FreeLock();                                                 // Free the lock
    int             Close();                                                    // Close the semaphore
};


// ****************************************************************************
// ****************************************************************************
//                               SPINLOCKS
// ****************************************************************************
// ****************************************************************************
/*  Spinlocks are simple, fast, exclusive locks.  If you need mixed read/write
locks, see the sharelocks below.
DON'T FORGET TO CALL ATInitSems().
*/
// **************************************************************************** ATInitSems
int ATInitSems();                                                               // ALWAYS CALL THIS BEFORE USING THE SPINLOCKS or SHARELOCKS!!!!!
// **************************************************************************** GetCPUInfo
ATCPUInfo *ATGetCPUInfo();                                                      // Returns ptr to an initialized CPUInfo struct
                                                                                // YOU MUST HAVE CALLED AT INIT SEMS FOR THIS TO WORK
// **************************************************************************** ATGetSpinLock
int ATGetSpinLock(                                                              // Get ownership of a spin lock, and don't return until we get it
                                                                                // These are process local locks, UNLESS they are located in shared memory
                    unsigned long   Kilroy,                                     // Unique, non-zero ID, such a the Proc ID and ThreadID rolled into one, etc.
                                                                                // Can't figure out a reasonable way to auto-gen this under Linux.  Too many possibilities for thread libraries, etc.
                    ATLOCK          *ALock                                      // Ptr to the lock (any 32bit sized piece of memory initialized to zero will do just fine, shared memory if you want a global lock)
                    );
// **************************************************************************** ATBounceSpinLock
int ATBounceSpinLock(                                                           // Get ownership of a spin lock, but return immediately if it is held
                                                                                // These are process local locks, UNLESS they are located in shared memory
                    unsigned long   Kilroy,                                     // Unique, non-zero ID, such a the Proc ID and ThreadID rolled into one, etc.
                                                                                // Can't figure out a reasonable way to auto-gen this under Linux.  Too many possibilities for thread libraries, etc.
                    ATLOCK          *ALock                                      // Ptr to the lock (any 32bit sized piece of memory initialized to zero will do just fine, shared memory if you want a global lock)
                    );
// **************************************************************************** ATFreeSpinLock
int ATFreeSpinLock(                                                             // Free up a lock- MAKES SURE YOU HAVE IT FIRST!
                    unsigned long   Kilroy,                                     // The Kilroy value you used to get the lock
                    ATLOCK          *ALock                                      // Ptr to the lock
                    );
// **************************************************************************** ATSpinLockArbitrate
int ATSpinLockArbitrate(                                                        // So you are trying to avoid a deadlock?  This helps.
                                                                                // Call it, increasing your attempts ctr (start at zero) each time, and it will help you spin constructively while you renegotiate a lock.
                    long            Attempts                                    // Number of attempts made so far
                    );


// ****************************************************************************
// ****************************************************************************
//                               SHARELOCKS
// ****************************************************************************
// ****************************************************************************
/* Sharelocks can have any number of readers (sharers), but only one writer (exclusive).
Very useful for shared data structures.  More overhead, but increased parallelism.
DON'T FORGET TO CALL ATInitSems().
*/
// **************************************************************************** ATBounceShare
int ATBounceShare(                                                              // Try to get a sharelock, return immediately if failed
                    ATLOCK          *ALock                                      // Lock to get
                    );
// **************************************************************************** ATGetShare
int ATGetShare(                                                                 // Try to get a sharelock, don't return until you get it
                    ATLOCK          *ALock                                      // Lock to get
                    );
// **************************************************************************** ATFreeShare
int ATFreeShare(                                                                // Frees a sharelock
                    ATLOCK          *ALock                                      // Lock to free
                    );
// **************************************************************************** ATGetShareExclusive
int ATGetShareExclusive(                                                        // Gets a sharelock in exclusive mode, blocks until all other readers are out
                    ATLOCK          *ALock                                      // Lock to get
                    );
// **************************************************************************** ATBounceShareExclusive
int ATBounceShareExclusive(                                                     // Tries to get a sharelock in exclusive mode, but returns immediately if another EXCLUSIVE lock exists, but will block until it gets it from other shared locks
                    ATLOCK          *ALock                                      // Lock to get
                    );
// **************************************************************************** ATQueueShareExclusive
// THIS IS AN EXTREMELY USEFUL, BUT POTENTIALLY DANGEROUS CALL IF MISUSED!  USE WITH CARE!  IT QUEUES THE LOCK BUT A SUCESS RETURN DOES NOT MEAN A FREE LOCK!
// AND ALSO DO NOT USE FreeShare() UNTIL ALL OTHER SHARES ARE GONE!  You can use WaitQueueExclusive to be sure.
int ATQueueShareExclusive(                                                      // Tries to get a sharelock in exclusive mode.  Fails only if someone else already has it- always immediate return regardless.
                                                                                // THIS CALL RETURNS *BEFORE* WAITING FOR ALL SHARE LOCKS TO FREE, so it is up to you to make sure the count is ZERO before you do anything unpleasant.
                                                                                // In other words, wait until (*ALock == AT_SHARE_EXC)
                    ATLOCK          *ALock                                      // Lock to get
                    );
// **************************************************************************** ATWaitQueueShareExclusive
int ATWaitQueueShareExclusive(                                                  // ONLY when you have made the QueueShareExclusive call, you may at then any time after call this to safely wait for the share locks to free
                                                                                // In other words, wait until (*ALock == AT_SHARE_EXC)
                    ATLOCK          *ALock                                      // Lock to get
                    );
// **************************************************************************** ATRemoveQueueShareExclusive
int ATRemoveQueueShareExclusive(                                                // Likely, you found out you no longer need this exclusive, and you want to free it before waiting for all the reads to clear.
                    ATLOCK          *ALock                                      // Lock to remove your queued exclusive from
                    );
// **************************************************************************** ATFreeShareExclusive
// If you have used QueueShareExclusive do NOT use this call until you are sure there are no shares still on the lock.
int ATFreeShareExclusive(                                                       // Free an exclusively held share lock
                    ATLOCK          *ALock                                      // The lock to free
                    );

// ****************************************************************************
// ****************************************************************************
//                              ATOMIC PRIMITIVES
// ****************************************************************************
// ****************************************************************************

// **************************************************************************** ATCompareAndExchange
static inline unsigned long ATCompareAndExchange(                               // Atomic compare and exchange for IA compatibles, 32bit.  Compare OLD with CELL, and if identical, store NEW in CELL.
                                volatile unsigned long *Cell,                   // Ptr to mem location where the op will be performed
                                unsigned long Old,                              // Original value (what the caller sees now)
                                unsigned long New                               // New value (what the caller wants to be there)
                                ) {
    unsigned long Orig;
    __asm__ __volatile__(   "lock ; cmpxchgl %1,%2"
                            : "=a"(Orig)
                            : "q"(New), "m"(*Cell), "0"(Old)
                            : "memory");
    if ( Orig == Old )  return ATERR_SUCCESS;
    else                return ATERR_OBJECT_IN_USE;
}
// **************************************************************************** ATAtomicAdd
static __inline__ void ATAtomicAdd(                                             // Atomically add 32bit int to variable
                                long Value,                                     // Value to add to variable
                                volatile long *Variable                         // Variable to add to
                                ) {
    __asm__ __volatile__(   "lock ; addl %1,%0"
                            :"=m" (*Variable)
                            :"ir" (Value), "m" (*Variable));
}
// **************************************************************************** ATAtomicSubtract
static __inline__ void ATAtomicSubtract(                                        // Atomically sub 32bit int from variable
                                long Value,                                     // Value to sub from variable
                                volatile long *Variable                         // Variable to sub from
                                ) {
    __asm__ __volatile__(	"lock ; subl %1,%0"
                            :"=m" (*Variable)
                            :"ir" (Value), "m" (*Variable));
}
// **************************************************************************** ATAtomicInc
static __inline__ void ATAtomicInc(                                             // Atomically increments a 32bit variable
                                volatile long *Variable                         // Variable to increment
                                ) {
    __asm__ __volatile__(   "lock ; incl %0"
                            :"=m" (*Variable)
                            :"m" (*Variable));
}
// **************************************************************************** ATAtomicDec
static __inline__ void ATAtomicDec(                                             // Atomically decrements a 32bit variable
                                volatile long *Variable                         // Variable to decrement
                                ) {
    __asm__ __volatile__(   "lock ; decl %0"
                            :"=m" (*Variable)
                            :"m" (*Variable));
}
// **************************************************************************** ATGetCPUTicks
static __inline__ int64 ATGetCPUTicks() {                                       // Returns the 64bit value of the current CPU ticks
    int64 Result;
    __asm__ __volatile__(  ".byte 0x0f, 0x31"
                        : "=A" (Result));
    return Result;
}

#endif
