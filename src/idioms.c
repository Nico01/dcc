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

// dcc project machine idiom recognition

#include "dcc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// JmpInst - Returns TRUE if opcode is a conditional or unconditional jump
bool JmpInst(llIcode opcode)
{
    switch (opcode) {
    case iJMP:
    case iJMPF:
    case iJCXZ:
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
        return true;
    default:
        break;
    }

    return false;
}

/*
 checkStkVars - Checks for PUSH SI
                           [PUSH DI]
                 or         PUSH DI
                           [PUSH SI]
 In which case, the stack variable flags are set
*/
static int checkStkVars(PICODE pIcode, PICODE pEnd, PPROC pProc)
{
    // Look for PUSH SI
    if (pIcode < pEnd) {
        if ((pIcode->ll.opcode == iPUSH) && (pIcode->ll.dst.regi == rSI)) {
            pProc->flg |= SI_REGVAR;

            // Look for PUSH DI
            if (++pIcode < pEnd && (pIcode->ll.opcode == iPUSH) &&
                (pIcode->ll.dst.regi == rDI)) {
                pProc->flg |= DI_REGVAR;
                return 2;
            } else
                return 1;
        } else if ((pIcode->ll.opcode == iPUSH) && (pIcode->ll.dst.regi == rDI)) {
            pProc->flg |= DI_REGVAR;

            // Look for PUSH SI
            if ((++pIcode < pEnd) && (pIcode->ll.opcode == iPUSH) &&
                (pIcode->ll.dst.regi == rSI)) {
                pProc->flg |= SI_REGVAR;
                return 2;
            } else
                return 1;
        }
    } else
        return 0;

    return 0;
}

/*
 idiom1 - HLL procedure prologue;  Returns number of instructions matched.
          PUSH BP         ==> ENTER immed, 0
          MOV  BP, SP     and sets PROC_HLL flag
          [SUB  SP, immed]
          [PUSH SI]
          [PUSH DI]
        - Second version: Push stack variables and then save BP
          PUSH BP
          PUSH SI
          [PUSH DI]
          MOV BP, SP
        - Third version: Stack variables
          [PUSH SI]
          [PUSH DI]
*/
static int idiom1(PICODE pIcode, PICODE pEnd, PPROC pProc)
{
    // PUSH BP as first instruction of procedure
    if (!(pIcode->ll.flg & I) && pIcode->ll.src.regi == rBP) {
        // MOV BP, SP as next instruction
        if (++pIcode < pEnd && !(pIcode->ll.flg & (I | TARGET | CASE)) &&
            pIcode->ll.opcode == iMOV && pIcode->ll.dst.regi == rBP &&
            pIcode->ll.src.regi == rSP) {
            pProc->args.minOff = 2;
            pProc->flg |= PROC_IS_HLL;

            // Look for SUB SP, immed
            if ((++pIcode < pEnd) && (pIcode->ll.flg & (I | TARGET | CASE)) == I &&
                pIcode->ll.opcode == iSUB && pIcode->ll.dst.regi == rSP) {
                return (3 + checkStkVars(++pIcode, pEnd, pProc));
            } else
                return (2 + checkStkVars(pIcode, pEnd, pProc));
        }

        /* PUSH SI
           [PUSH DI]
           MOV BP, SP */
        else {
            int n = checkStkVars(pIcode, pEnd, pProc);
            if (n > 0) { // Look for MOV BP, SP
                pIcode += n;
                if (pIcode < pEnd && !(pIcode->ll.flg & (I | TARGET | CASE)) &&
                    pIcode->ll.opcode == iMOV && pIcode->ll.dst.regi == rBP &&
                    pIcode->ll.src.regi == rSP) {
                    pProc->args.minOff = 2 + (n * 2);
                    return (2 + n);
                }
            }
        }
    }
    return checkStkVars(pIcode, pEnd, pProc);
}

/*
 popStkVars - checks for
          [POP DI]
          [POP SI]
      or  [POP SI]
          [POP DI]
*/
static void popStkVars(PICODE pIcode, PICODE pEnd, PPROC pProc)
{
    // Match [POP DI]
    if (pIcode->ll.opcode == iPOP) {
        if ((pProc->flg & DI_REGVAR) && (pIcode->ll.dst.regi == rDI))
            invalidateIcode(pIcode);
        else if ((pProc->flg & SI_REGVAR) && (pIcode->ll.dst.regi == rSI))
            invalidateIcode(pIcode);
    }

    // Match [POP SI]
    if ((pIcode + 1)->ll.opcode == iPOP) {
        if ((pProc->flg & SI_REGVAR) && ((pIcode + 1)->ll.dst.regi == rSI))
            invalidateIcode(pIcode + 1);
        else if ((pProc->flg & DI_REGVAR) && ((pIcode + 1)->ll.dst.regi == rDI))
            invalidateIcode(pIcode + 1);
    }
}

