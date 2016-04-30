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

// Purpose: Data flow analysis module.

#include "dcc.h"
#include <stdio.h>
#include <string.h>


// Returns a string with the source operand of Icode
static COND_EXPR *srcIdent(PICODE Icode, PPROC pProc, int i, PICODE duIcode, operDu du)
{
    COND_EXPR *n;

    if (Icode->ic.ll.flg & I) { // immediate operand
        if (Icode->ic.ll.flg & B)
            n = idCondExpKte(Icode->ic.ll.immed.op, 1);
        else
            n = idCondExpKte(Icode->ic.ll.immed.op, 2);
    } else
        n = idCondExp(Icode, SRC, pProc, i, duIcode, du);

    return n;
}

// Returns the destination operand 
static COND_EXPR *dstIdent(PICODE pIcode, PPROC pProc, int i, PICODE duIcode, operDu du)
{
    return idCondExp(pIcode, DST, pProc, i, duIcode, du);
}

// Eliminates all condition codes and generates new hlIcode instructions
static void elimCondCodes(PPROC pProc)
{
    int useAt;      // Index to instruction that used flag
    int defAt;      // Index to instruction that defined flag
    uint8_t use;    // Used flags bit vector
    uint8_t def;    // Defined flags bit vector
    bool notSup;    // Use/def combination not supported
    COND_EXPR *rhs; // Source operand
    COND_EXPR *lhs; // Destination operand
    COND_EXPR *exp; // Boolean expression
    PBB pBB;        // Pointer to BBs in dfs last ordering
    ICODE *prev;    // For extended basic blocks - previous icode inst

    for (int i = 0; i < pProc->numBBs; i++) {
        pBB = pProc->dfsLast[i];

        if (pBB->flg & INVALID_BB) // Do not process invalid BBs
            continue;

        for (useAt = pBB->start + pBB->length; useAt != pBB->start; useAt--)
            if ((pProc->Icode.icode[useAt - 1].type == LOW_LEVEL) &&
                (pProc->Icode.icode[useAt - 1].invalid == false) &&
                (use = pProc->Icode.icode[useAt - 1].ic.ll.flagDU.u)) {
                // Find definition within the same basic block
                for (defAt = useAt - 1; defAt != pBB->start; defAt--) {
                    def = pProc->Icode.icode[defAt - 1].ic.ll.flagDU.d;
                    if ((use & def) == use) {
                        notSup = false;
                        if ((pProc->Icode.icode[useAt - 1].ic.ll.opcode >= iJB) &&
                            (pProc->Icode.icode[useAt - 1].ic.ll.opcode <= iJNS)) {
                            switch (pProc->Icode.icode[defAt - 1].ic.ll.opcode) {
                            case iCMP:
                                rhs = srcIdent(&pProc->Icode.icode[defAt - 1], pProc, defAt - 1,
                                               &pProc->Icode.icode[useAt - 1], USE);
                                lhs = dstIdent(&pProc->Icode.icode[defAt - 1], pProc, defAt - 1,
                                               &pProc->Icode.icode[useAt - 1], USE);
                                break;

                            case iOR:
                                lhs =
                                    copyCondExp(pProc->Icode.icode[defAt - 1].ic.hl.oper.asgn.lhs);
                                copyDU(&pProc->Icode.icode[useAt - 1],
                                       &pProc->Icode.icode[defAt - 1], USE, DEF);
                                if (pProc->Icode.icode[defAt - 1].ic.ll.flg & B)
                                    rhs = idCondExpKte(0, 1);
                                else
                                    rhs = idCondExpKte(0, 2);
                                break;

                            case iTEST:
                                rhs = srcIdent(&pProc->Icode.icode[defAt - 1], pProc, defAt - 1,
                                               &pProc->Icode.icode[useAt - 1], USE);
                                lhs = dstIdent(&pProc->Icode.icode[defAt - 1], pProc, defAt - 1,
                                               &pProc->Icode.icode[useAt - 1], USE);
                                lhs = boolCondExp(lhs, rhs, AND);
                                if (pProc->Icode.icode[defAt - 1].ic.ll.flg & B)
                                    rhs = idCondExpKte(0, 1);
                                else
                                    rhs = idCondExpKte(0, 2);
                                break;

                            default:
                                notSup = true;
                                reportError(JX_NOT_DEF, pProc->Icode.icode[defAt - 1].ic.ll.opcode);
                                pProc->flg |= PROC_ASM; // generate asm
                            }
                            if (!notSup) {
                                exp = boolCondExp(
                                    lhs, rhs,
                                    condOpJCond[pProc->Icode.icode[useAt - 1].ic.ll.opcode - iJB]);
                                newJCondHlIcode(&pProc->Icode.icode[useAt - 1], exp);
                            }
                        }

                        else if (pProc->Icode.icode[useAt - 1].ic.ll.opcode == iJCXZ) {
                            lhs = idCondExpReg(rCX, 0, &pProc->localId);
                            setRegDU(&pProc->Icode.icode[useAt - 1], rCX, USE);
                            rhs = idCondExpKte(0, 2);
                            exp = boolCondExp(lhs, rhs, EQUAL);
                            newJCondHlIcode(&pProc->Icode.icode[useAt - 1], exp);
                        }

                        else {
                            reportError(NOT_DEF_USE, pProc->Icode.icode[defAt - 1].ic.ll.opcode,
                                        pProc->Icode.icode[useAt - 1].ic.ll.opcode);
                            pProc->flg |= PROC_ASM; // generate asm
                        }
                        break;
                    }
                }

                // Check for extended basic block
                if ((pBB->length == 1) && (pProc->Icode.icode[useAt - 1].ic.ll.opcode >= iJB) &&
                    (pProc->Icode.icode[useAt - 1].ic.ll.opcode <= iJNS)) {
                    prev = &pProc->Icode.icode[pBB->inEdges[0]->start + pBB->inEdges[0]->length - 1];
                    if (prev->ic.hl.opcode == JCOND) {
                        exp = copyCondExp(prev->ic.hl.oper.exp);
                        changeBoolCondExpOp(
                            exp, condOpJCond[pProc->Icode.icode[useAt - 1].ic.ll.opcode - iJB]);
                        copyDU(&pProc->Icode.icode[useAt - 1], prev, USE, USE);
                        newJCondHlIcode(&pProc->Icode.icode[useAt - 1], exp);
                    }
                }
                // Error - definition not found for use of a cond code
                else if (defAt == pBB->start)
                    fatalError(DEF_NOT_FOUND, pProc->Icode.icode[useAt - 1].ic.ll.opcode);
            }
    }
}

