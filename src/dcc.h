#ifndef DCC_H
#define DCC_H

/*
 * Copyright (C) 1991-4, Cristina Cifuentes
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

/****************************************************************************
 *          dcc project general header
 * (C) Cristina Cifuentes, Mike van Emmerik
 ****************************************************************************/

#include <stdint.h>
#include <stdbool.h>

// Common definitions and macros
typedef int Int;              /* Int: 0x80000000..0x7FFFFFFF  */
typedef unsigned int flags32; /* 32 bits  */
typedef unsigned int dword;   /* 32 bits  */
#define MAX 0x7FFFFFFF

// Type definitions used in the program */ //TODO use stdint everywhere
typedef unsigned char byte;  /* 8 bits   */
typedef unsigned short word; /* 16 bits  */
typedef short int16;         /* 16 bits  */
typedef unsigned char boolT; /* 8 bits   */

#ifndef TRUE /* X curses defines these already */
#define TRUE 1
#define FALSE 0
#endif

#define SYNTHESIZED_MIN 0x100000 // Synthesized labs use bits 21..32

// These are for C library signature detection
#define SYMLEN 16 // Length of proc symbols, incl null
#define PATLEN 23 // Length of proc patterns
#define WILD 0xF4 // The wild byte


// MACROS

// Macro to allocate a node of size sizeof(structType)
#define allocStruc(structType) (structType *)allocMem(sizeof(structType))

/* Macro reads a LH word from the image regardless of host convention
   Returns a 16 bit quantity, e.g. C000 is read into an Int as C000 */
#define LH(p)  ((int16_t)((uint8_t *)(p))[0] + ((int16_t)((uint8_t *)(p))[1] << 8))
/* Macro reads a LH word from the image regardless of host convention
   Returns a signed quantity, e.g. C000 is read into an Int as FFFFC000 */
#define LHS(p) (((uint8_t *)(p))[0] + (((char *)(p))[1] << 8))

// Macro tests bit b for type t in prog.map
#define BITMAP(b, t) (prog.map[(b) >> 2] & ((t) << (((b)&3) << 1)))

// Macro to convert a segment, offset definition into a 20 bit address
#define opAdr(seg, off) ((seg << 4) + off)

#include "ast.h"
#include "bundle.h"
#include "error.h"
#include "graph.h"
#include "icode.h"
#include "locident.h"

// STATE TABLE
typedef struct {
    uint32_t IP;          // Offset into Image
    int16_t r[INDEXBASE]; // Value of segs and AX
    uint8_t f[INDEXBASE]; // True if r[.] has a value
    struct {              // For case stmt indexed reg
        uint8_t regi;     // Last conditional jump
        int16_t immed;    // Contents of the previous register
    } JCond;
} STATE;
typedef STATE *PSTATE;

// SYMBOL TABLE
typedef struct {
    char name[10];  // New name for this variable
    uint32_t label; // physical address (20 bit)
    int32_t size;   // maximum size
    flags32 flg;    // SEG_IMMED, IMPURE, WORD_OFF
    hlType type;    // probable type
    uint16_t duVal; // DEF, USE, VAL
} SYM;
typedef SYM *PSYM;

typedef struct {
    int32_t csym;  // No. of symbols in table
    int32_t alloc; // Allocation
    PSYM sym;      // Symbols
} SYMTAB;
typedef SYMTAB *PSYMTAB;

// STACK FRAME
typedef struct {
    COND_EXPR *actual; // Expression tree of actual parameter
    COND_EXPR *regs;   // For register arguments only
    int16_t off;       // Immediate off from BP (+:args, -:params)
    uint8_t regOff;    // Offset is a register (e.g. SI, DI)
    int32_t size;      // Size
    hlType type;       // Probable type
    uint16_t duVal;    // DEF, USE, VAL
    bool hasMacro;     // This type needs a macro
    char macro[10];    // Macro name
    char name[10];     // Name for this symbol/argument
    bool invalid;      // Boolean: invalid entry in formal arg list
} STKSYM;
typedef STKSYM *PSTKSYM;

typedef struct _STKFRAME {
    int32_t csym;    // No. of symbols in table
    int32_t alloc;   // Allocation
    PSTKSYM sym;     // Symbols
    int16_t minOff;  // Initial offset in stack frame
    int16_t maxOff;  // Maximum offset in stack frame
    int32_t cb;      // Number of bytes in arguments
    int32_t numArgs; // No. of arguments in the table
} STKFRAME;
typedef STKFRAME *PSTKFRAME;

