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

/* Program for making the DCC signature file from a Turbo Pascal TURBO.TPU */

/* Fundamental problem: there seems to be no information linking the names
	in the system unit ("V" category) with their routines, except trial and
	error. I have entered a few. There is no guarantee that the same pmap
	offset will map to the same routine in all versions of turbo.tpl. They
	seem to match so far in version 4 and 5.0 */

#include <stdio.h>
#include <io.h>
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <string.h>

/*#include "dcc.h"*/
#include "perfhlib.h"		/* Symbol table prototypes */


/* Symbol table constnts */
#define	SYMLEN	16			/* Number of chars in the symbol name, incl null */
#define PATLEN	23			/* Number of bytes in the pattern part */
#define C		2.2			/* Sparseness of graph. See Czech,
								Havas and Majewski for details */
#define SYMALLOC 20			/* Number of entries in key[] to realloc for at once */
#define WILD	0xF4		/* The value used for wildcards in patterns */

/* prototypes */
int  readSyms(void);		/* Read the symbols into *keys[] */
void readPattern(word off);	/* Read the pattern to pat[] */
void saveFile(void);		/* Save the info */
void fixWildCards(byte p[]);/* Insert wild cards into the pattern p[] */

char buf[100], bufSave[7];	/* Temp buffer for reading the file */
char pat[PATLEN+3];			/* Buffer for the procedure pattern */
word cvSize, cvSizeSave;	/* Count of bytes remaining in the codeview area */
word byte6;					/* A temp */
unsigned long filePos;		/* File position at start of codeview area */
unsigned long imageOffset;	/* Offset of the executable image into the file */

typedef struct _hashEntry
{
	char name[SYMLEN];		/* The symbol name */
	byte pat [PATLEN];		/* The pattern */
	word offset;			/* Offset (needed temporarily) */
} HASHENTRY;

HASHENTRY *keys;			/* Pointer to the array of keys */
int	 numKeys;				/* Number of useful codeview symbols */
FILE *f, *f2;				/* .lib and output files respectively */

byte *leData;				/* Pointer to 64K of alloc'd data. Some .lib files
								have the symbols (PUBDEFs) *after* the data
								(LEDATA), so you need to keep the data here */
word maxLeData;				/* How much data we have in there */
	
void
main(int argc, char *argv[])
{
	int s;

	if (((argv[1][0] == '-') || (argv[1][0] == '-')) &&
		((argv[1][1] == 'h') || (argv[1][1] == '?')))
	{
		printf(
	"This program is to make 'signatures' of some Turbo Pascal library calls for\n"
	"the dcc program. It needs as the first arg the path to turbo.tpl, and as\n"
	"the second arg, the name of the signature file to be generated.\n\n"
	"Example: makedstp \\mylib\\turbo.tpl dcct4p.sig\n"
			  );
		exit(0);
	}
	if (argc <= 2)
	{
		printf("Usage: makedstp <path\turbo.tpl> <signame>\nor makedstp -h for help\n");
		exit(1);
	}

	if ((f = fopen(argv[1], "rb")) == NULL)
	{
		printf("Cannot read %s\n", argv[1]);
		exit(2);
	}

	if ((f2 = fopen(argv[2], "wb")) == NULL)
	{
		printf("Cannot write %s\n", argv[2]);
		exit(2);
	}

	fprintf(stderr, "Seed: ");
	scanf("%d", &s);
	srand(s);

	numKeys = readSyms();			/* Read the keys (symbols) */

printf("Num keys: %d; vertices: %d\n", numKeys, (int)(numKeys*C));

	hashParams(						/* Set the parameters for the hash table */
		numKeys,					/* The number of symbols */
		PATLEN,						/* The length of the pattern to be hashed */
		256,						/* The character set of the pattern (0-FF) */
		0,							/* Minimum pattern character value */
		(int)(numKeys*C));			/* C is the sparseness of the graph. See Czech,
										Havas and Majewski for details */

	/* The following two functions are in perfhlib.c */
	map();							/* Perform the mapping. This will call
										getKey() repeatedly */
	assign();						/* Generate the function g */

	saveFile();						/* Save the resultant information */

	fclose(f);
	fclose(f2);
	
}

/* Called by map(). Return the i+1th key in *pKeys */
void
getKey(int i, byte **pKeys)
{
	*pKeys = (byte *)&keys[i].pat;
}

/* Display key i */
void
dispKey(int i)
{
	printf("%s", keys[i].name);
}