/*
 Generates the LiveUse() and Def() sets for each basic block in the graph.
 Note: these sets are constant and could have been constructed during the construction of the graph,
       but since the code hasn't been analyzed yet for idioms, the procedure preamble misleads the
       analysis (eg: push si, would include si in LiveUse; although it is not really meant to be a
       register that is used before defined).
*/
static void genLiveKtes(PPROC pproc)
{
    uint32_t liveUse, def;

    for (int i = 0; i < pproc->numBBs; i++) {
        liveUse = def = 0;
        PBB pbb = pproc->dfsLast[i];
 
        if (pbb->flg & INVALID_BB) // skip invalid BBs
            continue;
 
        for (int j = pbb->start; j < (pbb->start + pbb->length); j++) {
            PICODE picode = &pproc->Icode.icode[j];

            if ((picode->type == HIGH_LEVEL) && (picode->invalid == false)) {
                liveUse |= (picode->du.use & ~def);
                def |= picode->du.def;
            }
        }

        pbb->liveUse = liveUse;
        pbb->def = def;
    }
}

/*
 Generates the liveIn() and liveOut() sets for each basic block via an iterative approach.
 Propagates register usage information to the procedure call.
*/
static void liveRegAnalysis(PPROC pproc, uint32_t liveOut)
{
    PBB pbb;              // pointer to current basic block
    PPROC pcallee;        // invoked subroutine
    PICODE ticode,        // icode that invokes a subroutine
           picode;        // icode of function return
    uint32_t prevLiveOut, // previous live out
             prevLiveIn;  // previous live in

    // liveOut for this procedure
    pproc->liveOut = liveOut;

    bool change = true;
 
    while (change) { // Process nodes in reverse postorder order
        change = false;

        for (int i = pproc->numBBs; i > 0; i--) {
            pbb = pproc->dfsLast[i - 1];

            if (pbb->flg & INVALID_BB) // Do not process invalid BBs
                continue;

            // Get current liveIn() and liveOut() sets
            prevLiveIn = pbb->liveIn;
            prevLiveOut = pbb->liveOut;

            // liveOut(b) = U LiveIn(s); where s is successor(b)
            // liveOut(b) = {liveOut}; when b is a RET node
            if (pbb->numOutEdges == 0) { // RET node
                pbb->liveOut = liveOut;

                // Get return expression of function
                if (pproc->flg & PROC_IS_FUNC) {
                    picode = &pproc->Icode.icode[pbb->start + pbb->length - 1];
                    if (picode->ic.hl.opcode == RET) {
                        picode->ic.hl.oper.exp = idCondExpID(&pproc->retVal, &pproc->localId,
                                                             pbb->start + pbb->length - 1);
                        picode->du.use = liveOut;
                    }
                }
            } else { // Check successors
                for (int j = 0; j < pbb->numOutEdges; j++)
                    pbb->liveOut |= pbb->edges[j].BBptr->liveIn;

                // propagate to invoked procedure
                if (pbb->nodeType == CALL_NODE) {
                    ticode = &pproc->Icode.icode[pbb->start + pbb->length - 1];
                    pcallee = ticode->ic.hl.oper.call.proc;

                    // user/runtime routine
                    if (!(pcallee->flg & PROC_ISLIB)) {
                        if (pcallee->liveAnal == false) // hasn't been processed
                            dataFlow(pcallee, pbb->liveOut);
                        pbb->liveOut = pcallee->liveIn;
                    } else { /* library routine */
                        if (pcallee->flg & PROC_IS_FUNC) // returns a value
                            pbb->liveOut = pcallee->liveOut;
                        else
                            pbb->liveOut = 0;
                    }

                    switch (pcallee->retVal.type) {
                    case TYPE_LONG_SIGN:
                    case TYPE_LONG_UNSIGN:
                        ticode->du1.numRegsDef = 2;
                        break;
                    case TYPE_WORD_SIGN:
                    case TYPE_WORD_UNSIGN:
                    case TYPE_BYTE_SIGN:
                    case TYPE_BYTE_UNSIGN:
                        ticode->du1.numRegsDef = 1;
                        break;
                    default:
                        break;
                    } // eos

                    // Propagate du/use results to calling icode
                    ticode->du.use = pcallee->liveIn;
                    ticode->du.def = pcallee->liveOut;
                }
            }

            // liveIn(b) = liveUse(b) U (liveOut(b) - def(b)
            pbb->liveIn = pbb->liveUse | (pbb->liveOut & ~pbb->def);

            // Check if live sets have been modified
            if ((prevLiveIn != pbb->liveIn) || (prevLiveOut != pbb->liveOut))
                change = true;
        }
    }

    // Propagate liveIn(b) to procedure header
    if (pbb->liveIn != 0) // uses registers
        pproc->liveIn = pbb->liveIn;

    // Remove any references to register variables
    if (pproc->flg & SI_REGVAR) {
        pproc->liveIn &= maskDuReg[rSI];
        pbb->liveIn &= maskDuReg[rSI];
    }

    if (pproc->flg & DI_REGVAR) {
        pproc->liveIn &= maskDuReg[rDI];
        pbb->liveIn &= maskDuReg[rDI];
    }
}