// PROCEDURE NODE
typedef struct _proc {
    uint32_t procEntry; // label number
    char name[SYMLEN];  // Meaningful name for this proc
    STATE state;        // Entry state
    int32_t depth;      // Depth at which we found it - for printing
    flags32 flg;        // Combination of Icode & Proc flags
    int16_t cbParam;    // Probable no. of bytes of parameters
    STKFRAME args;      // Array of arguments
    LOCAL_ID localId;   // Local identifiers
    ID retVal;          // Return value - identifier

    // Icodes and control flow graph
    ICODE_REC Icode; // Record of ICODE records
    PBB cfg;         // Ptr. to BB list/CFG
    PBB *dfsLast;    // Array of pointers to BBs in dfsLast (reverse postorder) order
    int32_t numBBs;  // Number of BBs in the graph cfg
    bool hasCase;    // Procedure has a case node

    // For interprocedural live analysis
    uint32_t liveIn;  // Registers used before defined
    uint32_t liveOut; // Registers that may be used in successors
    bool liveAnal;    // Procedure has been analysed already

    // Double-linked list
    struct _proc *next;
    struct _proc *prev;
} PROC;
typedef PROC *PPROC;

// CALL GRAPH NODE
typedef struct _callGraph {
    PPROC proc;                   // Pointer to procedure in pProcList
    int32_t numOutEdges;          // # of out edges (ie. # procs invoked)
    int32_t numAlloc;             // # of out edges allocated
    struct _callGraph **outEdges; // array of out edges
} CALL_GRAPH;
typedef CALL_GRAPH *PCALL_GRAPH;

#define NUM_PROCS_DELTA 5 // delta # procs a proc invokes

extern PPROC pProcList;       // Pointer to the head of the procedure list
extern PPROC pLastProc;       // Pointer to last node of the proc list
extern PCALL_GRAPH callGraph; // Pointer to the head of the call graph
extern bundle cCode;          // Output C procedure's declaration and code

// Procedure FLAGS
#define PROC_BADINST 0x000100  // Proc contains invalid or 386 instruction
#define PROC_IJMP 0x000200     // Proc incomplete due to indirect jmp
#define PROC_ICALL 0x000400    // Proc incomplete due to indirect call
#define PROC_HLL 0x001000      // Proc is likely to be from a HLL
#define CALL_PASCAL 0x002000   // Proc uses Pascal calling convention
#define CALL_C 0x004000        // Proc uses C calling convention
#define CALL_UNKNOWN 0x008000  // Proc uses unknown calling convention
#define PROC_NEAR 0x010000     // Proc exits with near return
#define PROC_FAR 0x020000      // Proc exits with far return
#define GRAPH_IRRED 0x100000   // Proc generates an irreducible graph
#define SI_REGVAR 0x200000     // SI is used as a stack variable
#define DI_REGVAR 0x400000     // DI is used as a stack variable
#define PROC_IS_FUNC 0x800000  // Proc is a function
#define REG_ARGS 0x1000000     // Proc has registers as arguments
#define PROC_VARARG 0x2000000  // Proc has variable arguments
#define PROC_OUTPUT 0x4000000  // C for this proc has been output
#define PROC_RUNTIME 0x8000000 // Proc is part of the runtime support
#define PROC_ISLIB 0x10000000  // Proc is a library function
#define PROC_ASM 0x20000000    // Proc is an intrinsic assembler routine
#define PROC_IS_HLL 0x40000000 // Proc has HLL prolog code
#define CALL_MASK 0xFFFF9FFF   // Masks off CALL_C and CALL_PASCAL

// duVal FLAGS
#define DEF 0x0010 // Variable was first defined than used
#define USE 0x0100 // Variable was first used than defined
#define VAL 0x1000 /* Variable has an initial value. 2 cases:
                    - 1. When variable is used first (ie. global)
                    - 2. When a value is moved into the variable for the first time. */
#define USEVAL 0x1100 // Use and Val


// Global variables

extern char *progname;              // Saved argv[0] for error messages
extern char *asm1_name, *asm2_name; // Assembler output filenames

// Command line option flags
typedef struct {
    bool verbose;
    bool VeryVerbose;
    bool asm1;     // Early disassembly listing
    bool asm2;     // Disassembly listing after restruct
    bool Map;
    bool Stats;
    bool Interact; // Interactive mode
} OPTION;

extern OPTION option; // Command line options
extern SYMTAB symtab; // Global symbol table