/*	*	*	*	*	*	*	*	*	*	*	*  *\
*												*
*		R e a d   t h e   t p l     f i l e		*
*												*
\*	*	*	*	*	*	*	*	*	*	*	*  */

/* Read the .tpl file, and put the keys into the array *keys[]. Return
	the count */


word cmap, pmap, csegBase, unitBase;
word offStCseg, skipPmap;
int count = 0;
static int	cAllocSym = 0;
int unitNum = 0;
char version, charProc, charFunc;

void
allocSym(int count)
{
	/* Reallocate keys[] for count+1 entries, if needed */
	if (count >= cAllocSym)
	{
		cAllocSym += SYMALLOC;
		if ((keys = (HASHENTRY *)realloc(keys, cAllocSym * sizeof(HASHENTRY)))
			== 0)
		{
			printf("Could not realloc keys[] to %d bytes\n",
				cAllocSym * sizeof(HASHENTRY));
			exit(10);
		}
	}
}


void grab(int n)
{
	if (fread(buf, 1, n, f) != (size_t)n)
	{
		printf("Could not read\n");
		exit(11);
	}
}

byte
readByte(void)
{
	byte b;

	if (fread(&b, 1, 1, f) != 1)
	{
		printf("Could not read\n");
		exit(11);
	}
	return b;
}

void
readString(void)
{
	byte len;

	len = readByte();
	grab(len);
	buf[len] = '\0';
}

word
readShort(void)
{
	byte b1, b2;

	if (fread(&b1, 1, 1, f) != 1)
	{
		printf("Could not read\n");
		exit(11);
	}
	if (fread(&b2, 1, 1, f) != 1)
	{
		printf("Could not read\n");
		exit(11);
	}
	return (b2 << 8) + b1;
}

word csegoffs[100];
word csegIdx;

void
readCmapOffsets(void)
{
	word cumsize, csize;
	word i;

	/* Read the cmap table to find the start address of each segment */
	fseek(f, unitBase+cmap, SEEK_SET);
	cumsize = 0;
	csegIdx = 0;
	for (i=cmap; i < pmap; i+=8)
	{
		readShort();					/* Always 0 */
		csize = readShort();
		if (csize == 0xFFFF) continue;	/* Ignore the first one... unit init */
		csegoffs[csegIdx++] = cumsize;
		cumsize += csize;
		grab(4);
	}
}


void
enterSym(char *name, word pmapOffset)
{
	word pm, cm, codeOffset, pcode;
	word j;

	/* Enter a symbol with given name */
	allocSym(count);
	strcpy(keys[count].name, name);
	pm = pmap + pmapOffset;			/* Pointer to the 4 byte pmap structure */
	fseek(f, unitBase+pm, SEEK_SET);/* Go there */
	cm = readShort();				/* CSeg map offset */
	codeOffset = readShort();		/* How far into the code segment is our rtn */
	j = cm / 8;						/* Index into the cmap array */
	pcode = csegBase+csegoffs[j]+codeOffset;
	fseek(f, unitBase+pcode, SEEK_SET);		/* Go there */
	grab(PATLEN);					/* Grab the pattern to buf[] */
	fixWildCards(buf);				/* Fix the wild cards */
	memcpy(keys[count].pat, buf, PATLEN);	/* Copy to the key array */
	count++;						/* Done one more */
}


#define roundUp(w) ((w + 0x0F) & 0xFFF0)

void
unknown(unsigned j, unsigned k)
{
	/* Mark calls j to k (not inclusive) as unknown */
	unsigned i;

	for (i=j; i < k; i+= 4)
	{
		sprintf(buf, "UNKNOWN%03X", i);
		enterSym(buf, i);
	}
}