// Generates the du chain of each instruction in a basic block
static void genDU1(PPROC pProc)
{
    uint8_t regi;          // Register that was defined
    PICODE picode, ticode; // Current and target bb
    PBB pbb, tbb;          // Current and target basic block

    bool res;
    int useIdx;

    // Traverse tree in dfsLast order
    for (int i = 0; i < pProc->numBBs; i++) {
        pbb = pProc->dfsLast[i];

        if (pbb->flg & INVALID_BB)
            continue;

        /* Process each register definition of a HIGH_LEVEL icode instruction.
           Note that register variables should not be considered registers. */
        int lastInst = pbb->start + pbb->length;

        for (int j = pbb->start; j < lastInst; j++) {
            picode = &pProc->Icode.icode[j];

            if (picode->type == HIGH_LEVEL) {
                regi = 0;
                int defRegIdx = 0;

                for (int k = 0; k < INDEXBASE; k++) {
                    if ((picode->du.def & power2(k)) != 0) {
                        regi = k + 1; // defined register
                        picode->du1.regi[defRegIdx] = regi;

                        /* Check remaining instructions of the BB for all uses of register regi,
                           before any definitions of the register */
                        if ((regi == rDI) && (pProc->flg & DI_REGVAR))
                            continue;
                        if ((regi == rSI) && (pProc->flg & SI_REGVAR))
                            continue;
                        if ((j + 1) < lastInst) { // several instructions
                            useIdx = 0;

                            for (int n = j + 1; n < lastInst; n++) {
                                // Only check uses of HIGH_LEVEL icodes
                                ticode = &pProc->Icode.icode[n];
                                if (ticode->type == HIGH_LEVEL) {
                                    // if used, get icode index
                                    if (ticode->du.use & duReg[regi])
                                        picode->du1.idx[defRegIdx][useIdx++] = n;

                                    // if defined, stop finding uses for this reg
                                    if (ticode->du.def & duReg[regi])
                                        break;
                                }
                            }

                            // Check if last definition of this register
                            if ((!(ticode->du.def & duReg[regi])) && (pbb->liveOut & duReg[regi]))
                                picode->du.lastDefRegi |= duReg[regi];
                        } else // only 1 instruction in this basic block
                            // Check if last definition of this register
                            if (pbb->liveOut & duReg[regi])
                                picode->du.lastDefRegi |= duReg[regi];

                        /* Find target icode for CALL icodes to procedures that are functions.
                           The target icode is in the next basic block (unoptimized code) or
                           somewhere else on optimized code. */
                        if ((picode->ic.hl.opcode == CALL) &&
                            (picode->ic.hl.oper.call.proc->flg & PROC_IS_FUNC)) {
                            tbb = pbb->edges[0].BBptr;
                            useIdx = 0;
                            for (int n = tbb->start; n < tbb->start + tbb->length; n++) {
                                ticode = &pProc->Icode.icode[n];
                                if (ticode->type == HIGH_LEVEL) {
                                    // if used, get icode index
                                    if (ticode->du.use & duReg[regi])
                                        picode->du1.idx[defRegIdx][useIdx++] = n;

                                    // if defined, stop finding uses for this reg
                                    if (ticode->du.def & duReg[regi])
                                        break;
                                }
                            }

                            /* if not used in this basic block, check if the register is live out,
                               if so, make it the last definition of this register */
                            if ((picode->du1.idx[defRegIdx][useIdx] == 0) && (tbb->liveOut & duReg[regi]))
                                picode->du.lastDefRegi |= duReg[regi];
                        }

                        /* If not used within this bb or in successors of this bb (ie. not in liveOut),
                           then register is useless, thus remove it. Also check that this is not a return
                           from a library function (routines such as printf return an integer,
                           which is normally not taken into account by the programmer). */
                        if ((picode->invalid == false) && (picode->du1.idx[defRegIdx][0] == 0) &&
                            (!(picode->du.lastDefRegi & duReg[regi])) &&
                            (!((picode->ic.hl.opcode != CALL) &&
                               (picode->ic.hl.oper.call.proc->flg & PROC_ISLIB)))) {
                            if (!(pbb->liveOut & duReg[regi])) { // not liveOut
                                res = removeDefRegi(regi, picode, defRegIdx + 1, &pProc->localId);

                                /* Backpatch any uses of this instruction, within the same BB,
                                   if the instruction was invalidated */
                                if (res == true)
                                    for (int p = j; p > pbb->start; p--) {
                                        ticode = &pProc->Icode.icode[p - 1];
                                        for (int n = 0; n < MAX_USES; n++) {
                                            if (ticode->du1.idx[0][n] == j) {
                                                if (n < MAX_USES - 1) {
                                                    memmove(&ticode->du1.idx[0][n],
                                                            &ticode->du1.idx[0][n + 1],
                                                            (MAX_USES - n - 1) * sizeof(int));
                                                    n--;
                                                }
                                                ticode->du1.idx[0][MAX_USES - 1] = 0;
                                            }
                                        }
                                    }
                            } else // liveOut
                                picode->du.lastDefRegi |= duReg[regi];
                        }
                        defRegIdx++;

                        // Check if all defined registers have been processed
                        if ((defRegIdx >= picode->du1.numRegsDef) || (defRegIdx == MAX_REGS_DEF))
                            break;
                    }
                }
            }
        }
    }
}

