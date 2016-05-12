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

// dcc project CFG related functions

#include "dcc.h"
#include <malloc.h>
#include <string.h>

static PBB rmJMP(PPROC pProc, int marker, PBB pBB);
static void mergeFallThrough(PPROC pProc, PBB pBB);
static void dfsNumbering(PBB pBB, PBB *dfsLast, int *first, int *last);

/*
 createCFG - Create the basic control flow graph

 Splits Icode associated with the procedure into Basic Blocks.
 The links between BBs represent the control flow graph of the procedure.
 A Basic Block is defined to end on one of the following instructions:
 1) Conditional and unconditional jumps
 2) CALL(F)
 3) RET(F)
 4) On the instruction before a join (a flagged TARGET)
 5) Repeated string instructions
 6) End of procedure
*/
PBB createCFG(PPROC pProc)
{
    int i, ip, start;
    BB cfg;
    PBB psBB;
    PBB pBB = &cfg;
    PICODE pIcode = pProc->Icode.icode;

    cfg.next = NULL;
    stats.numBBbef = stats.numBBaft = stats.numEdgesBef = stats.numEdgesAft = 0;

    for (ip = start = 0; ip < pProc->Icode.numIcode; ip++, pIcode++) {
        /* Stick a NOWHERE_NODE on the end if we terminate with anything
           other than a ret, jump or terminate */
        if (ip + 1 == pProc->Icode.numIcode && !(pIcode->ll.flg & TERMINATES) &&
            pIcode->ll.opcode != iJMP && pIcode->ll.opcode != iJMPF &&
            pIcode->ll.opcode != iRET && pIcode->ll.opcode != iRETF)
            newBB(pBB, start, ip, NOWHERE_NODE, 0, pProc);

        // Only process icodes that have valid instructions
        else if ((pIcode->ll.flg & NO_CODE) != NO_CODE) {
            switch (pIcode->ll.opcode) {
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
            case iJCXZ:
                pBB = newBB(pBB, start, ip, TWO_BRANCH, 2, pProc);
            CondJumps:
                start = ip + 1;
                pBB->edges[0].ip = (uint32_t)start;
                // This is for jumps off into nowhere
                if (pIcode->ll.flg & NO_LABEL)
                    pBB->numOutEdges--;
                else
                    pBB->edges[1].ip = pIcode->ll.immed.op;
                break;

            case iLOOP:
            case iLOOPE:
            case iLOOPNE:
                pBB = newBB(pBB, start, ip, LOOP_NODE, 2, pProc);
                goto CondJumps;

            case iJMPF:
            case iJMP:
                if (pIcode->ll.flg & SWITCH) {
                    pBB = newBB(pBB, start, ip, MULTI_BRANCH, pIcode->ll.caseTbl.numEntries,
                                pProc);
                    for (i = 0; i < pIcode->ll.caseTbl.numEntries; i++)
                        pBB->edges[i].ip = pIcode->ll.caseTbl.entries[i];
                    pProc->hasCase = true;
                } else if ((pIcode->ll.flg & (I | NO_LABEL)) == I) {
                    pBB = newBB(pBB, start, ip, ONE_BRANCH, 1, pProc);
                    pBB->edges[0].ip = pIcode->ll.immed.op;
                } else
                    newBB(pBB, start, ip, NOWHERE_NODE, 0, pProc);
                start = ip + 1;
                break;

            case iCALLF:
            case iCALL: {
                PPROC p = pIcode->ll.immed.proc.proc;
                if (p)
                    i = ((p->flg) & TERMINATES) ? 0 : 1;
                else
                    i = 1;
                pBB = newBB(pBB, start, ip, CALL_NODE, i, pProc);
                start = ip + 1;
                if (i)
                    pBB->edges[0].ip = (uint32_t)start;
            } break;

            case iRET:
            case iRETF:
                newBB(pBB, start, ip, RETURN_NODE, 0, pProc);
                start = ip + 1;
                break;

            default:
                // Check for exit to DOS
                if (pIcode->ll.flg & TERMINATES) {
                    pBB = newBB(pBB, start, ip, TERMINATE_NODE, 0, pProc);
                    start = ip + 1;
                }
                // Check for a fall through
                else if (pProc->Icode.icode[ip + 1].ll.flg & (TARGET | CASE)) {
                    pBB = newBB(pBB, start, ip, FALL_NODE, 1, pProc);
                    start = ip + 1;
                    pBB->edges[0].ip = (uint32_t)start;
                }
                break;
            }
        }
    }

    // Convert list of BBs into a graph
    for (pBB = cfg.next; pBB; pBB = pBB->next) {
        for (i = 0; i < pBB->numOutEdges; i++) {
            ip = pBB->edges[i].ip;
            if (ip >= SYNTHESIZED_MIN)
                fatalError(INVALID_SYNTHETIC_BB);
            else {
                for (psBB = cfg.next; psBB; psBB = psBB->next)
                    if (psBB->start == ip) {
                        pBB->edges[i].BBptr = psBB;
                        psBB->numInEdges++;
                        break;
                    }
                if (!psBB)
                    fatalError(NO_BB, ip, pProc->name);
            }
        }
    }
    return cfg.next;
}


