#ifndef GENERAL_H
#define GENERAL_H

// ****************************************************************************
// * General.h - A general include for Atlas.                                 *
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

// **************************************************************************** Errors
#define     ATERR_SUCCESS           ((int)0)
#define     ATERR_BAD_PARAMETERS    ((int)1)
#define     ATERR_OUT_OF_MEMORY     ((int)2)
#define     ATERR_NOT_FOUND         ((int)3)
#define     ATERR_OBJECT_IN_USE     ((int)4)
#define     ATERR_OPERATION_FAILED  ((int)5)
#define     ATERR_FILE_ERROR        ((int)6)
#define     ATERR_UNSAFE_OPERATION  ((int)7)
#define     ATERR_MAXIMUM_USERS     ((int)8)


// **************************************************************************** Defines
// The atlas version string- do not change the length...
#define     AT_ATLAS_VERSION        "01.30\0"

// Basic memory alignment (very important for many processors)
#define     AT_MEM_ALIGN            ((int)4)
// Maximum pathname
#define     AT_MAX_PATH             256
// Maximum size of a log write string
#define     AT_MAX_LOG_WRITE        4096

#ifdef      AT_WIN32
    #define     int64               __int64
    #define     sleep               Sleep
#else
    #define     strnicmp            strncasecmp
    #define     stricmp             strcasecmp
    #define     int64               long long
    #define     ULONG               unsigned long
#endif
#define     ULONG                   unsigned long
#define     UCHAR                   unsigned char
#define     ATTuple                 volatile void


#endif
