// ****************************************************************************
// * sem.cpp - The sempahore code for Atlas.                                  *
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

/*  General Notes:  Routines assume a 32bit+ IA compatible CPU, with guaranteed 32bit r/w integrity and atomicity
(which is every IA compatible CPU made at time of writing, but there are rumors of change in the air).
*/

/*  LINUX Notes:  Uses SysV IPC for the Kernel sem.  Yecch.  At the time I am writing this, POSIX stuff
is only starting to dribble in, and what sem functions there are are not truly shareable between procs.  And
by the way, ftok() under Linux is so broken as to be unsafe as of version 2.4.  This class seems to be
reliable in dealing with the many Linux SysV implementation problems, but be careful in porting...

This basic methodology of making SysV stuff useable is based on "UNIX Network Programming", V1, pg. 146.
(thanks to the late W. Richard Stevens).  A lot of his notes might be helpful here.
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
#include "sem.h"

#define     AT_BIGCOUNT        ((int)10000)

// Following are our semaphore operations defines (Have I mentioned I hate SysV?)
static struct sembuf    op_lock[2] = {
    2, 0, 0,            // Wait for [2] (lock) to equal 0
    2, 1, SEM_UNDO      // Then inc [2] to 1- this locks it
                        // UNDO to release the lock if processes exits before explicitly unlocking
};
static struct sembuf    op_endcreate[2] = {
    1, -1, SEM_UNDO,    // Dec [1] (proc counter) with undo on exit
                        // UNDO to adjust proc counter if process exits before calling close()
    2, -1, SEM_UNDO
};
static struct sembuf    op_open[1] = {
    1, -1, SEM_UNDO     // Dec [1] (proc counter) with undo on exit
};
static struct sembuf    op_close[3] = {
    2, 0, 0,            // Wait for [2] (lock) to equal 0
    2, 1, SEM_UNDO,     // Then inc [2] to 1- this locks it
    1, 1, SEM_UNDO      // Then inc [1] (proc counter)
};
static struct sembuf    op_unlock[1] = {
    2, -1, SEM_UNDO     // Dec [2] (lock) back to 0
};
static struct sembuf    op_op[1] = {
    0, 99, SEM_UNDO     // Dec or inc [0] with undo on exit
                        // The 99 is set to the actual amount to add or sub (pos or neg)
};
static struct sembuf    op_nbop[1] = {
    0, 99, ( SEM_UNDO | IPC_NOWAIT )// Dec or inc [0] with undo on exit, no blocking
                        // The 99 is set to the actual amount to add or sub (pos or neg)
};
union {
    int                 val;
    struct semid_ds     *buf;
    ushort              *array;
} semctl_arg;

ATCPUInfo   CPUInfo;

// ****************************************************************************
// ****************************************************************************
//                           KERNEL SEMAPHORE METHODS
// ****************************************************************************
// ****************************************************************************

// **************************************************************************** Constructor
ATKernelSem::ATKernelSem() {
    ID = -1;
};
// **************************************************************************** Destructor
ATKernelSem::~ATKernelSem() {
    if ( ID > -1 ) Close();
};
// **************************************************************************** Create
int ATKernelSem::Create(                                                        // Create a named Kernel semaphore- fails if exists
                            int         Key                                     // Key of the semaphore to create (a unique non-zero number)
                            ) {
    register int    id, semval;
    int             initval = 1;

    if ( ID > -1 ) return ATERR_OBJECT_IN_USE;
    if ( Key == IPC_PRIVATE || Key == (key_t)-1 )   return ATERR_BAD_PARAMETERS;// Not for priv sems

again:
    if ( (id = semget(Key, 3, 0666 | IPC_CREAT | IPC_EXCL )) < 0 )              // Create our group of 3 sems (we use 3 to provide some semblance of stability- see Stevens for explanation on broken SysV)
        return ATERR_OPERATION_FAILED;

    if ( semop(id, &op_lock[0], 2) < 0) {                                       // Lock the semaphore (guard against race condition...)
        if (errno == EINVAL)    goto again;
        return ATERR_OPERATION_FAILED;
    }

    semctl_arg.val = 0;
    if ( (semval = semctl(id, 1, GETVAL, semctl_arg)) < 0)                      // Make sure nobody else has init'd the sem yet (race condition...)
        return ATERR_OPERATION_FAILED;
    if ( semval == 0 ) {
        semctl_arg.val = initval;                                               // Let's init the sem values now
        if ( semctl(id, 0, SETVAL, semctl_arg) < 0)
            return ATERR_OPERATION_FAILED;
        semctl_arg.val = AT_BIGCOUNT;
        if ( semctl(id, 1, SETVAL, semctl_arg) < 0)
            return ATERR_OPERATION_FAILED;
    }

    if (semop(id, &op_endcreate[0], 2) < 0)                                     // Dec & release the lock
            return ATERR_OPERATION_FAILED;

    ID = id;
    return ATERR_SUCCESS;
}
// **************************************************************************** Open
int ATKernelSem::Open(                                                          // Open access to an exsting Kernel semaphore- fails if DOESN'T exist
                            int         Key                                     // Key of the semaphore to open (a unique non-zero number)
                            ) {
    register int    id;
    int             initval = 0;

    if ( ID > -1 ) return ATERR_OBJECT_IN_USE;
    if ( Key == IPC_PRIVATE || Key == (key_t)-1 )   return ATERR_BAD_PARAMETERS;// Not for priv sems

    if ( (id = semget(Key, 3, 0)) < 0 )                                         // Try to open the sem
        return ATERR_OPERATION_FAILED;

    if (semop(id, &op_open[0], 1) < 0)                                          // Dec instance
    // Now, Stevens says we don't need a lock for the above.  I'm not going to worry with it now, but I'm bothered by it.
        return ATERR_OPERATION_FAILED;

    ID = id;
    return ATERR_SUCCESS;
}
// **************************************************************************** Close
int ATKernelSem::Close() {                                                      // Close the semaphore
    int     semval;

    if ( ID == -1 ) return ATERR_SUCCESS;

    if ( semop(ID, &op_close[0], 3) < 0 )                                       // Try to lock & inc the ctr
        return ATERR_OPERATION_FAILED;

    semctl_arg.val = 0;
    if ( (semval = semctl(ID, 1, GETVAL, semctl_arg)) < 0 )                     // Check the counter
        return ATERR_OPERATION_FAILED;

    if ( semval == AT_BIGCOUNT )                                                // If I am the last guy out, I can delete this.
        Remove();
    else {
        if ( semop(ID, &op_unlock[0], 1) < 0 )                                  // Else just unlock it
            return ATERR_OPERATION_FAILED;
    }

    ID = -1;
    return ATERR_SUCCESS;
}
// **************************************************************************** Remove
int ATKernelSem::Remove() {                                                     // Remove the semaphore
    semctl_arg.val = 0;
    if ( semctl(ID, 0, IPC_RMID, semctl_arg) < 0 )                              // Remove it
        return ATERR_OPERATION_FAILED;
    return ATERR_SUCCESS;
}
// **************************************************************************** GetLock
int ATKernelSem::GetLock() {                                                    // Lock the semaphore, blocking until successful
    return SemOpBlock(-1);
}
// **************************************************************************** FreeLock
int ATKernelSem::FreeLock() {                                                   // Free the semaphore
    return SemOpBlock(1);
}
// **************************************************************************** BounceLock
int ATKernelSem::BounceLock() {                                                 // Try to lock the semaphore, but return immediately if already locked
    return SemOpBounce(-1);
}
// **************************************************************************** SemOpBlock
int  ATKernelSem::SemOpBlock(                                                   // Perform a blocking sem operation
                    int         SemVal                                          // Value to assign to sem
                    ) {
    if ( ( op_op[0].sem_op = SemVal ) == 0 )                                    // Assign argument
        return ATERR_BAD_PARAMETERS;                                            // Zero assign is bad mojo

    if ( semop(ID, &op_op[0], 1) < 0 )
        return ATERR_OPERATION_FAILED;

    return ATERR_SUCCESS;
}
// **************************************************************************** SemOpBounce
int  ATKernelSem::SemOpBounce(                                                  // Perform a blocking sem operation
                    int         SemVal                                          // Value to assign to sem
                    ) {
    if ( (op_nbop[0].sem_op = SemVal ) == 0 )                                   // Assign argument
        return ATERR_BAD_PARAMETERS;                                            // Zero assign is bad mojo

    if ( semop(ID, &op_nbop[0], 1) < 0 )
        return ATERR_OBJECT_IN_USE;

    return ATERR_SUCCESS;
}


// ****************************************************************************
// ****************************************************************************
//                               SPINLOCKS
// ****************************************************************************
// ****************************************************************************
/*  Spinlocks are simple, fast, exclusive locks.  If you need mixed read/write
locks, see the sharelocks below.
*/
// **************************************************************************** ATInitSems
int ATInitSems() {                                                              // ALWAYS CALL THIS BEFORE USING THE SPINLOCKS or SHARELOCKS!!!!!
    FILE    *Info;
    char    Buffer[512], *Token;

    Info = fopen("/proc/cpuinfo", "r");                                         // Try to read CPU info
    if ( !Info )                                                                // This will likely fail on many UNIX versions....
        return ATERR_BAD_PARAMETERS;

    CPUInfo.NumberProcs = 0;                                                    // Clear the struct for safety...
    CPUInfo.CPUMHz = 0;
    CPUInfo.bogomips = 0;

    while ( fgets( Buffer, 511, Info ) ) {                                      // Loop thru the file
        if ( !strnicmp(Buffer, "processor",9) )                                 // Count the processors
            CPUInfo.NumberProcs++;
        else if ( !strnicmp(Buffer, "cpu MHz", 7) ) {                           // Get the speed
            Token = strstr(Buffer, ":");
            if ( Token )
                CPUInfo.CPUMHz = atol(Token + 1);
        }
        else if ( !strnicmp(Buffer, "bogomips", 8) ) {                          // Get the bogomips (won't be found on non-linux systems)
            Token = strstr(Buffer, ":");
            if ( Token )
                CPUInfo.bogomips = atol(Token + 1);
        }
    }

    fclose(Info);
    return ATERR_SUCCESS;
}
// **************************************************************************** GetCPUInfo
ATCPUInfo *ATGetCPUInfo() {                                                     // Returns ptr to an initialized CPUInfo struct
                                                                                // YOU MUST HAVE CALLED AT INIT SEMS FOR THIS TO WORK
    return  &CPUInfo;
}
// **************************************************************************** ATSpin
long    ATSpin(long Number, volatile long *RedHerring) {                        // An internal routine to just spin waiting for a lock rather than give up our time slice and enter the scheduler
    volatile int i;
    for ( i = 0; i < Number; ++i ) {                                            // Just churn, working with a variable the optimizer is afraid to ignore
        *RedHerring = i;
    }
    return i;                                                                   // Try to keep optimizer from getting too smart...
}
// **************************************************************************** ATAdaptiveControl
void    ATAdaptiveControl(                                                      // Routine to handle contested locks
                    long            NumberAttempts                              // Number of attempts made to date to get the lock
                        ) {
    volatile long RH;

//    if ( NumberAttempts > 7 ) {                                                 // Uncomment for debug message
//        printf("Extreme lock contention... %i\r\n", NumberAttempts);
//    }

    if ( CPUInfo.NumberProcs > 1 ) {                                            // Change behavior on non-SMP boxes
        switch( NumberAttempts ) {                                              // Change behavior over time
            case    0:  ATSpin(3, &RH);     break;                              // At first, try assuming the spinlock is being held for a quickly used & freed resource
            case    1:  ATSpin(7, &RH);     break;
            case    2:  ATSpin(9, &RH);     break;
            case    3:  ATSpin(101, &RH);   break;
            case    4:  ATSpin(1007, &RH);  break;
            case    5:  usleep(10);         break;                              // Okay, at this point something is wrong, this was not a good spinlock candidate, or maybe the scheduler is letting us down, or it is a hotspot, etc.
            case    6:  ATSpin(7, &RH);     break;
            case    7:  ATSpin(101, &RH);   break;
            case    8:  usleep(10);         break;
            case    9:  ATSpin(103, &RH);   break;
            case    10: usleep(10);         break;                              // Okay, at this point something is wrong, this was not a good spinlock candidate, or maybe the scheduler is letting us down, or it is a hotspot, etc.
            case    11: usleep(100);        break;                              // Let's try freeing up the CPU a tiny bit so we have a chance at this thing
            case    12: ATSpin(101, &RH);   break;
            case    13: usleep(100);        break;
            case    14: usleep(100);        break;
            case    15: ATSpin(103, &RH);   break;
            case    16: usleep(1000);       break;                              // Looks like something is wrong, or this is a poor spinlock candidate, or maybe just a fluke
            case    17: usleep(1000);       break;
            case    18: ATSpin(101, &RH);   break;
            case    19: usleep(10000);      break;
            case    20: sleep(1);           break;
            default:    sleep(1);                                               // By this time we are probably locked up, so let's give it plenty of free CPU
        }
    }
    else {                                                                      // This is a SINGLE proc version
        switch( NumberAttempts ) {                                              // Change behavior over time
            case    0:  usleep(10);         break;                              // On a single proc box, spinning is likely the stupidest thing you can do...
            case    1:  usleep(10);         break;                              // Let's try freeing up the CPU a tiny bit so we have a chance at this thing
            case    2:  usleep(100);        break;
            case    3:  usleep(100);        break;
            case    4:  usleep(1000);       break;
            case    5:  usleep(1000);       break;
            case    6:  usleep(10000);      break;                              // Looks like something is wrong, or this is a poor spinlock candidate, or maybe just a fluke
            case    7:  usleep(10000);      break;
            case    8:  usleep(100000);     break;
            case    9:  usleep(100000);     break;
            case    10: sleep(1);           break;
            case    11: sleep(1);           break;
            default:    sleep(1);                                               // By this time we are probably locked up, so let's give it plenty of free CPU
        }
    }
}
// **************************************************************************** ATGetSpinLock
int ATGetSpinLock(                                                              // Get ownership of a spin lock, and don't return until we get it
                                                                                // These are process local locks, UNLESS they are located in shared memory
                    unsigned long   Kilroy,                                     // Unique, non-zero ID, such a the Proc ID and ThreadID rolled into one, etc.
                                                                                // Can't figure out a reasonable way to auto-gen this under Linux.  Too many possibilities for thread libraries, etc.
                    ATLOCK          *ALock                                      // Ptr to the lock (any 32bit sized piece of memory initialized to zero will do just fine, shared memory if you want a global lock)
                    ) {
    long    Result, NumberAttempts = 0;                                         // Number of attempts made to get the lock
retry:
    if ( !(*ALock) ) {                                                          // No point even trying if it is locked
        if ( (Result = ATCompareAndExchange(ALock, 0, Kilroy)) == ATERR_SUCCESS )// Try the lock
            return ATERR_SUCCESS;
    }
    ATAdaptiveControl(NumberAttempts);                                          // Adaptive behavior for lock contention
    NumberAttempts++;                                                           // Inc our number of attempts
    goto retry;                                                                 // Give it another try
}
// **************************************************************************** ATBounceSpinLock
int ATBounceSpinLock(                                                           // Get ownership of a spin lock, but return immediately if it is held
                                                                                // These are process local locks, UNLESS they are located in shared memory
                    unsigned long   Kilroy,                                     // Unique, non-zero ID, such a the Proc ID and ThreadID rolled into one, etc.
                                                                                // Can't figure out a reasonable way to auto-gen this under Linux.  Too many possibilities for thread libraries, etc.
                    ATLOCK          *ALock                                      // Ptr to the lock (any 32bit sized piece of memory initialized to zero will do just fine, shared memory if you want a global lock)
                    ) {
    int     Result;
    if ( !(*ALock) ) {                                                          // No point even trying if it is locked
        if ( (Result = ATCompareAndExchange(ALock, 0, Kilroy)) == ATERR_SUCCESS )// Try the lock
            return ATERR_SUCCESS;
    }
    return ATERR_OBJECT_IN_USE;
}
// **************************************************************************** ATFreeSpinLock
int ATFreeSpinLock(                                                             // Free up a lock- MAKES SURE YOU HAVE IT FIRST!
                    unsigned long   Kilroy,                                     // The Kilroy value you used to get the lock
                    ATLOCK          *ALock                                      // Ptr to the lock (any 32bit sized piece of memory initialized to zero will do just fine, shared memory if you want a global lock)
                    ) {
    if ( *ALock == Kilroy )                                                     // As long as it is yours...
        *ALock = 0;                                                             // Free it
    else                                                                        // You don't have it!
        return ATERR_BAD_PARAMETERS;
    return ATERR_SUCCESS;
}
// **************************************************************************** ATSpinLockArbitrate
int ATSpinLockArbitrate(                                                        // So you are trying to avoid a deadlock?  This helps.
                                                                                // Call it, increasing your attempts ctr (start at zero) each time, and it will help you spin constructively while you renegotiate a lock.
                    long            Attempts                                    // Number of attempts made so far
                    ) {
    ATAdaptiveControl(Attempts);
    return ATERR_SUCCESS;
}