// Substitutes the rhs (or lhs if rhs not possible) of ticode for the rhs of picode.
static void forwardSubs(COND_EXPR *lhs, COND_EXPR *rhs, PICODE picode, PICODE ticode, LOCAL_ID *locsym, int *numHlIcodes)
{
    if (rhs == NULL) // In case expression popped is NULL
        return;

    // Insert on rhs of ticode, if possible
    bool res = insertSubTreeReg(rhs, &ticode->ic.hl.oper.asgn.rhs,
                                locsym->id[lhs->expr.ident.idNode.regiIdx].id.regi, locsym);

    if (res) {
        invalidateIcode(picode);
        (*numHlIcodes)--;
    } else { // Try to insert it on lhs of ticode
        res = insertSubTreeReg(rhs, &ticode->ic.hl.oper.asgn.lhs,
                               locsym->id[lhs->expr.ident.idNode.regiIdx].id.regi, locsym);
        if (res) {
            invalidateIcode(picode);
            (*numHlIcodes)--;
        }
    }
}

// Substitutes the rhs (or lhs if rhs not possible) of ticode for the expression exp given
static void forwardSubsLong(int longIdx, COND_EXPR *exp, PICODE picode, PICODE ticode, int *numHlIcodes)
{
    if (exp == NULL) // In case expression popped is NULL
        return;

    // Insert on rhs of ticode, if possible
    bool res = insertSubTreeLongReg(exp, &ticode->ic.hl.oper.asgn.rhs, longIdx);

    if (res) {
        invalidateIcode(picode);
        (*numHlIcodes)--;
    } else { // Try to insert it on lhs of ticode
        res = insertSubTreeLongReg(exp, &ticode->ic.hl.oper.asgn.lhs, longIdx);
        if (res) {
            invalidateIcode(picode);
            (*numHlIcodes)--;
        }
    }
}

