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

// Back-end module. Generates C code for each procedure.

#include "dcc.h"
#include <stdio.h>
#include <string.h>

bundle cCode; // Procedure declaration and code

// Indentation buffer
#define indSize 81 /* size of the indentation buffer.
                      Each indentation is of 4 spaces => max. 20 indentation levels */

static char indentBuf[indSize] =
    "                                                                                ";

// Indentation according to the depth of the statement
static char *indent(int indLevel) { return (&indentBuf[indSize - (indLevel * 4) - 1]); }

// Returns a unique index to the next label @TODO WTF?
static int getNextLabel(void)
{
    static int labelIdx = 1; // index of the next label

    return labelIdx++;
}

/*
 Returns the corresponding C string for the given character c.
 Character constants such as carriage return and line feed, require 2 C characters.
*/
char *cChar(char c)
{
    static char res[3];

    switch (c) {
    case 0x08: // backspace
        sprintf(res, "\\b");
        break;
    case 0x09: // horizontal tab
        sprintf(res, "\\t");
        break;
    case 0x0A: // new line
        sprintf(res, "\\n");
        break;
    case 0x0C: // form feed
        sprintf(res, "\\f");
        break;
    case 0x0D: // carriage return
        sprintf(res, "\\r");
        break;
    default: // any other character
        sprintf(res, "%c", c);
    }
    return res;
}

// Writes the header information and global variables to the output C file fp.
static void writeHeader(FILE *fp, char *fileName)
{
    // Write header information
    newBundle(&cCode);
    appendStrTab(&cCode.decl, "/*\n");
    appendStrTab(&cCode.decl, " * Input file\t: %s\n", fileName);
//    appendStrTab(&cCode.decl, " * File type\t: %s\n", (prog.fCOM) ? "COM" : "EXE");
    appendStrTab(&cCode.decl, " * File type\t: EXE\n");
    appendStrTab(&cCode.decl, " */\n\n#include \"dcc.h\"\n\n");

    writeBundle(fp, cCode);
    freeBundle(&cCode);
}

// Writes the registers that are set in the bitvector
static void writeBitVector(uint32_t regi)
{
    for (int j = 0; j < INDEXBASE; j++) {
        if ((regi & power2(j)) != 0)
            printf("%s ", allRegs[j]);
    }
}

/*
 Checks the given icode to determine whether it has a label associated to it. If so, a goto is
 emitted to this label; otherwise, a new label is created and a goto is also emitted.
 Note: this procedure is to be used when the label is to be backpatched onto code in cCode.code
*/
static void emitGotoLabel(PICODE pt, int indLevel)
{
    if (!(pt->ic.ll.flg & HLL_LABEL)) { // node hasn't got a lab
        // Generate new label
        pt->ic.ll.hllLabNum = getNextLabel();
        pt->ic.ll.flg |= HLL_LABEL;

        // Node has been traversed already, so backpatch this label into the code
        addLabelBundle(&cCode.code, pt->codeIdx, pt->ic.ll.hllLabNum);
    }
    appendStrTab(&cCode.code, "%sgoto L%ld;\n", indent(indLevel), pt->ic.ll.hllLabNum);
}

/*
 Writes the code for the current basic block.
 @pBB  : pointer to the current basic block.
 @Icode: pointer to the array of icodes for current procedure.
 @lev  : indentation level - used for formatting.
*/
static void writeBB(PBB pBB, PICODE hli, int lev, PPROC pProc, int *numLoc)
{
    char *line = NULL; // Pointer to the HIGH-LEVEL line

    /* Save the index into the code table in case there is a later goto
       into this instruction (first instruction of the BB) */
    hli[pBB->start].codeIdx = nextBundleIdx(&cCode.code);

    // Generate code for each hlicode that is not a JCOND
    for (int i = pBB->start, last = i + pBB->length; i < last; i++)
        if ((hli[i].type == HIGH_LEVEL) && (hli[i].invalid == false)) {
            line = write1HlIcode(hli[i].ic.hl, pProc, numLoc);
            if (line[0] != '\0')
                appendStrTab(&cCode.code, "%s%s", indent(lev), line);
            if (option.verbose)
                writeDU(&hli[i], i);
        }
}