// ****************************************************************************
// ****************************************************************************
//                               SHARELOCKS
// ****************************************************************************
// ****************************************************************************
/* Sharelocks can have any number of readers (sharers), but only one writer (exclusive(.
Very useful for shared data structures.  More overhead, but increased parallelism.
*/

// **************************************************************************** ATBounceShare
int ATBounceShare(                                                              // Try to get a sharelock, return immediately if failed
                    ATLOCK          *ALock                                      // Lock to get
                    ) {
    if ( !(*ALock & AT_SHARE_EXC) ) {                                           // No use even trying if it is already locked
        ATAtomicInc((volatile long*)ALock);                                     // Try getting it
        if ( !(*ALock & AT_SHARE_EXC) ) {                                       // Make sure we were successful
            return ATERR_SUCCESS;
        }
        ATAtomicDec((volatile long*)ALock);                                     // We didn't get it, so pull our inc off of it
    }
    return ATERR_OBJECT_IN_USE;
}
// **************************************************************************** ATGetShare
int ATGetShare(                                                                 // Try to get a sharelock, don't return until you get it
                    ATLOCK          *ALock                                      // Lock to get
                    ) {
    long    NumberAttempts = 0;                                                 // Number of attempts made to get the lock
    volatile long RH;
retry:
    if ( !(*ALock & AT_SHARE_EXC) ) {                                           // No use even trying if it is already locked
        ATAtomicInc((volatile long*)ALock);                                     // Try getting it
        if ( !(*ALock & AT_SHARE_EXC) ) {                                       // Make sure we were successful
            return ATERR_SUCCESS;
        }
        ATAtomicDec((volatile long*)ALock);                                     // We didn't get it, so pull our inc off of it
    }
    ATAdaptiveControl(NumberAttempts);                                          // Adaptive behavior for lock contention
    NumberAttempts++;                                                           // Inc our number of attempts
    goto retry;                                                                 // Give it another try
}
// **************************************************************************** ATFreeShare
int ATFreeShare(                                                                // Frees a sharelock
                    ATLOCK          *ALock                                      // Lock to free
                    ) {
    ATAtomicDec((volatile long*)ALock);                                         // Dec the lock
    return ATERR_SUCCESS;
}
// **************************************************************************** ATGetShareExclusive
int ATGetShareExclusive(                                                        // Gets a sharelock in exclusive mode, blocks until all other readers are out
                    ATLOCK          *ALock                                      // Lock to get
                    ) {
    unsigned int    Orig, New, Result, NumberAttempts = 0;

retry_exclusive:
    Orig = *ALock;                                                              // What is the lock now
    if ( !( Orig & AT_SHARE_EXC ) ) {                                           // Make sure nobody already has it
        New = Orig | AT_SHARE_EXC;                                              // Add in our exclusive flag
        // Note that we HAVE to use compare & exchange here, since with no kilroys we don't want two different guys thinking they are the one with the exclusive
        if ( (Result = ATCompareAndExchange(ALock, Orig, New)) != ATERR_SUCCESS ){// Try the lock
            ATAdaptiveControl(NumberAttempts);                                   // Adaptive behavior for lock contention
            NumberAttempts++;
            goto retry_exclusive;
        }
    }
    else {
        ATAdaptiveControl(NumberAttempts);                                      // Adaptive behavior for lock contention
        NumberAttempts++;
        goto retry_exclusive;
    }

    // Once we are here, we have the exclusive but we still might have shares active... need to wait fot them to clear
    NumberAttempts = 0;
share_wait:
    if ( *ALock != AT_SHARE_EXC ) {                                             // Wait until everyone else is out...
        ATAdaptiveControl(NumberAttempts);                                      // Adaptive behavior for lock contention
        NumberAttempts++;
        goto share_wait;
    }

    return ATERR_SUCCESS;
}
// **************************************************************************** ATBounceShareExclusive
int ATBounceShareExclusive(                                                     // Tries to get a sharelock in exclusive mode, but returns immediately if another EXCLUSIVE lock exists, but will block until it gets it from other shared locks
                    ATLOCK          *ALock                                      // Lock to get
                    ) {
    unsigned int    Orig, New, Result, NumberAttempts = 0;

retry_exclusive:
    Orig = *ALock;                                                              // What is the lock now
    if ( !( Orig & AT_SHARE_EXC ) ) {                                           // Make sure nobody already has it
        New = Orig | AT_SHARE_EXC;                                              // Add in our exclusive flag
        // Note that we HAVE to use compare & exchange here, since with no kilroys we don't want two different guys thinking they are the one with the exclusive
        if ( (Result = ATCompareAndExchange(ALock, Orig, New)) != ATERR_SUCCESS ){// Try the lock
            ATAdaptiveControl(NumberAttempts);                                  // Adaptive behavior for lock contention
            NumberAttempts++;
            goto retry_exclusive;                                               // We retry here because it could be just a share lock interfering... if it is an exclusive we will find out and exit
        }
    }
    else {
        return ATERR_OBJECT_IN_USE;                                             // The moment we see another exclusive, just bail
    }

    // Once we are here, we have the exclusive but we still might have shares active... need to wait for them to clear
    NumberAttempts = 0;
share_wait:
    if ( *ALock != AT_SHARE_EXC ) {                                             // Wait until everyone else is out...
        ATAdaptiveControl(NumberAttempts);                                      // Adaptive behavior for lock contention
        NumberAttempts++;
        goto share_wait;
    }

    return ATERR_SUCCESS;
}

