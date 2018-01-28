#ifndef SUPPORT_H
#define SUPPORT_H
// ****************************************************************************
// * support.h - The support code for Atlas.                                  *
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

// **************************************************************************** ATLog
class ATLog {                                                                   // Log file class
private:
    char            FileName[AT_MAX_PATH];                                      // Filename & path for the log file
    int             OMode;                                                      // Open Mode:  Mode 0 (overwrite) or 1 (append)
    int             FMode;                                                      // Flush Mode: Mode 1 (flush every write) or 0 (don't flush unless called)
    FILE            *LogFile;                                                   // Log file handle
public:
    ATLog(
                            char    *LogName,                                   // Log path & filename
                            int     OpenMode,                                   // Mode 0 (overwrite) or 1 (append)
                            int     FlushMode                                   // Mode 1 (flush every write) or 0 (don't flush unless called)
                            );
    ~ATLog();
    void            Write(  	                                                // Write a string to the log file
                        char *Format,                                           // Format of string, printf style flags standard
                        ...                                                     // Variable # of following arguments
                        );
    void            Flush();                                                    // Flush log to disk
    void            Close();                                                    // Close the log file
};

#endif