// newBB - Allocate new BB and link to end of list
PBB newBB(PBB pBB, int start, int ip, uint8_t nodeType, int numOutEdges, PPROC pproc)
{
    PBB pnewBB = memset(allocStruc(BB), 0, sizeof(BB));

    pnewBB->nodeType = nodeType; // Initialise
    pnewBB->start = start;
    pnewBB->length = ip - start + 1;
    pnewBB->numOutEdges = (uint8_t)numOutEdges;
    pnewBB->immedDom = NO_DOM;
    pnewBB->loopHead = pnewBB->caseHead = pnewBB->caseTail = pnewBB->latchNode =
        pnewBB->loopFollow = NO_NODE;

    if (numOutEdges)
        pnewBB->edges = allocMem(numOutEdges * sizeof(union typeAdr));

    /* Mark the basic block to which the icodes belong to, but only for
       real code basic blocks (ie. not interval bbs) */
    if (start >= 0)
        for (int i = start; i <= ip; i++)
            pproc->Icode.icode[i].inBB = pnewBB;

    while (pBB->next) // Link
        pBB = pBB->next;
    pBB->next = pnewBB;

    if (start != -1) { // Only for code BB's
        stats.numBBbef++;
        stats.numEdgesBef += numOutEdges;
    }
    return pnewBB;
}


// freeCFG - Deallocates a cfg
void freeCFG(PBB cfg)
{
    PBB pBB;

    for (pBB = cfg; pBB; pBB = cfg) {
        if (pBB->inEdges)
            free(pBB->inEdges);
        if (pBB->edges)
            free(pBB->edges);
        cfg = pBB->next;
        free(pBB);
    }
}


// compressCFG - Remove redundancies and add in-edge information
void compressCFG(PPROC pProc)
{
    PBB pBB, pNxt;
    int ip, first = 0, last;

    // First pass over BB list removes redundant jumps of the form (Un)Conditional->Unconditional jump
    for (pBB = pProc->cfg; pBB; pBB = pBB->next)
        if (pBB->numInEdges != 0 && (pBB->nodeType == ONE_BRANCH || pBB->nodeType == TWO_BRANCH))
            for (int i = 0; i < pBB->numOutEdges; i++) {
                ip = pBB->start + pBB->length - 1;
                pNxt = rmJMP(pProc, ip, pBB->edges[i].BBptr);

                if (pBB->numOutEdges) { // Might have been clobbered
                    pBB->edges[i].BBptr = pNxt;
                    pProc->Icode.icode[ip].ll.immed.op = (uint32_t)pNxt->start;
                }
            }

    /* Next is a depth-first traversal merging any FALL_NODE or ONE_BRANCH that fall through
       to a node with that as their only in-edge. */
    mergeFallThrough(pProc, pProc->cfg);

    // Remove redundant BBs created by the above compressions and allocate in-edge arrays as required.
    stats.numEdgesAft = stats.numEdgesBef;
    stats.numBBaft = stats.numBBbef;

    for (pBB = pProc->cfg; pBB; pBB = pNxt) {
        pNxt = pBB->next;
        if (pBB->numInEdges == 0) {
            if (pBB == pProc->cfg) // Init it misses out on
                pBB->index = UN_INIT;
            else {
                if (pBB->numOutEdges)
                    free(pBB->edges);
                free(pBB);
                stats.numBBaft--;
                stats.numEdgesAft--;
            }
        } else {
            pBB->inEdgeCount = pBB->numInEdges;
            pBB->inEdges = allocMem(pBB->numInEdges * sizeof(PBB));
        }
    }

    // Allocate storage for dfsLast[] array
    pProc->numBBs = stats.numBBaft;
    pProc->dfsLast = allocMem(pProc->numBBs * sizeof(PBB));

    // Now do a dfs numbering traversal and fill in the inEdges[] array
    last = pProc->numBBs - 1;
    dfsNumbering(pProc->cfg, pProc->dfsLast, &first, &last);
}


