#ifdef  AT_USE_BKDB
#ifndef VTABLE_H
#define VTABLE_H
// ****************************************************************************
// * vtable.h - The virtual table code for Atlas.                             *
// * (c) 2002,2003 Shawn Houser, All Rights Reserved                          *
// * This property and it's ancillary properties are completely and solely    *
// * owned by Shawn Houser, and no part of it is a work for hire or the work  *
// * of any other.                                                            *
// *                                                                          *
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


#include "db.h"
#include "table.h"
#include "btree.h"

// ****************************************************************************
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

// ****************************************************************************
// ****************************************************************************
//                                  ATVTABLE
// ****************************************************************************
// ****************************************************************************
class ATVTable {                                                                // Virtual table class
private:
    DB              *DBP;                                                       // DB database ptr
    DBT             Key, Data;                                                  // DB key/data pair
    ATBTreeMakeKey  *MakeKey;                                                   // Routine to make a key from a tuple
    DBC             *ReadCursor;                                                // Read cursor

    DB_ENV          *DBENV;                                                     // DB environment ptr
    long            IPCKey;                                                     // Systemwide unique IPC key
    ULONG           CacheSize;                                                  // Cache size to use in bytes
    int             KeyLength;                                                  // Length of the key, in bytes
    int             PageSize;                                                   // Page size (may not be larger than 64k)

    void            Reset();                                                    // Call to reset the class members
    inline int      BTCompare(                                                  // Called to compare BTree keys
                            DB          *inDBP,                                 // DB Ptr
                            const DBT   *inDBT1,                                // DBT parm 1
                            const DBT   *inDBT2                                 // DBT parm 2
                            );
public:
    // ************************************************************************
    //                      INITIALIZATION/CLOSE OPERATIONS
    // ************************************************************************
    ATVTable();
    ~ATVTable();
    int             Create(                                                     // Create a virtual table
                            char            *inBasePath,                        // Base path for the database this table belongs to- MUST be an absolute, not relative path
                            char            *inName,                            // Filename for the virtual table
                            long            inIPCKey,                           // Systemwide unique IPC key
                            ULONG           inCacheSize,                        // Cache size to use in bytes
                            long            inPageSize,                         // Page size (may not be larger than 64k)
                            ATBTreeMakeKey  *inMakeKey,                         // Routine to make a key from a tuple
                            long            inKeyLength                         // Length of the key, in bytes
                            );
    int             Open(                                                       // Open a virtual table
                            char            *inBasePath,                        // Base path for the database this table belongs to- MUST be an absolute, not relative path
                            char            *inName,                            // Filename for the virtual table
                            ATBTreeMakeKey  *inMakeKey,                         // Routine to make a key from a tuple
                            long            inKeyLength                         // Length of the key, in bytes
                            );
    int             Close();                                                    // Close the virtual table

    // ************************************************************************
    //                      GENERAL OPERATIONS
    // ************************************************************************
    void            *FindTuple(                                                 // Call to find a tuple from the table- returns a ptr to the item
                            void            *inKey,                             // Ptr to the key to use to retrieve the item
                            long            *Size                               // Ptr to a long that will set to the size of the tuple
                            );
    int             AddTuple(                                                   // Call to write out a tuple to the table
                            void            *Tuple,                             // Ptr to the tuple
                            long            inSize                              // Size of the tuple (include any terminators you want back, like null bytes)
                            );
    int             DeleteTuple(                                                // Call to delete a tuple from the table
                            void            *inKey                              // Ptr to the key to use to retrieve the item
                            );
    // ************************************************************************
    //                      CURSOR OPERATIONS
    // ************************************************************************
    void            *SetCursor(                                                 // Opens (if needed) a cursor & sets it to a given location- returns a ptr to the tuple
                            void            *inKey,                             // Ptr to the key to use to retrieve the item
                            long            *Size,                              // Ptr to a long that will set to the size of the tuple
                            long            inMatchLength                       // Length of the key to use for a match
                            );
    void            *SetCursorToStart(                                          // Opens a read cursor (if needed) and positions it to the start of the table- returns a ptr to the tuple
                                                                                // ALWAYS FREE ASAP! REMEMBER- ONLY ONE GUY CAN WRITE AT A TIME, AND HE CAN'T WRITE UNTIL THERE ARE NO READ CURSORS OPEN!
                            long            *Size                               // Ptr to a long that will be set to the size of the tuple
                            );
    void            *SetCursorToEnd(                                            // Opens a read cursor (if needed) and positions it to the end of the table- returns a ptr to the tuple
                                                                                // ALWAYS FREE ASAP! REMEMBER- ONLY ONE GUY CAN WRITE AT A TIME, AND HE CAN'T WRITE UNTIL THERE ARE NO READ CURSORS OPEN!
                            long            *Size                               // Ptr to a long that will be set to the size of the tuple
                            );
    void            *CursorNext(                                                // Returns the next tuple for a cursor- returns a ptr to the tuple
                                                                                // ALWAYS FREE ASAP! REMEMBER- ONLY ONE GUY CAN WRITE AT A TIME, AND HE CAN'T WRITE UNTIL THERE ARE NO READ CURSORS OPEN!
                            long            *Size                               // Ptr to a long that will be set to the size of the tuple
                            );
    void            *CursorPrev(                                                // Returns the previous tuple for a cursor- returns a ptr to the tuple
                                                                                // ALWAYS FREE ASAP! REMEMBER- ONLY ONE GUY CAN WRITE AT A TIME, AND HE CAN'T WRITE UNTIL THERE ARE NO READ CURSORS OPEN!
                            long            *Size                               // Ptr to a long that will be set to the size of the tuple
                            );
    int             FreeCursor();                                               // Closes an open read cursor
                                                                                // ALWAYS CALL THIS ASAP AFTER OPENING A CURSOR!!!

};


#endif
#endif