void
enterSystemUnit(void)
{
	/* The system unit is special. The association between keywords and
		pmap entries is not stored in the .tpl file (as far as I can tell).
		So we hope that they are constant pmap entries.
	*/

	fseek(f, 0x0C, SEEK_SET);
	cmap = readShort();
	pmap = readShort();
	fseek(f, offStCseg, SEEK_SET);
	csegBase = roundUp(readShort());		/* Round up to next 16 bdry */
printf("CMAP table at %04X\n", cmap);
printf("PMAP table at %04X\n", pmap);
printf("Code seg base %04X\n", csegBase);

	readCmapOffsets();

	enterSym("INITIALISE",	0x04);
	enterSym("UNKNOWN008",	0x08);
	enterSym("EXIT",		0x0C);
	enterSym("BlockMove",	0x10);
	unknown(0x14, 0xC8);
	enterSym("PostIO",		0xC8);
	enterSym("UNKNOWN0CC",	0xCC);
	enterSym("STACKCHK",	0xD0);
	enterSym("UNKNOWN0D4",	0xD4);
	enterSym("WriteString",	0xD8);
	enterSym("WriteInt",	0xDC);
	enterSym("UNKNOWN0E0",	0xE0);
	enterSym("UNKNOWN0E4",	0xE4);
	enterSym("CRLF",		0xE8);
	enterSym("UNKNOWN0EC",	0xEC);
	enterSym("UNKNOWN0F0",	0xF0);
	enterSym("UNKNOWN0F4",	0xF4);
	enterSym("ReadEOL", 	0xF8);
	enterSym("Read",		0xFC);
	enterSym("UNKNOWN100",	0x100);
	enterSym("UNKNOWN104",	0x104);
	enterSym("PostWrite",	0x108);
	enterSym("UNKNOWN10C",	0x10C);
	enterSym("Randomize",	0x110);
	unknown(0x114, 0x174);
	enterSym("Random",		0x174);
	unknown(0x178, 0x1B8);
	enterSym("FloatAdd",	0x1B8);		/* A guess! */
	enterSym("FloatSub",	0x1BC);		/* disicx - dxbxax -> dxbxax*/
	enterSym("FloatMult",	0x1C0);		/* disicx * dxbxax -> dxbxax*/
	enterSym("FloatDivide",	0x1C4);		/* disicx / dxbxax -> dxbxax*/
	enterSym("UNKNOWN1C8",	0x1C8);
	enterSym("DoubleToFloat",0x1CC);	/* dxax to dxbxax */
	enterSym("UNKNOWN1D0",	0x1D0);
	enterSym("WriteFloat",	0x1DC);
	unknown(0x1E0, 0x200);

}

void
nextUnit(void)
{
	/* Find the start of the next unit */

	word dsegBase, sizeSyms, sizeOther1, sizeOther2;

	fseek(f, unitBase+offStCseg, SEEK_SET);
	dsegBase = roundUp(readShort());
	sizeSyms = roundUp(readShort());
	sizeOther1 = roundUp(readShort());
	sizeOther2 = roundUp(readShort());

	unitBase += dsegBase + sizeSyms + sizeOther1 + sizeOther2;

fseek(f, unitBase, SEEK_SET);
if (fread(buf, 1, 4, f) == 4)
{
  buf[4]='\0';
  printf("Start of unit: found %s\n", buf);
}

}

void
setVersionSpecifics(void)
{

	version = buf[3];			/* The x of TPUx */

	switch (version)
	{
		case '0':				/* Version 4.0 */
			offStCseg = 0x14;	/* Offset to the LL giving the Cseg start */
			charProc = 'T';		/* Indicates a proc in the dictionary */
			charFunc = 'U';		/* Indicates a function in the dictionary */
			skipPmap = 6;		/* Bytes to skip after Func to get pmap offset */
			break;


		case '5':				/* Version 5.0 */
			offStCseg = 0x18;	/* Offset to the LL giving the Cseg start */
			charProc = 'T';		/* Indicates a proc in the dictionary */
			charFunc = 'U';		/* Indicates a function in the dictionary */
			skipPmap = 1;		/* Bytes to skip after Func to get pmap offset */
			break;

		default:
			printf("Unknown version %c!\n", version);
			exit(1);

	}

}

long filePosn[20];
word posIdx = 0;

void
savePos(void)
{
	if (posIdx >= 20)
	{
		printf("Overflowed filePosn array\n");
		exit(1);
	}
	fseek(f, 0, SEEK_CUR);		/* Needed due to bug in MS fread()! */
	filePosn[posIdx++] = _lseek(fileno(f), 0, SEEK_CUR);
}

void
restorePos(void)
{
	if (posIdx == 0)
	{
		printf("Underflowed filePosn array\n");
		exit(1);
	}
	fseek(f, filePosn[--posIdx], SEEK_SET);
}



