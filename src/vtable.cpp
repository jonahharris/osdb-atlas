#ifdef  AT_USE_BKDB
// ****************************************************************************
// * vtable.cpp - The virtual table code for Atlas.                           *
// * (c) 2002,2003 Shawn Houser, All Rights Reserved                          *
// * This property and it's ancillary properties are completely and solely    *
// * owned by Shawn Houser, and no part of it is a work for hire or the work  *
// * of any other.                                                            *
// * Since much of the classes in vtable are encapsulations of the Berkeley   *
// * DB C API, you should review the license for the distribution of the      *
// * Berkeley DB API you are using.  In example, the class was initially      *
// * developed using distribution 4.1.24 from SleepyCat, which can be found   *
// * at http://www.sleepycat.com, and I understand there are numerous other   *
// * distributions available.  You should also see license.txt for more       *
// * information on applicable copyrights & license terms for Berkeley DB.    *
// * The version used to develop Atlas was GPL- you may choose to use another.*
// *                                                                          *
// * Known copyrights for Berkeley DB 4.1.24 (see license.txt for full):      *
// * Copyright (c) 1990-2002 Sleepycat Software.  All rights reserved.        *
// * Copyright (c) 1990, 1993, 1994, 1995 The Regents of the University of    *
// *       California.  All rights reserved.                                  *
// * Copyright (c) 1995, 1996 The President and Fellows of Harvard            *
// *        University.  All rights reserved.                                 *
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
#include <stdio.h>
#include <string.h>

#ifdef      AT_WIN32
    #include	<windows.h>
#else
    #include    <unistd.h>
    #include    <sys/types.h>
    #include    <sys/stat.h>
#endif
#include "db.h"
#include "general.h"
#include "table.h"
#include "btree.h"
#include "vtable.h"

#define     FUUNIXMASK      (0777)

// **************************************************************************** ATVTable
/*  Atlas virtual tables are an encapsulation of the Berkeley DB (BDB) C API.
They are a file of any size, with built-in BTree access.

BDB, though a wonderful product, has a few issues that cause irritation when one
is developing a high-performance system.  Atlas uses BDB in Concurrent Data Store
Product mode, which means you can have many readers and only one writer for the
WHOLE database.  Yeah, you're right, it is annoying.  But I figured, frankly, that
all of the real high perf stuff should be done in the native Atlas code anyhow,
since by the time you are worried about disk I/O, etc., that it was time to
just let it go...  May revisit using another mode later, but they all bring a lot
of overhead.

Right now VTables have the irritating behavior of storing the key twice in each
tuple... It is an encapsulation/interface issue, and I may return to it later.

IMPORTANT: Because of annoying characteristics in the key prefix stuff that BDB
does, I can't figure out how to nicely encapsulate an alternate BTree key
compare routine- anyone know how to turn the damned key prefix stuff off entirely?
Anyhow, because of that, you should NOT EXPECT INTEGER KEYS on little endian
architectures to sort by numerical order- the keys are all sorted lexically.
Of course, the same goes for other numeric types, etc.  A work around would be
for the user to correctly swap the byte ordering on both write & read...

Also IMPORTANT: Due to the lexixal compare, don't forget that for example a string
key must either completely fill the allocated space, or be initialized to the same
value when it was created.  In other words, memset() your variable length keys,
because "Apple\0ksd" is NOT the same as "Apple\0bbb".  Obviously, same goes for
search keys you pass in.
*/