// Returns whether the elements of the expression rhs are all x-clear from insn f up to insn t.
static bool xClear(COND_EXPR *rhs, int f, int t, int lastBBinst, PPROC pproc)
{
    int i;
    bool res;
    uint8_t regi;
    PICODE picode;

    if (rhs == NULL)
        return false;

    switch (rhs->type) {
    case IDENTIFIER:
        if (rhs->expr.ident.idType == REGISTER) {
            picode = pproc->Icode.icode;
            regi = pproc->localId.id[rhs->expr.ident.idNode.regiIdx].id.regi;

            for (i = (f + 1); (i < lastBBinst) && (i < t); i++)
                if ((picode[i].type == HIGH_LEVEL) && (picode[i].invalid == false)) {
                    if (picode[i].du.def & duReg[regi])
                        return false;
                }

            if (i < lastBBinst)
                return true;
            else
                return false;
        } else
            return true;
    case BOOLEAN:
        res = xClear(rhs->expr.boolExpr.rhs, f, t, lastBBinst, pproc);
        if (res == false)
            return false;
        return (xClear(rhs->expr.boolExpr.lhs, f, t, lastBBinst, pproc));
    case NEGATION:
    case ADDRESSOF:
    case DEREFERENCE:
        return (xClear(rhs->expr.unaryExp, f, t, lastBBinst, pproc));
    default:
        break;
    }

    return false;
}

/*
 Checks the type of the formal argument as against to the actual argument, whenever possible,
 and then places the actual argument on the procedure's argument list.
*/
static void processCArg(PPROC pp, PPROC pProc, PICODE picode, int numArgs, int *k)
{
    bool res;

    COND_EXPR *exp = popExpStk();

    if (pp->flg & PROC_ISLIB) { // library function
        if (pp->args.numArgs > 0) {
            if (pp->flg & PROC_VARARG) {
                if (numArgs < pp->args.csym)
                    adjustActArgType(exp, pp->args.sym[numArgs].type, pProc);
            } else
                adjustActArgType(exp, pp->args.sym[numArgs].type, pProc);
        }
        res = newStkArg(picode, exp, picode->ic.ll.opcode, pProc);
    } else { // user function
        if (pp->args.numArgs > 0)
            adjustForArgType(&pp->args, numArgs, expType(exp, pProc));
        res = newStkArg(picode, exp, picode->ic.ll.opcode, pProc);
    }

    // Do not update the size of k if the expression was a segment register in a near call
    if (res == false)
        *k += hlTypeSize(exp, pProc);
}

