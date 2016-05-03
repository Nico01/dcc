/*
 * Copyright (C) 1991-4, Cristina Cifuentes
 * Copyright (C) 1992-3, Queensland University of Technology
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
 dcc project procedure list builder
 (C) Cristina Cifuentes, Mike van Emmerik, Jeff Ledermann
*/

#include "dcc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// duVal FLAGS
#define DEF 0x0010    // Variable was first defined than used
#define USE 0x0100    // Variable was first used than defined
#define VAL 0x1000    /* Variable has an initial value.  2 cases:
                         1. When variable is used first (ie. global)
                         2. When a value is moved into the variable for the first time. */
#define USEVAL 0x1100 // Use and Val

static void FollowCtrl(PPROC pProc, PCALL_GRAPH pcallGraph, PSTATE pstate);
static bool process_JMP(PICODE pIcode, PPROC pProc, PSTATE pstate, PCALL_GRAPH pcallGraph);
static bool process_CALL(PICODE pIcode, PPROC pProc, PCALL_GRAPH pcallGraph, PSTATE pstate);
static void process_operands(PICODE pIcode, PPROC pProc, PSTATE pstate, int ix);
static void setBits(int16_t type, uint32_t start, uint32_t len);
static PSYM updateGlobSym(uint32_t operand, int size, uint16_t duFlag);
static void process_MOV(PICODE pIcode, PSTATE pstate);
static PSYM lookupAddr(PMEM pm, PSTATE pstate, int size, uint16_t duFlag);
void interactDis(PPROC initProc, int ic);
static uint32_t SynthLab;

// Parses the program, builds the call graph, and returns the list of procedures found
void parse(PCALL_GRAPH *pcallGraph)
{
    STATE state;

    // Set initial state
    memset(&state, 0, sizeof(STATE));
    setState(&state, rES, 0); // PSP segment
    setState(&state, rDS, 0);
    setState(&state, rCS, prog.initCS);
    setState(&state, rSS, prog.initSS);
    setState(&state, rSP, prog.initSP);
    state.IP = (prog.initCS << 4) + prog.initIP;
    SynthLab = SYNTHESIZED_MIN;

    // Check for special settings of initial state, based on idioms of the startup code
    checkStartup(&state);

    // Make a struct for the initial procedure
    pProcList = memset(allocStruc(PROC), 0, sizeof(PROC));

    if (prog.offMain != -1) {
        // We know where main() is. Start the flow of control from there
        pProcList->procEntry = prog.offMain;
        /* In medium and large models, the segment of main may (will?) not be
           the same as the initial CS segment (of the startup code) */
        setState(&state, rCS, prog.segMain);
        strcpy(pProcList->name, "main");
        state.IP = prog.offMain;
    } else // Create initial procedure at program start address
        pProcList->procEntry = state.IP;


    // The state info is for the first procedure
    memcpy(&(pProcList->state), &state, sizeof(STATE));
    pLastProc = pProcList;

    // Set up call graph initial node
    *pcallGraph = memset(allocStruc(CALL_GRAPH), 0, sizeof(CALL_GRAPH));
    (*pcallGraph)->proc = pProcList;

    /* This proc needs to be called to set things up for LibCheck(),
       which checks a proc to see if it is a know C (etc) library */
    bool err = SetupLibCheck();

    // Recursively build entire procedure list
    FollowCtrl(pProcList, *pcallGraph, &state);

    // This proc needs to be called to clean things up from SetupLibCheck()
    if (err)
        CleanupLibCheck();
}

/*
 Updates the type of the symbol in the symbol table.
 The size is updated if necessary (0 means no update necessary).
*/
static void updateSymType(uint32_t symbol, hlType symType, int size)
{
    for (int i = 0; i < symtab.csym; i++)
        if (symtab.sym[i].label == symbol) {
            symtab.sym[i].type = symType;
            if (size != 0)
                symtab.sym[i].size = size;
            break;
        }
}

// Returns the size of the string pointed by sym and delimited by delim. Size includes delimiter.
size_t strSize(uint8_t *sym, char delim)
{
    size_t i;

    for (i = 0; *sym++ != delim; i++);

    return (i + 1);
}

