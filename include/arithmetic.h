#ifndef ARITHMETIC_H
#define ARITHMETIC_H
// ****************************************************************************
// * arithmetic.h - The special arithmetic & types needed for Atlas.          *
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

                                                                                // Format options for ATCurrency to string-
// Just the plain number
#define     AT_CURR_FPLAIN      0x00
// Add in commas to the number
#define     AT_CURR_FCOMMA      0x01
// Add in a dollar sign
#define     AT_CURR_FDOLLAR     0x02
// Add in both commas and a dollar sign
#define     AT_CURR_FCURR       0x03

// The Atlas currency data type
#define     ATCurrency          long long

// **************************************************************************** ATAddCurrency
// Really should come back and make this a template...
#define ATAddCurrency(P1, P2, R)        (R) = ((P1) + (P2))

// **************************************************************************** ATSubtractCurrency
// Really should come back and make this a template...
#define ATSubtractCurrency(P1, P2, R)   (R) = ((P1) - (P2))

// **************************************************************************** ATDivideCurrency
void ATDivideCurrency(ATCurrency P1, ATCurrency P2, ATCurrency *R);             // Call to divide P1 by P2, put answer in R

// **************************************************************************** ATMultiplyCurrency
// Does NOT check for overflow!
void ATMultiplyCurrency(ATCurrency P1, ATCurrency P2, ATCurrency *R);           // Call to multiply P1 by P2, put answer in R

// **************************************************************************** ATLongToCurrency
// Macro to convert a long to a currency- really should come back and make this a template...
#define     ATLongToCurrency(CURRENCY, LONG)    \
                (CURRENCY) = ((ATCurrency)(((ATCurrency)(LONG)) * ((ATCurrency)10000)))

// **************************************************************************** ATCurrencyToLong
// Macro to convert a currency to a long- really should come back and make this a template...
#define     ATCurrencyToLong(LONG, CURRENCY)    \
                (LONG) = ((long)(((ATCurrency)(CURRENCY)) / ((ATCurrency)10000)))

// **************************************************************************** ATStringToCurrency
// Digits, signs, commas, $, and decimals recognized ONLY; leading spaces are okay, trailing garbage is okay, does not look past 5th decimal place.
void    ATStringToCurrency(                                                     // Routine to map a C string into a currency type
                            ATCurrency  *Currency,                              // Ptr to the currency to set to the value
                            char        *String                                 // String to convert
                            );

// **************************************************************************** ATCurrencyToString
void    ATCurrencyToString(                                                     // Routine to map a currency type into a C string
                            char        *String,                                // String to map copy data into
                                                                                // MAKE SURE THERE IS ENOUGH ROOM FOR THE RESULT!!! (30 should be plenty)
                            ATCurrency  *Currency,                              // Ptr to the currency to set to the value
                            long        Options,                                // Set to AT_CURR_xx options
                            long        Precision                               // Digits to the right of the decimal to return ( up to four )
                            );

#endif