/*
 Eliminates extraneous intermediate icode instructions when finding expressions.
 Generates new hlIcodes in the form of expression trees.
 For CALL hlIcodes, places the arguments in the argument list.
*/
static void findExps(PPROC pProc)
{
    PICODE picode;       // Current icode
    PICODE ticode;       // Target icode
    PBB pbb;             // Current basic block
    COND_EXPR *exp;      // expression pointer - for POP and CALL
    COND_EXPR *lhs;      // exp ptr for return value of a CALL
    uint8_t regi;        // register to be forward substituted
    ID *retVal;          // function return value

    int k;
    bool res;

    // Initialize expression stack
    initExpStk();

    // Traverse tree in dfsLast order
    for (int i = 0; i < pProc->numBBs; i++) { // Process one BB
        pbb = pProc->dfsLast[i];

        if (pbb->flg & INVALID_BB)
            continue;

        int lastInst = pbb->start + pbb->length;
        int numHlIcodes = 0;

        for (int j = pbb->start; j < lastInst; j++) {
            picode = &pProc->Icode.icode[j];
            if ((picode->type == HIGH_LEVEL) && (picode->invalid == false)) {
                numHlIcodes++;
                if (picode->du1.numRegsDef == 1) { // byte/word regs
                    /* Check for only one use of this register.  If this is
                       the last definition of the register in this BB, check
                       that it is not liveOut from this basic block */
                    if ((picode->du1.idx[0][0] != 0) && (picode->du1.idx[0][1] == 0)) {
                        /* Check that this register is not liveOut, if it
                           is the last definition of the register */
                        regi = picode->du1.regi[0];

                        // Check if we can forward substitute this register
                        switch (picode->ic.hl.opcode) {
                        default: break;
                        case ASSIGN: // Replace rhs of current icode into target icode expression
                            ticode = &pProc->Icode.icode[picode->du1.idx[0][0]];
                            if ((picode->du.lastDefRegi & duReg[regi]) &&
                                ((ticode->ic.hl.opcode != CALL) && (ticode->ic.hl.opcode != RET)))
                                continue;

                            if (xClear(picode->ic.hl.oper.asgn.rhs, j, picode->du1.idx[0][0],
                                       lastInst, pProc)) {
                                switch (ticode->ic.hl.opcode) {
                                case ASSIGN:
                                    forwardSubs(picode->ic.hl.oper.asgn.lhs, picode->ic.hl.oper.asgn.rhs,
                                                picode, ticode, &pProc->localId, &numHlIcodes);
                                    break;

                                case JCOND:
                                case PUSH:
                                case RET:
                                    res = insertSubTreeReg(picode->ic.hl.oper.asgn.rhs, &ticode->ic.hl.oper.exp,
                                    pProc->localId.id[picode->ic.hl.oper.asgn.lhs->expr.ident.idNode.regiIdx].id.regi, //TODO WTF?
                                                           &pProc->localId);
                                    if (res) {
                                        invalidateIcode(picode);
                                        numHlIcodes--;
                                    }
                                    break;

                                case CALL: // register arguments
                                    newRegArg(pProc, picode, ticode);
                                    invalidateIcode(picode);
                                    numHlIcodes--;
                                    break;
                                default:
                                    break;
                                }
                            }
                            break;

                        case POP:
                            ticode = &pProc->Icode.icode[picode->du1.idx[0][0]];
                            if ((picode->du.lastDefRegi & duReg[regi]) &&
                                ((ticode->ic.hl.opcode != CALL) && (ticode->ic.hl.opcode != RET)))
                                continue;

                            exp = popExpStk(); // pop last exp pushed
                            switch (ticode->ic.hl.opcode) {
                            case ASSIGN:
                                forwardSubs(picode->ic.hl.oper.exp, exp, picode, ticode,
                                            &pProc->localId, &numHlIcodes);
                                break;

                            case JCOND:
                            case PUSH:
                            case RET:
                                res = insertSubTreeReg(
                                    exp, &ticode->ic.hl.oper.exp,
                                    pProc->localId
                                        .id[picode->ic.hl.oper.exp->expr.ident.idNode.regiIdx]
                                        .id.regi,
                                    &pProc->localId);
                                if (res) {
                                    invalidateIcode(picode);
                                    numHlIcodes--;
                                }
                                break;
                            default:
                                break;
                            }
                            break;

                        case CALL:
                            ticode = &pProc->Icode.icode[picode->du1.idx[0][0]];
                            switch (ticode->ic.hl.opcode) {
                            default: break;
                            case ASSIGN:
                                exp = idCondExpFunc(picode->ic.hl.oper.call.proc,
                                                    picode->ic.hl.oper.call.args);
                                res = insertSubTreeReg(exp, &ticode->ic.hl.oper.asgn.rhs,
                                                       picode->ic.hl.oper.call.proc->retVal.id.regi,
                                                       &pProc->localId);
                                if (!res)
                                    insertSubTreeReg(exp, &ticode->ic.hl.oper.asgn.lhs,
                                                     picode->ic.hl.oper.call.proc->retVal.id.regi,
                                                     &pProc->localId);
                                //  HERE missing: 2 regs TODO
                                invalidateIcode(picode);
                                numHlIcodes--;
                                break;

                            case PUSH:
                            case RET:
                                exp = idCondExpFunc(picode->ic.hl.oper.call.proc,
                                                    picode->ic.hl.oper.call.args);
                                ticode->ic.hl.oper.exp = exp;
                                invalidateIcode(picode);
                                numHlIcodes--;
                                break;

                            case JCOND:
                                exp = idCondExpFunc(picode->ic.hl.oper.call.proc,
                                                    picode->ic.hl.oper.call.args);
                                retVal = &picode->ic.hl.oper.call.proc->retVal,
                                res = insertSubTreeReg(exp, &ticode->ic.hl.oper.exp,
                                                       retVal->id.regi, &pProc->localId);
                                if (res) // was substituted
                                {
                                    invalidateIcode(picode);
                                    numHlIcodes--;
                                } else // cannot substitute function
                                {
                                    lhs = idCondExpID(retVal, &pProc->localId, j);
                                    newAsgnHlIcode(picode, lhs, exp);
                                }
                                break;
                            } // eos
                            break;
                        } // eos
                    }
                }

                else if (picode->du1.numRegsDef == 2) { // long regs
                    // Check for only one use of these registers
                    if ((picode->du1.idx[0][0] != 0) && (picode->du1.idx[0][1] == 0) &&
                        (picode->du1.idx[1][0] != 0) && (picode->du1.idx[1][1] == 0)) {
                        switch (picode->ic.hl.opcode) {
                        default: break;
                        case ASSIGN:
                            // Replace rhs of current icode into target icode expression
                            if (picode->du1.idx[0][0] == picode->du1.idx[1][0]) {
                                ticode = &pProc->Icode.icode[picode->du1.idx[0][0]];
                                if ((picode->du.lastDefRegi & duReg[regi]) &&
                                    ((ticode->ic.hl.opcode != CALL) &&
                                     (ticode->ic.hl.opcode != RET)))
                                    continue;

                                switch (ticode->ic.hl.opcode) {
                                default: break;
                                case ASSIGN:
                                    forwardSubsLong(
                                        picode->ic.hl.oper.asgn.lhs->expr.ident.idNode.longIdx,
                                        picode->ic.hl.oper.asgn.rhs, picode, ticode, &numHlIcodes);
                                    break;

                                case JCOND:
                                case PUSH:
                                case RET:
                                    res = insertSubTreeLongReg(
                                        picode->ic.hl.oper.asgn.rhs, &ticode->ic.hl.oper.exp,
                                        picode->ic.hl.oper.asgn.lhs->expr.ident.idNode.longIdx);
                                    if (res) {
                                        invalidateIcode(picode);
                                        numHlIcodes--;
                                    }
                                    break;

                                case CALL: // register arguments
                                    newRegArg(pProc, picode, ticode);
                                    invalidateIcode(picode);
                                    numHlIcodes--;
                                    break;
                                } // eos
                            }
                            break;

                        case POP:
                            if (picode->du1.idx[0][0] == picode->du1.idx[1][0]) {
                                ticode = &pProc->Icode.icode[picode->du1.idx[0][0]];
                                if ((picode->du.lastDefRegi & duReg[regi]) &&
                                    ((ticode->ic.hl.opcode != CALL) &&
                                     (ticode->ic.hl.opcode != RET)))
                                    continue;

                                exp = popExpStk(); // pop last exp pushed
                                switch (ticode->ic.hl.opcode) {
                                default: break;
                                case ASSIGN:
                                    forwardSubsLong(
                                        picode->ic.hl.oper.exp->expr.ident.idNode.longIdx, exp,
                                        picode, ticode, &numHlIcodes);
                                    break;
                                case JCOND:
                                case PUSH:
                                    res = insertSubTreeLongReg(
                                        exp, &ticode->ic.hl.oper.exp,
                                        picode->ic.hl.oper.asgn.lhs->expr.ident.idNode.longIdx);
                                    if (res) {
                                        invalidateIcode(picode);
                                        numHlIcodes--;
                                    }
                                    break;
                                case CALL: // missing TODO
                                    break;
                                } // eos
                            }
                            break;

                        case CALL: // check for function return
                            ticode = &pProc->Icode.icode[picode->du1.idx[0][0]];
                            switch (ticode->ic.hl.opcode) {
                            default: break;
                            case ASSIGN:
                                exp = idCondExpFunc(picode->ic.hl.oper.call.proc,
                                                    picode->ic.hl.oper.call.args);
                                ticode->ic.hl.oper.asgn.lhs = idCondExpLong(
                                    &pProc->localId, DST, ticode, HIGH_FIRST, j, DEF, 1);
                                ticode->ic.hl.oper.asgn.rhs = exp;
                                invalidateIcode(picode);
                                numHlIcodes--;
                                break;

                            case PUSH:
                            case RET:
                                exp = idCondExpFunc(picode->ic.hl.oper.call.proc,
                                                    picode->ic.hl.oper.call.args);
                                ticode->ic.hl.oper.exp = exp;
                                invalidateIcode(picode);
                                numHlIcodes--;
                                break;

                            case JCOND:
                                exp = idCondExpFunc(picode->ic.hl.oper.call.proc,
                                                    picode->ic.hl.oper.call.args);
                                retVal = &picode->ic.hl.oper.call.proc->retVal;
                                res = insertSubTreeLongReg(
                                    exp, &ticode->ic.hl.oper.exp,
                                    newLongRegId(&pProc->localId, retVal->type, retVal->id.longId.h,
                                                 retVal->id.longId.l, j));
                                if (res) { // was substituted
                                    invalidateIcode(picode);
                                    numHlIcodes--;
                                } else {   // cannot substitute function
                                    lhs = idCondExpID(retVal, &pProc->localId, j);
                                    newAsgnHlIcode(picode, lhs, exp);
                                }
                                break;
                            } // eos
                        } // eos
                    }
                }

                /* PUSH doesn't define any registers, only uses registers.
                   Push the associated expression to the register on the local expression stack */
                else if (picode->ic.hl.opcode == PUSH) {
                    pushExpStk(picode->ic.hl.oper.exp);
                    invalidateIcode(picode);
                    numHlIcodes--;
                }

                /* For CALL instructions that use arguments from the stack, pop them from the
                   expression stack and place them on the procedure's argument list */
                if ((picode->ic.hl.opcode == CALL) && !(picode->ic.hl.oper.call.proc->flg & REG_ARGS)) {
                    PPROC pp;
                    int cb, numArgs;
                    bool res;

                    pp = picode->ic.hl.oper.call.proc;
                    if (pp->flg & CALL_PASCAL) {
                        cb = pp->cbParam; // fixed # arguments
                        for (k = 0, numArgs = 0; k < cb; numArgs++) {
                            exp = popExpStk();
                            if (pp->flg & PROC_ISLIB) { // library function
                                if (pp->args.numArgs > 0)
                                    adjustActArgType(exp, pp->args.sym[numArgs].type, pProc);
                                res = newStkArg(picode, exp, picode->ic.ll.opcode, pProc);
                            } else { // user function
                                if (pp->args.numArgs > 0)
                                    adjustForArgType(&pp->args, numArgs, expType(exp, pProc));
                                res = newStkArg(picode, exp, picode->ic.ll.opcode, pProc);
                            }
                            if (res == false)
                                k += hlTypeSize(exp, pProc);
                        }
                    } else { // CALL_C
                        cb = picode->ic.hl.oper.call.args->cb;
                        numArgs = 0;
                        if (cb)
                            for (k = 0; k < cb; numArgs++)
                                processCArg(pp, pProc, picode, numArgs, &k);
                        else if ((cb == 0) && (picode->ic.ll.flg & REST_STK))
                            while (!emptyExpStk()) {
                                processCArg(pp, pProc, picode, numArgs, &k);
                                numArgs++;
                            }
                    }
                }

                /* If we could not substitute the result of a function,
                   assign it to the corresponding registers */
                if ((picode->ic.hl.opcode == CALL) &&
                    ((picode->ic.hl.oper.call.proc->flg & PROC_ISLIB) != PROC_ISLIB) &&
                    (picode->du1.idx[0][0] == 0) && (picode->du1.numRegsDef > 0)) {
                    exp = idCondExpFunc(picode->ic.hl.oper.call.proc, picode->ic.hl.oper.call.args);
                    lhs = idCondExpID(&picode->ic.hl.oper.call.proc->retVal, &pProc->localId, j);
                    newAsgnHlIcode(picode, lhs, exp);
                }
            }
        }

        // Store number of high-level icodes in current basic block
        pbb->numHlIcodes = numHlIcodes;
    }
}

