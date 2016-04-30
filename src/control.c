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

// Description: Performs control flow analysis on the CFG

#include "dcc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


typedef struct list {
    int nodeIdx;       // dfsLast index to the node
    struct list *next;
} nodeList;


#define ancestor(a, b) ((a->dfsLastNum < b->dfsLastNum) && (a->dfsFirstNum < b->dfsFirstNum))
/* there is a path on the DFST from a to b if the a was first visited in a dfs,
   and a was later visited than b when doing the last visit of each node. */


/*
 Checks if the edge (p,s) is a back edge. If node s was visited first during the dfs traversal
 (ie. s has a smaller dfsFirst number) or s == p, then it is a backedge.
 Also incrementes the number of backedges entries to the header node.
*/
static bool isBackEdge(PBB p, PBB s)
{
    if (p->dfsFirstNum >= s->dfsFirstNum) {
        s->numBackEdges++;
        return true;
    }
    return false;
}

/*
 Finds the common dominator of the current immediate dominator currImmDom
 and its predecessor's immediate dominator predImmDom
*/
static int commonDom(int currImmDom, int predImmDom, PPROC pProc)
{
    if (currImmDom == NO_DOM)
        return predImmDom;

    if (predImmDom == NO_DOM) // predecessor is the root
        return currImmDom;

    while ((currImmDom != NO_DOM) && (predImmDom != NO_DOM) && (currImmDom != predImmDom)) {
        if (currImmDom < predImmDom)
            predImmDom = pProc->dfsLast[predImmDom]->immedDom;
        else
            currImmDom = pProc->dfsLast[currImmDom]->immedDom;
    }

    return currImmDom;
}

/*
 Finds the immediate dominator of each node in the graph pProc->cfg.
 Adapted version of the dominators algorithm by Hecht and Ullman;
 finds immediate dominators only.
 Note: graph should be reducible
*/
static void findImmedDom(PPROC pProc)
{
    for (int currIdx = 0; currIdx < pProc->numBBs; currIdx++) {
        PBB currNode = pProc->dfsLast[currIdx];
        if (currNode->flg & INVALID_BB) // Do not process invalid BBs
            continue;

        for (int j = 0; j < currNode->numInEdges; j++) {
            int predIdx = currNode->inEdges[j]->dfsLastNum;
            if (predIdx < currIdx)
                currNode->immedDom = commonDom(currNode->immedDom, predIdx, pProc);
        }
    }
}

// Inserts the node n to the list l.
static void insertList(nodeList **l, int n)
{
    nodeList *new;

    new = memset(allocStruc(nodeList), 0, sizeof(nodeList));
    new->nodeIdx = n;
    new->next = *l;
    *l = new;
}

// Returns whether or not the node n (dfsLast numbering of a basic block) is on the list l.
static bool inList(nodeList *l, int n)
{
    while (l)
        if (l->nodeIdx == n)
            return true;
        else
            l = l->next;

    return false;
}

// Frees space allocated by the list l.
static void freeList(nodeList **l)
{
    nodeList *next;

    if (*l) {
        next = (*l)->next;
        free(*l);
        *l = next;
    }

    *l = NULL;
}

// Returns whether the node n belongs to the queue list q.
static bool inInt(PBB n, queue *q)
{
    while (q)
        if (q->node == n)
            return true;
        else
            q = q->next;

    return false;
}

