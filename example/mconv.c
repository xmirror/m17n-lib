/* mconv.c -- Code converter.
   Copyright (C) 2003, 2004
     National Institute of Advanced Industrial Science and Technology (AIST)
     Registration Number H15PRO112

   This file is part of the m17n library.

   The m17n library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License
   as published by the Free Software Foundation; either version 2.1 of
   the License, or (at your option) any later version.

   The m17n library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the m17n library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.  */

/***en
    @page mconv convert file code

    @section mconv-synopsis SYNOPSIS

    mconv [ OPTION ... ] [ INFILE [ OUTFILE ] ]

    @section mconv-description DESCRIPTION

    Convert encoding of given files from one to another.

    If INFILE is omitted, the input is taken from standard input.  If
    OUTFILE is omitted, the output written to standard output.

    The following OPTIONs are available.

    <ul>

    <li> -f FROMCODE

    FROMCODE is the encoding of INFILE (defaults to UTF-8).

    <li> -t TOCODE

    TOCODE is the encoding of OUTFILE (defaults to UTF-8).

    <li> -k

    Do not stop conversion on error.

    <li> -s

    Suppress warnings.

    <li> -v

    Print progress information.

    <li> -l

    List available encodings.

    <li> --version

    Print version number.

    <li> -h, --help

    Print this message.

    </ul>
*/

#ifndef FOR_DOXYGEN

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <m17n.h>
#include <m17n-misc.h>

#define VERSION "1.0"

/* Print all coding system names.  */

void
list_coding ()
{
  MSymbol *codings;
  int i, n;
  char *name;
  int len, clm;

  n = mconv_list_codings (&codings);
  clm = 0;
  for (i = 0; i < n; i++)
    {
      name = msymbol_name (codings[i]);
      len = strlen (name) + 1;
      if (clm + len >= 80)
	{
	  printf ("\n");
	  clm = 0;
	}
      printf (" %s", name);
      clm += len;
    }
  printf ("\n");
  free (codings);
}


/* Print the usage of this program (the name is PROG), and exit with
   EXIT_CODE.  */

void
help_exit (char *prog, int exit_code)
{
  char *p = prog;

  while (*p)
    if (*p++ == '/')
      prog = p;

  printf ("Usage: %s [ OPTION ... ] [ INFILE [ OUTFILE ] ]\n", prog);
  printf ("Convert encoding of given files from one to another.\n");
  printf ("  If INFILE is omitted, the input is taken from standard input.\n");
  printf ("  If OUTFILE is omitted, the output is written to standard output.\n");
  printf ("The following OPTIONs are available.\n");
  printf ("  %-13s %s", "-f FROMCODE",
	  "FROMCODE is the encoding of INFILE (defaults to UTF-8).\n");
  printf ("  %-13s %s", "-t TOCODE",
	  "TOCODE is the encoding of OUTFILE (defaults to UTF-8).\n");
  printf ("  %-13s %s", "-k", "Do not stop conversion on error.\n");
  printf ("  %-13s %s", "-s", "Suppress warnings.\n");
  printf ("  %-13s %s", "-v", "Print progress information.\n");
  printf ("  %-13s %s", "-l", "List available encodings.\n");
  printf ("  %-13s %s", "--version", "Print version number.\n");
  printf ("  %-13s %s", "-h, --help", "Print this message.\n");
  exit (exit_code);
}


/* Check invalid bytes found in the last decoding.  Text property
   Mcharset of such a byte is Mcharset_binary.  */

void
check_invalid_bytes (MText *mt)
{
  int from = 0, to = 0;
  int len = mtext_len (mt);
  int first = 1;

  while (to < len)
    {
      int n = mtext_prop_range (mt, Mcharset, from, NULL, &to, 1);
      MSymbol charset
	= n > 0 ? (MSymbol) mtext_get_prop (mt, from, Mcharset) : Mnil;

      if (charset == Mcharset_binary)
	{
	  if (first)
	    {
	      fprintf (stderr,
		       "Invalid bytes (at each character position);\n");
	      first = 0;
	    }
	  for (; from < to; from++)
	    fprintf (stderr, " 0x%02X(%d)", mtext_ref_char (mt, from), from);
	}
      else
	from = to;
    }
  if (! first)
    fprintf (stderr, "\n");
}


/* Check unencoded characters in the last encoding.  Text property
   Mcoding of such a character is Mnil.  */

void
check_unencoded_chars (MText *mt, int len)
{
  int from = 0, to = 0;
  int first = 1;

  while (to < len)
    {
      int n = mtext_prop_range (mt, Mcoding, from, NULL, &to, 1);
      MSymbol coding
	= n > 0 ? (MSymbol) mtext_get_prop (mt, from, Mcoding) : Mnil;

      if (coding == Mnil)
	{
	  if (first)
	    {
	      fprintf (stderr,
		       "Unencoded characters (at each character position):\n");
	      first = 0;
	    }
	  for (; from < to; from++)
	    fprintf (stderr, " 0x%02X(%d)", mtext_ref_char (mt, from), from);
	}
      else
	from = to;
    }
  if (! first)
    fprintf (stderr, "\n");
}