/*
 Recursive procedure that writes the code for the given procedure, pointed to by pBB.
 @pBB:      pointer to the cfg.
 @Icode:    pointer to the Icode array for the cfg graph of the current procedure.
 @indLevel: indentation level - used for formatting.
 @numLoc:   last # assigned to local variables
*/
static void writeCode(PBB pBB, int indLevel, PPROC pProc, int *numLoc, int latchNode, int ifFollow)
{
    int follow,      // ifFollow
        loopType,    // Type of loop, if any
        nodeType;    // Type of node
    PBB succ, latch; // Successor and latching node
    PICODE picode;   // Pointer to JCOND instruction
    char *l;         // Pointer to JCOND expression
    bool emptyThen,  // THEN clause is empty
         repCond;    // Repeat condition for while()

    // Check if this basic block should be analysed
    if (!pBB)
        return;

    if ((ifFollow != UN_INIT) && (pBB == pProc->dfsLast[ifFollow]))
        return;

    if (pBB->traversed == DFS_ALPHA)
        return;

    pBB->traversed = DFS_ALPHA;

    // Check for start of loop
    repCond = false;
    latch = NULL;
    loopType = pBB->loopType;

    if (loopType) {
        latch = pProc->dfsLast[pBB->latchNode];
        switch (loopType) {
        case WHILE_TYPE:
            picode = &pProc->Icode.icode[pBB->start + pBB->length - 1];

            // Check for error in while condition
            if (picode->ic.hl.opcode != JCOND)
                reportError(WHILE_FAIL);

            // Check if condition is more than 1 HL instruction
            if (pBB->numHlIcodes > 1) { // Write the code for this basic block
                writeBB(pBB, pProc->Icode.icode, indLevel, pProc, numLoc);
                repCond = true;
            }

            /* Condition needs to be inverted if the loop body is along
               the THEN path of the header node */
            if (pBB->edges[ELSE].BBptr->dfsLastNum == pBB->loopFollow)
                inverseCondOp(&picode->ic.hl.oper.exp);
            appendStrTab(&cCode.code, "\n%swhile (%s) {\n", indent(indLevel),
                         walkCondExpr(picode->ic.hl.oper.exp, pProc, numLoc));
            invalidateIcode(picode);
            break;

        case REPEAT_TYPE:
            appendStrTab(&cCode.code, "\n%sdo {\n", indent(indLevel));
            picode = &pProc->Icode.icode[latch->start + latch->length - 1];
            invalidateIcode(picode);
            break;

        case ENDLESS_TYPE:
            appendStrTab(&cCode.code, "\n%sfor (;;) {\n", indent(indLevel));
        }
        indLevel++;
    }

    // Write the code for this basic block
    if (repCond == false)
        writeBB(pBB, pProc->Icode.icode, indLevel, pProc, numLoc);

    // Check for end of path
    nodeType = pBB->nodeType;
    if (nodeType == RETURN_NODE || nodeType == TERMINATE_NODE || nodeType == NOWHERE_NODE ||
        (pBB->dfsLastNum == latchNode))
        return;

    /* Check type of loop/node and process code */
    if (loopType) { // there is a loop
        if (pBB != latch) { // loop is over several bbs
            if (loopType == WHILE_TYPE) {
                succ = pBB->edges[THEN].BBptr;
                if (succ->dfsLastNum == pBB->loopFollow)
                    succ = pBB->edges[ELSE].BBptr;
            } else
                succ = pBB->edges[0].BBptr;

            if (succ->traversed != DFS_ALPHA)
                writeCode(succ, indLevel, pProc, numLoc, latch->dfsLastNum, ifFollow);
            else // has been traversed so we need a goto
                emitGotoLabel(&pProc->Icode.icode[succ->start], indLevel);
        }

        // Loop epilogue: generate the loop trailer
        indLevel--;
        if (loopType == WHILE_TYPE) {
            /* Check if there is need to repeat other statements involved
               in while condition, then, emit the loop trailer */
            if (repCond)
                writeBB(pBB, pProc->Icode.icode, indLevel + 1, pProc, numLoc);
            appendStrTab(&cCode.code, "%s} /* end of while */\n", indent(indLevel));
        }
        else if (loopType == ENDLESS_TYPE)
            appendStrTab(&cCode.code, "%s} /* end of loop */\n", indent(indLevel));
        else if (loopType == REPEAT_TYPE) {
            if (picode->ic.hl.opcode != JCOND)
                reportError(REPEAT_FAIL);
            appendStrTab(&cCode.code, "%s} while (%s);\n", indent(indLevel),
                         walkCondExpr(picode->ic.hl.oper.exp, pProc, numLoc));
        }

        // Recurse on the loop follow
        if (pBB->loopFollow != MAX) {
            succ = pProc->dfsLast[pBB->loopFollow];
            if (succ->traversed != DFS_ALPHA)
                writeCode(succ, indLevel, pProc, numLoc, latchNode, ifFollow);
            else // has been traversed so we need a goto
                emitGotoLabel(&pProc->Icode.icode[succ->start], indLevel);
        }
    } else { // no loop, process nodeType of the graph
        if (nodeType == TWO_BRANCH) { // if-then[-else]
            indLevel++;
            emptyThen = false;

            if (pBB->ifFollow != MAX) { // there is a follow
                // process the THEN part
                follow = pBB->ifFollow;
                succ = pBB->edges[THEN].BBptr;
                if (succ->traversed != DFS_ALPHA) { // not visited
                    if (succ->dfsLastNum != follow) { // THEN part
                        l = writeJcond(pProc->Icode.icode[pBB->start + pBB->length - 1].ic.hl,
                                       pProc, numLoc);
                        appendStrTab(&cCode.code, "\n%s%s", indent(indLevel - 1), l);
                        writeCode(succ, indLevel, pProc, numLoc, latchNode, follow);
                    } else { // empty THEN part => negate ELSE part
                        l = writeJcondInv(pProc->Icode.icode[pBB->start + pBB->length - 1].ic.hl,
                                          pProc, numLoc);
                        appendStrTab(&cCode.code, "\n%s%s", indent(indLevel - 1), l);
                        writeCode(pBB->edges[ELSE].BBptr, indLevel, pProc, numLoc, latchNode,
                                  follow);
                        emptyThen = true;
                    }
                } else // already visited => emit label
                    emitGotoLabel(&pProc->Icode.icode[succ->start], indLevel);

                // process the ELSE part
                succ = pBB->edges[ELSE].BBptr;
                if (succ->traversed != DFS_ALPHA) { // not visited
                    if (succ->dfsLastNum != follow) { // ELSE part
                        appendStrTab(&cCode.code, "%s}\n%selse {\n", indent(indLevel - 1),
                                     indent(indLevel - 1));
                        writeCode(succ, indLevel, pProc, numLoc, latchNode, follow);
                    }
                    // else (empty ELSE part)
                } else if (!emptyThen) { // already visited => emit label
                    appendStrTab(&cCode.code, "%s}\n%selse {\n", indent(indLevel - 1),
                                 indent(indLevel - 1));
                    emitGotoLabel(&pProc->Icode.icode[succ->start], indLevel);
                }
                appendStrTab(&cCode.code, "%s}\n", indent(--indLevel));

                // Continue with the follow
                succ = pProc->dfsLast[follow];
                if (succ->traversed != DFS_ALPHA)
                    writeCode(succ, indLevel, pProc, numLoc, latchNode, ifFollow);
            } else { // no follow => if..then..else
                l = writeJcond(pProc->Icode.icode[pBB->start + pBB->length - 1].ic.hl, pProc,
                               numLoc);
                appendStrTab(&cCode.code, "\n%s%s", indent(indLevel - 1), l);
                writeCode(pBB->edges[THEN].BBptr, indLevel, pProc, numLoc, latchNode, ifFollow);
                appendStrTab(&cCode.code, "%s}\n%selse {\n", indent(indLevel - 1),
                             indent(indLevel - 1));
                writeCode(pBB->edges[ELSE].BBptr, indLevel, pProc, numLoc, latchNode, ifFollow);
                appendStrTab(&cCode.code, "%s}\n", indent(--indLevel));
            }
        }

        else { // fall, call, 1w
            succ = pBB->edges[0].BBptr; // fall-through edge
            if (succ->traversed != DFS_ALPHA)
                writeCode(succ, indLevel, pProc, numLoc, latchNode, ifFollow);
        }
    }
}