// Flags nodes that belong to the loop determined by (latchNode, head) and determines the type of loop.
static void findNodesInLoop(PBB latchNode, PBB head, PPROC pProc, queue *intNodes)
{
    int i, headDfsNum, intNodeType;
    nodeList *loopNodes = NULL;
    int immedDom,         // dfsLast index to immediate dominator
        thenDfs, elseDfs; // dsfLast index for THEN and ELSE nodes
    PBB pbb;

    // Flag nodes in loop headed by head (except header node)
    headDfsNum = head->dfsLastNum;
    head->loopHead = headDfsNum;
    insertList(&loopNodes, headDfsNum);

    for (i = headDfsNum + 1; i < latchNode->dfsLastNum; i++) {
        if (pProc->dfsLast[i]->flg & INVALID_BB) // skip invalid BBs
            continue;

        immedDom = pProc->dfsLast[i]->immedDom;
        if (inList(loopNodes, immedDom) && inInt(pProc->dfsLast[i], intNodes)) {
            insertList(&loopNodes, i);
            if (pProc->dfsLast[i]->loopHead == NO_NODE) // not in other loop
                pProc->dfsLast[i]->loopHead = headDfsNum;
        }
    }

    latchNode->loopHead = headDfsNum;

    if (latchNode != head)
        insertList(&loopNodes, latchNode->dfsLastNum);

    // Determine type of loop and follow node
    intNodeType = head->nodeType;

    if (latchNode->nodeType == TWO_BRANCH)
        if ((intNodeType == TWO_BRANCH) || (latchNode == head))
            if ((latchNode == head) || (inList(loopNodes, head->edges[THEN].BBptr->dfsLastNum) &&
                                        inList(loopNodes, head->edges[ELSE].BBptr->dfsLastNum))) {
                head->loopType = REPEAT_TYPE;
                if (latchNode->edges[0].BBptr == head)
                    head->loopFollow = latchNode->edges[ELSE].BBptr->dfsLastNum;
                else
                    head->loopFollow = latchNode->edges[THEN].BBptr->dfsLastNum;
                pProc->Icode.icode[latchNode->start + latchNode->length - 1].ic.ll.flg |= JX_LOOP;
            } else {
                head->loopType = WHILE_TYPE;
                if (inList(loopNodes, head->edges[THEN].BBptr->dfsLastNum))
                    head->loopFollow = head->edges[ELSE].BBptr->dfsLastNum;
                else
                    head->loopFollow = head->edges[THEN].BBptr->dfsLastNum;
                pProc->Icode.icode[head->start + head->length - 1].ic.ll.flg |= JX_LOOP;
            }
        else { // head = anything besides 2-way, latch = 2-way
            head->loopType = REPEAT_TYPE;
            if (latchNode->edges[THEN].BBptr == head)
                head->loopFollow = latchNode->edges[ELSE].BBptr->dfsLastNum;
            else
                head->loopFollow = latchNode->edges[THEN].BBptr->dfsLastNum;
            pProc->Icode.icode[latchNode->start + latchNode->length - 1].ic.ll.flg |= JX_LOOP;
        }
    else // latch = 1-way
        if (latchNode->nodeType == LOOP_NODE) {
        head->loopType = REPEAT_TYPE;
        head->loopFollow = latchNode->edges[0].BBptr->dfsLastNum;
    } else if (intNodeType == TWO_BRANCH) {
        head->loopType = WHILE_TYPE;
        pbb = latchNode;
        thenDfs = head->edges[THEN].BBptr->dfsLastNum;
        elseDfs = head->edges[ELSE].BBptr->dfsLastNum;

        while (1) {
            if (pbb->dfsLastNum == thenDfs) {
                head->loopFollow = elseDfs;
                break;
            } else if (pbb->dfsLastNum == elseDfs) {
                head->loopFollow = thenDfs;
                break;
            }

            /* Check if couldn't find it, then it is a strangely formed
               loop, so it is safer to consider it an endless loop */
            if (pbb->dfsLastNum <= head->dfsLastNum) {
                head->loopType = ENDLESS_TYPE;
                // missing follow
                break;
            }
            pbb = pProc->dfsLast[pbb->immedDom];
        }
        if (pbb->dfsLastNum > head->dfsLastNum)
            pProc->dfsLast[head->loopFollow]->loopHead = NO_NODE;
        pProc->Icode.icode[head->start + head->length - 1].ic.ll.flg |= JX_LOOP;
    } else {
        head->loopType = ENDLESS_TYPE;
        // missing follow
    }

    freeList(&loopNodes);
}

