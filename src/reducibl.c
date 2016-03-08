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

/********************************************************************
   Checks for reducibility of a graph by intervals, and constructs
   an equivalent reducible graph if one is not found.
   (C) Cristina Cifuentes
 ********************************************************************/

#include "dcc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int numInt; // Number of intervals

// Returns whether the queue q is empty or not
#define nonEmpty(q) (q != NULL)

// Returns whether the graph is a trivial graph or not
#define trivialGraph(G) (G->numOutEdges == 0)

/* Returns the first element in the queue Q, and removes this element from the list.
   Q is not an empty queue. */
static BB *firstOfQueue(queue **Q)
{
    queue *elim = *Q;       // Pointer to first node
    BB *first = (*Q)->node; // First element
    *Q = (*Q)->next;        // Pointer to next node
    free(elim);             // Free storage

    return first;
}

/* Appends pointer to node at the end of the queue Q if node is not present in this queue.
   Returns the queue node just appended. */
queue *appendQueue(queue **Q, BB *node)
{
    queue *pq, *l;

    pq = allocStruc(queue);
    pq->node = node;
    pq->next = NULL;

    if (Q) {
        if (*Q) {
            for (l = *Q; l->next && l->node != node; l = l->next)
                ;
            if (l->node != node)
                l->next = pq;
        } else { // (*Q) == NULL
            *Q = pq;
        }
    }
    return (pq);
}

/* Returns the next unprocessed node of the interval list (pointed to by pI->currNode).
   Removes this element logically from the list, by updating the currNode pointer
   to the next unprocessed element. */
static BB *firstOfInt(interval *pI)
{
    queue *pq = pI->currNode;

    if (pq == NULL)
        return NULL;

    pI->currNode = pq->next;
    return pq->node;
}

/* Appends node node to the end of the interval list I, updates currNode if necessary,
   and removes the node from the header list H if it is there.
   The interval header information is placed in the field node->inInterval.
   Note: nodes are added to the interval list in interval order
   (which topsorts the dominance relation). */
static queue *appendNodeInt(queue *pqH, BB *node, interval *pI)
{
    queue *pq, // Pointer to current node of the list
        *prev; // Pointer to previous node in the list

    pq = appendQueue(&pI->nodes, node); // Append node if it is not already in the interval list

    if (pI->currNode == NULL) // Update currNode if necessary
        pI->currNode = pq;

    /* Check header list for occurrence of node, if found, remove it
       and decrement number of out-edges from this interval. */
    if (node->beenOnH && pqH) {
        prev = pqH;

        for (pq = prev; pq && pq->node != node; pq = pq->next)
            prev = pq;

        if (pq == prev) {
            pqH = pqH->next;
            pI->numOutEdges -= (uint8_t)pq->node->numInEdges - 1;
        } else if (pq) {
            prev->next = pq->next;
            pI->numOutEdges -= (uint8_t)pq->node->numInEdges - 1;
        }
    }

    node->inInterval = pI; // Update interval header information for this basic block

    return pqH;
}

/* Finds the intervals of graph derivedGi->Gi and places them in the list of intervals derivedGi->Ii
   Algorithm by M.S.Hecht. */
