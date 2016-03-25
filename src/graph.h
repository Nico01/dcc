#ifndef GRAPH_H
#define GRAPH_H

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

/*
   CFG, BB and interval related definitions
   (C) Cristina Cifuentes
*/

// Types of basic block nodes
// Real basic blocks: type defined according to their out-edges
#define ONE_BRANCH 0    // unconditional branch
#define TWO_BRANCH 1    // conditional branch
#define MULTI_BRANCH 2  // case branch
#define FALL_NODE 3     // fall through
#define RETURN_NODE 4   // procedure/program return
#define CALL_NODE 5     // procedure call
#define LOOP_NODE 6     // loop instruction
#define REP_NODE 7      // repeat instruction
#define INTERVAL_NODE 8 // contains interval list

#define TERMINATE_NODE 11 // Exit to DOS
#define NOWHERE_NODE 12   // No outedges going anywhere

// Depth-first traversal constants
#define DFS_DISP 1  // Display graph pass
#define DFS_MERGE 2 // Merge nodes pass
#define DFS_NUM 3   // DFS numbering pass
#define DFS_CASE 4  // Case pass
#define DFS_ALPHA 5 // Alpha code generation
#define DFS_JMP 9   // rmJMP pass - must be largest flag

// Control flow analysis constants
#define NO_TYPE 0      // node is not a loop header
#define WHILE_TYPE 1   // node is a while header
#define REPEAT_TYPE 2  // node is a repeat header
#define ENDLESS_TYPE 3 // endless loop header

// Uninitialized values for certain fields
#define NO_NODE MAX // node has no associated node
#define NO_DOM MAX  // node has no dominator
#define UN_INIT MAX // uninitialized variable

#define THEN 0 // then edge
#define ELSE 1 // else edge

// Basic Block (BB) flags
#define INVALID_BB 0x0001    // BB is not valid any more
#define IS_LATCH_NODE 0x0002 // BB is the latching node of a loop

// Interval structure
typedef struct _queueNode {
    struct _BB *node; // Ptr to basic block
    struct _queueNode *next;
} queue;

typedef struct _intNode {
    uint8_t numInt;        // # of the interval
    uint8_t numOutEdges;   // Number of out edges
    queue *nodes;          // Nodes of the interval
    queue *currNode;       // Current node
    struct _intNode *next; // Next interval
} interval;

// Basic block (BB) node definition
typedef struct _BB {
    uint8_t nodeType; // Type of node
    int traversed;    // Boolean: traversed yet?
    int start;        // First instruction offset
    int length;       // No. of instructions this BB
    int numHlIcodes;  // No. of high-level icodes
    uint32_t flg;     // BB flags

    // In edges and out edges
    int numInEdges;       // Number of in edges
    struct _BB **inEdges; // Array of ptrs. to in edges

    int numOutEdges; // Number of out edges
    union typeAdr {
        uint32_t ip;       // Out edge icode address
        struct _BB *BBptr; // Out edge pointer to next BB
        interval *intPtr;  // Out edge ptr to next interval
    } * edges;             // Array of ptrs. to out edges

    // For interval construction
    int beenOnH;             // #times been on header list H
    int inEdgeCount;         // #inEdges (to find intervals)
    struct _BB *reachingInt; // Reaching interval header
    interval *inInterval;    // Node's interval

    // For derived sequence construction
    interval *correspInt; // Corresponding interval in derived graph Gi-1

    // For live register analysis: LiveIn(b) = LiveUse(b) U (LiveOut(b) - Def(b))
    uint32_t liveUse; // LiveUse(b)
    uint32_t def;     // Def(b)
    uint32_t liveIn;  // LiveIn(b)
    uint32_t liveOut; // LiveOut(b)

    // For structuring analysis
    int dfsFirstNum;  // DFS #: first visit of node
    int dfsLastNum;   // DFS #: last visit of node
    int immedDom;     // Immediate dominator (dfsLast index)
    int ifFollow;     // node that ends the if
    int loopType;     // Type of loop (if any)
    int latchNode;    // latching node of the loop
    int numBackEdges; // # of back edges
    int loopHead;     // most nested loop head to which this node belongs (dfsLast)
    int loopFollow;   // node that follows the loop
    int caseHead;     // most nested case to which this node belongs (dfsLast)
    int caseTail;     // tail node for the case

    int index;        // Index, used in several ways
    struct _BB *next; // Next (list link)
} BB;
typedef BB *PBB;

// Derived Sequence structure
typedef struct _derivedNode {
    BB *Gi;                    // Graph pointer
    interval *Ii;              // Interval list of Gi
    struct _derivedNode *next; // Next derived graph
} derSeq;

#endif // GRAPH_H

