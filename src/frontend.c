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

/*
 dcc project Front End module
 Loads a program into simulated main memory and builds the procedure list.
*/

#include "dcc.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// PSP structure
typedef struct {
    uint16_t int20h;        // interrupt 20h
    uint16_t eof;           // segment, end of allocation block
    uint8_t  res1;          // reserved
    uint8_t  dosDisp[5];    // far call to DOS function dispatcher
    uint8_t  int22h[4];     // vector for terminate routine
    uint8_t  int23h[4];     // vector for ctrl+break routine
    uint8_t  int24h[4];     // vector for error routine
    uint8_t  res2[22];      // reserved
    uint16_t segEnv;        // segment address of environment block
    uint8_t  res3[34];      // reserved
    uint8_t  int21h[6];     // opcode for int21h and far return
    uint8_t  res4[6];       // reserved
    uint8_t  fcb1[16];      // default file control block 1
    uint8_t  fcb2[16];      // default file control block 2
    uint8_t  res5[4];       // reserved
    uint8_t  cmdTail[0x80]; // command tail and disk transfer area
} PSP;

// EXE file header
typedef struct {
    uint16_t signature;      // .EXE signature: 0x4D 0x5A
    uint16_t lastPageSize;   // Size of the last page
    uint16_t numPages;       // Number of pages in the file
    uint16_t numReloc;       // Number of relocation items
    uint16_t numParaHeader;  // # of paragraphs in the header
    uint16_t minAlloc;       // Minimum number of paragraphs
    uint16_t maxAlloc;       // Maximum number of paragraphs
    uint16_t initSS;         // Segment displacement of stack
    uint16_t initSP;         // Contents of SP at entry
    uint16_t checkSum;       // Complemented checksum
    uint16_t initIP;         // Contents of IP at entry
    uint16_t initCS;         // Segment displacement of code
    uint16_t relocTabOffset; // Relocation table offset
    uint16_t overlayNum;     // Overlay number
} __attribute__((packed, aligned(1))) MZ_Header;

// Relocation table
typedef struct {
    uint16_t off; // Offset
    uint16_t seg; // Segment
} __attribute__((packed, aligned(1))) MZ_Reloc;

#define EXE_RELOCATION 0x10 // EXE images rellocated to above PSP

static MZ_Header *read_mz_header(FILE *fp);
static void LoadImage(FILE *fp, MZ_Header *hdr);
static void displayLoadInfo(MZ_Header *hdr);
static void displayMemMap(void);

