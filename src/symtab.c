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

// Symbol table functions (C) Mike van Emmerik

/*
 This file implements a symbol table with a symbolic name, a symbol value (word), and a procedure number.
 Two tables are maintained, to be able to look up by name or by value.
 Pointers are used for the duplicated symbolic name to save space. Both tables have the same structure.
 The hash tables automatically expand when they get 90% full; they are never compressed.
 Expanding the tables could take some time, since about half of the entries have to be moved on average.
 Linear probing is used, due to the difficulty of implementing (e.g.) quadratic probing with a variable table size.
*/


#include "symtab.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TABLESIZE 16   // Number of entries added each expansion, probably has to be a power of 2
#define STRTABSIZE 256 // Size string table is inc'd by
#define NIL ((uint16_t) - 1)

static uint16_t numEntry;  // Number of entries in this table
static uint16_t tableSize; // Size of the table (entries)
static SYMTABLE *symTab;   // Pointer to the symbol hashed table
static SYMTABLE *valTab;   // Pointer to the value  hashed table

static char *pStrTab;  // Pointer to the current string table
static int strTabNext; // Next free index into pStrTab

static tableType curTableType; // Which table is current

typedef struct {
    SYMTABLE *symTab;
    SYMTABLE *valTab;
    uint16_t numEntry;
    uint16_t tableSize;
} _tableInfo;

_tableInfo tableInfo[NUM_TABLE_TYPES]; // Array of info about tables

// Local prototypes
static void expandSym(void);


// Create a new symbol table. Returns "handle"
void createSymTables(void)
{
    // Initilise the comment table
    // NB - there is no symbol hashed comment table

    numEntry = 0;
    tableSize = TABLESIZE;
    valTab = allocMem(sizeof(SYMTABLE) * TABLESIZE);
    memset(valTab, 0, sizeof(SYMTABLE) * TABLESIZE);

    tableInfo[Comment].symTab = 0;
    tableInfo[Comment].valTab = valTab;
    tableInfo[Comment].numEntry = numEntry;
    tableInfo[Comment].tableSize = tableSize;

    // Initialise the label table
    numEntry = 0;
    tableSize = TABLESIZE;
    symTab = allocMem(sizeof(SYMTABLE) * TABLESIZE);
    memset(symTab, 0, sizeof(SYMTABLE) * TABLESIZE);

    valTab = allocMem(sizeof(SYMTABLE) * TABLESIZE);
    memset(valTab, 0, sizeof(SYMTABLE) * TABLESIZE);

    tableInfo[Label].symTab = symTab;
    tableInfo[Label].valTab = valTab;
    tableInfo[Label].numEntry = numEntry;
    tableInfo[Label].tableSize = tableSize;
    curTableType = Label;

    // Now the string table
    strTabNext = 0;
    pStrTab = allocMem(STRTABSIZE);

    tableInfo[Label].symTab = symTab;
    tableInfo[Label].valTab = valTab;
    tableInfo[Label].numEntry = numEntry;
    tableInfo[Label].tableSize = tableSize;
    curTableType = Label;
}


void selectTable(tableType tt)
{
    if (curTableType == tt)
        return; // Nothing to do

    symTab = tableInfo[tt].symTab;
    valTab = tableInfo[tt].valTab;
    numEntry = tableInfo[tt].numEntry;
    tableSize = tableInfo[tt].tableSize;
    curTableType = tt;
}

void destroySymTables(void)
{
    selectTable(Label);
    free(symTab); // The symbol hashed label table
    free(valTab); // And the value hashed label table
    selectTable(Comment);
    free(valTab); // And the value hashed comment table
}


// Hash the symbolic name
uint16_t symHash(char *name, uint16_t *pre)
{
    uint16_t h = 0;

    for (size_t i = 0; i < strlen(name); i++) {
        char ch = name[i];
        h = (h << 2) ^ ch;
        h += (ch >> 2) + (ch << 5);
    }

    *pre = h;             // Pre modulo hash value
    return h % tableSize; // Post modulo hash value
}