void
enterUnitProcs(void)
{

	word i, LL;
	word hash, hsize, dhdr, pmapOff;
	char cat;
	char name[40];

	fseek(f, unitBase+0x0C, SEEK_SET);
	cmap = readShort();
	pmap = readShort();
	fseek(f, unitBase+offStCseg, SEEK_SET);
	csegBase = roundUp(readShort());		/* Round up to next 16 bdry */
printf("CMAP table at %04X\n", cmap);
printf("PMAP table at %04X\n", pmap);
printf("Code seg base %04X\n", csegBase);

	readCmapOffsets();

	fseek(f, unitBase+pmap, SEEK_SET);		/* Go to first pmap entry */
	if (readShort() != 0xFFFF)				/* FFFF means none */
	{
		sprintf(name, "UNIT_INIT_%d", ++unitNum);
		enterSym(name, 0);					/* This is the unit init code */
	}

	fseek(f, unitBase+0x0A, SEEK_SET);
	hash = readShort();
//printf("Hash table at %04X\n", hash);
	fseek(f, unitBase+hash, SEEK_SET);
	hsize = readShort();
//printf("Hash table size %04X\n", hsize);
	for (i=0; i <= hsize; i+= 2)
	{
		dhdr = readShort();
		if (dhdr)
		{
			savePos();
			fseek(f, unitBase+dhdr, SEEK_SET);
			do
			{
				LL = readShort();
				readString();
				strcpy(name, buf);
				cat = readByte();
				if ((cat == charProc) || (cat == charFunc))
				{
					grab(skipPmap);		/* Skip to the pmap */
					pmapOff = readShort();	/* pmap offset */
printf("pmap offset for %13s: %04X\n", name, pmapOff);
					enterSym(name, pmapOff);
				}
//printf("%13s %c ", name, cat);
				if (LL)
				{
//printf("LL seek to %04X\n", LL);
					fseek(f, unitBase+LL, SEEK_SET);
				}
			} while (LL);
			restorePos();
		}
	}

}

int
readSyms(void)
{
	grab(4);
	if ((strncmp(buf, "TPU0", 4) != 0) && ((strncmp(buf, "TPU5", 4) != 0)))
	{
		printf("Not a Turbo Pascal version 4 or 5 library file\n");
		fclose(f);
		exit(1);
	}

	setVersionSpecifics();

	enterSystemUnit();
	unitBase = 0;
	do
	{
		nextUnit();
		if (feof(f)) break;
		enterUnitProcs();
	} while (1);

	fclose(f);
	return count;
}


/*	*	*	*	*	*	*	*	*	*	*	*  *\
*												*
*		S a v e   t h e   s i g   f i l e		*
*												*
\*	*	*	*	*	*	*	*	*	*	*	*  */


void
writeFile(char *buffer, int len)
{
	if ((int)fwrite(buffer, 1, len, f2) != len)
	{
		printf("Could not write to file\n");
		exit(1);
	}
}

void
writeFileShort(word w)
{
	byte b;

	b = (byte)(w & 0xFF);
	writeFile(&b, 1);		/* Write a short little endian */
	b = (byte)(w>>8);
	writeFile(&b, 1);
}

void
saveFile(void)
{
	int i, len;
	word *pTable;

	writeFile("dccs", 4);					/* Signature */				
	writeFileShort(numKeys);				/* Number of keys */
	writeFileShort((short)(numKeys * C));	/* Number of vertices */
	writeFileShort(PATLEN);					/* Length of key part of entries */
	writeFileShort(SYMLEN);					/* Length of symbol part of entries */

	/* Write out the tables T1 and T2, with their sig and byte lengths in front */
	writeFile("T1", 2);						/* "Signature" */
	pTable = readT1();
	len = PATLEN * 256;
	writeFileShort(len * sizeof(word));
	for (i=0; i < len; i++)
	{
		writeFileShort(pTable[i]);
	}
	writeFile("T2", 2);
	pTable = readT2();
	writeFileShort(len * sizeof(word));
	for (i=0; i < len; i++)
	{
		writeFileShort(pTable[i]);
	}

	/* Write out g[] */
	writeFile("gg", 2);			  			/* "Signature" */
	pTable = readG();
	len = (short)(numKeys * C);
	writeFileShort(len * sizeof(word));
	for (i=0; i < len; i++)
	{
		writeFileShort(pTable[i]);
	}

	/* Now the hash table itself */
	writeFile("ht ", 2);			  			/* "Signature" */
	writeFileShort(numKeys * (SYMLEN + PATLEN + sizeof(word)));	/* byte len */
	for (i=0; i < numKeys; i++)
	{
		writeFile((char *)&keys[i], SYMLEN + PATLEN);
	}
}