/*
 Writes the procedure's declaration (including arguments), local variables,
 and invokes the procedure that writes the code of the given record *hli
*/
static void codeGen(PPROC pProc, FILE *fp)
{
    int i, numLoc;
    PSTKFRAME args; // Procedure arguments
    char buf[200],  // Procedure's definition
         arg[30];   // One argument
    ID *locid;      // Pointer to one local identifier
    BB *pBB;        // Pointer to basic block

    // Write procedure/function header
    newBundle(&cCode);

    if (pProc->flg & PROC_IS_FUNC) // Function
        appendStrTab(&cCode.decl, "\n%s %s (", hlTypes[pProc->retVal.type], pProc->name);
    else // Procedure
        appendStrTab(&cCode.decl, "\nvoid %s (", pProc->name);

    // Write arguments
    args = &pProc->args;
    memset(buf, 0, sizeof(buf));

    for (i = 0; i < args->csym; i++) {
        if (args->sym[i].invalid == false) {
            sprintf(arg, "%s %s", hlTypes[args->sym[i].type], args->sym[i].name);
            strcat(buf, arg);
            if (i < (args->numArgs - 1))
                strcat(buf, ", ");
        }
    }

    strcat(buf, ")\n");
    appendStrTab(&cCode.decl, "%s", buf);

    // Write comments
    writeProcComments(pProc, &cCode.decl);

    // Write local variables
    if (!(pProc->flg & PROC_ASM)) {
        locid = &pProc->localId.id[0];
        numLoc = 0;
        for (i = 0; i < pProc->localId.csym; i++, locid++) {
            // Output only non-invalidated entries
            if (locid->illegal == false) {
                if (locid->loc == REG_FRAME) {
                    // Register variables are assigned to a local variable
                    if (((pProc->flg & SI_REGVAR) && (locid->id.regi == rSI)) ||
                        ((pProc->flg & DI_REGVAR) && (locid->id.regi == rDI))) {
                        sprintf(locid->name, "loc%d", ++numLoc);
                        appendStrTab(&cCode.decl, "int %s;\n", locid->name);
                    }
                    /* Other registers are named when they are first used in
                       the output C code, and appended to the proc decl. */
                }
                else if (locid->loc == STK_FRAME) {
                    // Name local variables and output appropriate type
                    sprintf(locid->name, "loc%d", ++numLoc);
                    appendStrTab(&cCode.decl, "%s %s;\n", hlTypes[locid->type], locid->name);
                }
            }
        }
    }

    // Write procedure's code
    if (pProc->flg & PROC_ASM) // generate assembler
        disassem(3, pProc);
    else // generate C
        writeCode(pProc->cfg, 1, pProc, &numLoc, MAX, UN_INIT);

    appendStrTab(&cCode.code, "}\n\n");
    writeBundle(fp, cCode);
    freeBundle(&cCode);

    // Write Live register analysis information
    if (option.verbose)
        for (i = 0; i < pProc->numBBs; i++) {
            pBB = pProc->dfsLast[i];

            if (pBB->flg & INVALID_BB)
                continue; // skip invalid BBs

            printf("BB %d\n", i);
            printf("  Start = %d, end = %d\n", pBB->start, pBB->start + pBB->length - 1);
            printf("  LiveUse = ");
            writeBitVector(pBB->liveUse);
            printf("\n  Def = ");
            writeBitVector(pBB->def);
            printf("\n  LiveOut = ");
            writeBitVector(pBB->liveOut);
            printf("\n  LiveIn = ");
            writeBitVector(pBB->liveIn);
            printf("\n\n");
        }
}