/*
 FrontEnd - invokes the loader, parser, disassembler (if asm1), icode rewritter,
 and displays any useful information.
*/
void FrontEnd(char *filename, PCALL_GRAPH *pcallGraph)
{
    PPROC pProc;
    PSYM psym;
    int i, c;

    FILE *fp = fopen(filename, "rb");

    if (fp == NULL)
        fatalError(CANNOT_OPEN, filename); 

    MZ_Header *hdr = read_mz_header(fp);

    if (hdr == NULL) { // .com not handled for now
        fprintf(stderr, "%s: File format not recognized\n", filename);
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    // Load program into memory
    LoadImage(fp, hdr);

    if (option.verbose) {
        displayLoadInfo(hdr);
    }

    /* Do depth first flow analysis building call graph and procedure list,
       and attaching the I-code to each procedure */
    parse(pcallGraph);

    if (option.asm1) {
        printf("%s: writing assembler file %s\n", progname, asm1_name);
    }

    // Search through code looking for impure references and flag them
    for (pProc = pProcList; pProc; pProc = pProc->next) {
        for (i = 0; i < pProc->Icode.numIcode; i++) {
            if (pProc->Icode.icode[i].ic.ll.flg & (SYM_USE | SYM_DEF)) {
                psym = &symtab.sym[pProc->Icode.icode[i].ic.ll.caseTbl.numEntries];
                for (c = psym->label; c < psym->label + psym->size; c++) {
                    if (BITMAP(c, BM_CODE)) {
                        pProc->Icode.icode[i].ic.ll.flg |= IMPURE;
                        pProc->flg |= IMPURE;
                        break;
                    }
                }
            }
        }
        // Print assembler listing
        if (option.asm1)
            disassem(1, pProc);
    }

    if (option.Interact) {
        interactDis(pProcList, 0); // Interactive disassembler
    }

    // Converts jump target addresses to icode offsets
    for (pProc = pProcList; pProc; pProc = pProc->next)
        bindIcodeOff(pProc);

    // Print memory bitmap
    if (option.Map)
        displayMemMap();
}

// displayLoadInfo - Displays low level loader type info.
static void displayLoadInfo(MZ_Header *hdr)
{
//    printf("File type is %s\n", (prog.fCOM) ? "COM" : "EXE");
//    if (!prog.fCOM) {
    printf("Signature            = %04X\n",       hdr->signature);
    printf("File size %% 512     = %04X\n",       hdr->lastPageSize);
    printf("File size / 512      = %04X pages\n", hdr->numPages);
    printf("# relocation items   = %04X\n",       hdr->numReloc);
    printf("Offset to load image = %04X paras\n", hdr->numParaHeader);
    printf("Minimum allocation   = %04X paras\n", hdr->minAlloc);
    printf("Maximum allocation   = %04X paras\n", hdr->maxAlloc);
//    }
    size_t size = prog.cbImage - sizeof(PSP);
    printf("Load image size      = %04lX (%lu bytes)\n", size, size);
    printf("Initial SS:SP        = %04X:%04X\n",  prog.initSS, prog.initSP);
    printf("Initial CS:IP        = %04X:%04X\n",  prog.initCS, prog.initIP);

    if (option.VeryVerbose && prog.cReloc) {
        printf("\nRelocation Table\n");
        for (int i = 0; i < prog.cReloc; i++)
            printf("%06X -> [%04X]\n", prog.relocTable[i], LH(prog.Image + prog.relocTable[i]));
    }
    printf("\n");
}

// fill - Fills line for displayMemMap()
static void fill(int ip, char *bf)
{
    static uint8_t type[4] = { '.', 'd', 'c', 'x' };

    for (uint8_t i = 0; i < 16; i++, ip++) {
        *bf++ = ' ';
        *bf++ = (ip < prog.cbImage) ? type[(prog.map[ip >> 2] >> ((ip & 3) * 2)) & 3] : ' ';
    }
    *bf = '\0';
}

// displayMemMap - Displays the memory bitmap
static void displayMemMap(void)
{
    char c, b1[33], b2[33], b3[33];
    uint8_t i;
    int ip = 0;

    printf("\nMemory Map\n");
    while (ip < prog.cbImage) {
        fill(ip, b1);
        printf("%06X %s\n", ip, b1);
        ip += 16;
        for (i = 3, c = b1[1]; i < 32 && c == b1[i]; i += 2); // Check if all same
        if (i > 32) {
            fill(ip, b2); // Skip until next two are not same
            fill(ip + 16, b3);
            if (!(strcmp(b1, b2) || strcmp(b1, b3))) {
                printf("                   :\n");
                do {
                    ip += 16;
                    fill(ip + 16, b1);
                } while (!strcmp(b1, b2));
            }
        }
    }
    printf("\n");
}

static MZ_Header *read_mz_header(FILE *fp)
{
    MZ_Header *hdr = malloc(sizeof(MZ_Header));

    fseek(fp, 0, SEEK_SET);
    fread(hdr, sizeof(MZ_Header), 1, fp);

    if (hdr->signature != 0x5a4D && hdr->signature != 0x4D5a) {
        free(hdr);
        return NULL;
    }

    if (hdr->relocTabOffset == 0x40) { // This is a typical DOS kludge!
        free(hdr);
        fatalError(NEWEXE_FORMAT);
    }

    return hdr;
}

// LoadImage
static void LoadImage(FILE *fp, MZ_Header *hdr)
{
    /* Calculate the load module size. This is the number of pages in the file less the length
       of the header and reloc table less the number of bytes unused on last page */        
    size_t cb = hdr->numPages * 512 - (hdr->numParaHeader * 16);

    if (hdr->lastPageSize)
        cb -= (512 - hdr->lastPageSize);

    /* We quietly ignore minAlloc and maxAlloc since for our purposes it doesn't really matter
       where in real memory the program would end up. EXE programs can't really rely on their
       load location so setting the PSP segment to 0 is fine.
       Certainly programs that prod around in DOS or BIOS are going to have to load DS from
       a constant so it'll be pretty obvious. */
    prog.initCS = hdr->initCS + EXE_RELOCATION;
    prog.initIP = hdr->initIP;
    prog.initSS = hdr->initSS + EXE_RELOCATION;
    prog.initSP = hdr->initSP;
    prog.cReloc = hdr->numReloc;

    // Allocate the relocation table
    if (prog.cReloc) {
        MZ_Reloc *reloc_table = malloc(prog.cReloc * sizeof(MZ_Reloc *)); 
        prog.relocTable = allocMem(prog.cReloc * sizeof(uint32_t));
        fseek(fp, hdr->relocTabOffset, SEEK_SET);

        // Read in seg:offset pairs and convert to Image ptrs
        for (int i = 0; i < prog.cReloc; i++) {
            fread(&reloc_table[i], sizeof(MZ_Reloc), 1, fp);
            prog.relocTable[i] = reloc_table[i].off + ((reloc_table[i].seg + EXE_RELOCATION) << 4);
        }

        free(reloc_table);
    }

    // Seek to start of image
    fseek(fp, hdr->numParaHeader * 16, SEEK_SET);

    // Allocate a block of memory for the program.
    prog.cbImage = cb + sizeof(PSP);
    prog.Image = allocMem(prog.cbImage);
    prog.Image[0] = 0xCD; // Fill in PSP Int 20h location for termination checking
    prog.Image[1] = 0x20; 

    // Read in the image past where a PSP would go
    fread(prog.Image + sizeof(PSP), 1, cb, fp);

    // Set up memory map
    cb = (prog.cbImage + 3) / 4;
    prog.map = memset(allocMem(cb), BM_UNKNOWN, cb);

    // Relocate segment constants
    if (prog.cReloc) {
        for (int i = 0; i < prog.cReloc; i++) {
            uint8_t *p = &prog.Image[prog.relocTable[i]];
            uint16_t w = (uint16_t)LH(p) + EXE_RELOCATION;
            *p++ = (uint8_t)(w & 0x00FF);
            *p = (uint8_t)((w & 0xFF00) >> 8);
        }
    }

    fclose(fp);
}

// allocMem - malloc with failure test
void *allocMem(int cb)
{
    uint8_t *p;

    if ((p = malloc((size_t)cb)) == NULL) {
        fatalError(MALLOC_FAILED, cb);
    }

    return p;
}

// allocVar - reallocs extra variable space
void *allocVar(void *p, int newsize)
{
    if ((p = realloc(p, (size_t)newsize)) == NULL) {
        fatalError(MALLOC_FAILED, newsize);
    }

    return p;
}