// Recursive procedure to find nodes that belong to the interval (ie. nodes from G1).
static void findNodesInInt(queue **intNodes, int level, interval *Ii)
{
    queue *l;

    if (level == 1)
        for (l = Ii->nodes; l; l = l->next)
            appendQueue(intNodes, l->node);
    else
        for (l = Ii->nodes; l; l = l->next)
            findNodesInInt(intNodes, level - 1, l->node->correspInt);
}

// Algorithm for structuring loops
static void structLoops(PPROC pProc, derSeq *derivedG)
{
    interval *Ii;
    PBB intHead,       // interval header node
        pred,          // predecessor node
        latchNode;     // latching node (in case of loops)
    int i,             // counter
        level = 0;     // derived sequence level
    interval *initInt; // initial interval
    queue *intNodes;   // list of interval nodes

    // Structure loops
    while (derivedG) { // for all derived sequences Gi
        level++;
        Ii = derivedG->Ii;
        while (Ii) {   // for all intervals Ii of Gi
            latchNode = NULL;
            intNodes = NULL;

            // Find interval head (original BB node in G1) and createlist of nodes of interval Ii.
            initInt = Ii;
            for (i = 1; i < level; i++)
                initInt = initInt->nodes->node->correspInt;
            intHead = initInt->nodes->node;

            // Find nodes that belong to the interval (nodes from G1)
            findNodesInInt(&intNodes, level, Ii);

            // Find greatest enclosing back edge (if any)
            for (i = 0; i < intHead->numInEdges; i++) {
                pred = intHead->inEdges[i];
                if (inInt(pred, intNodes) && isBackEdge(pred, intHead)) {
                    if (!latchNode)
                        latchNode = pred;
                    else {
                        if (pred->dfsLastNum > latchNode->dfsLastNum)
                            latchNode = pred;
                    }
                }
            }

            // Find nodes in the loop and the type of loop
            if (latchNode) {
                /* Check latching node is at the same nesting level of case statements (if any)
                   and that the node doesn't belong to another loop. */
                if ((latchNode->caseHead == intHead->caseHead) &&
                    (latchNode->loopHead == NO_NODE)) {
                    intHead->latchNode = latchNode->dfsLastNum;
                    findNodesInLoop(latchNode, intHead, pProc, intNodes);
                    latchNode->flg |= IS_LATCH_NODE;
                }
            }
            // Next interval
            Ii = Ii->next;
        }
        // Next derived sequence
        derivedG = derivedG->next;
    }
}

/*
 Returns whether the BB indexed by s is a successor of the BB indexed by h.
 Note that h is a case node.
*/
static bool successor(int s, int h, PPROC pProc)
{
    PBB header = pProc->dfsLast[h];

    for (int i = 0; i < header->numOutEdges; i++)
        if (header->edges[i].BBptr->dfsLastNum == s)
            return true;

    return false;
}

/*
 Recursive procedure to tag nodes that belong to the case described by the list l,
 head and tail (dfsLast index to first and exit node of the case).
*/
static void tagNodesInCase(PBB pBB, nodeList **l, int head, int tail)
{
    pBB->traversed = DFS_CASE;
    int current = pBB->dfsLastNum; // index to current node

    if ((current != tail) && (pBB->nodeType != MULTI_BRANCH) && (inList(*l, pBB->immedDom))) {
        insertList(l, current);
        pBB->caseHead = head;

        for (int i = 0; i < pBB->numOutEdges; i++)
            if (pBB->edges[i].BBptr->traversed != DFS_CASE)
                tagNodesInCase(pBB->edges[i].BBptr, l, head, tail);
    }
}