/*
 Hash the symOff and symProc fields
 Note: for the time being, there no use is made of the symProc field
*/
uint16_t valHash(uint32_t symOff, PPROC symProc, uint16_t *pre)
{
    uint16_t h = (symOff ^ (symOff >> 8));

    *pre = h;             // Pre modulo hash value
    return h % tableSize; // Post modulo hash value
}


void enterSym(char *symName, uint32_t symOff, PPROC symProc, bool bSymToo)
{
    uint16_t h, pre, j;

    if ((numEntry / 9 * 10) >= tableSize) // Table is full. Expand it
        expandSym();

    // Enter it into the value hashed table first
    h = valHash(symOff, symProc, &pre); // Ideal spot for this entry
    if (valTab[h].symProc == 0)         // Collision?
    {
        // No. Just insert here
        valTab[h].pSymName = symName; // Symbol name ptr
        valTab[h].symOff = symOff;    // Offset of the symbol
        valTab[h].symProc = symProc;  // Symbol's proc num
        valTab[h].preHash = pre;      // Pre modulo hash value
        valTab[h].postHash = h;       // Post modulo hash value
        valTab[h].nextOvf = NIL;      // No overflow
        valTab[h].prevOvf = NIL;      // No back link
    } else {
        // Linear probing, for now
        j = (h + 1) % tableSize;
        while (j != h) {
            if (valTab[j].symProc == 0) {
                // Insert here
                valTab[j].pSymName = symName; // Symbol name ptr
                valTab[j].symOff = symOff;    // Offset of the symbol
                valTab[j].symProc = symProc;  // Symbol's proc num
                valTab[j].preHash = pre;      // Pre modulo hash value
                valTab[j].postHash = h;       // Post modulo hash value
                // Insert after the primary entry in the table
                valTab[j].nextOvf = valTab[h].nextOvf;
                valTab[h].nextOvf = j;
                valTab[j].prevOvf = h; // The backlink
                break;
            } else {
                // Probe further
                j = (j + 1) % tableSize;
            }
        }
        if (j == h) {
            printf("enterSym: val table overflow!\n");
            exit(1);
        }
    }

    // Now enter into the symbol hashed table as well, if reqd
    if (!bSymToo)
        return;
    h = symHash(symName, &pre);  // Ideal spot for this entry
    if (symTab[h].pSymName == 0) // Collision?
    {
        // No. Just insert here
        symTab[h].pSymName = symName; // Symbol name ptr
        symTab[h].symOff = symOff;    // Offset of the symbol
        symTab[h].symProc = symProc;  // Symbol's proc num
        symTab[h].preHash = pre;      // Pre modulo hash value
        symTab[h].postHash = h;       // Post modulo hash value
        symTab[h].nextOvf = NIL;      // No overflow
        symTab[h].prevOvf = NIL;      // No back link
    } else {
        // Linear probing, for now
        j = (h + 1) % tableSize;
        while (j != h) {
            if (symTab[j].pSymName == 0) {
                // Insert here
                symTab[j].pSymName = symName; // Symbol name ptr
                symTab[j].symOff = symOff;    // Offset of the symbol
                symTab[j].symProc = symProc;  // Symbol's proc num
                symTab[j].preHash = pre;      // Pre modulo hash value
                symTab[j].postHash = h;       // Post modulo hash value
                // Insert after the primary entry in the table
                symTab[j].nextOvf = symTab[h].nextOvf;
                symTab[h].nextOvf = j;
                symTab[j].prevOvf = h; // The backlink
                break;
            } else {
                // Probe further
                j = (j + 1) % tableSize;
            }
        }
        if (j == h) {
            printf("enterSym: sym table overflow!\n");
            exit(EXIT_FAILURE);
        }
    }
}


bool findSym(char *symName, uint16_t *pIndex)
{
    uint16_t pre;

    uint16_t j = symHash(symName, &pre);

    do {
        if (symTab[j].pSymName == 0) {
            return false; // No entry at all
        }
        if (strcmp(symName, symTab[j].pSymName) == 0) {
            *pIndex = j;
            return true; // Symbol found
        }
        j = symTab[j].nextOvf; // Follow the chain
    } while (j != NIL);

    return false; // End of chain
}


