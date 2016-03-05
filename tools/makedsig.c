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

/* Program for making the DCC signature file */

/* Note: there is an untested assumption that the *first* segment definition
	with class CODE will be the one containing all useful functions in the
	LEDATA records. Functions such as _exit() have more than one segment
	declared with class CODE (MSC8 libraries) */

#include <stdio.h>
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
	"This program is to make 'signatures' of known c library calls for the dcc "
	"program. It needs as the first arg the name of a library file, and as the "
	"second arg, the name of the signature file to be generated.\n"
			  );
		exit(0);
	}
	if (argc <= 2)
	{
		printf("Usage: makedsig <libname> <signame>\nor makedsig -h for help\n");
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
*		R e a d   t h e   l i b   f i l e		*
*												*
\*	*	*	*	*	*	*	*	*	*	*	*  */

/* Read the .lib file, and put the keys into the array *keys[]. Return
	the count */

unsigned long offset;
byte lnum = 0;				/* Count of LNAMES  so far */
byte segnum = 0;			/* Count of SEGDEFs so far */
byte codeLNAMES;			/* Index of the LNAMES for "CODE" class */
byte codeSEGDEF;			/* Index of the first SEGDEF that has class CODE */
#define NONE 0xFF			/* Improbable segment index */


byte readByte(void);
word readWord(void);
void readString(void);
void readNN(int n);
void readNbuf(int n);
char printable(char c);


/* read a byte from file f */
byte readByte()
{
	byte b;

	if (fread(&b, 1, 1, f) != 1)
	{
		printf("Could not read byte offset %lX\n", offset);
		exit(2);
	}
	offset++;
	return b;
}

word readWord()
{
	byte b1, b2;

	b1 = readByte();
	b2 = readByte();

	return b1 + (b2 << 8);
}

void readNbuf(int n)
{
	if (fread(buf, 1, n, f) != (size_t)n)
	{
		printf("Could not read word offset %lX\n", offset);
		exit(2);
	}
	offset += n;
}

void readNN(int n)
{
	if (fseek(f, (long)n, SEEK_CUR) != 0)
	{
		printf("Could not seek file\n");
		exit(2);
	}
	offset += n;
}

/* read a length then string to buf[]; make it an asciiz string */
void readString()
{
	byte len;

	len = readByte();
	if (fread(buf, 1, len, f) != len)
	{
		printf("Could not read string len %d\n", len);
		exit(2);
	}
	buf[len] = '\0';
	offset += len;
}

static int	cAllocSym = 0;

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


int
readSyms(void)
{
	int i;
	int count = 0;
	int	firstSym = 0;			/* First symbol this module */
	byte b, c, type;
	word w, len;

	codeLNAMES = NONE;			/* Invalidate indexes for code segment */
	codeSEGDEF = NONE;			/* Else won't be assigned */

    offset = 0;                 /* For diagnostics, really */

	if ((keys = (HASHENTRY *)malloc(SYMALLOC * sizeof(HASHENTRY))) == 0)
	{
		printf("Could not malloc the initial %d entries for keys[]\n");
		exit(10);
	}

	if ((leData = (byte *)malloc(0xFF80)) == 0)
	{
		printf("Could not malloc 64k bytes for LEDATA\n"); 
		exit(10);
	}
#if 0
	switch(_heapchk())
	{
		case _HEAPBADBEGIN:
			printf("Bad begin\n");
			break;
		case _HEAPBADNODE:
			printf("Bad node\n");
			break;
		case _HEAPEMPTY:
			printf("Bad empty\n");
			break;
		case _HEAPOK:
			printf("Heap OK\n");
			break;
	}
#endif

	while (!feof(f))
	{
		type = readByte();
		len = readWord();
/* Note: uncommenting the following generates a *lot* of output */
/*printf("Offset %05lX: type %02X len %d\n", offset-3, type, len);/**/
		switch (type)
		{

			case 0x96:				/* LNAMES */
				while (len > 1)
				{
				 	readString();
					++lnum;
					if (strcmp(buf, "CODE") == 0)
					{
						/* This is the class name we're looking for */
						codeLNAMES= lnum;
					}
					len -= strlen(buf)+1;
				}
				b = readByte();		/* Checksum */
				break;

			case 0x98:				/* Segment definition */
				b = readByte();		/* Segment attributes */
				if ((b & 0xE0) == 0)
				{
					/* Alignment field is zero. Frame and offset follow */
					readWord();
					readByte();
				}

				w = readWord();		/* Segment length */

				b = readByte();		/* Segment name index */
				++segnum;

				b = readByte();		/* Class name index */
				if ((b == codeLNAMES) && (codeSEGDEF == NONE))
				{
					/* This is the segment defining the code class */
					codeSEGDEF = segnum;
				}

				b = readByte();		/* Overlay index */
				b = readByte();		/* Checksum */
				break;

			case 0x90:				/* PUBDEF: public symbols */
				b = readByte();		/* Base group */
				c = readByte();		/* Base segment */
				len -= 2;
				if (c == 0)
				{
					w = readWord();
					len -= 2;
				}
				while (len > 1)
				{
					readString();
					w = readWord();		/* Offset */
					b = readByte();		/* Type index */
					if (c == codeSEGDEF)
					{
						byte *p;

						allocSym(count);
						p = buf;
						if (buf[0] == '_')	/* Leading underscore? */
						{
							p++; 			/* Yes, remove it*/
						}
						i = min(SYMLEN-1, strlen(p));
						memcpy(keys[count].name, p, i);
						keys[count].name[i] = '\0';
						keys[count].offset = w;
/*printf("%04X: %s is sym #%d\n", w, keys[count].name, count);/**/
						count++;
					}
					len -= strlen(buf) + 1 + 2 + 1;
				}
				b = readByte();		/* Checksum */
				break;


			case 0xA0:				/* LEDATA */
			{
				b = readByte();		/* Segment index */
				w = readWord();		/* Offset */
				len -= 3;
/*printf("LEDATA seg %d off %02X len %Xh, looking for %d\n", b, w, len-1, codeSEGDEF);/**/

				if (b != codeSEGDEF)
				{
					readNN(len);	/* Skip the data */
					break;			/* Next record */
				}


				if (fread(&leData[w], 1, len-1, f) != len-1)
				{
					printf("Could not read LEDATA length %d\n", len-1);
					exit(2);
				}
				offset += len-1;
				maxLeData = max(maxLeData, w+len-1);

			 	readByte();				/* Checksum */
				break;
			}

			default:
				readNN(len);			/* Just skip the lot */

				if (type == 0x8A)	/* Mod end */
				{
				/* Now find all the patterns for public code symbols that
					we have found */
					for (i=firstSym; i < count; i++)
					{
						word off = keys[i].offset;
						if (off == (word)-1)
						{
							continue;			/* Ignore if already done */
						}
						if (keys[i].offset > maxLeData)
						{
							printf(
							"Warning: no LEDATA for symbol #%d %s "
							"(offset %04X, max %04X)\n",
							i, keys[i].name, off, maxLeData);
							/* To make things consistant, we set the pattern for
								this symbol to nulls */
							memset(&keys[i].pat, 0, PATLEN);
							continue;
						}
						/* Copy to temp buffer so don't overrun later patterns.
							(e.g. when chopping a short pattern).
							Beware of short patterns! */
						if (off+PATLEN <= maxLeData)
						{
							/* Available pattern is >= PATLEN */
							memcpy(buf, &leData[off], PATLEN);
						}
						else
						{
							/* Short! Only copy what is available (and malloced!) */
							memcpy(buf, &leData[off], maxLeData-off);
							/* Set rest to zeroes */
							memset(&buf[maxLeData-off], 0, PATLEN-(maxLeData-off));
						}
						fixWildCards(buf);
						/* Save into the hash entry. */
						memcpy(keys[i].pat, buf, PATLEN);
						keys[i].offset = (word)-1;	/* Flag it as done */
/*printf("Saved pattern for %s\n", keys[i].name);/**/
					}


					while (readByte() == 0);
					readNN(-1);			/* Unget the last byte (= type) */
					lnum = 0;			/* Reset index into lnames */
					segnum = 0;			/* Reset index into snames */
					firstSym = count;	/* Remember index of first sym this mod */
					codeLNAMES = NONE;	/* Invalidate indexes for code segment */
					codeSEGDEF = NONE;
					memset(leData, 0, maxLeData);	/* Clear out old junk */
					maxLeData = 0;		/* No data read this module */
				}

				else if (type == 0xF1)
				{
					/* Library end record */
					return count;
				}

		}
	}


	free(leData);
	free(keys);

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



