#ifndef ERROR_H
#define ERROR_H

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
 * Error codes
 * (C) Cristina Cifuentes
 ****************************************************************************/

/* These definitions refer to errorMessage in error.c */

typedef enum {
    USAGE = 0,
    INVALID_ARG,
    INVALID_OPCODE,
    INVALID_386OP,
    FUNNY_SEGOVR,
    FUNNY_REP,
    CANNOT_OPEN,
    CANNOT_READ,
    MALLOC_FAILED,
    NEWEXE_FORMAT,
    NO_BB,
    INVALID_SYNTHETIC_BB,
    INVALID_INT_BB,
    IP_OUT_OF_RANGE,
    DEF_NOT_FOUND,
    JX_NOT_DEF,
    NOT_DEF_USE,
    REPEAT_FAIL,
    WHILE_FAIL
} error_msg;


void fatalError(error_msg id, ...);
void reportError(error_msg id, ...);

#endif // ERROR_H

