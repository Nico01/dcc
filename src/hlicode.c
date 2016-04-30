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

// Purpose: High-level icode routines

#include "dcc.h"
#include <malloc.h>
#include <string.h>

#define ICODE_DELTA 25;

// Masks off bits set by duReg[]
uint32_t maskDuReg[] = { 0x00,     0xFEEFFE, 0xFDDFFD, 0xFBB00B, 0xF77007, // word regs
                         0xFFFFEF, 0xFFFFDF, 0xFFFFBF, 0xFFFF7F, 0xFFFEFF,
                         0xFFFDFF, 0xFFFBFF, 0xFFF7FF,                     // seg regs
                         0xFFEFFF, 0xFFDFFF, 0xFFBFFF, 0xFF7FFF,           // byte regs
                         0xFEFFFF, 0xFDFFFF, 0xFBFFFF, 0xF7FFFF, 0xEFFFFF, // tmp reg
                         0xFFFFB7, 0xFFFF77, 0xFFFF9F, 0xFFFF5F,           // index regs
                         0xFFFFBF, 0xFFFF7F, 0xFFFFDF, 0xFFFFF7 };

static char buf[lineSize]; // Line buffer for hl icode output


/*
 Copies the icode that is pointed to by pIcode to the icode array.
 If there is need to allocate extra memory, it is done so, and the icode.alloc variable is adjusted.
*/
PICODE newIcode(ICODE_REC *icode, PICODE pIcode)
{
    if (icode->numIcode == icode->alloc) {
        icode->alloc += ICODE_DELTA;
        icode->icode = allocVar(icode->icode, icode->alloc * sizeof(ICODE));
    }

    PICODE resIcode = memcpy(&icode->icode[icode->numIcode], pIcode, sizeof(ICODE));
    icode->numIcode++;
    return resIcode;
}

// Places the new ASSIGN high-level operand in the high-level icode array
void newAsgnHlIcode(PICODE pIcode, COND_EXPR *lhs, COND_EXPR *rhs)
{
    pIcode->type = HIGH_LEVEL;
    pIcode->ic.hl.opcode = ASSIGN;
    pIcode->ic.hl.oper.asgn.lhs = lhs;
    pIcode->ic.hl.oper.asgn.rhs = rhs;
}

// Places the new CALL high-level operand in the high-level icode array
void newCallHlIcode(PICODE pIcode)
{
    pIcode->type = HIGH_LEVEL;
    pIcode->ic.hl.opcode = CALL;
    pIcode->ic.hl.oper.call.proc = pIcode->ic.ll.immed.proc.proc;
    pIcode->ic.hl.oper.call.args = allocMem(sizeof(STKFRAME));
    memset(pIcode->ic.hl.oper.call.args, 0, sizeof(STKFRAME));

    if (pIcode->ic.ll.immed.proc.cb != 0)
        pIcode->ic.hl.oper.call.args->cb = pIcode->ic.ll.immed.proc.cb;
    else
        pIcode->ic.hl.oper.call.args->cb = pIcode->ic.hl.oper.call.proc->cbParam;
}

// Places the new POP/PUSH/RET high-level operand in the high-level icode array
void newUnaryHlIcode(PICODE pIcode, hlIcode op, COND_EXPR *exp)
{
    pIcode->type = HIGH_LEVEL;
    pIcode->ic.hl.opcode = op;
    pIcode->ic.hl.oper.exp = exp;
}

// Places the new JCOND high-level operand in the high-level icode array
void newJCondHlIcode(PICODE pIcode, COND_EXPR *cexp)
{
    pIcode->type = HIGH_LEVEL;
    pIcode->ic.hl.opcode = JCOND;
    pIcode->ic.hl.oper.exp = cexp;
}

/*
 Sets the invalid field to TRUE as this low-level icode is no longer valid,
 it has been replaced by a high-level icode.
*/
void invalidateIcode(PICODE pIcode) { pIcode->invalid = true; }