/*
 idiom2 - HLL procedure epilogue;  Returns number of instructions matched.
          [POP DI]
          [POP SI]
          MOV  SP, BP
          POP  BP
          RET(F)
*/
static int idiom2(PICODE pIcode, PICODE pEnd, int ip, PPROC pProc)
{
    PICODE nicode;

    // Match MOV SP, BP
    if (ip != 0 && ((pIcode->ll.flg & I) != I) && pIcode->ll.dst.regi == rSP &&
        pIcode->ll.src.regi == rBP) {
        // Get next icode, skip over holes in the icode array
        nicode = pIcode + 1;
        while (nicode->ll.flg & NO_CODE)
            nicode++;

        // Match POP BP
        if (nicode < pEnd && !(nicode->ll.flg & (I | TARGET | CASE)) &&
            nicode->ll.opcode == iPOP && nicode->ll.dst.regi == rBP) {
            nicode++;

            // Match RET(F)
            if (nicode < pEnd && !(nicode->ll.flg & (I | TARGET | CASE)) &&
                (nicode->ll.opcode == iRET || nicode->ll.opcode == iRETF)) {
                popStkVars(pIcode - 2, pEnd, pProc);
                return 2;
            }
        }
    }
    return 0;
}

/*
 idiom3 - C calling convention.
          CALL(F)  proc_X
          ADD  SP, immed
      Eg: CALL proc_X
          ADD  SP, 6
          =>  pProc->cbParam = immed
 Special case: when the call is at the end of the procedure,
 sometimes the stack gets restored by a MOV sp, bp.
 Need to flag the procedure in these cases.
 Used by compilers to restore the stack when invoking a procedure using the C calling convention.
*/
static int idiom3(PICODE pIcode, PICODE pEnd)
{
    // Match ADD  SP, immed
    if ((++pIcode < pEnd) && (pIcode->ll.flg & I) && (pIcode->ll.opcode == iADD) &&
        (pIcode->ll.dst.regi == rSP))
        return (pIcode->ll.immed.op);
    else if ((pIcode->ll.opcode == iMOV) && (pIcode->ll.dst.regi == rSP) &&
             (pIcode->ll.src.regi == rBP))
        (pIcode - 1)->ll.flg |= REST_STK;

    return 0;
}

/*
 idiom 17 - C calling convention.
          CALL(F)  xxxx
          POP reg
          [POP reg]           reg in {AX, BX, CX, DX}
      Eg: CALL proc_X
          POP cx
          POP cx      (4 bytes of arguments)
          => pProc->cbParam = # pops * 2
 Found in Turbo C when restoring the stack for a procedure that uses the C calling convention.
 Used to restore the stack of 2 or 4 bytes args.
*/
static int idiom17(PICODE pIcode, PICODE pEnd)
{
    int i = 0; // Count on # pops

    // Match POP reg
    if ((++pIcode < pEnd) && (pIcode->ll.opcode == iPOP)) {
        uint8_t regi = pIcode->ll.dst.regi;
        if ((regi >= rAX) && (regi <= rBX))
            i++;
        while ((++pIcode)->ll.opcode == iPOP) {
            if (pIcode->ll.dst.regi == regi)
                i++;
            else
                break;
        }
        return i * 2;
    }
    return 0;
}

/*
 idiom4 - Pascal calling convention.
          RET(F) immed
          ==> pProc->cbParam = immed
              sets CALL_PASCAL flag
        - Second version: check for optional pop of stack vars
          [POP DI]
          [POP SI]
          POP BP
          RET(F) [immed]
        - Third version: pop stack vars
          [POP DI]
          [POP SI]
          RET(F) [immed]
*/
static void idiom4(PICODE pIcode, PICODE pEnd, PPROC pProc)
{
    /* Check for [POP DI]
                 [POP SI] */
    popStkVars(pIcode - 3, pEnd, pProc);

    // Check for POP BP
    if (((pIcode - 1)->ll.opcode == iPOP) && (((pIcode - 1)->ll.flg & I) != I) &&
        ((pIcode - 1)->ll.dst.regi == rBP))
        invalidateIcode(pIcode - 1);
    else
        popStkVars(pIcode - 2, pEnd, pProc);

    // Check for RET(F) immed
    if (pIcode->ll.flg & I) {
        pProc->cbParam = (int16_t)pIcode->ll.immed.op;
        pProc->flg |= CALL_PASCAL;
    }
}

/*
 idiom5 - Long addition.
      ADD reg/stackOff, reg/stackOff
      ADC reg/stackOff, reg/stackOff
      Eg:     ADD ax, [bp-4]
              ADC dx, [bp-2]
          =>  dx:ax = dx:ax + [bp-2]:[bp-4]
      Found in Borland Turbo C code.
      Commonly used idiom for long addition.
*/
static bool idiom5(PICODE pIcode, PICODE pEnd)
{
    if (pIcode < pEnd)
        if ((pIcode + 1)->ll.opcode == iADC)
            return true;

    return false;
}

/*
 idiom6 - Long substraction.
      SUB reg/stackOff, reg/stackOff
      SBB reg/stackOff, reg/stackOff
      Eg:     SUB ax, [bp-4]
              SBB dx, [bp-2]
          =>  dx:ax = dx:ax - [bp-2]:[bp-4]
      Found in Borland Turbo C code.
      Commonly used idiom for long substraction.
*/
static bool idiom6(PICODE pIcode, PICODE pEnd)
{
    if (pIcode < pEnd)
        if ((pIcode + 1)->ll.opcode == iSBB)
            return true;

    return false;
}