// Recursive procedure. Displays the procedure's code in depth-first order of the call graph.
static void backBackEnd(char *filename, PCALL_GRAPH pcallGraph, FILE *fp)
{
    // Check if this procedure has been processed already
    if ((pcallGraph->proc->flg & PROC_OUTPUT) || (pcallGraph->proc->flg & PROC_ISLIB))
        return;

    pcallGraph->proc->flg |= PROC_OUTPUT;

    // Dfs if this procedure has any successors
    if (pcallGraph->numOutEdges > 0)
        for (int i = 0; i < pcallGraph->numOutEdges; i++)
            backBackEnd(filename, pcallGraph->outEdges[i], fp);

    // Generate code for this procedure
    codeGen(pcallGraph->proc, fp);
}

// Invokes the necessary routines to produce code one procedure at a time.
void BackEnd(char *fileName, PCALL_GRAPH pcallGraph)
{
    char *outName, *ext;
    FILE *fp; // Output C file

    // Get output file name
    outName = strcpy(allocMem(strlen(fileName) + 1), fileName);

    if ((ext = strrchr(outName, '.')) != NULL)
        *ext = '\0';

    strcat(outName, ".b"); // b for beta

    // Open output file
    if (!(fp = fopen(outName, "wt")))
        fatalError(CANNOT_OPEN, outName);

    printf("%s: Writing C beta file %s\n", progname, outName);

    // Header information
    writeHeader(fp, fileName);

    // Process each procedure at a time
    backBackEnd(fileName, pcallGraph, fp);

    // Close output file
    fclose(fp);
    printf("%s: Finished writing C beta file\n", progname);
}