// **************************************************************************** ATQueueShareExclusive
// THIS IS AN EXTREMELY USEFUL, BUT POTENTIALLY DANGEROUS CALL IF MISUSED!  USE WITH CARE!  IT QUEUES THE LOCK BUT A SUCESS RETURN DOES NOT MEAN A FREE LOCK!
// AND ALSO DO NOT USE FreeShare() UNTIL ALL OTHER SHARES ARE GONE!  You can use WaitQueueExclusive to be sure.
int ATQueueShareExclusive(                                                      // Tries to get a sharelock in exclusive mode.  Fails only if someone else already has it- always immediate return regardless.
                                                                                // THIS CALL RETURNS *BEFORE* WAITING FOR ALL SHARE LOCKS TO FREE, so it is up to you to make sure the count is ZERO before you do anything unpleasant.
                                                                                // In other words, wait until (*ALock == AT_SHARE_EXC)
                    ATLOCK          *ALock                                      // Lock to get
                    ) {
    unsigned int    Orig, New, Result, NumberAttempts = 0;

retry_exclusive:
    Orig = *ALock;                                                              // What is the lock now
    if ( !( Orig & AT_SHARE_EXC ) ) {                                           // Make sure nobody already has it
        New = Orig | AT_SHARE_EXC;                                              // Add in our exclusive flag
        // Note that we HAVE to use compare & exchange here, since with no kilroys we don't want two different guys thinking they are the one with the exclusive
        if ( (Result = ATCompareAndExchange(ALock, Orig, New)) != ATERR_SUCCESS ){// Try the lock
            ATAdaptiveControl(NumberAttempts);                                  // Adaptive behavior for lock contention
            NumberAttempts++;
            goto retry_exclusive;                                               // We retry here because it could be just a share lock interfering... if it is an exclusive we will find out and exit
        }
    }
    else {
        return ATERR_OBJECT_IN_USE;                                             // The moment we see another exclusive, just bail
    }
    return ATERR_SUCCESS;                                                       // We got out EXC flag in there, so leave it up to the caller to wait for the shares to be gone
}
// **************************************************************************** ATWaitQueueShareExclusive
int ATWaitQueueShareExclusive(                                                  // ONLY when you have made the QueueShareExclusive call, you may at then any time after call this to safely wait for the share locks to free
                                                                                // In other words, wait until (*ALock == AT_SHARE_EXC)
                    ATLOCK          *ALock                                      // Lock to get
                    ) {
    long    Attempts = 0;

    while ( *ALock != AT_SHARE_EXC ) {                                          // Wait for nothing but caller's exclusive
        ATAdaptiveControl(Attempts);                                            // Try to wait intelligently
        Attempts++;
    }
    return ATERR_SUCCESS;
}
// **************************************************************************** ATRemoveQueueShareExclusive
int ATRemoveQueueShareExclusive(                                                // Likely, you found out you no longer need this exclusive, and you want to free it before waiting for all the reads to clear.
                    ATLOCK          *ALock                                      // Lock to remove your queued exclusive from
                    ) {
    unsigned int    Orig, New, Result, NumberAttempts = 0;

retry_remove:
    Orig = *ALock;                                                              // What is the lock now
    New = (Orig & AT_SHARE_EXC_I);                                              // Figure out what it will be without my lock on it
    // Note that we HAVE to use compare & exchange here, since other folks might be screwing with the read count
    if ( (Result = ATCompareAndExchange(ALock, Orig, New)) != ATERR_SUCCESS ){  // Try the removal
        ATAdaptiveControl(NumberAttempts);                                  // Adaptive behavior for lock contention
        NumberAttempts++;
        goto retry_remove;                                                  // We retry here because it could be just a share lock interfering... if it is an exclusive we will find out and exit
    }
    return ATERR_SUCCESS;                                                       // We got out EXC flag in there, so leave it up to the caller to wait for the shares to be gone
}
// **************************************************************************** ATFreeShareExclusive
// NOTE:  MAKE DARNED SURE YOU DON'T IMPLEMENT A BOUNCE EXCLUSIVE LOCK THAT DOESN'T WAIT FOR ALL SHARE FREES AND LEAVE THIS THIS WAY!  Otherwise you could lose sharelock incs- and go into negatives.
// If you have used QueueShareExclusive do NOT use this call until you are sure there are no shares still on the lock.
int ATFreeShareExclusive(                                                       // Free an exclusively held share lock
                    ATLOCK          *ALock                                      // The lock to free
                    ) {
    *ALock = 0;
    return ATERR_SUCCESS;
}