/* Format MSG by FMT and print the result to the stderr, and exit.  */

#define FATAL_ERROR(fmt, arg)	\
  do {				\
    fprintf (stderr, fmt, arg);	\
    exit (1);			\
  } while (0)


int
main (int argc, char **argv)
{
  int suppress_warning, verbose, continue_on_error;
  MSymbol incode, outcode;
  FILE *in, *out;
  MText *mt;
  MConverter *converter;
  int i;

  /* Initialize the m17n library.  */
  M17N_INIT ();
  if (merror_code != MERROR_NONE)
    FATAL_ERROR ("%s\n", "Fail to initialize the m17n library.");

  /* Default encodings are both UTF-8.  */
  incode = outcode = Mcoding_utf_8;
  /* By default, read from standard input and write to standard output. */
  in = stdin, out = stdout;
  /* By default, all these flags are 0.  */
  suppress_warning = verbose = continue_on_error = 0;
  /* Parse the command line arguments.  */
  for (i = 1; i < argc; i++)
    {
      if (! strcmp (argv[i], "--help")
	       || ! strcmp (argv[i], "-h")
	       || ! strcmp (argv[i], "-?"))
	help_exit (argv[0], 0);
      else if (! strcmp (argv[i], "--version"))
	{
	  printf ("mconv (m17n library) %s\n", VERSION);
	  printf ("Copyright (C) 2003 AIST, JAPAN\n");
	  exit (0);
	}
      else if (! strcmp (argv[i], "-l"))
	{
	  list_coding ();
	  M17N_FINI ();
	  exit (0);
	}
      else if (! strcmp (argv[i], "-f"))
	{
	  incode = mconv_resolve_coding (msymbol (argv[++i]));
	  if (incode == Mnil)
	    FATAL_ERROR ("Unknown encoding: %s\n", argv[i]);
	}
      else if (! strcmp (argv[i], "-t"))
	{
	  outcode = mconv_resolve_coding (msymbol (argv[++i]));
	  if (outcode == Mnil)
	    FATAL_ERROR ("Unknown encoding: %s\n", argv[i]);
	}
      else if (! strcmp (argv[i], "-k"))
	continue_on_error = 1;
      else if (! strcmp (argv[i], "-s"))
	suppress_warning = 1;
      else if (! strcmp (argv[i], "-v"))
	verbose = 1;
      else if (argv[i][0] != '-')
	{
	  if (in == stdin)
	    {
	      in = fopen (argv[i], "r");
	      if (! in)
		FATAL_ERROR ("Can't read the file %s\n", argv[i]);
	    }
	  else if (out == stdout)
	    {
	      out = fopen (argv[i], "w");
	      if (! out)
		FATAL_ERROR ("Can't write the file %s\n", argv[i]);
	    }
	  else
	    help_exit (argv[0], 1);
	}
      else
	help_exit (argv[0], 1);
    }

  /* Create an M-text to store the decoded characters.  */
  mt = mtext ();

  /* Create a converter for decoding.  */
  converter = mconv_stream_converter (incode, in);
  /* Instead of doing strict decoding, we decode all input bytes at
     once, and check invalid bytes later by the fuction
     check_invalid_bytes.  */
  converter->lenient = 1;

  mconv_decode (converter, mt);

  if (! suppress_warning)
    check_invalid_bytes (mt);
  if (verbose)
    fprintf (stderr, "%d bytes (%s) decoded into %d characters,\n",
	     converter->nbytes, msymbol_name (incode), mtext_len (mt));

  mconv_free_converter (converter);

  /* Create a converter for encoding.  */
  converter = mconv_stream_converter (outcode, out);
  /* Instead of doing strict encoding, we encode all characters at
     once, and check unencoded characters later by the fuction
     check_unencoded_chars.  */
  converter->lenient = 1;
  converter->last_block = 1;
  if (mconv_encode (converter, mt) < 0
      && ! suppress_warning)
    fprintf (stderr, "I/O error on writing\n");
  if (! suppress_warning)
    check_unencoded_chars (mt, converter->nchars);
  if (verbose)
    fprintf (stderr, "%d characters encoded into %d bytes (%s).\n",
	     converter->nchars, converter->nbytes, msymbol_name (outcode));

  /* Clear away.  */
  mconv_free_converter (converter);
  m17n_object_unref (mt);
  M17N_FINI ();
  exit (0);
}
#endif /* not FOR_DOXYGEN */