/*
 Removes the defined register regi from the lhs subtree. If all registers of this instruction
 are unused, the instruction is invalidated (ie. removed)
*/
bool removeDefRegi(uint8_t regi, PICODE picode, int thisDefIdx, LOCAL_ID *locId)
{
    int numDefs = picode->du1.numRegsDef;

    if (numDefs == thisDefIdx)
        for (; numDefs > 0; numDefs--) {
            if ((picode->du1.idx[numDefs - 1][0] != 0) || (picode->du.lastDefRegi))
                break;
        }

    if (numDefs == 0) {
        invalidateIcode(picode);
        return true;
    } else {
        switch (picode->ic.hl.opcode) {
        default: break;
        case ASSIGN:
            removeRegFromLong(regi, locId, picode->ic.hl.oper.asgn.lhs);
            picode->du1.numRegsDef--;
            picode->du.def &= maskDuReg[regi];
            break;
        case POP:
        case PUSH:
            removeRegFromLong(regi, locId, picode->ic.hl.oper.exp);
            picode->du1.numRegsDef--;
            picode->du.def &= maskDuReg[regi];
            break;
        }
        return true;
    }
}

/*
 Translates LOW_LEVEL icodes to HIGH_LEVEL icodes - 1st stage.
 Note: this process should be done before data flow analysis, which refines the HIGH_LEVEL icodes.
*/
void highLevelGen(PPROC pProc)
{   
    PICODE pIcode;        // ptr to current icode node
    COND_EXPR *lhs, *rhs; // left- and right-hand side of expression
    uint32_t flg;         // icode flags

    int numIcode = pProc->Icode.numIcode; // number of icode instructions

    for (int i = 0; i < numIcode; i++) {
        pIcode = &pProc->Icode.icode[i];
        if ((pIcode->ic.ll.flg & NOT_HLL) == NOT_HLL)
            invalidateIcode(pIcode);
        if ((pIcode->type == LOW_LEVEL) && (pIcode->invalid == false)) {
            flg = pIcode->ic.ll.flg;
            if ((flg & IM_OPS) != IM_OPS)         // not processing IM_OPS yet
                if ((flg & NO_OPS) != NO_OPS) {   // if there are opers
                    if ((flg & NO_SRC) != NO_SRC) // if there is src op
                        rhs = idCondExp(pIcode, SRC, pProc, i, pIcode, NONE);
                    lhs = idCondExp(pIcode, DST, pProc, i, pIcode, NONE);
                }

            switch (pIcode->ic.ll.opcode) {
            default: break;
            case iADD:
                rhs = boolCondExp(lhs, rhs, ADD);
                newAsgnHlIcode(pIcode, lhs, rhs);
                break;

            case iAND:
                rhs = boolCondExp(lhs, rhs, AND);
                newAsgnHlIcode(pIcode, lhs, rhs);
                break;

            case iCALL:
            case iCALLF:
                newCallHlIcode(pIcode);
                break;

            case iDEC:
                rhs = idCondExpKte(1, 2);
                rhs = boolCondExp(lhs, rhs, SUB);
                newAsgnHlIcode(pIcode, lhs, rhs);
                break;

            case iDIV:
            case iIDIV: // should be signed div
                rhs = boolCondExp(lhs, rhs, DIV);
                if (pIcode->ic.ll.flg & B) {
                    lhs = idCondExpReg(rAL, 0, &pProc->localId);
                    setRegDU(pIcode, rAL, DEF);
                } else {
                    lhs = idCondExpReg(rAX, 0, &pProc->localId);
                    setRegDU(pIcode, rAX, DEF);
                }
                newAsgnHlIcode(pIcode, lhs, rhs);
                break;

            case iIMUL:
                rhs = boolCondExp(lhs, rhs, MUL);
                lhs = idCondExp(pIcode, LHS_OP, pProc, i, pIcode, NONE);
                newAsgnHlIcode(pIcode, lhs, rhs);
                break;

            case iINC:
                rhs = idCondExpKte(1, 2);
                rhs = boolCondExp(lhs, rhs, ADD);
                newAsgnHlIcode(pIcode, lhs, rhs);
                break;

            case iLEA:
                rhs = unaryCondExp(ADDRESSOF, rhs);
                newAsgnHlIcode(pIcode, lhs, rhs);
                break;

            case iMOD:
                rhs = boolCondExp(lhs, rhs, MOD);
                if (pIcode->ic.ll.flg & B) {
                    lhs = idCondExpReg(rAH, 0, &pProc->localId);
                    setRegDU(pIcode, rAH, DEF);
                } else {
                    lhs = idCondExpReg(rDX, 0, &pProc->localId);
                    setRegDU(pIcode, rDX, DEF);
                }
                newAsgnHlIcode(pIcode, lhs, rhs);
                break;

            case iMOV:
                newAsgnHlIcode(pIcode, lhs, rhs);
                break;

            case iMUL:
                rhs = boolCondExp(lhs, rhs, MUL);
                lhs = idCondExp(pIcode, LHS_OP, pProc, i, pIcode, NONE);
                newAsgnHlIcode(pIcode, lhs, rhs);
                break;

            case iNEG:
                rhs = unaryCondExp(NEGATION, lhs);
                newAsgnHlIcode(pIcode, lhs, rhs);
                break;

            case iNOT:
                rhs = boolCondExp(NULL, rhs, NOT);
                newAsgnHlIcode(pIcode, lhs, rhs);
                break;

            case iOR:
                rhs = boolCondExp(lhs, rhs, OR);
                newAsgnHlIcode(pIcode, lhs, rhs);
                break;

            case iPOP:
                newUnaryHlIcode(pIcode, POP, lhs);
                break;

            case iPUSH:
                newUnaryHlIcode(pIcode, PUSH, lhs);
                break;

            case iRET:
            case iRETF:
                newUnaryHlIcode(pIcode, RET, NULL);
                break;

            case iSHL:
                rhs = boolCondExp(lhs, rhs, SHL);
                newAsgnHlIcode(pIcode, lhs, rhs);
                break;

            case iSAR: // signed
            case iSHR:
                rhs = boolCondExp(lhs, rhs, SHR); // unsigned
                newAsgnHlIcode(pIcode, lhs, rhs);
                break;

            case iSIGNEX:
                newAsgnHlIcode(pIcode, lhs, rhs);
                break;

            case iSUB:
                rhs = boolCondExp(lhs, rhs, SUB);
                newAsgnHlIcode(pIcode, lhs, rhs);
                break;

            case iXCHG:
                break;

            case iXOR:
                rhs = boolCondExp(lhs, rhs, XOR);
                newAsgnHlIcode(pIcode, lhs, rhs);
                break;
            }
        }
    }
}

