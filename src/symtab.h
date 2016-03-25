#ifndef SYMTAB_H
#define SYMTAB_H

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

/*
   Symbol table prototypes
   (C) Mike van Emmerik
*/

#include "dcc.h"

// Symbol table structs and protos

typedef struct {
    char *pSymName;    // Ptr to symbolic name or comment
    uint32_t symOff;   // Symbol image offset
    PPROC symProc;     // Procedure pointer
    uint16_t preHash;  // Hash value before the modulo
    uint16_t postHash; // Hash value after the modulo
    uint16_t nextOvf;  // Next entry this hash bucket, or -1
    uint16_t prevOvf;  // Back link in Ovf chain
} SYMTABLE;

// The table types
typedef enum _tableType {
    Label = 0,      // The label table
    Comment,        // The comment table
    NUM_TABLE_TYPES // Number of entries: must be last
} tableType;


void createSymTables(void);
void destroySymTables(void);
void enterSym(char *symName, uint32_t symOff, PPROC symProc, bool bSymToo);
bool readSym(char *symName, uint32_t *pSymOff, PPROC *pSymProc);
bool readVal(char *symName, uint32_t symOff, PPROC symProc);
void deleteSym(char *symName);
void deleteVal(uint32_t symOff, PPROC symProc, bool bSymToo);
bool findVal(uint32_t symOff, PPROC symProc, uint16_t *pIndex);
uint16_t symHash(char *name, uint16_t *pre);
uint16_t valHash(uint32_t off, PPROC proc, uint16_t *pre);
void selectTable(tableType); // Select a particular table */
char *addStrTbl(char *pStr); // Add string to string table */

#endif // SYMTAB_H