// Structures case statements.  This procedure is invoked only when pProc has a case node.
static void structCases(PPROC pProc)
{
    PBB caseHeader;             // case header node
    int exitNode = NO_NODE;     // case exit node
    nodeList *caseNodes = NULL; // temporary: list of nodes in case

    // Linear scan of the nodes in reverse dfsLast order, searching for case nodes
    for (int i = pProc->numBBs - 1; i >= 0; i--)
        if (pProc->dfsLast[i]->nodeType == MULTI_BRANCH) {
            caseHeader = pProc->dfsLast[i];

            /* Find descendant node which has as immediate predecessor
               the current header node, and is not a successor. */
            for (int j = i + 2; j < pProc->numBBs; j++) {
                if ((!successor(j, i, pProc)) && (pProc->dfsLast[j]->immedDom == i)) {
                    if (exitNode == NO_NODE)
                        exitNode = j;
                    else if (pProc->dfsLast[exitNode]->numInEdges < pProc->dfsLast[j]->numInEdges)
                        exitNode = j;
                }
            }

            pProc->dfsLast[i]->caseTail = exitNode;

            // Tag nodes that belong to the case by recording the header field with caseHeader.
            insertList(&caseNodes, i);
            pProc->dfsLast[i]->caseHead = i;
            for (int j = 0; j < caseHeader->numOutEdges; j++)
                tagNodesInCase(caseHeader->edges[j].BBptr, &caseNodes, i, exitNode);
            if (exitNode != NO_NODE)
                pProc->dfsLast[exitNode]->caseHead = i;
        }
}

// Flags all nodes in the list l as having follow node f, and deletes all nodes from the list.
static void flagNodes(nodeList **l, int f, PPROC pProc)
{
    nodeList *p = *l;

    while (p) {
        pProc->dfsLast[p->nodeIdx]->ifFollow = f;
        p = p->next;
        free(*l);
        *l = p;
    }
    *l = NULL;
}

// Structures if statements
static void structIfs(PPROC pProc)
{
    int curr,                 // Index for linear scan of nodes
        desc,                 // Index for descendant
        followInEdges,        // Largest # in-edges so far
        follow;               // Possible follow node
    nodeList *domDesc = NULL, // List of nodes dominated by curr
        *unresolved = NULL;   // List of unresolved if nodes
    PBB currNode,             // Pointer to current node
        pbb;

    // Linear scan of nodes in reverse dfsLast order
    for (curr = pProc->numBBs - 1; curr >= 0; curr--) {
        currNode = pProc->dfsLast[curr];
        if (currNode->flg & INVALID_BB) // Do not process invalid BBs
            continue;

        if ((currNode->nodeType == TWO_BRANCH) &&
            (!(pProc->Icode.icode[currNode->start + currNode->length - 1].ic.ll.flg & JX_LOOP))) {
            followInEdges = 0;
            follow = 0;

            // Find all nodes that have this node as immediate dominator
            for (desc = curr + 1; desc < pProc->numBBs; desc++) {
                if (pProc->dfsLast[desc]->immedDom == curr) {
                    insertList(&domDesc, desc);
                    pbb = pProc->dfsLast[desc];
                    if ((pbb->numInEdges - pbb->numBackEdges) > followInEdges) {
                        follow = desc;
                        followInEdges = pbb->numInEdges - pbb->numBackEdges;
                    }
                }
            }

            // Determine follow according to number of descendants immediately dominated by this node
            if ((follow != 0) && (followInEdges > 1)) {
                currNode->ifFollow = follow;
                if (unresolved)
                    flagNodes(&unresolved, follow, pProc);
            } else
                insertList(&unresolved, curr);
        }
        freeList(&domDesc);
    }
}