// Find symbol by value
bool findVal(uint32_t symOff, PPROC symProc, uint16_t *pIndex)
{
    uint16_t pre;

    uint16_t j = valHash(symOff, symProc, &pre);

    do {
        if (valTab[j].symProc == 0) {
            return false; // No entry at all
        }
        if (valTab[j].symOff == symOff) {
            *pIndex = j;
            return true; // Symbol found
        }
        j = valTab[j].nextOvf; // Follow the chain
    } while (j != NIL);

    return false; // End of chain
}


uint16_t findBlankSym(char *symName)
{
    uint16_t pre;

    uint16_t h = symHash(symName, &pre);
    uint16_t j = h;

    do {
        if (symTab[j].pSymName == 0) {
            return j; // Empty entry. Terminate probing
        }
        uint16_t tmp = (++j) % tableSize; // Linear probing
        j = tmp;
    } while (j != h);

    printf("Could not find blank entry in table! Num entries is %d of %d\n", numEntry, tableSize);
    return 0;
}


// Using the symbolic name, read the value
bool readSym(char *symName, uint32_t *pSymOff, PPROC *pSymProc)
{
    uint16_t i;

    if (!findSym(symName, &i))
        return false;

    *pSymOff = symTab[i].symOff;
    *pSymProc = symTab[i].symProc;
    return true;
}


// Using the value, read the symbolic name
bool readVal(char *symName, uint32_t symOff, PPROC symProc)
{
    uint16_t i;

    if (!findVal(symOff, symProc, &i))
        return false;

    strcpy(symName, valTab[i].pSymName);
    return true;
}


/*
 A doubly linked list of entries belonging to the same hash bucket is maintained,
 to prevent the need for many entries to be moved when deleting an entry.
 It is implemented with indexes, and is not an open hashing system.
 Symbols are deleted from both hash tables.

 Known limitation: strings are never deleted from the string table
*/
void deleteSym(char *symName)
{
    uint16_t i, j, back;
    uint32_t symOff;
    PPROC symProc;

    // Delete from symbol hashed table first
    if (!findSym(symName, &i)) {
        printf("Could not delete non existant symbol name %s\n", symName);
        exit(EXIT_FAILURE);
    }
    symOff = symTab[i].symOff;   // Remember these for valTab
    symProc = symTab[i].symProc;
    j = symTab[i].nextOvf;       // Look at next overflowed entry

    if (j == NIL) // Any overflows?
    {
        /* No, so we just wipe out this record.
           Must NIL the pointer of the previous record, however */
        symTab[symTab[i].prevOvf].nextOvf = NIL;
        j = i; // So we wipe out the current name
    } else {
        /* Yes, move this entry to this vacated spot. Note that the nextOvf
           field will still point to the next record in the overflow chain,
           but we need to preserve the backlink for adjusting the current
           item's backlink */
        back = symTab[j].prevOvf;
        memcpy(&symTab[i], &symTab[j], sizeof(SYMTABLE));
        symTab[i].prevOvf = back;
    }
    // And now mark the vacated record as empty
    symTab[j].pSymName = 0; // Rub out the name

    // Delete from value hashed table
    if (!findVal(symOff, symProc, &i)) {
        printf("Could not delete non existant symbol off %04X proc %d\n", symOff,
                symProc->procEntry);
        exit(1);
    }

    j = valTab[i].nextOvf; // Look at next overflowed entry

    if (j == NIL) // Any overflows?
    {
        /* No, so we just wipe out this record.
           Must NIL the pointer of the previous record, however */
        valTab[valTab[i].prevOvf].nextOvf = NIL;
        j = i; // So we wipe out the current entry
    } else {
        /* Yes, move this entry to this vacated spot. Note that the nextOvf
           field will still point to the next record in the overflow chain,
           but we need to preserve the backlink for adjusting the current
           item's backlink */
        back = valTab[j].prevOvf;
        memcpy(&valTab[i], &valTab[j], sizeof(SYMTABLE));
        valTab[i].prevOvf = back;
    }
    // And now mark the vacated record as empty
    valTab[j].symProc = 0; // Rub out the entry
}