static void findIntervals(derSeq *derivedGi)
{
    interval *pI, // Interval being processed
        *J;       // ^ last interval in derivedGi->Ii
    BB *h,        // Node being processed
        *header,  // Current interval's header node
        *succ;    // Successor basic block
    queue *H;     // Queue of possible header nodes

    bool first = true; // First pass through the loop

    H = appendQueue(NULL, derivedGi->Gi); // H = {first node of G}
    derivedGi->Gi->beenOnH = true;
    derivedGi->Gi->reachingInt = allocStruc(BB); // ^ empty BB

    while (nonEmpty(H)) // Process header nodes list H
    {
        header = firstOfQueue(&H);
        pI = memset(allocStruc(interval), 0, sizeof(interval));
        pI->numInt = (byte)numInt++;

        if (first) // ^ to first interval
            derivedGi->Ii = J = pI;

        H = appendNodeInt(H, header, pI); // pI(header) = {header}

        while ((h = firstOfInt(pI))) // Process all nodes in the current interval list
        {
            for (int i = 0; i < h->numOutEdges; i++) { // Check all immediate successors of h
                succ = h->edges[i].BBptr;
                succ->inEdgeCount--;

                if (succ->reachingInt == NULL) // first visit
                {
                    succ->reachingInt = header;

                    if (succ->inEdgeCount == 0)
                        H = appendNodeInt(H, succ, pI);
                    else if (!succ->beenOnH) // out edge
                    {
                        appendQueue(&H, succ);
                        succ->beenOnH = true;
                        pI->numOutEdges++;
                    }
                } else if (succ->inEdgeCount == 0) { // node has been visited before
                        if (succ->reachingInt == header || succ->inInterval == pI) // same interval
                        {
                            if (succ != header)
                                H = appendNodeInt(H, succ, pI);
                    } else /* out edge */
                        pI->numOutEdges++;
                } else if (succ != header && succ->beenOnH)
                    pI->numOutEdges++;
            }
        }

        if (!first) { // Link interval I to list of intervals
            J->next = pI;
            J = pI;
        } else // first interval
            first = false;
    }
}


// Displays the intervals of the graph Gi.
static void displayIntervals(interval *pI)
{
    queue *nodePtr;

    while (pI) {
        nodePtr = pI->nodes;
        printf("  Interval #: %hhu\t#OutEdges: %hhu\n", pI->numInt, pI->numOutEdges);

        while (nodePtr) {
            if (nodePtr->node->correspInt == NULL) // real BBs
                printf("    Node: %d\n", nodePtr->node->start);
            else // BBs represent intervals
                printf("   Node (corresp int): %d\n", nodePtr->node->correspInt->numInt);
            nodePtr = nodePtr->next;
        }
        pI = pI->next;
    }
}


// Allocates space for a new derSeq node.
static derSeq *newDerivedSeq(void)
{
    derSeq *pder = memset(allocStruc(derSeq), 0, sizeof(derSeq));
    return pder;
}


// Frees the storage allocated for the queue q
static void freeQueue(queue **q)
{
    queue *queuePtr;

    for (queuePtr = *q; queuePtr; queuePtr = *q) {
        *q = (*q)->next;
        free(queuePtr);
    }
}


// Frees the storage allocated for the interval pI
static void freeInterval(interval **pI)
{
    interval *Iptr;

    while (*pI) {
        freeQueue(&((*pI)->nodes));
        Iptr = *pI;
        *pI = (*pI)->next;
        free(Iptr);
    }
}


/* Frees the storage allocated by the derived sequence structure,
   except for the original graph cfg (derivedG->Gi). */
void freeDerivedSeq(derSeq *derivedG)
{
    derSeq *derivedGi;

    while (derivedG) {
        freeInterval(&(derivedG->Ii));
        if (derivedG->Gi->nodeType == INTERVAL_NODE)
            freeCFG(derivedG->Gi);
        derivedGi = derivedG;
        derivedG = derivedG->next;
        free(derivedGi);
    }
}


/* Finds the next order graph of derivedGi->Gi according to its intervals (derivedGi->Ii),
   and places it in derivedGi->next->Gi. */