/* This is destined to be a lockless freelist.  I want to come back and finish it.
Beautiful (and solid) concept.  But just too special purpose for what I need right now.
The issue is that for good performance, the users need to be marginally balanced (though
you can easily make up for a bit of imbalance with simple balancing thresholds).
So, for something like what MK Worker Threads it would be gorgeous- zero locking!  But for
general purpose use like Atlas, it might degrade too easily, even with the mechanisms I devised
to dynamically join & leave the list.  Sigh.

struct  ATNLFreeListList {                                                      // Each list should have this structure
    volatile long           Start1;                                             // First entry in list (first of two values)
    volatile long           Start2;
    volatile ULONG ULONG    Taken;                                              // Number of items I have taken from the list
    volatile ULONG ULONG    Added;                                              // Number of items that have been added to the list
    volatile long           Private1;                                           // First entry in my private list
    volatile long           Private2;
    volatile long           InUse;                                              // True if this list is in use
    volatile long           ForwardRequest;                                     // Area used to receive messages from lists above yours
    volatile long           BackwardRequest;                                    // Area used to receive messages from lists below yours
};
typedef struct ATNLFreeListList ATNLFLL;

struct ATNLFreeListInfo {
    long                AllocatedLists;                                         // Number of lists allocated
    long                NullValue;                                              // Value to use for the null value
    ATLOCK              ALock;                                                  // Lock used to add lists
};
typedef struct ATNLFreeListInfo ATNLFLI;

struct ATNLFreeListChain {                                                      // Structure that defines a chain for the class
    long                Value1;                                                 // User defined & used value- the class only passes it, and doesn't care what is in it
    long                Value2;                                                 // User defined & used value- the class only passes it, and doesn't care what is in it
};
typedef struct ATNLFreeListChain ATNLFLC;

// A message that tells a list to refind its neighbor- once it does, it sends a FOUNDYOU message to that neighbor
#define     AT_NLFL_REFINDNEIGHBOR      1
// See above
#define     AT_NLFL_FOUNDYOU            2
// The list has not yet sent a REFINDNEIGHBOR message to its lesser neighbor
#define     AT_NLFL_WAITTOSTART         3
// The list has sent the above message, but has not yet gotten confirmation that it can start yet
#define     AT_NLFL_WAITTOWRITE         4
// The list is chugging along in normal status
#define     AT_NLFL_NORMAL              5

// **************************************************************************** ATNLFreeList
class   ATNLFreeList {                                                          // No locking free list class
private:
    ATNLFLL         *Lists;                                                     // Ptr to the base of the lists in memory
    ATNLFLI         *Info;                                                      // Ptr to the base of the free list info in memory
    long            MyList;                                                     // My list handle
    long            NumberLists;                                                // The number of lists being tracked
    long            Null;                                                       // My locally cached NULL value
    long            MyNeighbor;                                                 // Cached value of my neighbor
    long            Status;                                                     // My current status

    (void *(*GetNext)(long, long));                                             // Function to get our next list location from stored values
    ATNLFLC         *LastLooked;                                                // Ptr to the last place we looked

    void            Reset();
    int             DetermineNextNeighbor();                                    // Finds the nearest occupied neighbor above you- the one you will write to
    int             DeterminePrevNeighbor();                                    // Finds the nearest occupied neighbor below you- the one who will write to you
public:
    ATNLFreeList();
    ~ATNLFreeList();
    int             Create(                                                     // Call to create the freelist (OUTSIDE ATOMICITY ASSUMED- ONLY CALL ONCE)
                            int         inLists,                                // Number of lists to maintain
                            char        *inListsMemory,                         // A chunk of global memory, (inLists * sizeof(ATNFLL)) in size for the class to use
                            ATNLFLI     *inInfo,                                // Ptr to a global section of memory that has the info struct allocated
                            (ATNLFLC *(*func)(long, long)) inNext,              // Function that takes the 2 user specified longs and returns a pointer to the next item in the list
                                                                                // Should look something like: ATNLFLC *NextPtr(long V1, long V2);
                            long        inNull                                  // Value to use for NULL
                            );
    int             Open(                                                       // Call to open an initialized free list (OUTSIDE ATOMICITY ASSUMED)
                            char        *inListsMemory                          // A chunk of global memory, (inLists * sizeof(ATNFLL)) in size
                            ATNLFLI     *inInfo,                                // Ptr to a global section of memory that has the info struct allocated
                            (ATNLFLC *(*func)(long, long)) inNext,              // Function that takes the 2 user specified longs and returns a pointer to the next item in the list
                                                                                // Should look something like: ATNLFLC *NextPtr(long V1, long V2);
                            );
    int             Close();                                                    // Call to detach from the freelist
    int             AddToList(                                                  // Call to add 2 values to the list
                            ATNLFLL     *Lists,                                 // Ptr to the base of the free lists
                            long        Value1,                                 // First value to add
                            long        Value2
                            );
    int             TakeFromList(                                               // Returns the next item on caller's list, if anything
                            long        *Value1,                                // Ptr to return first value
                            long        *Value2                                 // Ptr to return next value
                            );
}

// **************************************************************************** Constructor
ATNLFreeList::ANNLFreeList(){
    Reset();
}
// **************************************************************************** Destructor
ATNLFreeList::~ATNLFreeList() {
}
// **************************************************************************** Reset
void ATNLFreeList::Reset() {
    Lists = NULL;
    Info = NULL;
    MyList = -1;
    Null = -1;
    GetNext = NULL;
    LastLooked = NULL;
    NumberLists = 0;
    MyNeighbor = 1;
    Status = AT_NLFL_WAITTOWRITE;
}
// **************************************************************************** Create
int ATNLFreeList::Create(                                                       // Call to create the freelist (OUTSIDE ATOMICITY ASSUMED- ONLY CALL ONCE)
                            int         inLists,                                // Number of lists to maintain
                            char        *inListsMemory,                         // A chunk of global memory, (inLists * sizeof(ATNFLL)) in size
                            ATNLFLI     *inInfo,                                // Ptr to a global section of memory that has the info struct allocated
                            (ATNLFLC *(*func)(long, long)) inNext,              // Function that takes the 2 user specified longs and returns a pointer to the next item in the list
                                                                                // Should look something like: ATNLFLC *NextPtr(long V1, long V2);
                            long        inNull                                  // Value to use for NULL
                            ) {
    int     i;

    if ( inLists < 1 || !inListsMemory || !inInfo || !inNext )
        return ATERR_BAD_PARAMETERS;

    Lists =         inListsMemory;                                              // Init the class members
    Info =          inInfo;
    GetNext =       inNext;
    MyList =        0;
    Null =          inNull;
    NumberLists =   inLists;
    Status =        AT_NLFL_WAITTOSTART;

    Info->NullValue =       inNull;                                             // Init the info structure
    Info->ALock =           0;
    Info->AllocatedLists =  inLists;

    for ( i = 0; i < NumberLists; ++i) {                                        // Init all of the lists
        Lists[i]->Start1 = Lists[i]->Start2 =       Null;
        Lists[i]->Private1 = Lists[i]->Private2 =   Null;
        Lists[i]->Taken = Lists[i]->Added =         0;
        Lists[i]->InUse =                           0;
        Lists[i]->ForwardRequests =                 0;
        Lists[i]->BackwardRequests =                0;
    }

    Lists[MyList]->InUse = 1;                                                   // Init my own list

    return ATERR_SUCCESS;
}
// **************************************************************************** Open
int ATNLFreeList::Open(                                                         // Call to open an initialized free list (OUTSIDE ATOMICITY ASSUMED)
                            char        *inListsMemory                          // A chunk of global memory, (inLists * sizeof(ATNFLL)) in size
                            ATNLFLI     *inInfo,                                // Ptr to a global section of memory that has the info struct allocated
                            (ATNLFLC *(*func)(long, long)) inNext,              // Function that takes the 2 user specified longs and returns a pointer to the next item in the list
                                                                                // Should look something like: ATNLFLC *NextPtr(long V1, long V2);
                            ) {
    int     i;

    if ( inLists < 1 || !inListsMemory || !inInfo || !inNext )
        return ATERR_BAD_PARAMETERS;

    NumberLists =   inLists;                                                    // Init the class members
    Lists =         inListsMemory;
    Info =          inInfo;
    GetNext =       inNext;
    MyList =        0;
    Null =          Info->NullValue;
    NumberLists =   Info->AllocatedLists;

    GetSpinLock(Info->ALock);                                                   // Get a lock on the freelist
    for ( i = 0; i < NumberLists; ++i) {
        if ( !(Lists[i].InUse) ) {                                              // Look for the first empty spot
            Lists[i].InUse = 1;                                                 // Make it mine
            FreeSpinLock(Info->Alock);                                          // Release the lock
            MyList = i;
        }
    }
    if ( i >= NumberLists ) {                                                   // If there are no lists open, I am out of luck
        FreeSpinLock(Info->Alock);
        return ATERR_OUT_OF_MEMORY;
    }

    MyNeighbor = DetermineNextNeighbor();                                       // Try to someone to write to
    if ( (i = DeterminePrevNeighbor()) > -1 ) {                                 // Try to find someone to write to me
You left off right here....
    }


    return ATERR_SUCCESS;
}
// **************************************************************************** DetermineNextNeighbor
int ATNLFreeList::DetermineNextNeighbor() {                                     // Finds the nearest occupied neighbor above you- the one you will write to
    int N = MyList;                                                             // Start with MyList

    while ( N != MyList - 1 && N != MyList) {                                   // Loop, but don't make a complete circle
        N++;
        if ( N >= NumberLists ) N = 0;
        if ( Lists[N].InUse ) return N;
    }
    return -1;
}
// **************************************************************************** DeterminePrevNeighbor
int ATNLFreeList::DeterminePrevNeighbor() {                                     // Finds the nearest occupied neighbor below you- the one who will write to you
    int N = MyList;                                                             // Start with MyList

    while ( N != MyList + 1 && N != MyList) {                                   // Loop, but don't make a complete circle
        N--;
        if ( N < 0 ) N = NumberLists - 1;
        if ( Lists[N].InUse ) return N;
    }
    return -1;
}
*/