/*
 FollowCtrl - Given an initial procedure, state information and symbol table builds a list
 of procedures reachable from the initial procedure using a depth first search.
*/
static void FollowCtrl(PPROC pProc, PCALL_GRAPH pcallGraph, PSTATE pstate)
{
    ICODE Icode, *pIcode; // This gets copied to pProc->Icode[] later
    ICODE eIcode;         // extra icodes for iDIV, iIDIV, iXCHG
    PSYM psym;
    uint32_t offset;
    int err, lab;
    bool done = false;

    while (!done && !(err = scan(pstate->IP, &Icode))) {
        pstate->IP += Icode.ic.ll.numBytes;
        setBits(BM_CODE, Icode.ic.ll.label, Icode.ic.ll.numBytes);

        process_operands(&Icode, pProc, pstate, pProc->Icode.numIcode);

        // Keep track of interesting instruction flags in procedure
        pProc->flg |= (Icode.ic.ll.flg & (NOT_HLL | FLOAT_OP));

        // Check if this instruction has already been parsed
        if (labelSrch(pProc->Icode.icode, pProc->Icode.numIcode, Icode.ic.ll.label, &lab)) { // Synthetic jump
            Icode.type = LOW_LEVEL;
            Icode.ic.ll.opcode = iJMP;
            Icode.ic.ll.flg = I | SYNTHETIC | NO_OPS;
            Icode.ic.ll.immed.op = pProc->Icode.icode[lab].ic.ll.label;
            Icode.ic.ll.label = SynthLab++;
        }

        // Copy Icode to Proc
        if ((Icode.ic.ll.opcode == iDIV) || (Icode.ic.ll.opcode == iIDIV)) {
            // MOV rTMP, reg
            memset(&eIcode, 0, sizeof(ICODE));
            eIcode.type = LOW_LEVEL;
            eIcode.ic.ll.opcode = iMOV;
            eIcode.ic.ll.dst.regi = rTMP;

            if (Icode.ic.ll.flg & B) {
                eIcode.ic.ll.flg |= B;
                eIcode.ic.ll.src.regi = rAX;
                setRegDU(&eIcode, rAX, USE);
            } else { // implicit dx:ax
                eIcode.ic.ll.flg |= IM_SRC;
                setRegDU(&eIcode, rAX, USE);
                setRegDU(&eIcode, rDX, USE);
            }

            setRegDU(&eIcode, rTMP, DEF);
            eIcode.ic.ll.flg |= SYNTHETIC;
            eIcode.ic.ll.label = Icode.ic.ll.label;
            pIcode = newIcode(&pProc->Icode, &eIcode);

            // iDIV, iIDIV
            pIcode = newIcode(&pProc->Icode, &Icode);

            // iMOD
            memset(&eIcode, 0, sizeof(ICODE));
            eIcode.type = LOW_LEVEL;
            eIcode.ic.ll.opcode = iMOD;
            memcpy(&eIcode.ic.ll.src, &Icode.ic.ll.src, sizeof(ICODEMEM));
            memcpy(&eIcode.du, &Icode.du, sizeof(DU_ICODE));
            eIcode.ic.ll.flg = (Icode.ic.ll.flg | SYNTHETIC);
            eIcode.ic.ll.label = SynthLab++;
            pIcode = newIcode(&pProc->Icode, &eIcode);
        }
        else if (Icode.ic.ll.opcode == iXCHG) { // MOV rTMP, regDst
            memset(&eIcode, 0, sizeof(ICODE));
            eIcode.type = LOW_LEVEL;
            eIcode.ic.ll.opcode = iMOV;
            eIcode.ic.ll.dst.regi = rTMP;
            eIcode.ic.ll.src.regi = Icode.ic.ll.dst.regi;
            setRegDU(&eIcode, rTMP, DEF);
            setRegDU(&eIcode, eIcode.ic.ll.src.regi, USE);
            eIcode.ic.ll.flg |= SYNTHETIC;
            eIcode.ic.ll.label = Icode.ic.ll.label;
            pIcode = newIcode(&pProc->Icode, &eIcode);

            // MOV regDst, regSrc
            Icode.ic.ll.opcode = iMOV;
            Icode.ic.ll.flg |= SYNTHETIC;
            pIcode = newIcode(&pProc->Icode, &Icode);
            Icode.ic.ll.opcode = iXCHG; // for next case

            // MOV regSrc, rTMP
            memset(&eIcode, 0, sizeof(ICODE));
            eIcode.type = LOW_LEVEL;
            eIcode.ic.ll.opcode = iMOV;
            eIcode.ic.ll.dst.regi = Icode.ic.ll.src.regi;
            eIcode.ic.ll.src.regi = rTMP;
            setRegDU(&eIcode, eIcode.ic.ll.dst.regi, DEF);
            setRegDU(&eIcode, rTMP, USE);
            eIcode.ic.ll.flg |= SYNTHETIC;
            eIcode.ic.ll.label = SynthLab++;
            pIcode = newIcode(&pProc->Icode, &eIcode);
        }
        else
            pIcode = newIcode(&pProc->Icode, &Icode);

        switch (Icode.ic.ll.opcode) {
        default:
            break;
        // Conditional jumps
        case iLOOP:
        case iLOOPE:
        case iLOOPNE:
        case iJB:
        case iJBE:
        case iJAE:
        case iJA:
        case iJL:
        case iJLE:
        case iJGE:
        case iJG:
        case iJE:
        case iJNE:
        case iJS:
        case iJNS:
        case iJO:
        case iJNO:
        case iJP:
        case iJNP:
        case iJCXZ: {
            STATE StCopy;
            int ip = pProc->Icode.numIcode - 1; // curr icode idx
            PICODE prev = &pProc->Icode.icode[ip - 1];
            bool fBranch = false;
            pstate->JCond.regi = 0;

            /* This sets up range check for indexed JMPs hopefully
               Handles JA/JAE for fall through and JB/JBE on branch */
            if (ip > 0 && prev->ic.ll.opcode == iCMP && (prev->ic.ll.flg & I)) {
                pstate->JCond.immed = (int16_t)prev->ic.ll.immed.op;
                
                if (Icode.ic.ll.opcode == iJA || Icode.ic.ll.opcode == iJBE)
                    pstate->JCond.immed++;
                if (Icode.ic.ll.opcode == iJAE || Icode.ic.ll.opcode == iJA)
                    pstate->JCond.regi = prev->ic.ll.dst.regi;
                
                fBranch = (Icode.ic.ll.opcode == iJB || Icode.ic.ll.opcode == iJBE);
            }

            memcpy(&StCopy, pstate, sizeof(STATE));

            // Straight line code
            FollowCtrl(pProc, pcallGraph, &StCopy);

            if (fBranch) // Do branching code
                pstate->JCond.regi = prev->ic.ll.dst.regi;
            pIcode = &pProc->Icode.icode[ip]; // next icode
        }                                     // Fall through

        // Jumps
        case iJMP:
        case iJMPF: // Returns TRUE if we've run into a loop
            done = process_JMP(pIcode, pProc, pstate, pcallGraph);
            break;

        // Calls
        case iCALL:
        case iCALLF:
            done = process_CALL(pIcode, pProc, pcallGraph, pstate);
            break;

        // Returns
        case iRET:
        case iRETF:
            pProc->flg |= (Icode.ic.ll.opcode == iRET) ? PROC_NEAR : PROC_FAR;
        // Fall through
        case iIRET:
            pProc->flg &= ~TERMINATES;
            done = true;
            break;

        case iINT:
            if (Icode.ic.ll.immed.op == 0x21 && pstate->f[rAH]) {
                int funcNum = pstate->r[rAH];
                int operand;
                size_t size;

                // Save function number
                pProc->Icode.icode[pProc->Icode.numIcode - 1].ic.ll.dst.off = funcNum;

                // Program termination: int21h, fn 00h, 31h, 4Ch
                done = (funcNum == 0x00 || funcNum == 0x31 || funcNum == 0x4C);

                // String functions: int21h, fn 09h
                if (pstate->f[rDX]) // offset goes into DX
                    if (funcNum == 0x09) {
                        operand = (pstate->r[rDS] << 4) + pstate->r[rDX];
                        //size = prog.fCOM ? strSize(&prog.Image[operand], '$') : strSize(&prog.Image[operand + 0x100], '$');
                        size = strSize(&prog.Image[operand + 0x100], '$');
                        updateSymType(operand, TYPE_STR, size);
                    }
            } else if ((Icode.ic.ll.immed.op == 0x2F) && (pstate->f[rAH]))
                pProc->Icode.icode[pProc->Icode.numIcode - 1].ic.ll.dst.off = pstate->r[rAH];
            else // Program termination: int20h, int27h
                done = (Icode.ic.ll.immed.op == 0x20 || Icode.ic.ll.immed.op == 0x27);
            if (done)
                pIcode->ic.ll.flg |= TERMINATES;
            break;

        case iMOV:
            process_MOV(pIcode, pstate);
            break;

        /* case iXCHG:
            process_MOV (pIcode, pstate);

            break; **** HERE ***/

        case iSHL:
            if (pstate->JCond.regi == Icode.ic.ll.dst.regi) {
                if ((Icode.ic.ll.flg & I) && Icode.ic.ll.immed.op == 1)
                    pstate->JCond.immed *= 2;
                else
                    pstate->JCond.regi = 0;
            }
            break;

        case iLEA:
            if (Icode.ic.ll.src.regi == 0) // direct mem offset
                setState(pstate, Icode.ic.ll.dst.regi, Icode.ic.ll.src.off);
            break;

        case iLDS:
        case iLES:
            if ((psym = lookupAddr(&Icode.ic.ll.src, pstate, 4, USE))) {
                offset = LH(&prog.Image[psym->label]);
                setState(pstate, (Icode.ic.ll.opcode == iLDS) ? rDS : rES,
                         LH(&prog.Image[psym->label + 2]));
                setState(pstate, Icode.ic.ll.dst.regi, (int16_t)offset);
                psym->type = TYPE_PTR;
            }
            break;
        }
    }

    if (err) {
        pProc->flg &= ~TERMINATES;

        if (err == INVALID_386OP || err == INVALID_OPCODE) {
            fatalError(err, prog.Image[Icode.ic.ll.label], Icode.ic.ll.label);
            pProc->flg |= PROC_BADINST;
        } else if (err == IP_OUT_OF_RANGE)
            fatalError(err, Icode.ic.ll.label);
        else
            reportError(err, Icode.ic.ll.label);
    }
}