/*
 Checks for compound conditions of basic blocks that have only 1 high level instruction.
 Whenever these blocks are found, they are merged into one block with the appropriate condition
*/
void compoundCond(PPROC pproc)
{
    int i, j;
    PBB pbb, t, e, obb;
    PICODE picode, ticode;
    COND_EXPR *exp;

    bool change = false;

    while (change) {
        change = true;

        // Traverse nodes in postorder, this way, the header node of a compound condition is analysed first
        for (i = 0; i < pproc->numBBs; i++) {
            pbb = pproc->dfsLast[i];
            if (pbb->flg & INVALID_BB)
                continue;

            if (pbb->nodeType == TWO_BRANCH) {
                t = pbb->edges[THEN].BBptr;
                e = pbb->edges[ELSE].BBptr;

                // Check (X || Y) case
                if ((t->nodeType == TWO_BRANCH) && (t->numHlIcodes == 1) && (t->numInEdges == 1) &&
                    (t->edges[ELSE].BBptr == e)) {
                    obb = t->edges[THEN].BBptr;

                    // Construct compound DBL_OR expression
                    picode = &pproc->Icode.icode[pbb->start + pbb->length - 1];
                    ticode = &pproc->Icode.icode[t->start + t->length - 1];
                    exp = boolCondExp(picode->ic.hl.oper.exp, ticode->ic.hl.oper.exp, DBL_OR);
                    picode->ic.hl.oper.exp = exp;

                    // Replace in-edge to obb from t to pbb
                    for (j = 0; j < obb->numInEdges; j++)
                        if (obb->inEdges[j] == t) {
                            obb->inEdges[j] = pbb;
                            break;
                        }

                    // New THEN out-edge of pbb
                    pbb->edges[THEN].BBptr = obb;

                    // Remove in-edge t to e
                    for (j = 0; j < (e->numInEdges - 1); j++)
                        if (e->inEdges[j] == t) {
                            memmove(&e->inEdges[j], &e->inEdges[j + 1],
                                    (e->numInEdges - j - 1) * sizeof(PBB));
                            break;
                        }
                    e->numInEdges--; // looses 1 arc
                    t->flg |= INVALID_BB;

                    if (pbb->flg & IS_LATCH_NODE)
                        pproc->dfsLast[t->dfsLastNum] = pbb;
                    else
                        i--; // to repeat this analysis

                    // Update statistics
                    stats.numBBaft--;
                    stats.numEdgesAft -= 2;
                    change = true;
                }

                // Check (!X && Y) case
                else if ((t->nodeType == TWO_BRANCH) && (t->numHlIcodes == 1) &&
                         (t->numInEdges == 1) && (t->edges[THEN].BBptr == e)) {
                    obb = t->edges[ELSE].BBptr;

                    // Construct compound DBL_AND expression
                    picode = &pproc->Icode.icode[pbb->start + pbb->length - 1];
                    ticode = &pproc->Icode.icode[t->start + t->length - 1];
                    inverseCondOp(&picode->ic.hl.oper.exp);
                    exp = boolCondExp(picode->ic.hl.oper.exp, ticode->ic.hl.oper.exp, DBL_AND);
                    picode->ic.hl.oper.exp = exp;

                    // Replace in-edge to obb from t to pbb
                    for (j = 0; j < obb->numInEdges; j++)
                        if (obb->inEdges[j] == t) {
                            obb->inEdges[j] = pbb;
                            break;
                        }

                    // New THEN and ELSE out-edges of pbb
                    pbb->edges[THEN].BBptr = e;
                    pbb->edges[ELSE].BBptr = obb;

                    // Remove in-edge t to e
                    for (j = 0; j < (e->numInEdges - 1); j++)
                        if (e->inEdges[j] == t) {
                            memmove(&e->inEdges[j], &e->inEdges[j + 1],
                                    (e->numInEdges - j - 1) * sizeof(PBB));
                            break;
                        }
                    e->numInEdges--; // looses 1 arc
                    t->flg |= INVALID_BB;

                    if (pbb->flg & IS_LATCH_NODE)
                        pproc->dfsLast[t->dfsLastNum] = pbb;
                    else
                        i--; // to repeat this analysis

                    // Update statistics
                    stats.numBBaft--;
                    stats.numEdgesAft -= 2;
                    change = true;
                }

                // Check (X && Y) case
                else if ((e->nodeType == TWO_BRANCH) && (e->numHlIcodes == 1) &&
                         (e->numInEdges == 1) && (e->edges[THEN].BBptr == t)) {
                    obb = e->edges[ELSE].BBptr;

                    // Construct compound DBL_AND expression
                    picode = &pproc->Icode.icode[pbb->start + pbb->length - 1];
                    ticode = &pproc->Icode.icode[t->start + t->length - 1];
                    exp = boolCondExp(picode->ic.hl.oper.exp, ticode->ic.hl.oper.exp, DBL_AND);
                    picode->ic.hl.oper.exp = exp;

                    // Replace in-edge to obb from e to pbb
                    for (j = 0; j < obb->numInEdges; j++)
                        if (obb->inEdges[j] == e) {
                            obb->inEdges[j] = pbb;
                            break;
                        }

                    // New ELSE out-edge of pbb
                    pbb->edges[ELSE].BBptr = obb;

                    // Remove in-edge e to t
                    for (j = 0; j < (t->numInEdges - 1); j++)
                        if (t->inEdges[j] == e) {
                            memmove(&t->inEdges[j], &t->inEdges[j + 1],
                                    (t->numInEdges - j - 1) * sizeof(PBB));
                            break;
                        }
                    t->numInEdges--; // looses 1 arc
                    e->flg |= INVALID_BB;

                    if (pbb->flg & IS_LATCH_NODE)
                        pproc->dfsLast[e->dfsLastNum] = pbb;
                    else
                        i--; // to repeat this analysis

                    // Update statistics
                    stats.numBBaft--;
                    stats.numEdgesAft -= 2;
                    change = true;
                }

                // Check (!X || Y) case
                else if ((e->nodeType == TWO_BRANCH) && (e->numHlIcodes == 1) &&
                         (e->numInEdges == 1) && (e->edges[ELSE].BBptr == t)) {
                    obb = e->edges[THEN].BBptr;

                    // Construct compound DBL_OR expression
                    picode = &pproc->Icode.icode[pbb->start + pbb->length - 1];
                    ticode = &pproc->Icode.icode[t->start + t->length - 1];
                    inverseCondOp(&picode->ic.hl.oper.exp);
                    exp = boolCondExp(picode->ic.hl.oper.exp, ticode->ic.hl.oper.exp, DBL_OR);
                    picode->ic.hl.oper.exp = exp;

                    // Replace in-edge to obb from e to pbb
                    for (j = 0; j < obb->numInEdges; j++)
                        if (obb->inEdges[j] == e) {
                            obb->inEdges[j] = pbb;
                            break;
                        }

                    // New THEN and ELSE out-edges of pbb
                    pbb->edges[THEN].BBptr = obb;
                    pbb->edges[ELSE].BBptr = t;

                    // Remove in-edge e to t
                    for (j = 0; j < (t->numInEdges - 1); j++)
                        if (t->inEdges[j] == e) {
                            memmove(&t->inEdges[j], &t->inEdges[j + 1],
                                    (t->numInEdges - j - 1) * sizeof(PBB));
                            break;
                        }
                    t->numInEdges--; // looses 1 arc
                    e->flg |= INVALID_BB;

                    if (pbb->flg & IS_LATCH_NODE)
                        pproc->dfsLast[e->dfsLastNum] = pbb;
                    else
                        i--; // to repeat this analysis

                    // Update statistics
                    stats.numBBaft--;
                    stats.numEdgesAft -= 2;
                    change = true;
                }
            }
        }
    }
}

// Structuring algorithm to find the structures of the graph pProc->cfg
void structure(PPROC pProc, derSeq *derivedG)
{
    // Find immediate dominators of the graph
    findImmedDom(pProc);

    if (pProc->hasCase)
        structCases(pProc);

    structLoops(pProc, derivedG);
    structIfs(pProc);
}
