			   dcc Distribution

The code provided in this distribution is (C) by their authors:
	Cristina Cifuentes (most of dcc code)
	Mike van Emmerik (signatures and prototype code)
	Jeff Ledermann (some disassembly code)
and is provided "as is".

The following files are included in the dcc.tar.gz distribution:
- dcc.zip (dcc.exe DOS program, 1995)
- dccsrc.zip (source code *.c, *.h for dcc, 1993-1994)
- dccbsig.zip (library signatures for Borland C compilers, 1994)
- dccmsig.zip (library signatures for Microsoft C compilers, 1994)
- dcctpsig.zip (library signatures for Turbo Pascal compilers, 1994)
- dcclibs.dat (prototype file for C headers, 1994)
- test.zip (sample test files: *.c *.exe *.b, 1993-1996)
- makedsig.zip (creates a .sig file from a .lib C file, 1994)
- makedstp.zip (creates a .sig file from a Pascal library file, 1994)
- readsig.zip (reads signatures in a .sig file, 1994)
- dispsrch.zip (displays the name of a function given a signature, 1994)
- parsehdr.zip (generates a prototype file (dcclibs.dat) from C *.h files, 1994)

The following files are included in the test.zip file:  fibo,
benchsho, benchlng, benchfn, benchmul, byteops, intops, longops,
max, testlong, matrixmu, strlen, dhamp.
The version of dcc included in this distribution is broken in
some cases, we do not have the time to work in this project at
present so we cannot provide any other version.
Comments on individual files:
- fibo (fibonacci): the small model (fibos.exe) decompiles correctly,
  the large model (fibol.exe) expects an extra argument for scanf().
  This argument is the segment and is not displayed.
- benchsho: the first scanf() takes loc0 as an argument.  This is
  part of a long variable, but dcc does not have any clue at that
  stage that the stack offset pushed on the stack is to be used
  as a long variable rather than an integer variable.
- benchlng: as part of the main() code, LO(loc1) | HI(loc1) should
  be displayed instead of loc3 | loc9.  These two integer variables
  are equivalent to the one long loc1 variable.
- benchfn: see benchsho.
- benchmul: see benchsho.
- byteops: decompiles correctly.
- intops: the du analysis for DIV and MOD is broken.  dcc currently
  generates code for a long and an integer temporary register that
  were used as part of the analysis.
- longops: decompiles correctly.
- max: decompiles correctly.
- testlong: this example decompiles correctly given the algorithms
  implemented in dcc.  However, it shows that when long variables
  are defined and used as integers (or long) without giving dcc
  any hint that this is happening, the variable will be treated as
  two integer variables.  This is due to the fact that the assembly
  code is in terms of integer registers, and long registers are not
  available in 80286, so a long variable is equivalent to two integer
  registers.  dcc only knows of this through idioms such as add two
  long variables.
- matrixmu: decompiles correctly.  Shows that arrays are not supported
  in dcc.
- strlen: decompiles correctly.  Shows that pointers are partially
  supported by dcc.
- dhamp: this program has far more data types than what dcc recognizes
  at present.

Our thanks to Gary Shaffstall for some debugging work.  Current bugs
are:
- if the code generated in the one line is too long, the (static)
  buffer used for that line is clobbered.  Solution: make the buffer
  larger (currently 200 chars).
- the large memory model problem & scanf()
- dcc's error message shows a p option available which doesn't
  exist, and doesn't show an i option which exists.

For more information refer to the thesis "Reverse Compilation
Techniques" by Cristina Cifuentes, Queensland University of
Technology, 1994, and the dcc home page:
	http://crg.cs.utas.edu.au/dcc.html

Please note that the executable version of dcc provided in this
distribution does not necessarily match the source code provided,
some changes were done without us keeping track of every change.