// process_JMP - Handles JMPs, returns TRUE if we should end recursion
static bool process_JMP(PICODE pIcode, PPROC pProc, PSTATE pstate, PCALL_GRAPH pcallGraph)
{
    static uint8_t i2r[4] = { rSI, rDI, rBP, rBX };
    ICODE Icode;
    uint32_t cs, offTable, endTable;
    uint32_t i, k, target;
    int tmp;

    if (pIcode->ic.ll.flg & I) {
        if (pIcode->ic.ll.opcode == iJMPF)
            setState(pstate, rCS, LH(prog.Image + pIcode->ic.ll.label + 3));
        i = pstate->IP = pIcode->ic.ll.immed.op;

        // Return TRUE if jump target is already parsed
        return labelSrch(pProc->Icode.icode, pProc->Icode.numIcode, i, &tmp);
    }

    /* We've got an indirect JMP - look for switch() stmt.
       idiom of the form JMP  word ptr  word_offset[rBX | rSI | rDI] */
    uint32_t seg = (pIcode->ic.ll.src.seg) ? pIcode->ic.ll.src.seg : rDS;

    // Ensure we have a word offset & valid seg
    if (pIcode->ic.ll.opcode == iJMP && (pIcode->ic.ll.flg & WORD_OFF) && pstate->f[seg] &&
        (pIcode->ic.ll.src.regi == INDEXBASE + 4 ||
         pIcode->ic.ll.src.regi == INDEXBASE + 5 || // Idx reg. BX, SI, DI
         pIcode->ic.ll.src.regi == INDEXBASE + 7)) {

        offTable = (pstate->r[seg] << 4) + pIcode->ic.ll.src.off;

        /* Firstly look for a leading range check of the form:
           CMP {BX | SI | DI}, immed
           JA | JAE | JB | JBE
           This is stored in the current state as if we had just
           followed a JBE branch (i.e. [reg] lies between 0 - immed). */

        if (pstate->JCond.regi == i2r[pIcode->ic.ll.src.regi - (INDEXBASE + 4)])
            endTable = offTable + pstate->JCond.immed;
        else
            endTable = prog.cbImage;

        // Search for first byte flagged after start of table
        for (i = offTable; i <= endTable; i++)
            if (BITMAP(i, BM_CODE | BM_DATA))
                break;
        endTable = i & ~1; // Max. possible table size

        /* Now do some heuristic pruning. Look for ptrs. into the table
           and for addresses that don't appear to point to valid code. */
        cs = pstate->r[rCS] << 4;

        for (i = offTable; i < endTable; i += 2) {
            target = cs + LH(&prog.Image[i]);
            if (target < endTable && target >= offTable)
                endTable = target;
            else if (target >= prog.cbImage)
                endTable = i;
        }

        for (i = offTable; i < endTable; i += 2) {
            target = cs + LH(&prog.Image[i]);
            // Be wary of 00 00 as code - it's probably data
            if (!(prog.Image[target] || prog.Image[target + 1]) || scan(target, &Icode))
                endTable = i;
        }

        /* Now for each entry in the table take a copy of the current state
           and recursively call FollowCtrl(). */
        if (offTable < endTable) {
            STATE StCopy;
            int ip;

            setBits(BM_DATA, offTable, endTable - offTable);

            pIcode->ic.ll.flg |= SWITCH;
            pIcode->ic.ll.caseTbl.numEntries = (endTable - offTable) / 2;
            uint32_t *psw = allocMem(pIcode->ic.ll.caseTbl.numEntries * sizeof(uint32_t));
            pIcode->ic.ll.caseTbl.entries = psw;

            for (i = offTable, k = 0; i < endTable; i += 2) {
                memcpy(&StCopy, pstate, sizeof(STATE));
                StCopy.IP = cs + LH(&prog.Image[i]);
                ip = pProc->Icode.numIcode;

                FollowCtrl(pProc, pcallGraph, &StCopy);

                pProc->Icode.icode[ip].ic.ll.caseTbl.numEntries = k++;
                pProc->Icode.icode[ip].ic.ll.flg |= CASE;
                *psw++ = pProc->Icode.icode[ip].ic.ll.label;
            }
            return true;
        }
    }

    // Can't do anything with this jump

    pProc->flg |= PROC_IJMP;
    pProc->flg &= ~TERMINATES;
    interactDis(pProc, pProc->Icode.numIcode - 1);
    return true;
}