void deleteVal(uint32_t symOff, PPROC symProc, bool bSymToo)
{
    uint16_t i, j, back;
    char *symName;

    // Delete from value hashed table
    if (!findVal(symOff, symProc, &i)) {
        printf("Could not delete non existant symbol off %04X proc %p\n", symOff, symProc);
        exit(EXIT_FAILURE);
    }
    symName = symTab[i].pSymName; // Remember this for symTab
    j = valTab[i].nextOvf;        // Look at next overflowed entry

    if (j == NIL) // Any overflows?
    {
        /* No, so we just wipe out this record.
           Must NIL the pointer of the previous record, however */
        valTab[valTab[i].prevOvf].nextOvf = NIL;
        j = i; // So we wipe out the current entry
    } else {
        /* Yes, move this entry to this vacated spot. Note that the nextOvf
           field will still point to the next record in the overflow chain,
           but we need to preserve the backlink for adjusting the current
           item's backlink */
        back = valTab[j].prevOvf;
        memcpy(&valTab[i], &valTab[j], sizeof(SYMTABLE));
        valTab[i].prevOvf = back;
    }
    // And now mark the vacated record as empty
    valTab[j].symProc = 0; // Rub out the entry

    // If requested, delete from symbol hashed table now
    if (!bSymToo)
        return;
    if (!findSym(symName, &i)) {
        printf("Could not delete non existant symbol name %s\n", symName);
        exit(1);
    }
    j = symTab[i].nextOvf; // Look at next overflowed entry

    if (j == NIL) // Any overflows?
    {
        /* No, so we just wipe out this record.
           Must NIL the pointer of the previous record, however */
        symTab[symTab[i].prevOvf].nextOvf = NIL;
        j = i; // So we wipe out the current name
    } else {
        /* Yes, move this entry to this vacated spot. Note that the nextOvf
           field will still point to the next record in the overflow chain,
           but we need to preserve the backlink for adjusting the current
           item's backlink */
        back = symTab[j].prevOvf;
        memcpy(&symTab[i], &symTab[j], sizeof(SYMTABLE));
        symTab[i].prevOvf = back;
    }
    // And now mark the vacated record as empty
    symTab[j].pSymName = 0; // Rub out the name
}


static void expandSym(void)
{
    uint16_t j, n, newPost;

    printf("\nResizing table...\r");
    /* We double the table size each time, so on average only half of the
       entries move to the new half. This works because we are effectively
       shifting the "binary point" of the hash value to the left each time,
       thereby leaving the number unchanged or adding an MSBit of 1. */

    tableSize <<= 2;
    symTab = allocVar(symTab, tableSize * sizeof(SYMTABLE));

    // Now we have to move some of the entries to take advantage of the extra space

    for (int i = 0; i < numEntry; i++) {
        newPost = symTab[i].preHash % tableSize;
        if (newPost != symTab[i].postHash) {
            // This entry is now in the wrong place. Copy it to the new position, then delete it.
            j = findBlankSym(symTab[i].pSymName);
            memcpy(&symTab[j], &symTab[i], sizeof(SYMTABLE));
            // Correct the post hash value
            symTab[j].postHash = newPost;

            // Now adjust links
            n = symTab[j].prevOvf;
            if (n != NIL) {
                symTab[n].nextOvf = j;
            }

            n = symTab[j].nextOvf;
            if (n != NIL) {
                symTab[n].prevOvf = j;
            }

            // Mark old position as deleted
            symTab[i].pSymName = 0;
        }
    }
}


// This function adds to the string table. At this stage, strings are not deleted
char *addStrTbl(char *pStr)
{
    if ((strTabNext + strlen(pStr) + 1) >= STRTABSIZE) {
        /* We can't realloc the old string table pointer, since that will
           potentially move the string table, and pointers will be invalid.
           So we realloc this one to its present size (hopefully it won't move),
           and allocate a new one */
        allocVar(pStrTab, strTabNext);
        pStrTab = allocMem(STRTABSIZE);
        strTabNext = 0;
    }
    char *p = strcpy(&pStrTab[strTabNext], pStr);
    strTabNext += strlen(pStr) + 1;
    return p;
}