/*
 idiom7 - Assign zero
      XOR reg/stackOff, reg/stackOff
      Eg:     XOR ax, ax
          =>  ax = 0
      Found in Borland Turbo C and Microsoft C code.
*/
static bool idiom7(PICODE pIcode)
{
    PMEM dst = &pIcode->ll.dst;
    PMEM src = &pIcode->ll.src;

    if (dst->regi == 0) { // global variable
        if ((dst->segValue == src->segValue) && (dst->off == src->off))
            return true;
    } else if (dst->regi < INDEXBASE) { // register
        if (dst->regi == src->regi)
            return true;
    } else if ((dst->off) && (dst->seg == rSS) && (dst->regi == INDEXBASE + 6)) { // offset from BP
        if ((dst->off == src->off) && (dst->seg == src->seg) && (dst->regi == src->regi))
            return true;
    }

    return false;
}

/*
 idiom21 - Assign long kte with high part zero
      XOR regH, regH
      MOV regL, kte
      => regH:regL = kte
  Eg: XOR dx, dx
      MOV ax, 3
      => dx:ax = 3
 Note: only the following valid combinations are available:
      dx:ax
      cx:bx
 Found in Borland Turbo C code.
*/
static bool idiom21(PICODE picode, PICODE pend)
{
    PMEM dst = &picode->ll.dst;
    PMEM src = &picode->ll.src;

    if (((picode + 1) < pend) && ((picode + 1)->ll.flg & I)) {
        if ((dst->regi == src->regi) && (dst->regi > 0) && (dst->regi < INDEXBASE)) {
            if ((dst->regi == rDX) && ((picode + 1)->ll.dst.regi == rAX))
                return true;
            if ((dst->regi == rCX) && ((picode + 1)->ll.dst.regi == rBX))
                return true;
        }
    }
    return false;
}

/*
 idiom8 - Shift right by 1 (signed long ops)
      SAR reg, 1
      RCR reg, 1
  Eg: SAR dx, 1
      RCR ax, 1
      =>  dx:ax = dx:ax >> 1 (dx:ax are signed long)
 Found in Microsoft C code for long signed variable shift right.
*/
static bool idiom8(PICODE pIcode, PICODE pEnd)
{
    if (pIcode < pEnd) {
        if (((pIcode->ll.flg & I) == I) && (pIcode->ll.immed.op == 1))
            if (((pIcode + 1)->ll.opcode == iRCR) && (((pIcode + 1)->ll.flg & I) == I) &&
                ((pIcode + 1)->ll.immed.op == 1))
                return true;
    }
    return false;
}

/*
 idiom 15 - Shift left by n
       SHL reg, 1
       SHL reg, 1
       [...]
       [SHL    reg, 1]
 Eg:   SHL ax, 1
       SHL ax, 1
       =>  ax = ax << 2
 Found in Borland Turbo C code to index an array (array multiplication)
*/
static int idiom15(PICODE picode, PICODE pend)
{
    int n = 1;

    if (picode < pend) {
        // Match SHL reg, 1
        if ((picode->ll.flg & I) && (picode->ll.immed.op == 1)) {
            uint8_t regi = picode->ll.dst.regi;
            while (1) {
                if (((picode + n) < pend) && ((picode + n)->ll.opcode == iSHL) &&
                    ((picode + n)->ll.flg & I) && ((picode + n)->ll.immed.op == 1) &&
                    ((picode + n)->ll.dst.regi == regi))
                    n++;
                else
                    break;
            }
        }
    }

    if (n > 1)
        return n;
    else
        return 0;
}

/*
 idiom12 - Shift left long by 1
      SHL reg, 1
      RCL reg, 1
 Eg:  SHL ax, 1
      RCL dx, 1
      =>  dx:ax = dx:ax << 1
 Found in Borland Turbo C code for long variable shift left.
*/
static bool idiom12(PICODE pIcode, PICODE pEnd)
{
    if (pIcode < pEnd) {
        if (((pIcode->ll.flg & I) == I) && (pIcode->ll.immed.op == 1))
            if (((pIcode + 1)->ll.opcode == iRCL) && (((pIcode + 1)->ll.flg & I) == I) &&
                ((pIcode + 1)->ll.immed.op == 1))
                return true;
    }
    return false;
}

/*
 idiom9 - Shift right by 1 (unsigned long ops)
      SHR reg, 1
      RCR reg, 1
 Eg:  SHR dx, 1
      RCR ax, 1
      =>  dx:ax = dx:ax >> 1   (dx:ax are unsigned long)
 Found in Microsoft C code for long unsigned variable shift right.
*/
static bool idiom9(PICODE pIcode, PICODE pEnd)
{
    if (pIcode < pEnd) {
        if (((pIcode->ll.flg & I) == I) && (pIcode->ll.immed.op == 1))
            if (((pIcode + 1)->ll.opcode == iRCR) && (((pIcode + 1)->ll.flg & I) == I) &&
                ((pIcode + 1)->ll.immed.op == 1))
                return true;
    }
    return false;
}