/*
 Process procedure call.
 Note: We assume that CALL's will return unless there is good evidence to the contrary
       - thus we return FALSE unless all paths in the called procedure end in DOS exits.
       This is reasonable since C procedures will always include the epilogue after the
       call anyway and it's to be assumed that if an assembler program contains a CALL
       that the programmer expected it to come back - otherwise surely a JMP would have been used.
*/
static bool process_CALL(PICODE pIcode, PPROC pProc, PCALL_GRAPH pcallGraph, PSTATE pstate)
{
    PPROC p, pPrev;
    int ip = pProc->Icode.numIcode - 1;
    STATE localState; // Local copy of the machine state
    uint32_t off;

    // For Indirect Calls, find the function address
    bool indirect = false;

    if (!(pIcode->ic.ll.flg & I)) {
        // Offset into program image is seg:off of read input
        off = pIcode->ic.ll.dst.off + (pIcode->ic.ll.dst.segValue << 4);

        /* Address of function is given by 4 (CALLF) or 2 (CALL) bytes at
           previous offset into the program image */
        if (pIcode->ic.ll.opcode == iCALLF)
            pIcode->ic.ll.immed.op = LH(&prog.Image[off]) + (LH(&prog.Image[off + 2]) << 4);
        else
            pIcode->ic.ll.immed.op = LH(&prog.Image[off]) + (pProc->state.r[rCS] << 4);
        pIcode->ic.ll.flg |= I;
        indirect = true;
    }

    // Process CALL. Function address is located in pIcode->ic.ll.immed.op
    if (pIcode->ic.ll.flg & I) {
        // Search procedure list for one with appropriate entry point
        for (p = pProcList; p && p->procEntry != pIcode->ic.ll.immed.op; p = p->next)
            pPrev = p;

        // Create a new procedure node and save copy of the state
        if (!p) {
            p = memset(allocStruc(PROC), 0, sizeof(PROC));
            pPrev->next = p;
            p->prev = pPrev;
            p->procEntry = pIcode->ic.ll.immed.op;
            pLastProc = p; // Pointer to last node in the list

            LibCheck(p);

            if (p->flg & PROC_ISLIB) {
                // A library function. No need to do any more to it
                insertCallGraph(pcallGraph, pProc, p);
                pProc->Icode.icode[ip].ic.ll.immed.proc.proc = p;
                return false;
            }

            if (indirect)
                p->flg |= PROC_ICALL;

            if (p->name[0] == '\0') { // Don't overwrite existing name
                sprintf(p->name, "proc_%u", ++prog.cProcs);
            }

            p->depth = pProc->depth + 1;
            p->flg |= TERMINATES;

            // Save machine state in localState, load up IP and CS.
            memcpy(&localState, pstate, sizeof(STATE));
            pstate->IP = pIcode->ic.ll.immed.op;

            if (pIcode->ic.ll.opcode == iCALLF)
                setState(pstate, rCS, LH(prog.Image + pIcode->ic.ll.label + 3));

            memcpy(&(p->state), pstate, sizeof(STATE));

            // Insert new procedure in call graph
            insertCallGraph(pcallGraph, pProc, p);

            // Process new procedure
            FollowCtrl(p, pcallGraph, pstate);

            // Restore segment registers & IP from localState
            pstate->IP = localState.IP;
            setState(pstate, rCS, localState.r[rCS]);
            setState(pstate, rDS, localState.r[rDS]);
            setState(pstate, rES, localState.r[rES]);
            setState(pstate, rSS, localState.r[rSS]);

        } else
            insertCallGraph(pcallGraph, pProc, p);

        pProc->Icode.icode[ip].ic.ll.immed.proc.proc = p; // ^ target proc

        return false;
    }
    return false;
}