/*
Notes on the DBT structure from the library documentation:

typedef struct {
void *data;
u_int32_t size;
u_int32_t ulen;
u_int32_t dlen;
u_int32_t doff;
u_int32_t flags;
} DBT;
By default, the flags structure element is expected to be set
to 0.  In this default case, when the application is providing Berkeley DB a
key or data item to store into the database, Berkeley DB expects the
data structure element to point to a byte string of size
bytes.  When returning a key/data item to the application, Berkeley DB will
store into the data structure element a pointer to a byte string
of size bytes, and the memory to which the pointer refers will be
allocated and managed by Berkeley DB.
The elements of the DBT structure are defined as follows:
void *data;
A pointer to a byte string.
u_int32_t size;
The length of data, in bytes.
u_int32_t ulen;
The size of the user's buffer (to which data refers), in bytes.
This location is not written by the Berkeley DB functions.
Note that applications can determine the length of a record by setting
the ulen field to 0 and checking the return value in the
size field.  See the DB_DBT_USERMEM flag for more information.
u_int32_t dlen;
The length of the partial record being read or written by the application,
in bytes.  See the DB_DBT_PARTIAL flag for more information.
u_int32_t doff;
The offset of the partial record being read or written by the application,
in bytes.  See the DB_DBT_PARTIAL flag for more information.
u_int32_t flags;
*/
// **************************************************************************** FindTuple
void *ATVTable::FindTuple(                                                      // Call to find a tuple from the table- returns a ptr to the item
                            void            *inKey,                             // Ptr to the key to use to retrieve the item
                            long            *Size                               // Ptr to a long that will be set to the size of the tuple
                            ) {
    long    Result;
    Key.data = inKey;                                                           // Set a ptr to the key
    Key.size = KeyLength;                                                       // Set the length

    if ( (Result = DBP->get(DBP, NULL, &Key, &Data, DB_DIRTY_READ)) )           // Write it out
        return NULL;
    *Size = Data.size;                                                          // Set return info
    return Data.data;
}
// **************************************************************************** AddTuple
int ATVTable::AddTuple(                                                         // Call to write out a tuple to the table
                            void            *Tuple,                             // Ptr to the tuple
                            long            inSize                              // Size of the tuple (include any terminators you want back, like null bytes)
                            ) {
    long    Result;
    DBC     *DBCP;

    Key.data = MakeKey(Tuple);                                                  // Get a ptr to the key
    Key.size = KeyLength;                                                       // Set the rest of the parms
    Data.data = Tuple;
    Data.size = inSize;

    if ( (Result = DBP->cursor(DBP, NULL, &DBCP, DB_WRITECURSOR)) )             // Create the write cursor
        return ATERR_OPERATION_FAILED;

    if ( (Result = DBCP->c_put(DBCP, &Key, &Data, DB_KEYLAST)) )                // Write it out
        goto error;

    if ( (Result = DBCP->c_close(DBCP)) )                                       // Close the write cursor
        return ATERR_OPERATION_FAILED;

    return ATERR_SUCCESS;

error:
    DBCP->c_close(DBCP);                                                        // Always close the write cursor so we don't deadlock
    return ATERR_OPERATION_FAILED;
}
// **************************************************************************** DeleteTuple
int ATVTable::DeleteTuple(                                                      // Call to delete a tuple from the table
                            void            *inKey                              // Ptr to the key to use to retrieve the item
                            ) {
    long    Result;
    DBC     *DBCP;

    Key.data = inKey;                                                           // Set a ptr to the key
    Key.size = KeyLength;                                                       // Set the length

    if ( (Result = DBP->cursor(DBP, NULL, &DBCP, DB_WRITECURSOR)) )             // Create the write cursor
        return ATERR_OPERATION_FAILED;

    if ( (Result = DBCP->c_get(DBCP, &Key, &Data, DB_SET)) )                    // Set the cursor out
        goto error;

    if ( (Result = DBCP->c_del(DBCP, 0)) )                                      // Delete the item
        goto error;

    if ( (Result = DBCP->c_close(DBCP)) )                                       // Close the write cursor
        return ATERR_OPERATION_FAILED;

    return ATERR_SUCCESS;

error:
    DBCP->c_close(DBCP);                                                        // Always close the write cursor so we don't deadlock
    return ATERR_OPERATION_FAILED;
}
// **************************************************************************** Create
int ATVTable::Create(                                                           // Create a virtual table
                            char            *inBasePath,                        // Base path for the database this table belongs to- MUST be an absolute, not relative path
                            char            *inName,                            // Filename for the virtual table
                            long            inIPCKey,                           // Systemwide unique IPC key
                            ULONG           inCacheSize,                        // Cache size to use in bytes
                            long            inPageSize,                         // Page size (may not be larger than 64k)
                            ATBTreeMakeKey  *inMakeKey,                         // Routine to make a key from a tuple
                            long            inKeyLength                         // Length of the key, in bytes
                            ) {
    long    Result;
    char    Scratch[AT_MAX_PATH];

    if ( !inBasePath || !inName || !inIPCKey || !inCacheSize || !inPageSize ||
        inPageSize > 65536 || !inMakeKey || !inKeyLength )
        return ATERR_BAD_PARAMETERS;

    if ( (Result = db_env_create(&DBENV, 0)) ) {                                // Create the DB environment
        fprintf(stderr,"%s: db_env_create: %s\n", inName, db_strerror(Result));
        return ATERR_OPERATION_FAILED;}

    if ( (Result = DBENV->set_cachesize(DBENV, 0, inCacheSize, 0)) ) {          // Set the cache size
        fprintf(stderr,"%s: set_cachesize: %s\n", inName, db_strerror(Result));
        return ATERR_OPERATION_FAILED;}

    if ( (Result = DBENV->set_flags(DBENV, DB_DIRECT_DB, 1)) ) {                // Turn off double buffering (maybe)
        fprintf(stderr,"%s: set_flags: %s\n", inName, db_strerror(Result));
        return ATERR_OPERATION_FAILED;}
    if ( (Result = DBENV->set_flags(DBENV, DB_REGION_INIT, 1)) ) {              // Force init of mem & file backing
        fprintf(stderr,"%s: set_flags: %s\n", inName, db_strerror(Result));
        return ATERR_OPERATION_FAILED;}
    if ( (Result = DBENV->set_flags(DBENV, DB_TXN_NOSYNC, 1)) ) {               // Don't sync to logs
        fprintf(stderr,"%s: set_flags: %s\n", inName, db_strerror(Result));
        return ATERR_OPERATION_FAILED;}

    if ( (Result = DBENV->set_shm_key(DBENV, inIPCKey)) ) {                     // Set the IPC key
        fprintf(stderr,"%s: set_shm_key: %s\n", inName, db_strerror(Result));
        return ATERR_OPERATION_FAILED;}

    umask(0000);
    sprintf(Scratch,"mkdir %s &> /dev/null", inBasePath);                       // Make sure the directory is there, just in case
    system(Scratch);

    if ( (Result = DBENV->open(DBENV, inBasePath,                               // Open up the environment
        DB_CREATE | DB_SYSTEM_MEM | DB_INIT_CDB | DB_INIT_MPOOL, FUUNIXMASK )) ){
        fprintf(stderr,"%s: env open: %s\n", inName, db_strerror(Result));
        return ATERR_OPERATION_FAILED;}


    if ( (Result = db_create(&DBP, DBENV, 0)) ) {                               // Create the database
        fprintf(stderr,"%s: db_create: %s\n", inName, db_strerror(Result));
        return ATERR_OPERATION_FAILED;}
    if ( (Result = DBP->set_pagesize(DBP, inPageSize)) ) {                      // Set the page size
        fprintf(stderr,"%s: set_pagesize: %s\n", inName, db_strerror(Result));
        return ATERR_OPERATION_FAILED;}

    strcpy(Scratch, inBasePath);                                                // Create the full file name
    strcat(Scratch, inName);
    umask(0000);
    if ( (Result = DBP->open(DBP, NULL, Scratch, inName, DB_BTREE,              // Open up the database
        DB_CREATE | DB_DIRTY_READ, FUUNIXMASK ))) {
        fprintf(stderr,"%s: db open: %s\n", inName, db_strerror(Result));
        return ATERR_OPERATION_FAILED;}

    IPCKey =        inIPCKey;                                                   // Init class members
    CacheSize =     inCacheSize;
    PageSize =      inPageSize;
    KeyLength =     inKeyLength;
    MakeKey =       inMakeKey;

    return ATERR_SUCCESS;
}
// **************************************************************************** Open
int ATVTable::Open(                                                             // Create a virtual table
                            char            *inBasePath,                        // Base path for the database this table belongs to- MUST be an absolute, not relative path
                            char            *inName,                            // Filename for the virtual table
                            ATBTreeMakeKey  *inMakeKey,                         // Routine to make a key from a tuple
                            long            inKeyLength                         // Length of the key, in bytes
                            ) {
    long    Result;
    char    Scratch[AT_MAX_PATH];

    if ( !inBasePath || !inName || !inKeyLength || !inMakeKey )
        return ATERR_BAD_PARAMETERS;

    if ( (Result = db_env_create(&DBENV, 0)) ) {                                // Create the DB environment
        fprintf(stderr,"%s: db_env_create: %s\n", inName, db_strerror(Result));
        return ATERR_OPERATION_FAILED;}

    umask(0000);
    if ( (Result = DBENV->open(DBENV, inBasePath,                               // Open up the environment
        DB_JOINENV, FUUNIXMASK )) ){
        fprintf(stderr,"%s: env open: %s\n", inName, db_strerror(Result));
        return ATERR_OPERATION_FAILED;}

    if ( (Result = db_create(&DBP, DBENV, 0)) ) {                               // Create the database
        fprintf(stderr,"%s: db_create: %s\n", inName, db_strerror(Result));
        return ATERR_OPERATION_FAILED;}

    strcpy(Scratch, inBasePath);                                                // Create the full file name
    strcat(Scratch, inName);
    umask(0000);
    if ( (Result = DBP->open(DBP, NULL, Scratch, inName, DB_BTREE,              // Open up the database
        DB_DIRTY_READ, FUUNIXMASK ))) {
        fprintf(stderr,"%s: db open: %s\n", inName, db_strerror(Result));
        return ATERR_OPERATION_FAILED;}

    KeyLength =     inKeyLength;
    MakeKey =       inMakeKey;

    return ATERR_SUCCESS;
}
// **************************************************************************** Constructor
ATVTable::ATVTable() {
    Reset();
}
// **************************************************************************** Destructor
ATVTable::~ATVTable() {
    if ( DBP ) Close();
}
// **************************************************************************** Close
int ATVTable::Close() {                                                         // Close the virtual table
    long R1, R2, R3;
    if ( !DBP ) return ATERR_SUCCESS;                                           // If we aren't really open just return

    if ( ReadCursor )                                                           // Close the read cursor if one is open
        ReadCursor->c_close(ReadCursor);
    R1 = DBP->sync(DBP, 0);                                                     // Sync the cache to disk
    R2 = DBP->close(DBP, 0);                                                    // Close the database
    R3 = DBENV->close(DBENV, 0);                                                // Close the environment last
    Reset();                                                                    // Reset the class members

    if ( !R1 && !R2 && !R3 )
        return ATERR_SUCCESS;
    else
        return ATERR_OPERATION_FAILED;
}