/*
 idiom10 - Jump if not equal to 0
      OR  reg, reg
      JNE labX
 Eg:  OR  ax, ax
      JNE labX
      => CMP reg 0
      JNE labX

 This instruction is NOT converted into the equivalent high-level instruction "JCOND(reg != 0) labX"
 because we do not know yet if it forms part of a long register conditional test.
 It is therefore modified to simplify the analysis.
 Found in Borland Turbo C.
*/
static void idiom10(PICODE pIcode, PICODE pEnd)
{
    if (pIcode < pEnd) {
        // Check OR reg, reg
        if (((pIcode->ll.flg & I) != I) && (pIcode->ll.src.regi > 0) &&
            (pIcode->ll.src.regi < INDEXBASE) &&
            (pIcode->ll.src.regi == pIcode->ll.dst.regi))
            if (((pIcode + 1) < pEnd) && ((pIcode + 1)->ll.opcode == iJNE)) {
                pIcode->ll.opcode = iCMP;
                pIcode->ll.flg |= I;
                pIcode->ll.immed.op = 0;
                pIcode->du.def = 0;
                pIcode->du1.numRegsDef = 0;
            }
    }
}

/*
 idiom 13 - Word assign
       MOV regL, mem
       MOV regH, 0
 Eg:   MOV al, [bp-2]
       MOV ah, 0
       => MOV ax, [bp-2]
 Found in Borland Turbo C, used for multiplication and division of byte operands
 (ie. they need to be extended to words).
*/
static uint8_t idiom13(PICODE picode, PICODE pend)
{
    if (picode < pend) {
        // Check for regL
        uint8_t regi = picode->ll.dst.regi;
        if (((picode->ll.flg & I) != I) && (regi >= rAL) && (regi <= rBH)) {
            // Check for MOV regH, 0
            if (((picode + 1)->ll.opcode == iMOV) && ((picode + 1)->ll.flg & I) &&
                ((picode + 1)->ll.immed.op == 0)) {
                if ((picode + 1)->ll.dst.regi == (regi + 4))
                    return (regi - rAL + rAX);
            }
        }
    }
    return 0;
}

/*
 idiom 14 - Long word assign
       MOV regL, mem/reg
       XOR regH, regH
 Eg:   MOV ax, di
       XOR dx, dx
       => MOV dx:ax, di
 Note: only the following combinations are allowed:
       dx:ax
       cx:bx
 this is to remove the possibility of making errors in situations like this:
       MOV dx, offH
       MOV ax, offL
       XOR cx, cx
 Found in Borland Turbo C, used for division of unsigned integer operands.
*/
static bool idiom14(PICODE picode, PICODE pend, uint8_t *regL, uint8_t *regH)
{
    if (picode < pend) {
        // Check for regL
        *regL = picode->ll.dst.regi;
        if (((picode->ll.flg & I) != I) && ((*regL == rAX) || (*regL == rBX))) {
            // Check for XOR regH, regH
            if (((picode + 1)->ll.opcode == iXOR) && (((picode + 1)->ll.flg & I) != I)) {
                *regH = (picode + 1)->ll.dst.regi;
                if (*regH == (picode + 1)->ll.src.regi) {
                    if ((*regL == rAX) && (*regH == rDX))
                        return true;
                    if ((*regL == rBX) && (*regH == rCX))
                        return true;
                }
            }
        }
    }
    return false;
}

/*
 idiom11 - Negate long integer
      NEG regH
      NEG regL
      SBB regH, 0
 Eg:  NEG dx
      NEG ax
      SBB dx, 0
      => dx:ax = - dx:ax
 Found in Borland Turbo C.
*/
static bool idiom11(PICODE pIcode, PICODE pEnd)
{
    condId type; // type of argument

    if ((pIcode + 2) < pEnd) {
        type = idType(pIcode, DST);
        if ((type != CONST) && (type != OTHER)) {
            /* Check NEG reg/mem
                     SBB reg/mem, 0 */
            if (((pIcode + 1)->ll.opcode == iNEG) && ((pIcode + 2)->ll.opcode == iSBB))
                switch (type) {
                default:
                    break;
                case GLOB_VAR:
                    if (((pIcode + 2)->ll.dst.segValue == pIcode->ll.dst.segValue) &&
                        ((pIcode + 2)->ll.dst.off == pIcode->ll.dst.off))
                        return true;
                    break;
                case REGISTER:
                    if ((pIcode + 2)->ll.dst.regi == pIcode->ll.dst.regi)
                        return true;
                    break;
                case PARAM:
                case LOCAL_VAR:
                    if ((pIcode + 2)->ll.dst.off == pIcode->ll.dst.off)
                        return true;
                    break;
                }
        }
    }
    return true;
}

/*
 idiom 16: Bitwise negation
       NEG reg
       SBB reg, reg
       INC reg
       => ASGN reg, !reg
 Eg:   NEG ax
       SBB ax, ax
       INC ax
       => ax = !ax
 Found in Borland Turbo C when negating bitwise.
*/
static bool idiom16(PICODE picode, PICODE pend)
{
    if ((picode + 2) < pend) {
        uint8_t regi = picode->ll.dst.regi;
        if ((regi >= rAX) && (regi < INDEXBASE)) {
            if (((picode + 1)->ll.opcode == iSBB) && ((picode + 2)->ll.opcode == iINC))
                if (((picode + 1)->ll.dst.regi == ((picode + 1)->ll.src.regi)) &&
                    ((picode + 1)->ll.dst.regi == regi) &&
                    ((picode + 2)->ll.dst.regi == regi))
                    return true;
        }
    }
    return false;
}

