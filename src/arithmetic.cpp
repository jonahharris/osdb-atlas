// ****************************************************************************
// * arithmetic.cpp - The special arithmetic & types needed for Atlas.        *
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
#include <string.h>

#ifdef      AT_WIN32
    #include	<windows.h>
#else
    #include    <unistd.h>
    #include    <sys/types.h>
#endif
#include "general.h"
#include "arithmetic.h"

// **************************************************************************** ATStringToCurrency
// Digits, signs, commas, $, and decimals recognized ONLY; leading spaces are okay, trailing garbage is okay, does not look past 5th decimal place.
void    ATStringToCurrency(                                                     // Routine to map a C string into a currency type
                            ATCurrency  *Currency,                              // Ptr to the currency to set to the value
                            char        *String                                 // String to convert
                            ) {
    long        i, Length = 0, Dec = 0, Neg = 0, Round = 0, Div = 1;
    char        *C = String, *Start;
    ATCurrency  Temp = 0, Mult = 10000, Check;

    while ( *C ) {                                                              // First we have to find where it starts and ends...
        if ( *C >= '0' && *C <= '9' ) {                                         // If it is a digit
            if ( !Length ) Start = C;                                           // If the first one, set our start
            Length++;                                                           // Add to our length
            if ( Dec ) Dec++;                                                   // If we have a decimal, keep track of how far we are to the right
            if ( Dec == 6 ) break;                                              // Don't go past 4 places to the right
        }
        else if ( *C == '.' ) {                                                 // If it is a decimal
            if ( Dec ) break;                                                   // Make sure only one decimal, otherwise treat it like a period
            Dec = 1;
            if ( !Length ) Start = C;                                           // If the first one, set our start
            Length++;
        }
        else if ( *C == ' ' ) {                                                 // Spaces before the number we skip, spaces after terminate it
            if ( Length ) break;
        }
        else if ( *C == ',' ) {                                                 // Commas in the number allowed
            if ( Length ) Length++;
        }
        else if ( *C == '-' ) {                                                 // If a negative sign
            if ( !Length )                                                      // If at the start of the number
                Neg = 1;                                                        // Set the negative flag
            else
                break;                                                          // Anywhere else is a terminator
        }
        else if ( *C == '$' ) {                                                 // Dollar sign
            if ( Length ) break;                                                // If at the end of the number
        }
        else if ( *C == '+' ) {                                                 // If a positive sign
            if ( Length ) break;                                                // If at the end of the number
        }
        else {
            if ( *(C - 1) == ',' ) {                                            // A very common case
                C--;
                Length--;
                break;
            }
            break;                                                              // Any other character always stops it
        }
        ++C;                                                                    // Move to the next character
    }

    if ( *(C - 1) == ',' && Length ) {                                          // A very common case
        C--;
        Length--;
    }

    if ( Dec == 6 ) {                                                           // If we went out to five digits
        if ( *C >= '5' )                                                        // And the last digit means we have to round
            Round++;                                                            // Set it
        Length--;                                                               // And forget it
        Dec = 5;
    }

    C = (Start + (Length - 1));                                                 // Start at the end of the number
    switch ( Dec ) {                                                            // Quickest way I could think of, okay?!?
        case 1: Div = 1;    break;
        case 2: Div = 10;   break;
        case 3: Div = 100;  break;
        case 4: Div = 1000; break;
        case 5: Div = 10000;break;
    }

    for ( i = 0; i < Length; ++i ) {
        if ( *C >= '0' && *C <= '9' ) {                                         // As long as it is a digit
            Check = (long)(*C - 48);                                            // Make it a number
            if ( Round ) {                                                      // If rounding is active
                Check++;                                                        // Do it
                ( Check < 10 ) ? Round = 0: Check -= 10;                        // As long as we don't carry we can clear the rounding flag
            }
            Temp += (Check * Mult);                                             // Multiply it as appropriate and add it in
            Mult *= 10;                                                         // Inc our multiplier
        }
        else if ( *C == '.' ) {                                                 // It is our decimal
            Temp /= Div;                                                        // Shift the digits as needed
            Mult = 10000;                                                       // Reset our multiplier
        }
        --C;                                                                    // Move to the prior character
    }
    if ( Length ) {                                                             // As long as this was not an empty string
        if ( Neg ) Temp *= -1;                                                  // Make negative if needed
        *Currency = Temp;
        return;
    }
error:
    *Currency = (ATCurrency)0;                                                  // Set empty/invalid strings to zero
}
// **************************************************************************** ATCurrencyToString
void    ATCurrencyToString(                                                     // Routine to map a currency type into a C string
                            char        *String,                                // String to map copy data into
                                                                                // MAKE SURE THERE IS ENOUGH ROOM FOR THE RESULT!!! (35 should be plenty)
                            ATCurrency  *Currency,                              // Ptr to the currency to set to the value
                            long        Options,                                // Set to AT_CURR_xx options
                            long        Precision                               // Digits to the right of the decimal to return ( up to four )
                            ) {
    char        Scratch[40], *C = Scratch + 38;
    long        i, Temp, Round = 0, Digits = 0, Neg = 0;
    ATCurrency  Input = *Currency;
    Scratch[39] = '\0';

    if ( Precision > 4 )                                                        // Verify precision
        Precision = 4;
    else if (Precision < 0 )
        Precision = 0;

    if ( Input < 0 ) {                                                          // Check for negative
        Neg++;
        Input *= -1;
    }

    for ( i = 4; i > 0; --i ){                                                  // Start with four digits to the right of the decimal
        Temp = (long)( Input % ((ATCurrency)10) );                              // Get the last digit
        if ( Round ) {                                                          // If we had prior rounding
            Temp++;                                                             // Round up this digit
            ( Temp < 10 ) ? Round = 0: Temp -= 10;                              // As long as we don't carry we can clear the rounding flag
        }
        if ( Precision >= i ) {                                                 // If this is a digit we are supposed to print
            *C = (char)(Temp + 48);                                             // Output it
            --C;                                                                // Dec our print pos
        }
        else if ( i == Precision + 1 ) {                                        // If this is the digit just past our precision
            if ( Temp >= 5 )                                                    // See if we need to round
                Round++;                                                        // Set the rounding flag if needed
        }
        Input /= 10;                                                            // Move to the next digit
    }

    if ( Precision ) {                                                          // If we need to write a decimal
        *C = '.';                                                               // Do it;
        --C;
    }

    while ( Input > 0 ) {                                                       // Loop as long as needed
        Temp = (long)( Input % ((ATCurrency)10) );                              // Get the last digit
        if ( Round ) {                                                          // If we had prior rounding
            Temp++;                                                             // Round up this digit
            ( Temp < 10 ) ? Round = 0: Temp -= 10;                              // As long as we don't carry we can clear the rounding flag
        }
        *C = (char)(Temp + 48);                                                 // Output it
        --C;                                                                    // Dec our print pos
        ++Digits;                                                               // Keep track of how many digits we have printed
        Input /= 10;                                                            // Move to the next digit
        if ( Options & AT_CURR_FCOMMA ) {                                       // If they requested commas
            if ( !(Digits % 3) && Input ) {                                     // If we are at a comma spot
                *C = ',';                                                       // Output it
                --C;
            }
        }
    }
    if ( !Digits ) {                                                            // If there were no digits to the left of the decimal
        if ( Round )                                                            // Are we rounding up?
            *C = '1';                                                           // Write out a one
        else
            *C = '0';                                                           // Write out a zero
        --C;
    }
    if ( Options & AT_CURR_FDOLLAR ) {                                          // If they asked for a dollar sign
        *C = '$';
        --C;
    }
    if ( Neg ) {                                                                // If a negative number
        *C = '-';                                                               // Write out a decimal
        --C;
    }
    strcpy(String, (C + 1));                                                    // Copy over the result
}
// **************************************************************************** ATDivideCurrency
void ATDivideCurrency(ATCurrency P1, ATCurrency P2, ATCurrency *R) {            // Call to divide P1 by P2, put answer in R
    ATCurrency Upper = P1 / P2;                                                 // Do the integer divide
    ATCurrency Mod = P1 % P2;                                                   // Get the remainder
    Mod = Mod * 100000000;                                                      // Make the remainder a currency integer, plus enough digits to stay whole during divide
    Mod /= P2;                                                                  // Find the proportion
    Mod /= 10000;                                                               // Correct to a currency precision
    *R = ( Upper * 10000 ) + Mod;                                               // Combine it with the original integer divide
}
// **************************************************************************** ATMultiplyCurrency
// Does NOT check for overflow!
void ATMultiplyCurrency(ATCurrency P1, ATCurrency P2, ATCurrency *R) {          // Call to multiply P1 by P2, put answer in R
    ATCurrency Sum = P1 * P2;                                                   // Do multiplication
    *R = (Sum / 10000);                                                         // Correct it
}