// **************************************************************************** Reset
void ATVTable::Reset() {                                                        // Call to reset the class members
    DBENV = NULL;
    DBP = NULL;
    memset((void*)&Key, 0, sizeof(DBT));
    memset((void*)&Data, 0, sizeof(DBT));
    IPCKey = KeyLength = PageSize = CacheSize = 0;
    MakeKey = NULL;
    ReadCursor = NULL;
}
// **************************************************************************** SetCursorToStart
void *ATVTable::SetCursorToStart(                                               // Opens a read cursor (if needed) and positions it to the start of the table- returns a ptr to the tuple
                                                                                // ALWAYS FREE ASAP! REMEMBER- ONLY ONE GUY CAN WRITE AT A TIME, AND HE CAN'T WRITE UNTIL THERE ARE NO READ CURSORS OPEN!
                            long            *Size                               // Ptr to a long that will be set to the size of the tuple
                            ) {
    long    Result;

    if ( !ReadCursor ) {                                                        // If we don't already have a read cursor open
        if ( (Result = DBP->cursor(DBP, NULL, &ReadCursor, DB_WRITECURSOR)) )   // Create the read cursor
            return NULL;
    }
    else {                                                                      // If we have one open already
        if ( (Result = ReadCursor->c_close(ReadCursor)) )                       // Close it
            return NULL;
        if ( (Result = DBP->cursor(DBP, NULL, &ReadCursor, DB_WRITECURSOR)) )   // Create a new one- because it must be uninitialized
            return NULL;
    }
    Key.data = NULL;                                                            // Clear the key
    Key.size = 0;
    if ( (Result = ReadCursor->c_get(ReadCursor, &Key, &Data, DB_FIRST)) ) {    // Set it to the start of the DB
        FreeCursor();
        return NULL;
    }
    *Size = Data.size;
    return Data.data;
}
// **************************************************************************** FreeCursor
int ATVTable::FreeCursor() {                                                    // Closes an open read cursor
                                                                                // ALWAYS CALL THIS ASAP AFTER OPENING A CURSOR!!!
    long Result;

    if ( ReadCursor ) {                                                         // If there is a cursor open
        Result = ReadCursor->c_close(ReadCursor);                               // Close it
        ReadCursor = NULL;                                                      // Regardless of outcome, NULL it, we can't fix it
        if ( Result )   return ATERR_OPERATION_FAILED;
    }
    return ATERR_SUCCESS;
}
// **************************************************************************** SetCursorToEnd
void *ATVTable::SetCursorToEnd(                                                 // Opens a read cursor (if needed) and positions it to the end of the table- returns a ptr to the tuple
                                                                                // ALWAYS FREE ASAP! REMEMBER- ONLY ONE GUY CAN WRITE AT A TIME, AND HE CAN'T WRITE UNTIL THERE ARE NO READ CURSORS OPEN!
                            long            *Size                               // Ptr to a long that will be set to the size of the tuple
                            ) {
    long    Result;

    if ( !ReadCursor ) {                                                        // If we don't already have a read cursor open
        if ( (Result = DBP->cursor(DBP, NULL, &ReadCursor, DB_WRITECURSOR)) )   // Create a new one- because it must be uninitialized
            return NULL;
    }
    else {                                                                      // If we have one open already
        if ( (Result = ReadCursor->c_close(ReadCursor)) )                       // Close it
            return NULL;
        if ( (Result = DBP->cursor(DBP, NULL, &ReadCursor, DB_WRITECURSOR)) )   // Create a new one- because it must be uninitialized
            return NULL;
    }
    Key.data = NULL;                                                            // Clear the key
    Key.size = 0;
    if ( (Result = ReadCursor->c_get(ReadCursor, &Key, &Data, DB_LAST)) ) {     // Set it to the start of the DB
        FreeCursor();
        return NULL;
    }

    *Size = Data.size;
    return Data.data;
}
// **************************************************************************** SetCursor
void *ATVTable::SetCursor(                                                      // Opens (if needed) a cursor & sets it to a given location- returns a ptr to the tuple
                            void            *inKey,                             // Ptr to the key to use to retrieve the item
                            long            *Size,                              // Ptr to a long that will set to the size of the tuple
                            long            inMatchLength                       // Length of the key to use for a match
                            ) {
    long    Result;

    if ( !ReadCursor ) {                                                        // If we don't already have a read cursor open
        if ( (Result = DBP->cursor(DBP, NULL, &ReadCursor, DB_WRITECURSOR)) )   // Create a new one- because it must be uninitialized
            return NULL;
    }
    Key.data = inKey;                                                           // Set a ptr to the key
    Key.size = inMatchLength;                                                   // Set the length
    if ( (Result = ReadCursor->c_get(ReadCursor, &Key, &Data, DB_SET_RANGE)) ) {// Find the match
//        char Scratch[AT_MAX_PATH];
//        sprintf(Scratch, "SetCursor: %s\n", db_strerror(Result));
        FreeCursor();
        return NULL;
    }
    *Size = Data.size;
    return Data.data;
}
// **************************************************************************** CursorNext
void *ATVTable::CursorNext(                                                     // Returns the next tuple for a cursor- returns a ptr to the tuple
                                                                                // ALWAYS FREE ASAP! REMEMBER- ONLY ONE GUY CAN WRITE AT A TIME, AND HE CAN'T WRITE UNTIL THERE ARE NO READ CURSORS OPEN!
                            long            *Size                               // Ptr to a long that will be set to the size of the tuple
                            ) {
    long    Result;

    if ( !ReadCursor ) return NULL;                                             // If we don't already have a read cursor open

    if ( (Result = ReadCursor->c_get(ReadCursor, &Key, &Data, DB_NEXT)) )       // Set it to the next tuple
        return NULL;

    *Size = Data.size;
    return Data.data;
}
// **************************************************************************** CursorPrev
void *ATVTable::CursorPrev(                                                     // Returns the previous tuple for a cursor- returns a ptr to the tuple
                                                                                // ALWAYS FREE ASAP! REMEMBER- ONLY ONE GUY CAN WRITE AT A TIME, AND HE CAN'T WRITE UNTIL THERE ARE NO READ CURSORS OPEN!
                            long            *Size                               // Ptr to a long that will be set to the size of the tuple
                            ) {
    long    Result;

    if ( !ReadCursor ) return NULL;                                             // If we don't already have a read cursor open

    if ( (Result = ReadCursor->c_get(ReadCursor, &Key, &Data, DB_PREV)) )       // Set it to the next tuple
        return NULL;

    *Size = Data.size;
    return Data.data;
}
#else
void Empty() {
    int foo = 5;
}
#endif