/*
 Invokes procedures related with data flow analysis. Works on a procedure at a time basis.
 Note: indirect recursion in liveRegAnalysis is possible.
*/
void dataFlow(PPROC pProc, uint32_t liveOut)
{
    // Remove references to register variables
    if (pProc->flg & SI_REGVAR)
        liveOut &= maskDuReg[rSI];

    if (pProc->flg & DI_REGVAR)
        liveOut &= maskDuReg[rDI];

    // Function - return value register(s)
    if (liveOut != 0) {
        pProc->flg |= PROC_IS_FUNC;
        bool isAx = (liveOut & power2(rAX - rAX));
        bool isBx = (liveOut & power2(rBX - rAX));
        bool isCx = (liveOut & power2(rCX - rAX));
        bool isDx = (liveOut & power2(rDX - rAX));

        if (isAx && isDx) { // long or pointer
            pProc->retVal.type = TYPE_LONG_SIGN;
            pProc->retVal.loc = REG_FRAME;
            pProc->retVal.id.longId.h = rDX;
            pProc->retVal.id.longId.l = rAX;
            newLongRegId(&pProc->localId, TYPE_LONG_SIGN, rDX, rAX, 0);
            propLongId(&pProc->localId, rAX, rDX, "\0");
        } else if (isAx || isBx || isCx || isDx) { // word
            pProc->retVal.type = TYPE_WORD_SIGN;
            pProc->retVal.loc = REG_FRAME;
            if (isAx)
                pProc->retVal.id.regi = rAX;
            else if (isBx)
                pProc->retVal.id.regi = rBX;
            else if (isCx)
                pProc->retVal.id.regi = rCX;
            else
                pProc->retVal.id.regi = rDX;
            newByteWordRegId(&pProc->localId, TYPE_WORD_SIGN, pProc->retVal.id.regi);
        }
    }

    // Data flow analysis
    pProc->liveAnal = true;
    elimCondCodes(pProc);
    genLiveKtes(pProc);
    liveRegAnalysis(pProc, liveOut); // calls dataFlow() recursively

    if (!(pProc->flg & PROC_ASM)) { // can generate C for pProc
        genDU1(pProc);   // generate def/use level 1 chain
        findExps(pProc); // forward substitution algorithm
    }
}