/*
 Modifies the given conditional operator to its inverse.
 This is used in if..then[..else] statements, to reflect the condition that takes the then part.
*/
void inverseCondOp(COND_EXPR **exp)
{
    static condOp invCondOp[] = { GREATER, GREATER_EQUAL, NOT_EQUAL, EQUAL, LESS_EQUAL, LESS, DUMMY,
                                  DUMMY, DUMMY, DUMMY, DUMMY, DUMMY, DUMMY, DUMMY, DUMMY, DUMMY,
                                  DUMMY, DBL_OR, DBL_AND };

    if (*exp == NULL)
        return;

    if ((*exp)->type == BOOLEAN) {
        switch ((*exp)->expr.boolExpr.op) {
        default: break;
        case LESS_EQUAL:
        case LESS:
        case EQUAL:
        case NOT_EQUAL:
        case GREATER:
        case GREATER_EQUAL:
            (*exp)->expr.boolExpr.op = invCondOp[(*exp)->expr.boolExpr.op];
            break;

        case AND:
        case OR:
        case XOR:
        case NOT:
        case ADD:
        case SUB:
        case MUL:
        case DIV:
        case SHR:
        case SHL:
        case MOD:
            *exp = unaryCondExp(NEGATION, *exp);
            break;

        case DBL_AND:
        case DBL_OR:
            (*exp)->expr.boolExpr.op = invCondOp[(*exp)->expr.boolExpr.op];
            inverseCondOp(&(*exp)->expr.boolExpr.lhs);
            inverseCondOp(&(*exp)->expr.boolExpr.rhs);
            break;
        }

    } else if ((*exp)->type == NEGATION)
        *exp = (*exp)->expr.unaryExp;

    // other types are left unmodified
}

// Returns the string that represents the procedure call of tproc (ie. with actual parameters)
char *writeCall(PPROC tproc, PSTKFRAME args, PPROC pproc, int *numLoc)
{
    char *s = allocMem(100 * sizeof(char));
    s[0] = '\0';
    sprintf(s, "%s (", tproc->name);

    for (int i = 0; i < args->csym; i++) {
        char *condExp = walkCondExpr(args->sym[i].actual, pproc, numLoc);
        strcat(s, condExp);
        if (i < (args->csym - 1))
            strcat(s, ", ");
    }

    strcat(s, ")");

    return s;
}

// Displays the output of a JCOND icode.
char *writeJcond(struct _hl h, PPROC pProc, int *numLoc)
{
    memset(buf, ' ', sizeof(buf));
    buf[0] = '\0';
    strcat(buf, "if ");
    inverseCondOp(&h.oper.exp);
    char *e = walkCondExpr(h.oper.exp, pProc, numLoc);
    strcat(buf, e);
    strcat(buf, " {\n");

    return buf;
}