// rmJMP - If BB addressed is just a JMP it is replaced with its target
static PBB rmJMP(PPROC pProc, int marker, PBB pBB)
{
    marker += DFS_JMP;

    while (pBB->nodeType == ONE_BRANCH && pBB->length == 1) {
        if (pBB->traversed != marker) {
            pBB->traversed = marker;
            if (--pBB->numInEdges)
                pBB->edges[0].BBptr->numInEdges++;
            else {
                pProc->Icode.icode[pBB->start].ll.flg |= NO_CODE;
                pProc->Icode.icode[pBB->start].invalid = true;
            }

            pBB = pBB->edges[0].BBptr;
        } else { // We are going around in circles
            pBB->nodeType = NOWHERE_NODE;
            pProc->Icode.icode[pBB->start].ll.immed.op = (uint32_t)pBB->start;
            do {
                pBB = pBB->edges[0].BBptr;
                if (!--pBB->numInEdges) {
                    pProc->Icode.icode[pBB->start].ll.flg |= NO_CODE;
                    pProc->Icode.icode[pBB->start].invalid = true;
                }
            } while (pBB->nodeType != NOWHERE_NODE);

            free(pBB->edges);
            pBB->numOutEdges = 0;
            pBB->edges = NULL;
        }
    }
    return pBB;
}


// mergeFallThrough
static void mergeFallThrough(PPROC pProc, PBB pBB)
{
    PBB pChild;
    int i, ip;

    if (pBB) {
        while (pBB->nodeType == FALL_NODE || pBB->nodeType == ONE_BRANCH) {
            pChild = pBB->edges[0].BBptr;
            /* Jump to next instruction can always be removed */
            if (pBB->nodeType == ONE_BRANCH) {
                ip = pBB->start + pBB->length;
                for (i = ip; i < pChild->start && (pProc->Icode.icode[i].ll.flg & NO_CODE); i++)
                    ;
                if (i != pChild->start)
                    break;
                pProc->Icode.icode[ip - 1].ll.flg |= NO_CODE;
                pProc->Icode.icode[ip - 1].invalid = true;
                pBB->nodeType = FALL_NODE;
                pBB->length--;
            }
            // If there's no other edges into child can merge
            if (pChild->numInEdges != 1)
                break;

            pBB->nodeType = pChild->nodeType;
            pBB->length = pChild->start + pChild->length - pBB->start;
            pProc->Icode.icode[pChild->start].ll.flg &= ~TARGET;
            pBB->numOutEdges = pChild->numOutEdges;
            free(pBB->edges);
            pBB->edges = pChild->edges;

            pChild->numOutEdges = pChild->numInEdges = 0;
            pChild->edges = NULL;
        }
        pBB->traversed = DFS_MERGE;

        // Process all out edges recursively
        for (i = 0; i < pBB->numOutEdges; i++)
            if (pBB->edges[i].BBptr->traversed != DFS_MERGE)
                mergeFallThrough(pProc, pBB->edges[i].BBptr);
    }
}


// dfsNumbering - Numbers nodes during first and last visits and determine in-edges
static void dfsNumbering(PBB pBB, PBB *dfsLast, int *first, int *last)
{
    PBB pChild;

    if (pBB) {
        pBB->traversed = DFS_NUM;
        pBB->dfsFirstNum = (*first)++;

        // index is being used as an index to inEdges[].
        for (int i = 0; i < pBB->numOutEdges; i++) {
            pChild = pBB->edges[i].BBptr;
            pChild->inEdges[pChild->index++] = pBB;

            // Is this the last visit?
            if (pChild->index == pChild->numInEdges)
                pChild->index = UN_INIT;

            if (pChild->traversed != DFS_NUM)
                dfsNumbering(pChild, dfsLast, first, last);
        }
        pBB->dfsLastNum = *last;
        dfsLast[(*last)--] = pBB;
    }
}