static uint8_t nextOrderGraph(derSeq *derivedGi)
{
    interval *Ii;      // Interval being processed
    BB *BBnode,        // New basic block of intervals
       *curr,          // BB being checked for out edges
       *succ,          // Successor node
       derInt;
    queue *listIi;     // List of intervals
    int i,             // Index to outEdges array
        j;             // Index to successors
    uint8_t sameGraph; // Boolean, isomorphic graphs

    /* Process Gi's intervals */
    derivedGi->next = newDerivedSeq();
    Ii = derivedGi->Ii;
    sameGraph = true;
    derInt.next = NULL;
    BBnode = &derInt;

    while (Ii) {
        i = 0;
        BBnode = newBB(BBnode, -1, -1, INTERVAL_NODE, Ii->numOutEdges, NULL);
        BBnode->correspInt = Ii;
        listIi = Ii->nodes;

        if (sameGraph && listIi->next) // Check for more than 1 interval
            sameGraph = false;

        if (BBnode->numOutEdges > 0) // Find out edges
            while (listIi) {
                curr = listIi->node;
                for (j = 0; j < curr->numOutEdges; j++) {
                    succ = curr->edges[j].BBptr;
                    if (succ->inInterval != curr->inInterval)
                        BBnode->edges[i++].intPtr = succ->inInterval;
                }
                listIi = listIi->next;
            }
        Ii = Ii->next; // Next interval
    }

    /* Convert list of pointers to intervals into a real graph.
       Determines the number of in edges to each new BB, and places it
       in numInEdges and inEdgeCount for later interval processing. */
    curr = derivedGi->next->Gi = derInt.next;
    while (curr) {
        for (i = 0; i < curr->numOutEdges; i++) {
            BBnode = derivedGi->next->Gi; // BB of an interval
            while (BBnode && curr->edges[i].intPtr != BBnode->correspInt)
                BBnode = BBnode->next;
            if (BBnode) {
                curr->edges[i].BBptr = BBnode;
                BBnode->numInEdges++;
                BBnode->inEdgeCount++;
            } else
                fatalError(INVALID_INT_BB);
        }
        curr = curr->next;
    }
    return (!sameGraph);
}


/* Finds the derived sequence of the graph derivedG->Gi (ie. cfg).
   Constructs the n-th order graph and places all the intermediate
   graphs in the derivedG list sequence. */
static bool findDerivedSeq(derSeq *derivedGi)
{
    BB *Gi = derivedGi->Gi; // Current derived sequence graph

    while (!trivialGraph(Gi)) {
        findIntervals(derivedGi); // Find the intervals of Gi and place them in derivedGi->Ii

        if (!nextOrderGraph(derivedGi)) // Create Gi+1 and check if it is equivalent to Gi
            break;

        derivedGi = derivedGi->next;
        Gi = derivedGi->Gi;
        stats.nOrder++;
    }

    if (!trivialGraph(Gi)) {
        freeDerivedSeq(derivedGi->next); // remove Gi+1
        derivedGi->next = NULL;
        return false;
    }
    findIntervals(derivedGi);
    return true;
}


// Converts the irreducible graph G into an equivalent reducible one, by means of node splitting.
static void nodeSplitting(BB *G) {} //TODO ??


// Displays the derived sequence and intervals of the graph G 
void displayDerivedSeq(derSeq *derGi)
{
    int n = 1; // Derived sequence number

    printf("\nDerived Sequence Intervals\n");

    while (derGi) {
        printf("\nIntervals for G%X\n", n++);
        displayIntervals(derGi->Ii);
        derGi = derGi->next;
    }
}


/* Checks whether the control flow graph, cfg, is reducible or not.
   If it is not reducible, it is converted into an equivalent reducible graph by node splitting.
   The derived sequence of graphs built from cfg are returned in the pointer *derivedG. */
void checkReducibility(PPROC pProc, derSeq **derivedG)
{
    bool reducible; // Reducible graph flag
    numInt = 1;        // reinitialize no. of intervals
    stats.nOrder = 1;  // nOrder(cfg) = 1

    *derivedG = newDerivedSeq();
    (*derivedG)->Gi = pProc->cfg;
    reducible = findDerivedSeq(*derivedG);

    if (!reducible) {
        pProc->flg |= GRAPH_IRRED;
        nodeSplitting(pProc->cfg);
    }
}