// process_MOV - Handles state changes due to simple assignments
static void process_MOV(PICODE pIcode, PSTATE pstate)
{
    PSYM psym, psym2; // Pointer to symbol in global symbol table
    uint8_t dstReg = pIcode->ic.ll.dst.regi;
    uint8_t srcReg = pIcode->ic.ll.src.regi;

    if (dstReg > 0 && dstReg < INDEXBASE) {
        if (pIcode->ic.ll.flg & I)
            setState(pstate, dstReg, (int16_t)pIcode->ic.ll.immed.op);
        else if (srcReg == 0) { // direct memory offset
            psym = lookupAddr(&pIcode->ic.ll.src, pstate, 2, USE);
            if (psym && ((psym->flg & SEG_IMMED) || (psym->duVal & VAL)))
                setState(pstate, dstReg, LH(&prog.Image[psym->label]));
        } else if (srcReg < INDEXBASE && pstate->f[srcReg]) { // reg
            setState(pstate, dstReg, pstate->r[srcReg]);

            // Follow moves of the possible index register
            if (pstate->JCond.regi == srcReg)
                pstate->JCond.regi = dstReg;
        }
    } else if (dstReg == 0) { // direct memory offset
        psym = lookupAddr(&pIcode->ic.ll.dst, pstate, 2, DEF);
        if (psym && !(psym->duVal & VAL)) { // no initial value yet
            if (pIcode->ic.ll.flg & I) {  // immediate
                prog.Image[psym->label] = (uint8_t)pIcode->ic.ll.immed.op;
                prog.Image[psym->label + 1] = (uint8_t)(pIcode->ic.ll.immed.op >> 8);
                psym->duVal |= VAL;
            } else if (srcReg == 0) { // direct mem offset
                psym2 = lookupAddr(&pIcode->ic.ll.src, pstate, 2, USE);
                if (psym2 && ((psym->flg & SEG_IMMED) || (psym->duVal & VAL))) {
                    prog.Image[psym->label] = (uint8_t)prog.Image[psym2->label];
                    prog.Image[psym->label + 1] = (uint8_t)(prog.Image[psym2->label + 1] >> 8);
                    psym->duVal |= VAL;
                }
            } else if (srcReg < INDEXBASE && pstate->f[srcReg]) { // reg
                prog.Image[psym->label] = (uint8_t)pstate->r[srcReg];
                prog.Image[psym->label + 1] = (uint8_t)(pstate->r[srcReg] >> 8);
                psym->duVal |= VAL;
            }
        }
    }
}

// Type of the symbol according to the number of bytes it uses
static hlType cbType[] = {
    TYPE_UNKNOWN,
    TYPE_BYTE_UNSIGN,
    TYPE_WORD_SIGN,
    TYPE_UNKNOWN,
    TYPE_LONG_SIGN,
};

/*
 Creates an entry in the global symbol table (symtab) if the variable is not there yet.
 If it is part of the symtab, the size of the variable is checked and updated if the old size
 was less than the new size (ie. the maximum size is always saved).
*/
static PSYM updateGlobSym(uint32_t operand, int size, uint16_t duFlag)
{
    int i;

    // Check for symbol in symbol table
    for (i = 0; i < symtab.csym; i++)
        if (symtab.sym[i].label == operand) {
            if (symtab.sym[i].size < size)
                symtab.sym[i].size = size;
            break;
        }

    // New symbol, not in symbol table
    if (i == symtab.csym) {
        if (++symtab.csym > symtab.alloc) {
            symtab.alloc += 5;
            symtab.sym = allocVar(symtab.sym, symtab.alloc * sizeof(SYM));
        }

        sprintf(symtab.sym[i].name, "var%05X", operand);
        symtab.sym[i].label = operand;
        symtab.sym[i].size = size;
        symtab.sym[i].flg = 0;
        symtab.sym[i].type = cbType[size];

        if (duFlag == USE) // must already have init value
            symtab.sym[i].duVal = USEVAL;
        else
            symtab.sym[i].duVal = duFlag;
    }

    return (&symtab.sym[i]);
}

/*
 Updates the offset entry to the stack frame table (arguments), and returns a pointer to such entry.
*/
static void updateFrameOff(PSTKFRAME ps, int16_t off, int size, uint16_t duFlag)
{
    int i;

    // Check for symbol in stack frame table
    for (i = 0; i < ps->csym; i++)
        if (ps->sym[i].off == off) {
            if (ps->sym[i].size < size)
                ps->sym[i].size = size;
            break;
        }

    // New symbol, not in table
    if (i == ps->csym) {
        if (++ps->csym > ps->alloc) {
            ps->alloc += 5;
            ps->sym = allocVar(ps->sym, ps->alloc * sizeof(STKSYM));
        }
        sprintf(ps->sym[i].name, "arg%d", i);
        ps->sym[i].off = off;
        ps->sym[i].regOff = 0;
        ps->sym[i].size = size;
        ps->sym[i].type = cbType[size];
        if (duFlag == USE) // must already have init value
            ps->sym[i].duVal = USEVAL;
        else
            ps->sym[i].duVal = duFlag;
        ps->cb += size;
        ps->numArgs++;
    }

    // Save maximum argument offset
    if (ps->maxOff < (off + size))
        ps->maxOff = off + (int16_t)size;
}