/*
 idiom 18: Post-increment or post-decrement in a conditional jump
       MOV  reg, var (including register variables)
       INC  var  or DEC var
       CMP  var, Y
       JX   label
       => JCOND (var++ X Y)
 Eg:   MOV  ax, si
       INC  si
       CMP  ax, 8
       JL   labX
       =>  JCOND (si++ < 8)
 Found in Borland Turbo C. Intrinsic to C languages.
*/
static bool idiom18(PICODE picode, PICODE pend, PPROC pproc)
{
    uint8_t type = 0; // type of variable: 1 = reg-var, 2 = local
    uint8_t regi;     // register of the MOV

    // Get variable
    if (picode->ll.dst.regi < INDEXBASE) { // register
        if ((picode->ll.dst.regi == rSI) && (pproc->flg & SI_REGVAR))
            type = 1;
        else if ((picode->ll.dst.regi == rDI) && (pproc->flg & DI_REGVAR))
            type = 1;
    } else if (picode->ll.dst.off) // local variable
        type = 2;

    // Check previous instruction for a MOV
    if (type == 1) { // register variable
        if (((picode - 1)->ll.opcode == iMOV) &&
            ((picode - 1)->ll.src.regi == picode->ll.dst.regi)) {
            regi = (picode - 1)->ll.dst.regi;
            if ((regi > 0) && (regi < INDEXBASE)) {
                if ((picode < pend) && ((picode + 1) < pend) &&
                    ((picode + 1)->ll.opcode == iCMP) &&
                    ((picode + 1)->ll.dst.regi == regi) &&
                    (((picode + 2)->ll.opcode >= iJB) && ((picode + 2)->ll.opcode < iJCXZ)))
                    return true;
            }
        }
    } else if (type == 2) { // local
        if (((picode - 1)->ll.opcode == iMOV) &&
            ((picode - 1)->ll.src.off == picode->ll.dst.off)) {
            regi = (picode - 1)->ll.dst.regi;
            if ((regi > 0) && (regi < INDEXBASE)) {
                if ((picode < pend) && ((picode + 1) < pend) &&
                    ((picode + 1)->ll.opcode == iCMP) &&
                    ((picode + 1)->ll.dst.regi == regi) &&
                    (((picode + 2)->ll.opcode >= iJB) && ((picode + 2)->ll.opcode < iJCXZ)))
                    return true;
            }
        }
    }
    return false;
}

/*
 idiom 19: pre-increment or pre-decrement in conditional jump, comparing against 0.
       INC var or DEC var (including register vars)
       JX  lab
       =>  JCOND (++var X 0) or JCOND (--var X 0)
 Eg:   INC [bp+4]
       JG  lab2
       => JCOND (++[bp+4] > 0)
 Found in Borland Turbo C. Intrinsic to C language.
*/
static bool idiom19(PICODE picode, PICODE pend, PPROC pproc)
{
    if (picode->ll.dst.regi < INDEXBASE) { // register
        if (((picode->ll.dst.regi == rSI) && (pproc->flg & SI_REGVAR)) ||
            ((picode->ll.dst.regi == rDI) && (pproc->flg & DI_REGVAR)))
            if ((picode < pend) && ((picode + 1)->ll.opcode >= iJB) &&
                ((picode + 1)->ll.opcode < iJCXZ))
                return true;
    } else if (picode->ll.dst.off) { // stack variable
        if ((picode < pend) && ((picode + 1)->ll.opcode >= iJB) &&
            ((picode + 1)->ll.opcode < iJCXZ))
            return true;
    }
    return false;
}

/*
 idiom20: Pre increment/decrement in conditional expression (compares against a register,
 variable or constant different than 0).
      INC var or DEC var (including register vars)
      MOV reg, var
      CMP reg, Y
      JX  lab
      => JCOND (++var X Y) or JCOND (--var X Y)
 Eg:  INC si (si is a register variable)
      MOV ax, si
      CMP ax, 2
      JL  lab4
      => JCOND (++si < 2)
 Found in Turbo C.  Intrinsic to C language.
 ****************************************************************************/
static bool idiom20(PICODE picode, PICODE pend, PPROC pproc)
{
    uint8_t type = 0; // type of variable: 1 = reg-var, 2 = local
    uint8_t regi;     // register of the MOV

    // Get variable
    if (picode->ll.dst.regi < INDEXBASE) { // register
        if ((picode->ll.dst.regi == rSI) && (pproc->flg & SI_REGVAR))
            type = 1;
        else if ((picode->ll.dst.regi == rDI) && (pproc->flg & DI_REGVAR))
            type = 1;
    }
    else if (picode->ll.dst.off) // local variable
        type = 2;

    // Check previous instruction for a MOV
    if (type == 1) { // register variable
        if ((picode < pend) && ((picode + 1)->ll.opcode == iMOV) &&
            ((picode + 1)->ll.src.regi == picode->ll.dst.regi)) {
            regi = (picode + 1)->ll.dst.regi;
            if ((regi > 0) && (regi < INDEXBASE)) {
                if (((picode + 1) < pend) && ((picode + 2) < pend) &&
                    ((picode + 2)->ll.opcode == iCMP) &&
                    ((picode + 2)->ll.dst.regi == regi) &&
                    (((picode + 3)->ll.opcode >= iJB) && ((picode + 3)->ll.opcode < iJCXZ)))
                    return true;
            }
        }
    }
    else if (type == 2) { // local
        if ((picode < pend) && ((picode + 1)->ll.opcode == iMOV) &&
            ((picode + 1)->ll.src.off == picode->ll.dst.off)) {
            regi = (picode + 1)->ll.dst.regi;
            if ((regi > 0) && (regi < INDEXBASE)) {
                if (((picode + 1) < pend) && ((picode + 2) < pend) &&
                    ((picode + 2)->ll.opcode == iCMP) &&
                    ((picode + 2)->ll.dst.regi == regi) &&
                    (((picode + 3)->ll.opcode >= iJB) && ((picode + 3)->ll.opcode < iJCXZ)))
                    return true;
            }
        }
    }
    return false;
}