// Loaded program image parameters
typedef struct {
    int16_t  initCS;
    int16_t  initIP;      // These are initial load values
    int16_t  initSS;      // Probably not of great interest
    int16_t  initSP;
    bool     fCOM;        // Flag set if COM program (else EXE)
    int32_t  cReloc;      // No. of relocation table entries
    uint32_t *relocTable; // Ptr. to relocation table
    uint8_t  *map;        // Memory bitmap ptr
    int32_t  cProcs;      // Number of procedures so far
    int32_t  offMain;     // The offset  of the main() proc
    int16_t  segMain;     // The segment of the main() proc
    int32_t  cbImage;     // Length of image in bytes
    uint8_t  *Image;      // Allocated by loader to hold entire program image
                        
} PROG;

extern PROG prog;           // Loaded program image parameters
extern char condExp[200];   // Conditional expression buffer
extern char callBuf[100];   // Function call buffer
extern dword duReg[30];     // def/use bits for registers
extern dword maskDuReg[30]; // masks off du bits for regs


// Registers used by icode instructions
static char *allRegs[21] = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di", "es", "cs", "ss",
                             "ds", "al", "cl", "dl", "bl", "ah", "ch", "dh", "bh", "tmp" };

// Memory map states
#define BM_UNKNOWN 0 // Unscanned memory
#define BM_DATA 1    // Data
#define BM_CODE 2    // Code
#define BM_IMPURE 3  // Used as Data and Code

// Graph statistics
typedef struct {
    int32_t numBBbef;    // # BBs before deleting redundant ones
    int32_t numBBaft;    // # BBs after deleting redundant ones
    int32_t numEdgesBef; // # out edges before removing redundancy
    int32_t numEdgesAft; // # out edges after removing redundancy
    int32_t nOrder;      // nth order graph, value for n
} STATS;

extern STATS stats; // cfg statistics


// Global function prototypes

void FrontEnd(char *filename, PCALL_GRAPH *);              // frontend.c
void *allocMem(int cb);                                    // frontend.c
void *allocVar(void *p, int newsize);                      // frontend.c
void fixWildCards(uint8_t pat[]);                          // fixwild.c
void udm(void);                                            // udm.c
PBB createCFG(PPROC pProc);                                // graph.c
void compressCFG(PPROC pProc);                             // graph.c
void freeCFG(PBB cfg);                                     // graph.c
PBB newBB(PBB, int, int, uint8_t, int, PPROC);             // graph.c
void BackEnd(char *filename, PCALL_GRAPH);                 // backend.c
char *cChar(uint8_t c);                                    // backend.c
int scan(dword ip, PICODE p);                              // scanner.c
void parse(PCALL_GRAPH *);                                 // parser.c
bool labelSrch(PICODE pIc, int n, uint32_t tg, int *pIdx); // parser.c
void setState(PSTATE state, uint16_t reg, int16_t value);  // parser.c
int strSize(uint8_t *, char);                              // parser.c
void disassem(int pass, PPROC pProc);                      // disassem.c
void interactDis(PPROC initProc, int initIC);              // disassem.c
void bindIcodeOff(PPROC);                                  // idioms.c
void lowLevelAnalysis(PPROC pProc);                        // idioms.c
void propLong(PPROC pproc);                                // proplong.c
bool JmpInst(llIcode opcode);                              // idioms.c
void checkReducibility(PPROC pProc, derSeq **derG);        // reducible.c
queue *appendQueue(queue **Q, BB *node);                   // reducible.c
void freeDerivedSeq(derSeq *derivedG);                     // reducible.c
void displayDerivedSeq(derSeq *derG);                      // reducible.c
void structure(PPROC pProc, derSeq *derG);                 // control.c
void compoundCond(PPROC);                                  // control.c
void dataFlow(PPROC pProc, uint32_t liveOut);              // dataflow.c
void writeIntComment(PICODE icode, char *s);               // comwrite.c
void writeProcComments(PPROC pProc, strTable *sTab);       // comwrite.c
void checkStartup(PSTATE pState);                          // chklib.c
void SetupLibCheck(void);                                  // chklib.c
void CleanupLibCheck(void);                                // chklib.c
bool LibCheck(PPROC p);                                    // chklib.c

// Exported functions from procs.c
bool insertCallGraph(PCALL_GRAPH, PPROC, PPROC);
void writeCallGraph(PCALL_GRAPH);
void newRegArg(PPROC, PICODE, PICODE);
bool newStkArg(PICODE, COND_EXPR *, llIcode, PPROC);
void allocStkArgs(PICODE, int);
void placeStkArg(PICODE, COND_EXPR *, int);
void adjustActArgType(COND_EXPR *, hlType, PPROC);
void adjustForArgType(PSTKFRAME, int, hlType);