/*
 lookupAddr - Looks up a data reference in the symbol table and stores it if necessary.
 Returns a pointer to the symbol in the symbol table, or NULL if it's not a direct memory offset.
*/
static PSYM lookupAddr(PMEM pm, PSTATE pstate, int size, uint16_t duFlag)
{
    int i;
    PSYM psym;
    uint32_t operand;

    if (pm->regi == 0) {    // Global var
        if (pm->segValue) { // there is a value in the seg field
            operand = opAdr(pm->segValue, pm->off);
            psym = updateGlobSym(operand, size, duFlag);

            // Check for out of bounds
            if (psym->label >= prog.cbImage)
                return NULL;
            return psym;
        } else if (pstate->f[pm->seg]) { // new value
            pm->segValue = pstate->r[pm->seg];
            operand = opAdr(pm->segValue, pm->off);
            i = symtab.csym;
            psym = updateGlobSym(operand, size, duFlag);

            // Flag new memory locations that are segment values
            if (symtab.csym > i) {
                if (size == 4)
                    operand += 2; // High word
                for (i = 0; i < prog.cReloc; i++)
                    if (prog.relocTable[i] == operand) {
                        psym->flg = SEG_IMMED;
                        break;
                    }
            }

            // Check for out of bounds
            if (psym->label >= prog.cbImage)
                return NULL;
            return psym;
        }
    }
    return NULL;
}

// setState - Assigns a value to a reg.
void setState(PSTATE pstate, uint16_t reg, int16_t value)
{
    value &= 0xFFFF;
    pstate->r[reg] = value;
    pstate->f[reg] = true;

    switch (reg) {
    case rAX:
    case rCX:
    case rDX:
    case rBX:
        pstate->r[reg + rAL - rAX] = value & 0xFF;
        pstate->f[reg + rAL - rAX] = true;
        pstate->r[reg + rAH - rAX] = (value >> 8) & 0xFF;
        pstate->f[reg + rAH - rAX] = true;
        break;

    case rAL:
    case rCL:
    case rDL:
    case rBL:
        if (pstate->f[reg - rAL + rAH]) {
            pstate->r[reg - rAL + rAX] = (pstate->r[reg - rAL + rAH] << 8) + (value & 0xFF);
            pstate->f[reg - rAL + rAX] = true;
        }
        break;

    case rAH:
    case rCH:
    case rDH:
    case rBH:
        if (pstate->f[reg - rAH + rAL]) {
            pstate->r[reg - rAH + rAX] = pstate->r[reg - rAH + rAL] + ((value & 0xFF) << 8);
            pstate->f[reg - rAH + rAX] = true;
        }
        break;
    }
}

/*
 labelSrchRepl - Searches Icode for instruction with label = target,
 and replaces *pIndex with an icode index
*/
bool labelSrch(PICODE pIcode, int numIp, uint32_t target, int *pIndex)
{
    for (int i = 0; i < numIp; i++) {
        if (pIcode[i].ic.ll.label == target) {
            *pIndex = i;
            return true;
        }
    }
    return false;
}

// setBits - Sets memory bitmap bits for BM_CODE or BM_DATA (additively)
static void setBits(int16_t type, uint32_t start, uint32_t len)
{
    if (start < prog.cbImage) {
        if (start + len > prog.cbImage)
            len = prog.cbImage - start;

        for (uint32_t i = start + len - 1; i >= start; i--)
            prog.map[i >> 2] |= type << ((i & 3) << 1);
    }
}

// DU bit definitions for each reg value - including index registers
uint32_t duReg[] = {
    0x00,
    0x11001, 0x22002, 0x44004, 0x88008, 0x10, 0x20, 0x40, 0x80,         // word regs
    0x100, 0x200, 0x400, 0x800,                                         // seg regs
    0x1000, 0x2000, 0x4000, 0x8000, 0x10000, 0x20000, 0x40000, 0x80000, // byte regs
    0x100000,                                                           // tmp reg
    0x48, 0x88, 0x60, 0xA0, 0x40, 0x80, 0x20, 0x08                      // index regs
};