// findIdioms  - translates LOW_LEVEL icode idioms into HIGH_LEVEL icodes.
void findIdioms(PPROC pProc)
{
    int ip;               // Index to current icode
    PICODE pEnd, pIcode;  // Pointers to end of BB and current icodes
    COND_EXPR *rhs, *lhs; // Pointers to left and right hand side exps
    COND_EXPR *exp;       // Pointer to temporal expression
    int idx;              // Index into local identifier table
    uint8_t regH, regL;   // High and low registers for long word reg

    pIcode = pProc->Icode.icode;
    pEnd = pIcode + pProc->Icode.numIcode;
    ip = 0;

    while (pIcode < pEnd) {
        switch (pIcode->ll.opcode) {
        case iDEC:
        case iINC:
            if (idiom18(pIcode, pEnd, pProc)) {
                lhs = idCondExp(pIcode - 1, SRC, pProc, ip, pIcode, USE);
                if (pIcode->ll.opcode == iDEC)
                    lhs = unaryCondExp(POST_DEC, lhs);
                else
                    lhs = unaryCondExp(POST_INC, lhs);
                rhs = idCondExp(pIcode + 1, SRC, pProc, ip, pIcode + 2, USE);
                exp = boolCondExp(lhs, rhs, condOpJCond[(pIcode + 2)->ll.opcode - iJB]);
                newJCondHlIcode(pIcode + 2, exp);
                invalidateIcode(pIcode - 1);
                invalidateIcode(pIcode);
                invalidateIcode(pIcode + 1);
                pIcode += 3;
                ip += 2;
            } else if (idiom19(pIcode, pEnd, pProc)) {
                lhs = idCondExp(pIcode, DST, pProc, ip, pIcode + 1, USE);
                if (pIcode->ll.opcode == iDEC)
                    lhs = unaryCondExp(PRE_DEC, lhs);
                else
                    lhs = unaryCondExp(PRE_INC, lhs);
                rhs = idCondExpKte(0, 2);
                exp = boolCondExp(lhs, rhs, condOpJCond[(pIcode + 1)->ll.opcode - iJB]);
                newJCondHlIcode(pIcode + 1, exp);
                invalidateIcode(pIcode);
                pIcode += 2;
                ip++;
            } else if (idiom20(pIcode, pEnd, pProc)) {
                lhs = idCondExp(pIcode + 1, SRC, pProc, ip, pIcode, USE);
                if (pIcode->ll.opcode == iDEC)
                    lhs = unaryCondExp(PRE_DEC, lhs);
                else
                    lhs = unaryCondExp(PRE_INC, lhs);
                rhs = idCondExp(pIcode + 2, SRC, pProc, ip, pIcode + 3, USE);
                exp = boolCondExp(lhs, rhs, condOpJCond[(pIcode + 3)->ll.opcode - iJB]);
                newJCondHlIcode(pIcode + 3, exp);
                invalidateIcode(pIcode);
                invalidateIcode(pIcode + 1);
                invalidateIcode(pIcode + 2);
                pIcode += 3;
                ip += 2;
            } else
                pIcode++;
            break;

        case iPUSH: // Idiom 1
            if ((!(pProc->flg & PROC_HLL)) && (idx = idiom1(pIcode, pEnd, pProc))) {
                pProc->flg |= PROC_HLL;
                for (; idx > 0; idx--) {
                    invalidateIcode(pIcode++);
                    ip++;
                }
                ip--;
            } else
                pIcode++;
            break;

        case iMOV: // Idiom 2
            if ((idx = idiom2(pIcode, pEnd, ip, pProc))) {
                invalidateIcode(pIcode);
                invalidateIcode(pIcode + 1);
                pIcode += 3;
                ip += 2;
            } else if (idiom14(pIcode, pEnd, &regL, &regH)) // Idiom 14
            {
                idx = newLongRegId(&pProc->localId, TYPE_LONG_SIGN, regH, regL, ip);
                lhs = idCondExpLongIdx(idx);
                setRegDU(pIcode, regH, DEF);
                rhs = idCondExp(pIcode, SRC, pProc, ip, pIcode, NONE);
                newAsgnHlIcode(pIcode, lhs, rhs);
                invalidateIcode(pIcode + 1);
                pIcode += 2;
                ip++;
            } else if ((idx = idiom13(pIcode, pEnd))) // Idiom 13
            {
                lhs = idCondExpReg(idx, 0, &pProc->localId);
                setRegDU(pIcode, idx, DEF);
                pIcode->du1.numRegsDef--; // prev byte reg def
                rhs = idCondExp(pIcode, SRC, pProc, ip, pIcode, NONE);
                newAsgnHlIcode(pIcode, lhs, rhs);
                invalidateIcode(pIcode + 1);
                pIcode += 2;
                ip++;
            } else
                pIcode++;
            break;

        case iCALL:
        case iCALLF:
            /* Check for library functions that return a long register.
               Propagate this result */
            if ((pIcode->ll.immed.proc.proc->flg & PROC_ISLIB) &&
                (pIcode->ll.immed.proc.proc->flg & PROC_IS_FUNC)) {
                if ((pIcode->ll.immed.proc.proc->retVal.type == TYPE_LONG_SIGN) ||
                    (pIcode->ll.immed.proc.proc->retVal.type == TYPE_LONG_UNSIGN))
                    newLongRegId(&pProc->localId, TYPE_LONG_SIGN, rDX, rAX, ip);
            }

            // Check for idioms
            if ((idx = idiom3(pIcode, pEnd))) // idiom 3
            {
                if (pIcode->ll.flg & I) {
                    (pIcode->ll.immed.proc.proc)->cbParam = (int16_t)idx;
                    pIcode->ll.immed.proc.cb = idx;
                    (pIcode->ll.immed.proc.proc)->flg |= CALL_C;
                    pIcode++;
                    invalidateIcode(pIcode++);
                    ip++;
                }
            } else if ((idx = idiom17(pIcode, pEnd))) // idiom 17
            {
                if (pIcode->ll.flg & I) {
                    (pIcode->ll.immed.proc.proc)->cbParam = (int16_t)idx;
                    pIcode->ll.immed.proc.cb = idx;
                    (pIcode->ll.immed.proc.proc)->flg |= CALL_C;
                    ip += idx / 2 - 1;
                    pIcode++;
                    for (idx /= 2; idx > 0; idx--)
                        invalidateIcode(pIcode++);
                }
            } else
                pIcode++;
            break;

        case iRET: // Idiom 4
        case iRETF:
            idiom4(pIcode, pEnd, pProc);
            pIcode++;
            break;

        case iADD: // Idiom 5
            if (idiom5(pIcode, pEnd)) {
                lhs = idCondExpLong(&pProc->localId, DST, pIcode, LOW_FIRST, ip, USE_DEF, 1);
                rhs = idCondExpLong(&pProc->localId, SRC, pIcode, LOW_FIRST, ip, USE, 1);
                exp = boolCondExp(lhs, rhs, ADD);
                newAsgnHlIcode(pIcode, lhs, exp);
                invalidateIcode(pIcode + 1);
                pIcode++;
                ip++;
            }
            pIcode++;
            break;

        case iSAR: // Idiom 8
            if (idiom8(pIcode, pEnd)) {
                idx = newLongRegId(&pProc->localId, TYPE_LONG_SIGN, pIcode->ll.dst.regi,
                                   (pIcode + 1)->ll.dst.regi, ip);
                lhs = idCondExpLongIdx(idx);
                setRegDU(pIcode, (pIcode + 1)->ll.dst.regi, USE_DEF);
                rhs = idCondExpKte(1, 2);
                exp = boolCondExp(lhs, rhs, SHR);
                newAsgnHlIcode(pIcode, lhs, exp);
                invalidateIcode(pIcode + 1);
                pIcode++;
                ip++;
            }
            pIcode++;
            break;

        case iSHL:
            if ((idx = idiom15(pIcode, pEnd))) // idiom 15
            {
                lhs = idCondExpReg(pIcode->ll.dst.regi, pIcode->ll.flg & NO_SRC_B,
                                   &pProc->localId);
                rhs = idCondExpKte(idx, 2);
                exp = boolCondExp(lhs, rhs, SHL);
                newAsgnHlIcode(pIcode, lhs, exp);
                pIcode++;
                for (idx--; idx > 0; idx--) {
                    invalidateIcode(pIcode++);
                    ip++;
                }
            } else if (idiom12(pIcode, pEnd)) // idiom 12
            {
                idx = newLongRegId(&pProc->localId, TYPE_LONG_UNSIGN, (pIcode + 1)->ll.dst.regi,
                                   pIcode->ll.dst.regi, ip);
                lhs = idCondExpLongIdx(idx);
                setRegDU(pIcode, (pIcode + 1)->ll.dst.regi, USE_DEF);
                rhs = idCondExpKte(1, 2);
                exp = boolCondExp(lhs, rhs, SHL);
                newAsgnHlIcode(pIcode, lhs, exp);
                invalidateIcode(pIcode + 1);
                pIcode += 2;
                ip++;
            } else
                pIcode++;
            break;

        case iSHR: // Idiom 9
            if (idiom9(pIcode, pEnd)) {
                idx = newLongRegId(&pProc->localId, TYPE_LONG_UNSIGN, pIcode->ll.dst.regi,
                                   (pIcode + 1)->ll.dst.regi, ip);
                lhs = idCondExpLongIdx(idx);
                setRegDU(pIcode, (pIcode + 1)->ll.dst.regi, USE_DEF);
                rhs = idCondExpKte(1, 2);
                exp = boolCondExp(lhs, rhs, SHR);
                newAsgnHlIcode(pIcode, lhs, exp);
                invalidateIcode(pIcode + 1);
                pIcode++;
                ip++;
            }
            pIcode++;
            break;

        case iSUB: // Idiom 6
            if (idiom6(pIcode, pEnd)) {
                lhs = idCondExpLong(&pProc->localId, DST, pIcode, LOW_FIRST, ip, USE_DEF, 1);
                rhs = idCondExpLong(&pProc->localId, SRC, pIcode, LOW_FIRST, ip, USE, 1);
                exp = boolCondExp(lhs, rhs, SUB);
                newAsgnHlIcode(pIcode, lhs, exp);
                invalidateIcode(pIcode + 1);
                pIcode++;
                ip++;
            }
            pIcode++;
            break;

        case iOR: // Idiom 10
            idiom10(pIcode, pEnd);
            pIcode++;
            break;

        case iNEG: // Idiom 11
            if (idiom11(pIcode, pEnd)) {
                lhs = idCondExpLong(&pProc->localId, DST, pIcode, HIGH_FIRST, ip, USE_DEF, 1);
                rhs = unaryCondExp(NEGATION, lhs);
                newAsgnHlIcode(pIcode, lhs, rhs);
                invalidateIcode(pIcode + 1);
                invalidateIcode(pIcode + 2);
                pIcode += 3;
                ip += 2;
            } else if (idiom16(pIcode, pEnd)) {
                lhs = idCondExpReg(pIcode->ll.dst.regi, pIcode->ll.flg, &pProc->localId);
                rhs = copyCondExp(lhs);
                rhs = unaryCondExp(NEGATION, lhs);
                newAsgnHlIcode(pIcode, lhs, rhs);
                invalidateIcode(pIcode + 1);
                invalidateIcode(pIcode + 2);
                pIcode += 3;
                ip += 2;
            } else
                pIcode++;
            break;

        case iNOP:
            invalidateIcode(pIcode++);
            break;

        case iENTER: // ENTER is equivalent to init PUSH bp
            if (ip == 0)
                pProc->flg |= (PROC_HLL | PROC_IS_HLL);
            pIcode++;
            break;

        case iXOR: // Idiom 7
            if (idiom21(pIcode, pEnd)) {
                lhs = idCondExpLong(&pProc->localId, DST, pIcode, HIGH_FIRST, ip, DEF, 1);
                rhs = idCondExpKte((pIcode + 1)->ll.immed.op, 4);
                newAsgnHlIcode(pIcode, lhs, rhs);
                pIcode->du.use = 0; // clear register used in iXOR
                invalidateIcode(pIcode + 1);
                pIcode++;
                ip++;
            } else if (idiom7(pIcode)) {
                lhs = idCondExp(pIcode, DST, pProc, ip, pIcode, NONE);
                rhs = idCondExpKte(0, 2);
                newAsgnHlIcode(pIcode, lhs, rhs);
                pIcode->du.use = 0; // clear register used in iXOR
                pIcode->ll.flg |= I;
            }
            pIcode++;
            break;

        default:
            pIcode++;
        }
        ip++;
    }

    // Check if number of parameter bytes match their calling convention
    if ((pProc->flg & PROC_HLL) && (pProc->args.csym)) {
        pProc->args.minOff += (pProc->flg & PROC_FAR ? 4 : 2);
        int16_t delta = pProc->args.maxOff - pProc->args.minOff;
        if (pProc->cbParam != delta) {
            pProc->cbParam = delta;
            pProc->flg |= (CALL_MASK & CALL_UNKNOWN);
        }
    }
}

