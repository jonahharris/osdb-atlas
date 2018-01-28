// ****************************************************************************
// * support.cpp - The support code for Atlas.                                *
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
#include <stdio.h>
#include <stdarg.h>
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

#include "general.h"
#include "support.h"

// ****************************************************************************
// ****************************************************************************
//                                LOG METHODS
// ****************************************************************************
// ****************************************************************************

// **************************************************************************** Constructor
ATLog::ATLog(
                            char    *LogName,                                   // Log path & filename
                            int     OpenMode,                                   // Mode 0 (overwrite) or 1 (append)
                            int     FlushMode                                   // Mode 1 (flush every write) or 0 (don't flush unless called)
                            ) {
    strncpy(FileName, LogName, AT_MAX_PATH);
    FileName[AT_MAX_PATH - 1] = '\0';
    OMode = OpenMode;
    FMode = FlushMode;

    if ( OMode )                                                                // Open the log file as requested
        LogFile = fopen(LogName, "a");
    else
        LogFile = fopen(LogName, "w");
}
// **************************************************************************** Destructor
ATLog::~ATLog() {
    if ( LogFile ) fclose(LogFile);
}
// **************************************************************************** Write
void    ATLog::Write(                                                           // Write a string to the log file- SHOULD NOT EXCEED AT_MAX_LOG_WRITE IN LENGTH!!!
                        char *Format,                                           // Format of string, printf style flags standard
                        ...                                                     // Variable # of following arguments
                        ) {
    va_list	Args;
    char        Scratch[AT_MAX_LOG_WRITE];
    int         Length;

    if( !Format || !LogFile )   return;

    va_start(Args, Format);                                                     // Format the string
    vsprintf((char*)Scratch,(const char*)Format, Args);
    va_end(Args);

    Length = strlen(Scratch);                                                   // Terminate it ASCII Style
    Scratch[Length] = (char)10;
    Scratch[Length + 1] = (char)0;

    fputs((const char*)Scratch, LogFile);                                       // Write it to the log

    if ( FMode )
        fflush(LogFile);                                                        // Flush it out NOW
}
// **************************************************************************** Flush
void    ATLog::Flush() {
    if ( LogFile )
        fflush(LogFile);                                                        // Flush it out
}
// **************************************************************************** Close
void    ATLog::Close() {
    if ( LogFile ) {
        fclose(LogFile);
        LogFile = NULL;
    }
}