/*
 Checks which registers where used and updates the du.u flag.
 Places local variables on the local symbol table.

 @d     : SRC or DST icode operand
 @pIcode: ptr to icode instruction
 @pProc : ptr to current procedure structure
 @pstate: ptr to current procedure state
 @size  : size of the operand
 @ix    : current index into icode array
*/
static void use(opLoc d, PICODE pIcode, PPROC pProc, PSTATE pstate, int size, int ix)
{
    PMEM pm = (d == SRC) ? &pIcode->ic.ll.src : &pIcode->ic.ll.dst;
    PSYM psym;

    if (pm->regi == 0 || pm->regi >= INDEXBASE) {
        if (pm->regi == INDEXBASE + 6) { // indexed on bp
            if (pm->off >= 2)
                updateFrameOff(&pProc->args, pm->off, size, USE);
            else if (pm->off < 0)
                newByteWordStkId(&pProc->localId, TYPE_WORD_SIGN, pm->off, 0);
        }
        else if (pm->regi == INDEXBASE + 2 || pm->regi == INDEXBASE + 3) {
            newByteWordStkId(&pProc->localId, TYPE_WORD_SIGN, pm->off,
                             (uint8_t)((pm->regi == INDEXBASE + 2) ? rSI : rDI));
        }
        else if ((pm->regi >= INDEXBASE + 4) && (pm->regi <= INDEXBASE + 7)) {
            if ((pm->seg == rDS) && (pm->regi == INDEXBASE + 7)) { // bx
                if (pm->off > 0) // global indexed variable
                    newIntIdxId(&pProc->localId, pm->segValue, pm->off, rBX, ix, TYPE_WORD_SIGN);
            }
            pIcode->du.use |= duReg[pm->regi];
        }
        else if ((psym = lookupAddr(pm, pstate, size, USE))) {
            setBits(BM_DATA, psym->label, (uint32_t)size);
            pIcode->ic.ll.flg |= SYM_USE;
            pIcode->ic.ll.caseTbl.numEntries = psym - symtab.sym;
        }
    }
    // Use of register
    else if ((d == DST) || ((d == SRC) && (pIcode->ic.ll.flg & I) != I))
        pIcode->du.use |= duReg[pm->regi];
}

/*
 Checks which registers were defined (ie. got a new value) and updates the du.d flag.
 Places local variables in the local symbol table.
*/
static void def(opLoc d, PICODE pIcode, PPROC pProc, PSTATE pstate, int size, int ix)
{
    PMEM pm = (d == SRC) ? &pIcode->ic.ll.src : &pIcode->ic.ll.dst;
    PSYM psym;

    if (pm->regi == 0 || pm->regi >= INDEXBASE) {
        if (pm->regi == INDEXBASE + 6) { // indexed on bp
            if (pm->off >= 2)
                updateFrameOff(&pProc->args, pm->off, size, DEF);
            else if (pm->off < 0)
                newByteWordStkId(&pProc->localId, TYPE_WORD_SIGN, pm->off, 0);
        }
        else if (pm->regi == INDEXBASE + 2 || pm->regi == INDEXBASE + 3) {
            newByteWordStkId(&pProc->localId, TYPE_WORD_SIGN, pm->off,
                             ((pm->regi == INDEXBASE + 2) ? rSI : rDI));
        }
        else if ((pm->regi >= INDEXBASE + 4) && (pm->regi <= INDEXBASE + 7)) {
            if ((pm->seg == rDS) && (pm->regi == INDEXBASE + 7)) { // bx
                if (pm->off > 0) // global var
                    newIntIdxId(&pProc->localId, pm->segValue, pm->off, rBX, ix, TYPE_WORD_SIGN);
            }
            pIcode->du.use |= duReg[pm->regi];
        }
        else if ((psym = lookupAddr(pm, pstate, size, DEF))) {
            setBits(BM_DATA, psym->label, (uint32_t)size);
            pIcode->ic.ll.flg |= SYM_DEF;
            pIcode->ic.ll.caseTbl.numEntries = psym - symtab.sym;
        }
    }
    // Definition of register
    else if ((d == DST) || ((d == SRC) && (pIcode->ic.ll.flg & I) != I)) {
        pIcode->du.def |= duReg[pm->regi];
        pIcode->du1.numRegsDef++;
    }
}

/*
 use_def - operand is both use and def'd.
 Note: the destination will always be a register, stack variable, or global variable.
*/
static void use_def(opLoc d, PICODE pIcode, PPROC pProc, PSTATE pstate, int cb, int ix)
{
    PMEM pm = (d == SRC) ? &pIcode->ic.ll.src : &pIcode->ic.ll.dst;

    use(d, pIcode, pProc, pstate, cb, ix);

    if (pm->regi < INDEXBASE) { // register
        pIcode->du.def |= duReg[pm->regi];
        pIcode->du1.numRegsDef++;
    }
}

