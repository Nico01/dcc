#ifndef LOCIDENT_H
#define LOCIDENT_H

/*
 * Copyright (C) 1991-4, Cristina Cifuentes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

// Purpose: High-level local identifier definitions

#include <stdint.h>
#include <stdbool.h>

// Type definition
typedef struct {
    int csym;  // # symbols used
    int alloc; // # symbols allocated
    int *idx;  // Array of integer indexes
} IDX_ARRAY;

// Type definitions used in the decompiled program
typedef enum {
    TYPE_UNKNOWN = 0, // unknown so far
    TYPE_BYTE_SIGN,   // signed byte (8 bits)
    TYPE_BYTE_UNSIGN, // unsigned byte
    TYPE_WORD_SIGN,   // signed word (16 bits)
    TYPE_WORD_UNSIGN, // unsigned word (16 bits)
    TYPE_LONG_SIGN,   // signed long (32 bits)
    TYPE_LONG_UNSIGN, // unsigned long (32 bits)
    TYPE_RECORD,      // record structure
    TYPE_PTR,         // pointer (32 bit ptr)
    TYPE_STR,         // string
    TYPE_CONST,       // constant (any type)
    TYPE_FLOAT,       // floating point
    TYPE_DOUBLE,      // double precision float
} hlType;

static char *hlTypes[13] = { "",     "char",          "unsigned char", "int",   "unsigned int",
                             "long", "unsigned long", "record",        "int *", "char *",
                             "",     "float",         "double" };

typedef enum {
    STK_FRAME, // For stack vars
    REG_FRAME, // For register variables
    GLB_FRAME, // For globals
} frameType;

// Enumeration to determine whether pIcode points to the high or low part of a long number
typedef enum {
    HIGH_FIRST, // High value is first
    LOW_FIRST,  // Low value is first
} hlFirst;

// LOCAL_ID
typedef struct {
    hlType type;            // Probable type
    bool illegal;           // Boolean: not a valid field any more
    IDX_ARRAY idx;          // Index into icode array (REG_FRAME only)
    frameType loc;          // Frame location
    bool hasMacro;          // Identifier requires a macro
    char macro[10];         // Macro for this identifier
    char name[20];          // Identifier's name
    union {                 // Different types of identifiers
        uint8_t regi;       // For TYPE_BYTE(WORD)_(UN)SIGN registers
        struct {            // For TYPE_BYTE(WORD)_(UN)SIGN on the stack
            uint8_t regOff; // register offset (if any)
            int off;        // offset from BP
        } bwId;
        struct _bwGlb {     // For TYPE_BYTE(WORD)_(UN)SIGN globals
            int16_t seg;    // segment value
            int16_t off;    // offset
            uint8_t regi;   // optional indexed register
        } bwGlb;
        struct _longId {    // For TYPE_LONG_(UN)SIGN registers
            uint8_t h;      // high register
            uint8_t l;      // low register
        } longId;
        struct _longStkId { // For TYPE_LONG_(UN)SIGN on the stack
            int offH;       // high offset from BP
            int offL;       // low offset from BP
        } longStkId;
        struct {            // For TYPE_LONG_(UN)SIGN globals
            int16_t seg;    // segment value
            int16_t offH;   // offset high
            int16_t offL;   // offset low
            uint8_t regi;   // optional indexed register
        } longGlb;
        struct {            // For TYPE_LONG_(UN)SIGN constants
            uint32_t h;     // high word
            uint32_t l;     // low word
        } longKte;
    } id;
} ID;

typedef struct {
    int csym;  // No. of symbols in the table
    int alloc; // No. of symbols allocated
    ID *id;    // Identifier
} LOCAL_ID;

#endif // LOCIDENT_H