// Exported functions from ast.c
COND_EXPR *boolCondExp(COND_EXPR *lhs, COND_EXPR *rhs, condOp op);
COND_EXPR *unaryCondExp(condNodeType, COND_EXPR *exp);
COND_EXPR *idCondExpGlob(int16_t segValue, int16_t off);
COND_EXPR *idCondExpReg(uint8_t regi, flags32 flg, LOCAL_ID *);
COND_EXPR *idCondExpRegIdx(int idx, regType);
COND_EXPR *idCondExpLoc(int off, LOCAL_ID *);
COND_EXPR *idCondExpParam(int off, PSTKFRAME argSymtab);
COND_EXPR *idCondExpKte(uint32_t kte, uint8_t);
COND_EXPR *idCondExpLong(LOCAL_ID *, opLoc, PICODE, hlFirst, int idx, operDu, int);
COND_EXPR *idCondExpLongIdx(int);
COND_EXPR *idCondExpFunc(PPROC, PSTKFRAME);
COND_EXPR *idCondExpOther(uint8_t seg, uint8_t regi, int16_t off);
COND_EXPR *idCondExpID(ID *, LOCAL_ID *, int);
COND_EXPR *idCondExp(PICODE, opLoc, PPROC, int i, PICODE duIcode, operDu);
COND_EXPR *copyCondExp(COND_EXPR *);
void removeRegFromLong(uint8_t, LOCAL_ID *, COND_EXPR *);
char *walkCondExpr(COND_EXPR *exp, PPROC pProc, int *);
condId idType(PICODE pIcode, opLoc sd);
int hlTypeSize(COND_EXPR *, PPROC);
hlType expType(COND_EXPR *, PPROC);
void setRegDU(PICODE, uint8_t regi, operDu);
void copyDU(PICODE, PICODE, operDu, operDu);
void changeBoolCondExpOp(COND_EXPR *, condOp);
bool insertSubTreeReg(COND_EXPR *, COND_EXPR **, uint8_t, LOCAL_ID *);
bool insertSubTreeLongReg(COND_EXPR *, COND_EXPR **, int);
void freeCondExpr(COND_EXPR *exp);
COND_EXPR *concatExps(SEQ_COND_EXPR *, COND_EXPR *, condNodeType);
void initExpStk();
void pushExpStk(COND_EXPR *);
COND_EXPR *popExpStk();
int numElemExpStk();
bool emptyExpStk();

// Exported functions from hlicode.c */
PICODE newIcode(ICODE_REC *, PICODE);
void newAsgnHlIcode(PICODE, COND_EXPR *, COND_EXPR *);
void newCallHlIcode(PICODE);
void newUnaryHlIcode(PICODE, hlIcode, COND_EXPR *);
void newJCondHlIcode(PICODE, COND_EXPR *);
void invalidateIcode(PICODE);
boolT removeDefRegi(uint8_t, PICODE, int, LOCAL_ID *);
void highLevelGen(PPROC);
char *writeCall(PPROC, PSTKFRAME, PPROC, int *);
char *write1HlIcode(struct _hl, PPROC, int *);
char *writeJcond(struct _hl, PPROC, int *);
char *writeJcondInv(struct _hl, PPROC, int *);
int power2(int);
void writeDU(PICODE, int);
void inverseCondOp(COND_EXPR **);

// Exported funcions from locident.c
int newByteWordRegId(LOCAL_ID *, hlType t, uint8_t regi);
int newByteWordStkId(LOCAL_ID *, hlType t, int off, uint8_t regOff);
int newIntIdxId(LOCAL_ID *, int16_t seg, int16_t off, uint8_t regi, int, hlType);
int newLongRegId(LOCAL_ID *, hlType t, uint8_t regH, uint8_t regL, int idx);
int newLongStkId(LOCAL_ID *, hlType t, int offH, int offL);
int newLongId(LOCAL_ID *, opLoc sd, PICODE, hlFirst, int idx, operDu, int);
bool checkLongEq(struct _longStkId, PICODE, int, int, PPROC, COND_EXPR **, COND_EXPR **, int);
bool checkLongRegEq(struct _longId, PICODE, int, int, PPROC, COND_EXPR **, COND_EXPR **, int);
uint8_t otherLongRegi(uint8_t, int, LOCAL_ID *);
void insertIdx(IDX_ARRAY *, int);
void propLongId(LOCAL_ID *, uint8_t, uint8_t, char *);

#endif // DCC_H