// Set DU vector, local variables and arguments, and DATA bits in the bitmap
static void process_operands(PICODE pIcode, PPROC pProc, PSTATE pstate, int ix)
{
    int sseg = (pIcode->ic.ll.src.seg) ? pIcode->ic.ll.src.seg : rDS;
    int cb = (pIcode->ic.ll.flg & B) ? 1 : 2;
    uint32_t Imm = (pIcode->ic.ll.flg & I);

    switch (pIcode->ic.ll.opcode) {
    default:
            break;
    case iAND:
    case iOR:
    case iXOR:
    case iSAR:
    case iSHL:
    case iSHR:
    case iRCL:
    case iRCR:
    case iROL:
    case iROR:
    case iADD:
    case iADC:
    case iSUB:
    case iSBB:
        if (!Imm) {
            use(SRC, pIcode, pProc, pstate, cb, ix);
        }
    case iINC:
    case iDEC:
    case iNEG:
    case iNOT:
    case iAAA:
    case iAAD:
    case iAAM:
    case iAAS:
    case iDAA:
    case iDAS:
        use_def(DST, pIcode, pProc, pstate, cb, ix);
        break;

    case iXCHG:
        /* This instruction is replaced by 3 instructions, only need to define
           the src operand and use the destination operand in the mean time. */
        use(SRC, pIcode, pProc, pstate, cb, ix);
        def(DST, pIcode, pProc, pstate, cb, ix);
        break;

    case iTEST:
    case iCMP:
        if (!Imm)
            use(SRC, pIcode, pProc, pstate, cb, ix);
        use(DST, pIcode, pProc, pstate, cb, ix);
        break;

    case iDIV:
    case iIDIV:
        use(SRC, pIcode, pProc, pstate, cb, ix);
        if (cb == 1)
            pIcode->du.use |= duReg[rTMP];
        break;

    case iMUL:
    case iIMUL:
        use(SRC, pIcode, pProc, pstate, cb, ix);
        if (!Imm) {
            use(DST, pIcode, pProc, pstate, cb, ix);
            if (cb == 1) {
                pIcode->du.def |= duReg[rAX];
                pIcode->du1.numRegsDef++;
            } else {
                pIcode->du.def |= (duReg[rAX] | duReg[rDX]);
                pIcode->du1.numRegsDef += 2;
            }
        } else
            def(DST, pIcode, pProc, pstate, cb, ix);
        break;

    case iSIGNEX:
        cb = (pIcode->ic.ll.flg & SRC_B) ? 1 : 2;
        if (cb == 1) { // byte
            pIcode->du.def |= duReg[rAX];
            pIcode->du1.numRegsDef++;
            pIcode->du.use |= duReg[rAL];
        } else { // word
            pIcode->du.def |= (duReg[rDX] | duReg[rAX]);
            pIcode->du1.numRegsDef += 2;
            pIcode->du.use |= duReg[rAX];
        }
        break;

    case iCALLF: // Ignore def's on CS for now
        cb = 4;
    case iCALL:
    case iPUSH:
    case iPOP:
        if (!Imm) {
            if (pIcode->ic.ll.opcode == iPOP)
                def(DST, pIcode, pProc, pstate, cb, ix);
            else
                use(DST, pIcode, pProc, pstate, cb, ix);
        }
        break;

    case iESC: // operands may be larger
        use(DST, pIcode, pProc, pstate, cb, ix);
        break;

    case iLDS:
    case iLES:
        pIcode->du.def |= duReg[(pIcode->ic.ll.opcode == iLDS) ? rDS : rES];
        pIcode->du1.numRegsDef++;
        cb = 4;
    case iMOV:
        use(SRC, pIcode, pProc, pstate, cb, ix);
        def(DST, pIcode, pProc, pstate, cb, ix);
        break;

    case iLEA:
        use(SRC, pIcode, pProc, pstate, 2, ix);
        def(DST, pIcode, pProc, pstate, 2, ix);
        break;

    case iBOUND:
        use(SRC, pIcode, pProc, pstate, 4, ix);
        use(DST, pIcode, pProc, pstate, cb, ix);
        break;

    case iJMPF:
        cb = 4;
    case iJMP:
        if (!Imm)
            use(SRC, pIcode, pProc, pstate, cb, ix);
        break;

    case iLOOP:
    case iLOOPE:
    case iLOOPNE:
        pIcode->du.def |= duReg[rCX];
        pIcode->du1.numRegsDef++;
    case iJCXZ:
        pIcode->du.use |= duReg[rCX];
        break;

    case iREPNE_CMPS:
    case iREPE_CMPS:
    case iREP_MOVS:
        pIcode->du.def |= duReg[rCX];
        pIcode->du1.numRegsDef++;
        pIcode->du.use |= duReg[rCX];
    case iCMPS:
    case iMOVS:
        pIcode->du.def |= duReg[rSI] | duReg[rDI];
        pIcode->du1.numRegsDef += 2;
        pIcode->du.use |= duReg[rSI] | duReg[rDI] | duReg[rES] | duReg[sseg];
        break;

    case iREPNE_SCAS:
    case iREPE_SCAS:
    case iREP_STOS:
    case iREP_INS:
        pIcode->du.def |= duReg[rCX];
        pIcode->du1.numRegsDef++;
        pIcode->du.use |= duReg[rCX];
    case iSCAS:
    case iSTOS:
    case iINS:
        pIcode->du.def |= duReg[rDI];
        pIcode->du1.numRegsDef++;
        if (pIcode->ic.ll.opcode == iREP_INS || pIcode->ic.ll.opcode == iINS) {
            pIcode->du.use |= duReg[rDI] | duReg[rES] | duReg[rDX];
        } else {
            pIcode->du.use |= duReg[rDI] | duReg[rES] | duReg[(cb == 2) ? rAX : rAL];
        }
        break;

    case iREP_LODS:
        pIcode->du.def |= duReg[rCX];
        pIcode->du1.numRegsDef++;
        pIcode->du.use |= duReg[rCX];
    case iLODS:
        pIcode->du.def |= duReg[rSI] | duReg[(cb == 2) ? rAX : rAL];
        pIcode->du1.numRegsDef += 2;
        pIcode->du.use |= duReg[rSI] | duReg[sseg];
        break;

    case iREP_OUTS:
        pIcode->du.def |= duReg[rCX];
        pIcode->du1.numRegsDef++;
        pIcode->du.use |= duReg[rCX];
    case iOUTS:
        pIcode->du.def |= duReg[rSI];
        pIcode->du1.numRegsDef++;
        pIcode->du.use |= duReg[rSI] | duReg[rDX] | duReg[sseg];
        break;

    case iIN:
    case iOUT:
        def(DST, pIcode, pProc, pstate, cb, ix);
        if (!Imm) {
            pIcode->du.use |= duReg[rDX];
        }
        break;
    }

    for (int i = rSP; i <= rBH; i++) // Kill all defined registers
        if (pIcode->ic.ll.flagDU.d & (1 << i))
            pstate->f[i] = false;
}