/*
 Displays the inverse output of a JCOND icode. This is used in the case when the THEN clause
 of an if..then..else is empty. The clause is negated and the ELSE clause is used instead.
*/
char *writeJcondInv(struct _hl h, PPROC pProc, int *numLoc)
{
    memset(buf, ' ', sizeof(buf));
    buf[0] = '\0';
    strcat(buf, "if ");
    char *e = walkCondExpr(h.oper.exp, pProc, numLoc);
    strcat(buf, e);
    strcat(buf, " {\n");

    return buf;
}

/*
 Returns a string with the contents of the current high-level icode.
 Note: this routine does not output the contens of JCOND icodes. This is done in a separate routine
 to be able to support the removal of empty THEN clauses on an if..then..else.
*/
char *write1HlIcode(struct _hl h, PPROC pProc, int *numLoc)
{
    char *e;
    memset(buf, ' ', sizeof(buf));
    buf[0] = '\0';

    switch (h.opcode) {
    default: break;
    case ASSIGN:
        e = walkCondExpr(h.oper.asgn.lhs, pProc, numLoc);
        strcat(buf, e);
        strcat(buf, " = ");
        e = walkCondExpr(h.oper.asgn.rhs, pProc, numLoc);
        strcat(buf, e);
        strcat(buf, ";\n");
        break;
    case CALL:
        e = writeCall(h.oper.call.proc, h.oper.call.args, pProc, numLoc);
        strcat(buf, e);
        strcat(buf, ";\n");
        break;
    case RET:
        e = walkCondExpr(h.oper.exp, pProc, numLoc);
        if (e[0] != '\0') {
            strcat(buf, "return (");
            strcat(buf, e);
            strcat(buf, ");\n");
        }
        break;
    case POP:
        strcat(buf, "POP ");
        e = walkCondExpr(h.oper.exp, pProc, numLoc);
        strcat(buf, e);
        strcat(buf, "\n");
        break;
    case PUSH:
        strcat(buf, "PUSH ");
        e = walkCondExpr(h.oper.exp, pProc, numLoc);
        strcat(buf, e);
        strcat(buf, "\n");
        break;
    }
    return buf;
}

// Returns the value of 2 to the power of i
int power2(int i)
{
    if (i == 0)
        return 1;

    return (2 << (i - 1));
}

// Writes the registers/stack variables that are used and defined by this instruction.
void writeDU(PICODE pIcode, int idx)
{
    static char buf[100];

    memset(buf, ' ', sizeof(buf));
    buf[0] = '\0';

    for (int i = 0; i < (INDEXBASE - 1); i++) {
        if ((pIcode->du.def & power2(i)) != 0) {
            strcat(buf, allRegs[i]);
            strcat(buf, " ");
        }
    }

    if (buf[0] != '\0')
        printf("Def (reg) = %s\n", buf);

    memset(buf, ' ', sizeof(buf));
    buf[0] = '\0';

    for (int i = 0; i < INDEXBASE; i++) {
        if ((pIcode->du.use & power2(i)) != 0) {
            strcat(buf, allRegs[i]);
            strcat(buf, " ");
        }
    }

    if (buf[0] != '\0')
        printf("Use (reg) = %s\n", buf);

    // Print du1 chain
    printf("# regs defined = %d\n", pIcode->du1.numRegsDef);

    for (int i = 0; i < MAX_REGS_DEF; i++)
        if (pIcode->du1.idx[i][0] != 0) {
            printf("%d: du1[%d][] = ", idx, i);
            for (int j = 0; j < MAX_USES; j++) {
                if (pIcode->du1.idx[i][j] == 0)
                    break;
                printf("%d ", pIcode->du1.idx[i][j]);
            }
            printf("\n");
        }

    // For CALL, print # parameter bytes
    if (pIcode->ic.hl.opcode == CALL)
        printf("# param bytes = %d\n", pIcode->ic.hl.oper.call.args->cb);
    printf("\n");
}

// Frees the storage allocated to h->hlIcode
void freeHlIcode(PICODE icode, int numIcodes)
{
    for (int i = 0; i < numIcodes; i++) {
        struct _hl h = icode[i].ic.hl;
        switch (h.opcode) {
        default: break;
        case ASSIGN:
            freeCondExpr(h.oper.asgn.lhs);
            freeCondExpr(h.oper.asgn.rhs);
            break;
        case POP:
        case PUSH:
        case JCOND:
            freeCondExpr(h.oper.exp);
            break;
        }
    }
}
