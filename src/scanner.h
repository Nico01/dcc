#ifndef SCANNER_H
#define SCANNER_H

/*
 * Copyright (C) 1992, Queensland University of Technology
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
 Scanner functions
 (C) Cristina Cifuentes, Jeff Ledermann
*/

static void rm(int i);
static void modrm(int i);
static void segrm(int i);
static void data1(int i);
static void data2(int i);
static void regop(int i);
static void segop(int i);
static void strop(int i);
static void escop(int i);
static void axImp(int i);
static void alImp(int i);
static void axSrcIm(int i);
static void memImp(int i);
static void memReg0(int i);
static void memOnly(int i);
static void dispM(int i);
static void dispS(int i);
static void dispN(int i);
static void dispF(int i);
static void prefix(int i);
static void immed(int i);
static void shift(int i);
static void arith(int i);
static void trans(int i);
static void const1(int i);
static void const3(int i);
static void none1(int i);
static void none2(int i);
static void checkInt(int i);

// Extracts reg bits from middle of mod-reg-rm byte
#define REG(x)  ((byte)(x & 0x38) >> 3)

#endif // SCANNER_H

