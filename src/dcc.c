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

/*****************************************************************************
 *              dcc decompiler
 * Reads the command line switches and then executes each major section in turn
 * (C) Cristina Cifuentes
 ****************************************************************************/

#include "dcc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdbool.h>

/* Global variables - extern to other modules */
char *progname;              /* argv[0] - for error msgs               */
char *asm1_name, *asm2_name; /* Assembler output filenames             */
SYMTAB symtab;               /* Global symbol table                    */
STATS stats;                 /* cfg statistics                         */
PROG prog;                   /* programs fields                        */
OPTION option;               /* Command line options                   */
PPROC pProcList;             /* List of procedures, topologically sort */
PPROC pLastProc;             /* Pointer to last node in procedure list */
CALL_GRAPH *callGraph;       /* Call graph of the program              */


static struct option opt[] = {
    {"help",         no_argument,       0, 'h'},
    {"verbose",      no_argument,       0, 'v'},
    {"very-verbose", no_argument,       0, 'V'},
    {"stat",         no_argument,       0, 's'},
    {"memory-map",   no_argument,       0, 'm'},
    {"interactive",  no_argument,       0, 'i'},
    {"asm1",         no_argument,       0, 'a'},
    {"asm2",         no_argument,       0, 'A'},
    {"file",         required_argument, 0, 'f'},
    {0, 0, 0, 0}
};


static void make_asmname(const char *str)
{
    char *buff1 = malloc(strlen(str) + 4);
    char *buff2 = malloc(strlen(str) + 4);

    strncpy(buff1, str, strlen(str));
    strncpy(buff2, str, strlen(str));

    strcat(buff1, ".a1");
    strcat(buff2, ".a2");

    asm1_name = strdup(buff1);
    asm2_name = strdup(buff2);

    remove(asm1_name);
    remove(asm2_name);
}


static void help() {
    fprintf(stderr,
        "\n  Usage: dcc [options] [-f file]"
        "\n"
        "\n  Options:"
        "\n"
        "\n    -h, --help           Display this information"
        "\n    -v, --verbose        Verbose output"
        "\n    -V, --very-verbose   Very verbose output"
        "\n    -s, --stat           Statistics summary"
        "\n    -m, --memory-map     Memory map"
        "\n    -i, --interactive    Enter interactive disassembler"
        "\n    -a, --asm1           Assembler output before re-ordering of input code"
        "\n    -A, --asm2           Assembler output after re-ordering of input code"
        "\n    -f, --file           Filename of the executable"
        "\n"
    );
    exit(EXIT_FAILURE);
}


/****************************************************************************
 * initargs - Extract command line arguments
 ***************************************************************************/
static char *initargs(int argc, char *argv[])
{
    progname = argv[0];

    if (argc < 2)
        fatalError(USAGE);

    int c, opt_idx = 0;
    char *filename;

    while ((c = getopt_long(argc, argv, "hvVsmiaAf:", opt, &opt_idx)) != -1) {
        switch (c) {
        case 'h':
            help();
            break;
        case 'v': /* Make everything verbose */
            option.verbose = true;
            break;
        case 'V': /* Very verbose => verbose */
            option.VeryVerbose = true;
            break;
        case 's': /* Print Stats */
            option.Stats = true;
            break;
        case 'm': /* Print memory map */
            option.Map = true;
            break;
        case 'i':
            option.Interact = true;
            break;
        case 'a': /* Print assembler listing */
            option.asm1 = true;
            break;
        case 'A':
            option.asm2 = true;
            break;
        case 'f':
            filename = optarg;
            break;
        default:
            fatalError(USAGE);
        }
    }

    if (option.asm1 || option.asm2)
        make_asmname(filename);

    return filename;
}


/****************************************************************************
 * main
 ***************************************************************************/
int main(int argc, char *argv[])
{
    /* Extract switches and filename */
    char *filename = initargs(argc, argv);

    /* Front end reads in EXE or COM file, parses it into I-code while
     * building the call graph and attaching appropriate bits of code for
     * each procedure.
    */
    FrontEnd(filename, &callGraph);

    /* In the middle is a so called Universal Decompiling Machine.
     * It processes the procedure list and I-code and attaches where it can
     * to each procedure an optimised cfg and ud lists
    */
    udm();

    /* Back end converts each procedure into C using I-code, interval
     * analysis, data flow etc. and outputs it to output file ready for
     * re-compilation.
    */
    BackEnd(filename, callGraph);

    writeCallGraph(callGraph);
    /*
        freeDataStructures(pProcList);
    */
    return 0;
}