/*
 Sets up the TARGET flag for jump target addresses, finds idioms,
 and binds jump target addresses to icode offsets.
*/
void bindIcodeOff(PPROC pProc)
{
    int j;       // idx into icode array
    uint32_t *p; // for case table

    if (!pProc->Icode.numIcode) // No Icode
        return;

    PICODE pIcode = pProc->Icode.icode;

    // Flag all jump targets for BB construction and disassembly stage 2
    for (int i = 0; i < pProc->Icode.numIcode; i++)
        if ((pIcode[i].ll.flg & I) && JmpInst(pIcode[i].ll.opcode)) {
            if (labelSrch(pIcode, pProc->Icode.numIcode, pIcode[i].ll.immed.op, &j))
                pIcode[j].ll.flg |= TARGET;
        }

    /* Finally bind jump targets to Icode offsets. Jumps for which no label is found
       (no code at dest. of jump) are simply left unlinked and flagged as going nowhere. */
    pIcode = pProc->Icode.icode;

    for (int i = 0; i < pProc->Icode.numIcode; i++)
        if (JmpInst(pIcode[i].ll.opcode)) {
            if (pIcode[i].ll.flg & I) {
                if (!labelSrch(pIcode, pProc->Icode.numIcode, pIcode[i].ll.immed.op,
                               (int *)&pIcode[i].ll.immed.op))
                    pIcode[i].ll.flg |= NO_LABEL;
            } else if (pIcode[i].ll.flg & SWITCH) {
                p = pIcode[i].ll.caseTbl.entries;
                for (j = 0; j < pIcode[i].ll.caseTbl.numEntries; j++, p++)
                    labelSrch(pIcode, pProc->Icode.numIcode, *p, (int *)p);
            }
        }
}

// Performs idioms analysis, and propagates long operands, if any
void lowLevelAnalysis(PPROC pProc)
{
    // Idiom analysis - sets up some flags and creates some HIGH_LEVEL icodes
    findIdioms(pProc);

    // Propagate HIGH_LEVEL idiom information for long operands
    propLong(pProc);
}
