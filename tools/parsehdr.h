#ifndef DCC_PARSEHDR_H
#define DCC_PARSEHDR_H
/*
 * Copyright (C) 1993, Queensland University of Technology
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

// Header file for parsehdr.c

#include <stdint.h>
#include <stdbool.h>

#define BUFF_SIZE 8192  // Holds a declaration
#define FBUF_SIZE 32700 // Holds part of a header file

#define NARGS 15
#define NAMES_L 160
#define TYPES_L 160
#define FUNC_L 160

#define ERRF stdout

void phError(const char *errmsg);
void phWarning(const char *errmsg);

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define ERR(msg)  phError(msg)
#define WARN(msg) phWarning(msg)
#define OUT(str)  fprintf(outfile, str)

#ifdef DEBUG
    #define DBG(str) printf(str);
#else
    #define DBG(str) ;
#endif

#define PH_PARAMS 32
#define PH_NAMESZ 15

#define SYMLEN 16 // Including the null


#define TOK_TYPE 256 // A type name (e.g. "int")
#define TOK_NAME 257 // A function or parameter name
#define TOK_DOTS 258 // "..."
#define TOK_EOL 259  // End of line

typedef enum {
    BT_INT,
    BT_CHAR,
    BT_FLOAT,
    BT_DOUBLE,
    BT_STRUCT,
    BT_VOID,
    BT_UNKWN
} baseType;

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

typedef struct ph_func_tag {
    char name[SYMLEN]; // Name of function or arg
    hlType typ;        // Return type
    int numArg;        // Number of args
    int firstArg;      // Index of first arg in chain
    int next;          // Index of next function in chain
    bool bVararg;      // True if variable num args
} PH_FUNC_STRUCT;

typedef struct ph_arg_tag {
    char name[SYMLEN]; // Name of function or arg
    hlType typ;        // Parameter type
} PH_ARG_STRUCT;

#define DELTA_FUNC 32 // Number to alloc at once

#define PH_JUNK 0     // LPSTR      buffer, nothing happened
#define PH_PROTO 1    // LPPH_FUNC  ret val, func name, args
#define PH_FUNCTION 2 // LPPH_FUNC  ret val, func name, args
#define PH_TYPEDEF 3  // LPPH_DEF   definer and definee
#define PH_DEFINE 4   // LPPH_DEF   definer and definee
#define PH_ERROR 5    // LPSTR      error string
#define PH_WARNING 6  // LPSTR      warning string
#define PH_MPROTO 7   // ?????      multi proto????
#define PH_VAR 8      // ?????      var decl

// PROTOS
bool phData(char *buff, int ndata);
bool phPost(void);
bool phFree(void);
void phBuffToFunc(char *buff);
void phBuffToDef(char *buff);

#endif // DCC_PARSEHDR_H
