/* m17n-flt.c -- Font Layout Table sub-module.
   Copyright (C) 2003, 2004, 2007
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
   Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   02111-1307, USA.  */

/***en
    @addtogroup m17nFLT
    @brief FLT support for a window system.

    This section defines the m17n FLT API concerning character
    layouting facility using FLT (Font Layout Table).  */

/*=*/

#if !defined (FOR_DOXYGEN) || defined (DOXYGEN_INTERNAL_MODULE)
/*** @addtogroup m17nInternal
     @{ */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <regex.h>

#include "m17n-core.h"
#include "m17n-flt.h"
#include "m17n-misc.h"
#include "internal.h"
#include "mtext.h"
#include "symbol.h"
#include "plist.h"
#include "database.h"
#include "internal-flt.h"

/* Font Layouter */

/* Font Layout Table (FLT)

Predefined terms: SYMBOL, INTEGER, STRING

FLT ::=	'(' STAGE + ')'

STAGE ::= CATEGORY-TABLE ? FONT-LAYOUT-RULE

;; Each STAGE consumes a source (code sequence) and produces another
;; code sequence that is given to the next STAGE as a source.  The
;; source given to the first stage is a sequence of character codes
;; that are assigned category codes by CATEGORY-TABLE.  The output of
;; the last stage is a glyph code sequence given to the renderer.

CATEGORY-TABLE ::=
	'(' 'category' CATEGORY-SPEC + ')'
CATEGORY-SPEC ::=
	'(' CODE [ CODE ] CATEGORY ')'
CODE ::= INTEGER
CATEGORY ::= INTEGER
;; ASCII character codes of alphabet ('A' .. 'Z' 'a' .. 'z').
;; Ex: CATEGORY-TABLE
;; (category
;;   (0x0900 0x097F	?E)	; All Devanagari characters
;;   (0x093C		?N))	; DEVANAGARI-LETTER NUKTA
;;	Assign the category 'E' to all Devanagari characters but 0x093C,
;;	assign the category 'N' to 0x093C.

FONT-LAYOUT-RULE ::=
	'(' 'generator' RULE MACRO-DEF * ')'

RULE ::= COMMAND | REGEXP-RULE | MATCH-RULE | MAP-RULE
	 | COND-STRUCT | MACRO-NAME

COMMAND ::=
	DIRECT-CODE | COMBINING | PREDEFIND-COMMAND | OTF-COMMAND

DIRECT-CODE ::= INTEGER
;; Always succeed.  Produce the code.  Consume no source.

PREDEFIND-COMMAND ::=
	'=' | '*' | '<' | '>' | '|'

;; '=': Succeed when the current run contains at least one code.
;; Consume the first code in the current run, and produce it as is.

;; '*': If the the previous command succeeded, repeat it until it
;; fails.  

;; '<': Produce a special code that indicates the start of grapheme
;; cluster.  Succeed always, consume nothing.

;; '>': Produce a special code that indicates the end of grapheme
;; cluster.  Succeed always, consume nothing.

;; '|': Produce a special code whose category is ' '.  Succeed always,
;; consume nothing.

OTF-COMMAND ::=
	'otf:''SCRIPT'[':'['LANGSYS'][':'[GSUB-FEATURES][':'GPOS-FEATURES]]]
;; Run the Open Type Layout Table on the current run.  Succeed always,
;; consume nothing.

SCRIPT ::= OTF-TAG
;;	OTF's ScriptTag name (four letters) listed at:
;;	<http://www.microsoft.om/typograph/otspec/scripttags.htm>
LANGSYS ::= OTF-TAG
;;	OTF's Language System name (four letters) listed at:
;;	<http://www.microsoft.om/typograph/otspec/languagetags.htm>

GSUB-FEATURES ::= [FEATURE[,FEATURE]*] | ' '
GPOS-FEATURES ::= [FEATURE[,FEATURE]*] | ' '
FEATURE ::= OTF-TAG
;;	OTF's Feature name (four letters) listed at:
;;	<http://www.microsoft.om/typograph/otspec/???.htm>

OTF-TAG ::= PRINTABLE-CHAR PRINTABLE-CHAR PRINTABLE-CHAR PRINTABLE-CHAR

;; Ex. OTF-COMMAND
;; 'otf:deva'
;;	Run all features in the default langsys of 'deva' script.
;; 'otf:deva::nukt:haln'
;;	Run all GSUB features, run 'nukt' and 'haln' GPOS features.
;; 'otf:deva:: :'
;;	Run all GSUB features, run no GPOS features.

REGEXP-RULE ::=
	'(' REGEXP RULE * ')'

;; Succeed if REGXP matches the head of source.  Run RULEs while
;; limiting the source to the matching part.  Consume that part.

REGEXP ::= STRING
;; Must be composed only from ASCII characters.  'A' - 'Z', 'a' - 'z'
;; correspond to CATEGORY.

;; Ex: REGEXP-RULE
;; ("VA?"
;;   < | vowel * | >)

MATCH-RULE ::=
	'(' MATCH-IDX RULE * ')'

;; Succeed if the previous REGEXP-RULE found a matching part for
;; MATCH-IDX.  Run RULEs while limiting the source to the matching
;; part.  If MATCH-IDX is zero, consume the whole part, else consume
;; nothing.

MATCH-IDX ::= INTEGER
;; Must be 0..20.

;; Ex. MATCH-RULE
;; (2 consonant *)

MAP-RULE ::=
	'(' ( SOURCE-SEQ | SOURCE-RANGE ) RULE * ')'

;; Succeed if the source matches SOURCE-SEQ or SOURCE-RANGE.  Run
;; RULEs while limiting the source to the matching part.  Consume that
;; part.

SOURCE-SEQ ::=
	'(' CODE + ')'
SOURCE-RANGE ::=
	'(' 'range' CODE CODE ')'
;; Ex. MAP-RULE
;; ((0x0915 0x094D)		0x43)
;;	If the source code sequence is 0x0915 0x094D, produce 0x43.
;; ((range 0x0F40 0x0F6A)	0x2221)
;;	If the first source code CODE is in the range 0x0F40..0x0F6A, 
;;	produce (0x2221 + (CODE - 0x0F40)).

COND-STRUCT ::=
	'(' 'cond' RULE + ')'

;; Try each rule in sequence until one succeeds.  Succeed if one
;; succeeds.  Consume nothing.

;; Ex. COND-STRUCT
;; (cond
;;  ((0x0915 0x094D)		0x43)
;;  ((range 0x0F40 0x0F6A)	0x2221)
;;  = )

COMBINING ::= 'V''H''O''V''H'
V ::= ( 't' | 'c' | 'b' | 'B' )
H ::= ( 'l' | 'c' | 'r' )
O ::= ( '.' | XOFF | YOFF | XOFF YOFF )
XOFF ::= '<'INTEGER | '>'INTEGER 
YOFF ::= '+'INTEGER | '-'INTEGER
;; INTEGER must be integer 0..127

;; VH pair indicates 12 reference points of a glyph as below:
;;
;;   0----1----2 <---- ascent	 0:tl (top-left)
;;   |         |		 1:tc (top-center)
;;   |         |		 2:tr (top-right)
;;   |         |		 3:Bl (base-left)
;;   9   10   11 <---- center	 4:Bc (base-center)
;;   |         |		 5:Br (base-right)
;; --3----4----5-- <-- baseline	 6:bl (bottom-left)
;;   |         |		 7:bc (bottom-center)
;;   6----7----8 <---- descent	 8:br (bottom-right)
;;				 9:cl (center-left)
;;   |    |    |		10:cc (center-center)
;; left center right		11:cr (center-right)
;;
;; Ex. COMBINING
;; 'tc.bc':
;;	Align top-left point of the previous glyph and bottom-center
;;	point of the current glyph.
;; 'Bl<20-10Br'
;;	Align 20% left and 10% below of base-left point of the previous
;;	glyph and base-right point of the current glyph.

MACRO-DEF ::=
	'(' MACRO-NAME RULE + ')'
MACRO-NAME ::= SYMBOL

*/

static int mdebug_flag = MDEBUG_FONT_FLT;

MSymbol Mfont, Mlayouter, Mcombining;

static MSymbol Mgenerator, Mend;

static MPlist *flt_list;
static int flt_min_coverage, flt_max_coverage;

enum GlyphInfoMask
{
  CombiningCodeMask = 0xFFFFFFF,
  LeftPaddingMask = 1 << 28,
  RightPaddingMask = 1 << 29
};

#define SET_GLYPH_INFO(g, mask, ctx, info)			\
  ((g)->internal = (((g)->internal & ~(mask)) | (info)),	\
   (ctx)->check_mask |= (mask))

#define GET_COMBINING_CODE(g) ((g)->internal & CombiningCodeMask)
#define SET_COMBINING_CODE(g, ctx, code)	\
  SET_GLYPH_INFO (g, CombiningCodeMask, ctx, code)
#define GET_LEFT_PADDING(g) ((g)->internal & LeftPaddingMask)
#define SET_LEFT_PADDING(g, ctx, flag)	\
  SET_GLYPH_INFO (g, LeftPaddingMask, ctx, flag)
#define GET_RIGHT_PADDING(g) ((g)->internal & RightPaddingMask)
#define SET_RIGHT_PADDING(g, ctx, flag)	\
  SET_GLYPH_INFO (g, RightPaddingMask, ctx, flag)
#define GET_ENCODED(g) ((g)->encoded)
#define SET_ENCODED(g, flag) ((g)->encoded = (flag))
#define GET_MEASURED(g) ((g)->measured)
#define SET_MEASURED(g, flag) ((g)->measured = (flag))

#define GINIT(gstring, n)					\
  do {								\
    if (! (gstring)->glyph_size)				\
      (gstring)->glyph_size = sizeof (MFLTGlyph);		\
    (gstring)->glyphs = alloca ((gstring)->glyph_size * (n));	\
    (gstring)->allocated = (n);					\
    (gstring)->used = 0;					\
  } while (0)

#define GALLOCA (gstring)	\
  ((MFLTGlyph *) alloca ((gstring)->glyph_size))

#define GREF(gstring, idx)	\
  ((MFLTGlyph *) ((char *) ((gstring)->glyphs) + (gstring)->glyph_size * (idx)))

#define PREV(gstring, g)	\
  ((MFLTGlyph *) ((char *) (g) - (gstring)->glyph_size))

#define NEXT(gstring, g)	\
  ((MFLTGlyph *) ((char *) (g) + (gstring)->glyph_size))

#define GCPY(src, src_idx, n, tgt, tgt_idx)				\
  do {									\
    memcpy ((char *) ((tgt)->glyphs) + (tgt)->glyph_size * (tgt_idx),	\
	    (char *) ((src)->glyphs) + (src)->glyph_size * (src_idx),	\
	    (src)->glyph_size * (n));					\
  } while (0)

#define GDUP(ctx, idx)				\
  do {						\
    MFLTGlyphString *src = (ctx)->in;		\
    MFLTGlyphString *tgt = (ctx)->out;		\
    if (tgt->allocated <= tgt->used)		\
      return -2;				\
    GCPY (src, (idx), 1, tgt, tgt->used);	\
    tgt->used++;				\
  } while (0)

static int
GREPLACE (MFLTGlyphString *src, int src_from, int src_to,
	  MFLTGlyphString *tgt, int tgt_from, int tgt_to)
{
  int src_len = src_to - src_from;
  int tgt_len = tgt_to - tgt_from;
  int inc = src_len - tgt_len;

  if (tgt->allocated < tgt->used + inc)
    return -2;
  if (inc != 0 && tgt_to < tgt->used)
    memmove ((char *) tgt->glyphs + tgt->glyph_size * (tgt_from + src_len),
	     (char *) tgt->glyphs + tgt->glyph_size * tgt_to,
	     tgt->glyph_size * (tgt->used - tgt_to));
  if (src_len)
    memcpy ((char *) tgt->glyphs + tgt->glyph_size * tgt_from,
	    (char *) src->glyphs + src->glyph_size * src_from,
	    src->glyph_size * src_len);
  tgt->used += inc;
  return 0;
}


/* Command ID:
	         0 ...		: direct code
	           -1		: invalid
	     -0x0F .. -2	: builtin commands
	-0x100000F .. -0x10	: combining code
	          ... -0x1000010: index to FontLayoutStage->cmds
 */

#define INVALID_CMD_ID -1
#define CMD_ID_OFFSET_BUILTIN	-3
#define CMD_ID_OFFSET_COMBINING	-0x10
#define CMD_ID_OFFSET_INDEX	-0x1000010

/* Builtin commands. */
#define CMD_ID_COPY		-3 /* '=' */
#define CMD_ID_REPEAT		-4 /* '*' */
#define CMD_ID_CLUSTER_BEGIN	-5 /* '<' */
#define CMD_ID_CLUSTER_END	-6 /* '>' */
#define CMD_ID_SEPARATOR	-7 /* '|' */
#define CMD_ID_LEFT_PADDING	-8 /* '[' */
#define CMD_ID_RIGHT_PADDING	-9 /* ']' */

#define CMD_ID_TO_COMBINING_CODE(id) (CMD_ID_OFFSET_COMBINING - (id))
#define COMBINING_CODE_TO_CMD_ID(code) (CMD_ID_OFFSET_COMBINING - (code))

#define CMD_ID_TO_INDEX(id) (CMD_ID_OFFSET_INDEX - (id))
#define INDEX_TO_CMD_ID(idx) (CMD_ID_OFFSET_INDEX - (idx))

static MSymbol Mcond, Mrange, Mfont_facility;

#define GLYPH_CODE_P(code)	\
  ((code) >= GLYPH_CODE_MIN && (code) <= GLYPH_CODE_MAX)

#define GLYPH_CODE_INDEX(code) ((code) - GLYPH_CODE_MIN)

#define UPDATE_CLUSTER_RANGE(ctx, g)		\
  do {						\
    if ((ctx)->cluster_begin_idx >= 0)		\
      {						\
	if (ctx->cluster_begin_pos > (g)->from)	\
	  ctx->cluster_begin_pos = (g)->from;	\
	if (ctx->cluster_end_pos < (g)->to)	\
	  ctx->cluster_end_pos = (g)->to;	\
      }						\
  } while (0)

enum FontLayoutCmdRuleSrcType
  {
    SRC_REGEX,
    SRC_INDEX,
    SRC_SEQ,
    SRC_RANGE,
    SRC_HAS_GLYPH,
    SRC_OTF_SPEC
  };

typedef struct
{
  enum FontLayoutCmdRuleSrcType src_type;
  union {
    struct {
      char *pattern;
      regex_t preg;
    } re;
    int match_idx;
    struct {
      int n_codes;
      int *codes;
    } seq;
    struct {
      int from, to;
    } range;
    int supported_glyph;
    MFLTOtfSpec otf_spec;
  } src;

  int n_cmds;
  int *cmd_ids;
} FontLayoutCmdRule;

typedef struct
{
  /* Beginning and end indices of series of SEQ commands.  */
  int seq_beg, seq_end;
  /* Range of the first character appears in the above series.  */
  int seq_from, seq_to;

  int n_cmds;
  int *cmd_ids;
} FontLayoutCmdCond;

enum FontLayoutCmdType
  {
    FontLayoutCmdTypeRule,
    FontLayoutCmdTypeCond,
    FontLayoutCmdTypeOTF,
    FontLayoutCmdTypeMAX
  };

typedef struct
{
  enum FontLayoutCmdType type;
  union {
    FontLayoutCmdRule rule;
    FontLayoutCmdCond cond;
    MFLTOtfSpec otf;
  } body;
} FontLayoutCmd;

typedef struct 
{
  MCharTable *category;
  int size, inc, used;
  FontLayoutCmd *cmds;
} FontLayoutStage;

struct _MFLT
{
  MSymbol name;
  MSymbol family;
  MSymbol registry;
  MFLTOtfSpec otf;
  MDatabase *mdb;
  MCharTable *coverage;
  MPlist *stages;
};

/* Font layout table loader */

static int parse_otf_command (MSymbol symbol, MFLTOtfSpec *spec);

/* Load a category table from PLIST.  PLIST has this form:
      PLIST ::= ( FROM-CODE TO-CODE ? CATEGORY-CHAR ) *
*/

static MCharTable *
load_category_table (MPlist *plist)
{
  MCharTable *table;

  table = mchartable (Minteger, (void *) 0);

  MPLIST_DO (plist, plist)
    {
      MPlist *elt;
      int from, to, category_code;

      if (! MPLIST_PLIST (plist))
	MERROR (MERROR_FONT, NULL);
      elt = MPLIST_PLIST (plist);
      if (! MPLIST_INTEGER_P (elt))
	MERROR (MERROR_FONT, NULL);
      from = MPLIST_INTEGER (elt);
      elt = MPLIST_NEXT (elt);
      if (! MPLIST_INTEGER_P (elt))
	MERROR (MERROR_FONT, NULL);
      to = MPLIST_INTEGER (elt);
      elt = MPLIST_NEXT (elt);
      if (MPLIST_TAIL_P (elt))
	{
	  category_code = to;
	  to = from;
	}
      else
	{
	  if (! MPLIST_INTEGER_P (elt))
	    MERROR (MERROR_FONT, NULL);
	  category_code = MPLIST_INTEGER (elt);
	}
      if (! isalnum (category_code))
	MERROR (MERROR_FONT, NULL);

      if (from == to)
	mchartable_set (table, from, (void *) category_code);
      else
	mchartable_set_range (table, from, to, (void *) category_code);
    }

  return table;
}

static unsigned int
gen_otf_tag (char *p)
{
  unsigned int tag = 0;
  int i;

  for (i = 0; i < 4 && *p; i++, p++)
    tag = (tag << 8) | *p;
  for (; i < 4; i++)
    tag = (tag << 8) | 0x20;
  return tag;
}

static char *
otf_count_features (char *p, char *end, char stopper, int *count)
{
  int negative = 0;

  *count = 0;
  if (*p != stopper && *p != '\0')
    while (1)
      {
	(*count)++;
	if (*p == '*')
	  {
	    p++;
	    if (*p == stopper || *p == '\0')
	      break;
	    return NULL;
	  }
	if (*p == '~')
	  {
	    if (negative++ == 0)
	      (*count)++;
	    p += 5;
	  }
	else 
	  p += 4;
	if (p > end)
	  return NULL;
	if (*p == stopper || *p == '\0')
	  break;
	if (*p != ',')
	  return NULL;
	p++;
	if (! *p)
	  return NULL;
      }
  return p;
}

static void
otf_store_features (char *p, char *end, unsigned *buf)
{
  int negative = 0;
  int i;

  for (i = 0; p < end;)
    {
      if (*p == '*')
	buf[i++] = 0xFFFFFFFF, p += 2, negative = 1;
      else if (*p == '~')
	{
	  if (negative++ == 0)
	    buf[i++] = 0xFFFFFFFF;
	  buf[i++] = gen_otf_tag (p + 1), p += 6;
	}
      else
	buf[i++] = gen_otf_tag (p), p += 5;
    }
  buf[i] = 0;
}

static int
parse_otf_command (MSymbol symbol, MFLTOtfSpec *spec)
{
  char *str = MSYMBOL_NAME (symbol);
  char *end = str + MSYMBOL_NAMELEN (symbol);
  unsigned int script, langsys;
  char *gsub, *gpos;
  int gsub_count = 0, gpos_count = 0;
  char *p;

  memset (spec, 0, sizeof (MFLTOtfSpec));

  spec->sym = symbol;
  str += 5;			/* skip the heading ":otf=" */
  script = gen_otf_tag (str);
  str += 4;
  if (*str == '/')
    {
      langsys = gen_otf_tag (str);
      str += 4;
    }
  else
    langsys = 0;
  gsub = str;
  if (*str != '=')
    /* Apply all GSUB features.  */
      gsub_count = 1;
  else
    {
      p = str + 1;
      str = otf_count_features (p, end, '+', &gsub_count);
      if (! str)
	MERROR (MERROR_FLT, -1);
    }
  gpos = str;
  if (*str != '+')
    /* Apply all GPOS features.  */
    gpos_count = 1;
  else
    {
      p = str + 1;
      str = otf_count_features (p, end, '\0', &gpos_count);
      if (! str)
	MERROR (MERROR_FLT, -1);
    }

  spec->script = script;
  spec->langsys = langsys;
  if (gsub_count > 0)
    {
      spec->features[0] = malloc (sizeof (int) * (gsub_count + 1));
      if (! spec->features[0])
	return -2;
      if (*gsub == '=')
	otf_store_features (gsub + 1, gpos, spec->features[0]);
      else
	spec->features[0][0] = 0xFFFFFFFF, spec->features[0][1] = 0;
    }
  if (gpos_count > 0)
    {
      spec->features[1] = malloc (sizeof (int) * (gpos_count + 1));
      if (! spec->features[1])
	{
	  if (spec->features[0])
	    free (spec->features[0]);
	  return -2;
	}
      if (*gpos == '+')
	otf_store_features (gpos + 1, str, spec->features[1]);
      else
	spec->features[1][0] = 0xFFFFFFFF, spec->features[1][1] = 0;
    }
  return 0;
}


/* Parse OTF command name NAME and store the result in CMD.
   NAME has this form:
	:SCRIPT[/[LANGSYS][=[GSUB-FEATURES][+GPOS-FEATURES]]]
   where GSUB-FEATURES and GPOS-FEATURES have this form:
	[FEATURE[,FEATURE]*] | ' '  */

static int
load_otf_command (FontLayoutCmd *cmd, MSymbol sym)
{
  char *name = MSYMBOL_NAME (sym);
  int result;

  if (name[0] != ':')
    {
      /* This is old format of "otf:...".  Change it to ":otf=...".  */
      char *str = alloca (MSYMBOL_NAMELEN (sym) + 2);

      sprintf (str, ":otf=");
      strcat (str, name + 4);
      sym = msymbol (str);
    }

  result = parse_otf_command (sym, &cmd->body.otf);
  if (result == -2)
    return result;
  cmd->type = FontLayoutCmdTypeOTF;
  return 0;
}


/* Read a decimal number from STR preceded by one of "+-><".  '+' and
   '>' means a plus sign, '-' and '<' means a minus sign.  If the
   number is greater than 127, limit it to 127.  */

static int
read_decimal_number (char **str)
{
  char *p = *str;
  int sign = (*p == '-' || *p == '<') ? -1 : 1;
  int n = 0;

  p++;
  while (*p >= '0' && *p <= '9')
    n = n * 10 + *p++ - '0';
  *str = p;
  if (n == 0)
    n = 5;
  return (n < 127 ? n * sign : 127 * sign);
}


/* Read a horizontal and vertical combining positions from STR, and
   store them in the place pointed by X and Y.  The horizontal
   position left, center, and right are represented by 0, 1, and 2
   respectively.  The vertical position top, center, bottom, and base
   are represented by 0, 1, 2, and 3 respectively.  If successfully
   read, return 0, else return -1.  */

static int
read_combining_position (char *str, int *x, int *y)
{
  int c = *str++;
  int i;

  /* Vertical position comes first.  */
  for (i = 0; i < 4; i++)
    if (c == "tcbB"[i])
      {
	*y = i;
	break;
      }
  if (i == 4)
    return -1;
  c = *str;
  /* Then comse horizontal position.  */
  for (i = 0; i < 3; i++)
    if (c == "lcr"[i])
      {
	*x = i;
	return 0;
      }
  return -1;
}


/* Return a combining code corresponding to SYM.  */

static int
get_combining_command (MSymbol sym)
{
  char *str = msymbol_name (sym);
  int base_x, base_y, add_x, add_y, off_x, off_y;
  int c;

  if (read_combining_position (str, &base_x, &base_y) < 0)
    return 0;
  str += 2;
  c = *str;
  if (c == '.')
    {
      off_x = off_y = 128;
      str++;
    }
  else
    {
      if (c == '+' || c == '-')
	{
	  off_y = read_decimal_number (&str) + 128;
	  c = *str;
	}
      else
	off_y = 128;
      if (c == '<' || c == '>')
	off_x = read_decimal_number (&str) + 128;
      else
	off_x = 128;
    }
  if (read_combining_position (str, &add_x, &add_y) < 0)
    return 0;

  c = MAKE_COMBINING_CODE (base_y, base_x, add_y, add_x, off_y, off_x);
  return (COMBINING_CODE_TO_CMD_ID (c));
}


/* Load a command from PLIST into STAGE, and return that
   identification number.  If ID is not INVALID_CMD_ID, that means we
   are loading a top level command or a macro.  In that case, use ID
   as the identification number of the command.  Otherwise, generate a
   new id number for the command.  MACROS is a list of raw macros.  */

static int
load_command (FontLayoutStage *stage, MPlist *plist,
	      MPlist *macros, int id)
{
  int i;
  int result;

  if (MPLIST_INTEGER_P (plist))
    {
      int code = MPLIST_INTEGER (plist);

      if (code < 0)
	MERROR (MERROR_DRAW, INVALID_CMD_ID);
      return code;
    }
  else if (MPLIST_PLIST_P (plist))
    {
      /* PLIST ::= ( cond ... ) | ( STRING ... ) | ( INTEGER ... )
 	           | ( ( INTEGER INTEGER ) ... )
		   | ( ( range INTEGER INTEGER ) ... )
		   | ( ( font-facilty [ INTEGER ] ) ... )
		   | ( ( font-facilty OTF-SPEC ) ... )  */
      MPlist *elt = MPLIST_PLIST (plist);
      int len = MPLIST_LENGTH (elt) - 1;
      FontLayoutCmd *cmd;

      if (id == INVALID_CMD_ID)
	{
	  FontLayoutCmd dummy;
	  id = INDEX_TO_CMD_ID (stage->used);
	  MLIST_APPEND1 (stage, cmds, dummy, MERROR_DRAW);
	}
      cmd = stage->cmds + CMD_ID_TO_INDEX (id);

      if (MPLIST_SYMBOL_P (elt))
	{
	  FontLayoutCmdCond *cond;

	  if (MPLIST_SYMBOL (elt) != Mcond)
	    MERROR (MERROR_DRAW, INVALID_CMD_ID);
	  elt = MPLIST_NEXT (elt);
	  cmd->type = FontLayoutCmdTypeCond;
	  cond = &cmd->body.cond;
	  cond->seq_beg = cond->seq_end = -1;
	  cond->seq_from = cond->seq_to = 0;
	  cond->n_cmds = len;
	  MTABLE_CALLOC (cond->cmd_ids, len, MERROR_DRAW);
	  for (i = 0; i < len; i++, elt = MPLIST_NEXT (elt))
	    {
	      int this_id = load_command (stage, elt, macros, INVALID_CMD_ID);

	      if (this_id == INVALID_CMD_ID || this_id == -2)
		MERROR (MERROR_DRAW, this_id);
	      /* The above load_command may relocate stage->cmds.  */
	      cmd = stage->cmds + CMD_ID_TO_INDEX (id);
	      cond = &cmd->body.cond;
	      cond->cmd_ids[i] = this_id;
	      if (this_id <= CMD_ID_OFFSET_INDEX)
		{
		  FontLayoutCmd *this_cmd
		    = stage->cmds + CMD_ID_TO_INDEX (this_id);

		  if (this_cmd->type == FontLayoutCmdTypeRule
		      && this_cmd->body.rule.src_type == SRC_SEQ)
		    {
		      int first_char = this_cmd->body.rule.src.seq.codes[0];

		      if (cond->seq_beg < 0)
			{
			  /* The first SEQ command.  */
			  cond->seq_beg = i;
			  cond->seq_from = cond->seq_to = first_char;
			}
		      else if (cond->seq_end < 0)
			{
			  /* The following SEQ command.  */
			  if (cond->seq_from > first_char)
			    cond->seq_from = first_char;
			  else if (cond->seq_to < first_char)
			    cond->seq_to = first_char;
			}
		    }
		  else
		    {
		      if (cond->seq_beg >= 0 && cond->seq_end < 0)
			/* The previous one is the last SEQ command.  */
			cond->seq_end = i;
		    }
		}
	      else
		{
		  if (cond->seq_beg >= 0 && cond->seq_end < 0)
		    /* The previous one is the last SEQ command.  */
		    cond->seq_end = i;
		}
	    }
	  if (cond->seq_beg >= 0 && cond->seq_end < 0)
	    /* The previous one is the last SEQ command.  */
	    cond->seq_end = i;
	}
      else
	{
	  cmd->type = FontLayoutCmdTypeRule;
	  if (MPLIST_MTEXT_P (elt))
	    {
	      MText *mt = MPLIST_MTEXT (elt);
	      char *str = (char *) MTEXT_DATA (mt);

	      if (str[0] != '^')
		{
		  mtext_ins_char (mt, 0, '^', 1);
		  str = (char *) MTEXT_DATA (mt);
		}
	      if (regcomp (&cmd->body.rule.src.re.preg, str, REG_EXTENDED))
		MERROR (MERROR_FONT, INVALID_CMD_ID);
	      cmd->body.rule.src_type = SRC_REGEX;
	      cmd->body.rule.src.re.pattern = strdup (str);
	    }
	  else if (MPLIST_INTEGER_P (elt))
	    {
	      cmd->body.rule.src_type = SRC_INDEX;
	      cmd->body.rule.src.match_idx = MPLIST_INTEGER (elt);
	    }
	  else if (MPLIST_PLIST_P (elt))
	    {
	      MPlist *pl = MPLIST_PLIST (elt);
	      int size = MPLIST_LENGTH (pl);

	      if (MPLIST_INTEGER_P (pl))
		{
		  int i;

		  cmd->body.rule.src_type = SRC_SEQ;
		  cmd->body.rule.src.seq.n_codes = size;
		  MTABLE_CALLOC (cmd->body.rule.src.seq.codes, size,
				 MERROR_FONT);
		  for (i = 0; i < size; i++, pl = MPLIST_NEXT (pl))
		    {
		      if (! MPLIST_INTEGER_P (pl))
			MERROR (MERROR_DRAW, INVALID_CMD_ID);
		      cmd->body.rule.src.seq.codes[i]
			= (unsigned) MPLIST_INTEGER (pl);
		    }
		}
	      else if (MPLIST_SYMBOL_P (pl) && size == 3)
		{
		  if (MPLIST_SYMBOL (pl) != Mrange)
		    MERROR (MERROR_FLT, INVALID_CMD_ID);
		  cmd->body.rule.src_type = SRC_RANGE;
		  pl = MPLIST_NEXT (pl);
		  if (! MPLIST_INTEGER_P (pl))
		    MERROR (MERROR_DRAW, INVALID_CMD_ID);
		  cmd->body.rule.src.range.from
		    = (unsigned) MPLIST_INTEGER (pl);
		  pl = MPLIST_NEXT (pl);
		  if (! MPLIST_INTEGER_P (pl))
		    MERROR (MERROR_DRAW, INVALID_CMD_ID);
		  cmd->body.rule.src.range.to
		    = (unsigned) MPLIST_INTEGER (pl);
		}
	      else if (MPLIST_SYMBOL_P (pl) && size <= 2)
		{
		  if (MPLIST_SYMBOL (pl) != Mfont_facility)
		    MERROR (MERROR_FLT, INVALID_CMD_ID);
		  pl = MPLIST_NEXT (pl);
		  if (MPLIST_SYMBOL_P (pl))
		    {
		      MSymbol sym = MPLIST_SYMBOL (pl);
		      char *otf_spec = MSYMBOL_NAME (sym);

		      if (otf_spec[0] == ':' && otf_spec[1] == 'o'
			  && otf_spec[2] == 't' && otf_spec[3] == 'f')
			parse_otf_command (sym, &cmd->body.rule.src.otf_spec);
		      else
			MERROR (MERROR_FLT, INVALID_CMD_ID);
		      cmd->body.rule.src_type = SRC_OTF_SPEC;
		    }
		  else
		    {
		      cmd->body.rule.src_type = SRC_HAS_GLYPH;
		      if (MPLIST_INTEGER_P (pl))
			cmd->body.rule.src.supported_glyph
			  = MPLIST_INTEGER (pl);
		      else
			cmd->body.rule.src.supported_glyph = -1;
		    }
		}
	      else
		MERROR (MERROR_DRAW, INVALID_CMD_ID);
	    }
	  else
	    MERROR (MERROR_DRAW, INVALID_CMD_ID);

	  elt = MPLIST_NEXT (elt);
	  cmd->body.rule.n_cmds = len;
	  MTABLE_CALLOC (cmd->body.rule.cmd_ids, len, MERROR_DRAW);
	  for (i = 0; i < len; i++, elt = MPLIST_NEXT (elt))
	    {
	      int this_id = load_command (stage, elt, macros, INVALID_CMD_ID);

	      if (this_id == INVALID_CMD_ID || this_id == -2)
		MERROR (MERROR_DRAW, this_id);
	      /* The above load_command may relocate stage->cmds.  */
	      cmd = stage->cmds + CMD_ID_TO_INDEX (id);
	      cmd->body.rule.cmd_ids[i] = this_id;
	    }
	}
    }
  else if (MPLIST_SYMBOL_P (plist))
    {
      MPlist *elt;
      MSymbol sym = MPLIST_SYMBOL (plist);
      char *name = msymbol_name (sym);
      int len = strlen (name);
      FontLayoutCmd cmd;

      if (len > 4
	  && ((name[0] == 'o' && name[1] == 't'
	       && name[2] == 'f' && name[3] == ':')
	      || (name[0] == ':' && name[1] == 'o' && name[2] == 't'
		  && name[3] == 'f' && name[4] == '=')))
	{
	  result = load_otf_command (&cmd, sym);
	  if (result < 0)
	    return result;
	  if (id == INVALID_CMD_ID)
	    {
	      id = INDEX_TO_CMD_ID (stage->used);
	      MLIST_APPEND1 (stage, cmds, cmd, MERROR_DRAW);
	    }
	  else
	    stage->cmds[CMD_ID_TO_INDEX (id)] = cmd;
	  return id;
	}

      if (len == 1)
	{
	  if (*name == '=')
	    return CMD_ID_COPY;
	  else if (*name == '*')
	    return CMD_ID_REPEAT;
	  else if (*name == '<')
	    return CMD_ID_CLUSTER_BEGIN;
	  else if (*name == '>')
	    return CMD_ID_CLUSTER_END;
	  else if (*name == '|')
	    return CMD_ID_SEPARATOR;
	  else if (*name == '[')
	    return CMD_ID_LEFT_PADDING;
	  else if (*name == ']')
	    return CMD_ID_RIGHT_PADDING;
	  else
	    id = 0;
	}
      else
	{
	  id = get_combining_command (sym);
	  if (id)
	    return id;
	}

      i = 1;
      MPLIST_DO (elt, macros)
	{
	  if (sym == MPLIST_SYMBOL (MPLIST_PLIST (elt)))
	    {
	      id = INDEX_TO_CMD_ID (i);
	      if (stage->cmds[i].type == FontLayoutCmdTypeMAX)
		id = load_command (stage, MPLIST_NEXT (MPLIST_PLIST (elt)),
				   macros, id);
	      return id;
	    }
	  i++;
	}
      MERROR (MERROR_DRAW, INVALID_CMD_ID);
    }
  else
    MERROR (MERROR_DRAW, INVALID_CMD_ID);

  return id;
}

static void
free_flt_command (FontLayoutCmd *cmd)
{
  if (cmd->type == FontLayoutCmdTypeRule)
    {
      FontLayoutCmdRule *rule = &cmd->body.rule;

      if (rule->src_type == SRC_REGEX)
	{
	  free (rule->src.re.pattern);
	  regfree (&rule->src.re.preg);
	}
      else if (rule->src_type == SRC_SEQ)
	free (rule->src.seq.codes);
      free (rule->cmd_ids);
    }
  else if (cmd->type == FontLayoutCmdTypeCond)
    free (cmd->body.cond.cmd_ids);
  else if (cmd->type == FontLayoutCmdTypeOTF)
    {
      if (cmd->body.otf.features[0])
	free (cmd->body.otf.features[0]);
      if (cmd->body.otf.features[1])
	free (cmd->body.otf.features[1]);
    }
}

/* Load a generator from PLIST into a newly allocated FontLayoutStage,
   and return it.  PLIST has this form:
      PLIST ::= ( COMMAND ( CMD-NAME COMMAND ) * )
*/

static FontLayoutStage *
load_generator (MPlist *plist)
{
  FontLayoutStage *stage;
  MPlist *elt, *pl;
  FontLayoutCmd dummy;
  int result;

  MSTRUCT_CALLOC (stage, MERROR_DRAW);
  MLIST_INIT1 (stage, cmds, 32);
  dummy.type = FontLayoutCmdTypeMAX;
  MLIST_APPEND1 (stage, cmds, dummy, MERROR_FONT);
  MPLIST_DO (elt, MPLIST_NEXT (plist))
    {
      if (! MPLIST_PLIST_P (elt))
	MERROR (MERROR_FONT, NULL);
      pl = MPLIST_PLIST (elt);
      if (! MPLIST_SYMBOL_P (pl))
	MERROR (MERROR_FONT, NULL);
      MLIST_APPEND1 (stage, cmds, dummy, MERROR_FONT);
    }

  /* Load the first command from PLIST into STAGE->cmds[0].  Macros
     called in the first command are also loaded from MPLIST_NEXT
     (PLIST) into STAGE->cmds[n].  */
  result = load_command (stage, plist, MPLIST_NEXT (plist),
			 INDEX_TO_CMD_ID (0));
  if (result == INVALID_CMD_ID || result == -2)
    {
      MLIST_FREE1 (stage, cmds);
      free (stage);
      return NULL;
    }

  return stage;
}


/* Load stages of the font layout table FLT.  */

static int
load_flt (MFLT *flt, MPlist *key_list)
{
  MPlist *top, *plist, *pl, *p;
  MCharTable *category = NULL;
  MSymbol sym;

  if (key_list)
    top = (MPlist *) mdatabase__load_for_keys (flt->mdb, key_list);
  else
    top = (MPlist *) mdatabase_load (flt->mdb);
  if (! top)
    return -1;
  if (! MPLIST_PLIST_P (top))
    {
      M17N_OBJECT_UNREF (top);
      MERROR (MERROR_FLT, -1);
    }

  if (key_list)
    {
      plist = mdatabase__props (flt->mdb);
      if (! plist)
	MERROR (MERROR_FLT, -1);
      MPLIST_DO (plist, plist)
	if (MPLIST_PLIST_P (plist))
	  {
	    pl = MPLIST_PLIST (plist);
	    if (! MPLIST_SYMBOL_P (pl)
		|| MPLIST_SYMBOL (pl) != Mfont)
	      continue;
	    pl = MPLIST_NEXT (pl);
	    if (! MPLIST_PLIST_P (pl))
	      continue;
	    p = MPLIST_PLIST (pl);
	    if (! MPLIST_SYMBOL_P (p))
	      continue;
	    p = MPLIST_NEXT (p);
	    if (! MPLIST_SYMBOL_P (p))
	      continue;
	    flt->family = MPLIST_SYMBOL (p);
	    MPLIST_DO (p, MPLIST_NEXT (p))
	      if (MPLIST_SYMBOL_P (p))
		{
		  sym = MPLIST_SYMBOL (p);
		  if (MSYMBOL_NAME (sym)[0] != ':')
		    flt->registry = sym, sym = Mnil;
		  else
		    break;
		}
	    if (sym)
	      {
		char *otf_spec = MSYMBOL_NAME (sym);

		if (otf_spec[0] == ':' && otf_spec[1] == 'o'
		    && otf_spec[2] == 't' && otf_spec[3] == 'f')
		  parse_otf_command (sym, &flt->otf);
	      }
	    break;
	  }
    }
  MPLIST_DO (plist, top)
    {
      if (MPLIST_SYMBOL_P (plist)
	  && MPLIST_SYMBOL (plist) == Mend)
	{
	  mplist_set (plist, Mnil, NULL);
	  break;
	}
      if (! MPLIST_PLIST (plist))
	continue;
      pl = MPLIST_PLIST (plist);
      if (! MPLIST_SYMBOL_P (pl))
	continue;
      sym = MPLIST_SYMBOL (pl);
      pl = MPLIST_NEXT (pl);
      if (! pl)
	continue;
      if (sym == Mcategory)
	{
	  if (category)
	    M17N_OBJECT_UNREF (category);
	  else if (flt->coverage)
	    {
	      category = flt->coverage;
	      continue;
	    }
	  category = load_category_table (pl);
	  if (! flt->coverage)
	    {
	      flt->coverage = category;
	      M17N_OBJECT_REF (category);
	    }
	}
      else if (sym == Mgenerator)
	{
	  FontLayoutStage *stage;

	  if (! category)
	    break;
	  stage = load_generator (pl);
	  if (! stage)
	    break;
	  stage->category = category;
	  M17N_OBJECT_REF (category);
	  if (! flt->stages)
	    flt->stages = mplist ();
	  mplist_add (flt->stages, Mt, stage);
	}
    }
  if (category)
    M17N_OBJECT_UNREF (category);
  M17N_OBJECT_UNREF (top);
  if (! MPLIST_TAIL_P (plist))
    {
      M17N_OBJECT_UNREF (flt->stages);
      MERROR (MERROR_FLT, -1);
    }
  return 0;
}


static void
free_flt_stage (FontLayoutStage *stage)
{
  int i;

  M17N_OBJECT_UNREF (stage->category);
  for (i = 0; i < stage->used; i++)
    free_flt_command (stage->cmds + i);
  MLIST_FREE1 (stage, cmds);
  free (stage);
}

static void
free_flt_list ()
{
  if (flt_list)
    {
      MPlist *plist, *pl;

      MPLIST_DO (plist, flt_list)
	{
	  MFLT *flt = MPLIST_VAL (plist);

	  if (flt->coverage)
	    M17N_OBJECT_UNREF (flt->coverage);
	  if (flt->stages)
	    {
	      MPLIST_DO (pl, MPLIST_NEXT (flt->stages))
		free_flt_stage (MPLIST_VAL (pl));
	      M17N_OBJECT_UNREF (flt->stages);
	    }
	}
      M17N_OBJECT_UNREF (flt_list);
    }
}

static int
list_flt ()
{
  MPlist *plist, *key_list = NULL;
  MPlist *pl;
  int result = 0;

  if (! (plist = mdatabase_list (Mfont, Mlayouter, Mnil, Mnil)))
    return -1;
  if (! (flt_list = mplist ()))
    goto err;
  if (! (key_list = mplist ()))
    goto err;
  if (! mplist_add (key_list, Mcategory, Mt))
    goto err;

  MPLIST_DO (pl, plist)
    {
      MDatabase *mdb = MPLIST_VAL (pl);
      MSymbol *tags = mdatabase_tag (mdb);
      MFLT *flt;

      if (! MSTRUCT_CALLOC_SAFE (flt))
	goto err;
      flt->name = tags[2];
      flt->mdb = mdb;
      if (load_flt (flt, key_list) < 0)
	free (flt);
      else
	{
	  if (MPLIST_TAIL_P (flt_list))
	    {
	      flt_min_coverage = mchartable_min_char (flt->coverage);
	      flt_max_coverage = mchartable_max_char (flt->coverage);
	    }
	  else
	    {
	      int c;

	      c = mchartable_min_char (flt->coverage);
	      if (flt_min_coverage > c)
		flt_min_coverage = c;
	      c = mchartable_max_char (flt->coverage);
	      if (flt_max_coverage < c)
		flt_max_coverage = c;
	    }
	  if (! mplist_push (flt_list, flt->name, flt))
	    goto err;
	}
    }
  goto end;

 err:
  free_flt_list ();
  result = -1;
 end:
  M17N_OBJECT_UNREF (plist);  
  M17N_OBJECT_UNREF (key_list);
  return result;
}

/* FLS (Font Layout Service) */

/* Structure to hold information about a context of FLS.  */

typedef struct
{
  /* Pointer to the current stage.  */
  FontLayoutStage *stage;

  /* Pointer to the font.  */
  MFLTFont *font;

  /* Input and output glyph string.  */
  MFLTGlyphString *in, *out;

  /* Encode each character or code of a glyph by the current category
     table into this array.  An element is a category letter used for
     a regular expression matching.  */
  char *encoded;
  int *match_indices;
  int code_offset;
  int cluster_begin_idx;
  int cluster_begin_pos;
  int cluster_end_pos;
  int combining_code;
  int left_padding;
  int check_mask;
} FontLayoutContext;

static int run_command (int, int, int, int, FontLayoutContext *);

#define NMATCH 20

static int
run_rule (int depth,
	  FontLayoutCmdRule *rule, int from, int to, FontLayoutContext *ctx)
{
  int *saved_match_indices = ctx->match_indices;
  int match_indices[NMATCH * 2];
  int consumed;
  int i;
  int orig_from = from;

  if (rule->src_type == SRC_SEQ)
    {
      int len;

      len = rule->src.seq.n_codes;
      if (len > (to - from))
	return 0;
      for (i = 0; i < len; i++)
	if (rule->src.seq.codes[i] != GREF (ctx->in, from + i)->code)
	  break;
      if (i < len)
	return 0;
      to = from + len;
      if (MDEBUG_FLAG () > 2)
	MDEBUG_PRINT3 ("\n [FLT] %*s(SEQ 0x%X", depth, "",
		       rule->src.seq.codes[0]);
    }
  else if (rule->src_type == SRC_RANGE)
    {
      int head;

      if (from >= to)
	return 0;
      head = GREF (ctx->in, from)->code;
      if (head < rule->src.range.from || head > rule->src.range.to)
	return 0;
      ctx->code_offset = head - rule->src.range.from;
      to = from + 1;
      if (MDEBUG_FLAG () > 2)
	MDEBUG_PRINT4 ("\n [FLT] %*s(RANGE 0x%X-0x%X", depth, "",
		       rule->src.range.from, rule->src.range.to);
    }
  else if (rule->src_type == SRC_REGEX)
    {
      regmatch_t pmatch[NMATCH];
      char saved_code;
      int result;

      if (from > to)
	return 0;
      saved_code = ctx->encoded[to];
      ctx->encoded[to] = '\0';
      result = regexec (&(rule->src.re.preg),
			ctx->encoded + from, NMATCH, pmatch, 0);
      if (result == 0 && pmatch[0].rm_so == 0)
	{
	  if (MDEBUG_FLAG () > 2)
	    MDEBUG_PRINT5 ("\n [FLT] %*s(REGEX \"%s\" \"%s\" %d", depth, "",
			   rule->src.re.pattern,
			   ctx->encoded + from,
			   pmatch[0].rm_eo);
	  ctx->encoded[to] = saved_code;
	  for (i = 0; i < NMATCH; i++)
	    {
	      if (pmatch[i].rm_so < 0)
		match_indices[i * 2] = match_indices[i * 2 + 1] = -1;
	      else
		{
		  match_indices[i * 2] = from + pmatch[i].rm_so;
		  match_indices[i * 2 + 1] = from + pmatch[i].rm_eo;
		}
	    }
	  ctx->match_indices = match_indices;
	  to = match_indices[1];
	}
      else
	{
	  ctx->encoded[to] = saved_code;
	  return 0;
	}
    }
  else if (rule->src_type == SRC_INDEX)
    {
      if (rule->src.match_idx >= NMATCH)
	return 0;
      from = ctx->match_indices[rule->src.match_idx * 2];
      if (from < 0)
	return 0;
      to = ctx->match_indices[rule->src.match_idx * 2 + 1];
      if (MDEBUG_FLAG () > 2)
	MDEBUG_PRINT3 ("\n [FLT] %*s(INDEX %d", depth, "", rule->src.match_idx);
    }
  else if (rule->src_type == SRC_HAS_GLYPH)
    {
      int encoded;
      unsigned code;

      if (rule->src.supported_glyph < 0)
	{
	  if (from >= to)
	    return 0;
	  code = GREF (ctx->in, from)->code;
	  to = from + 1;
	  encoded = GREF (ctx->in, from)->encoded;
	}
      else
	{
	  code = rule->src.supported_glyph;
	  to = from;
	  encoded = 0;
	}
      if (! encoded)
	{
	  static MFLTGlyphString gstring;

	  if (! gstring.glyph_size)
	    {
	      gstring.glyph_size = ctx->in->glyph_size;
	      gstring.glyphs = calloc (1, gstring.glyph_size);
	      gstring.allocated = 1;
	      gstring.used = 1;
	    }
	  gstring.glyphs[0].code = code;
	  if (ctx->font->get_glyph_id (ctx->font, &gstring, 0, 1) < 0
	      || ! gstring.glyphs[0].encoded)
	    return 0;
	}
    }
  else if (rule->src_type == SRC_OTF_SPEC)
    {
      MFLTOtfSpec *spec = &rule->src.otf_spec;

      if (! ctx->font->check_otf)
	{
	  if ((spec->features[0] && spec->features[0][0] != 0xFFFFFFFF)
	      || (spec->features[1] && spec->features[1][0] != 0xFFFFFFFF))
	    return 0;
	}
      else if (! ctx->font->check_otf (ctx->font, spec))
	return 0;
    }

  consumed = 0;
  depth++;
  for (i = 0; i < rule->n_cmds; i++)
    {
      int pos;

      if (rule->cmd_ids[i] == CMD_ID_REPEAT)
	{
	  if (! consumed)
	    continue;
	  i--;
	}
      pos = run_command (depth, rule->cmd_ids[i], from, to, ctx);
      if (pos < 0)
	return pos;
      consumed = pos > from;
      if (consumed)
	from = pos;
    }

  ctx->match_indices = saved_match_indices;
  if (MDEBUG_FLAG () > 2)
    MDEBUG_PRINT (")");
  return (rule->src_type == SRC_INDEX ? orig_from : to);
}

static int
run_cond (int depth,
	  FontLayoutCmdCond *cond, int from, int to, FontLayoutContext *ctx)
{
  int i, pos = 0;

  if (MDEBUG_FLAG () > 2)
    MDEBUG_PRINT2 ("\n [FLT] %*s(COND", depth, "");
  depth++;
  for (i = 0; i < cond->n_cmds; i++)
    {
      /* TODO: Write a code for optimization utilizaing the info
	 cond->seq_XXX.  */
      if ((pos = run_command (depth, cond->cmd_ids[i], from, to, ctx))
	  != 0)
	break;
    }
  if (pos < 0)
    return pos;
  if (MDEBUG_FLAG () > 2)
    MDEBUG_PRINT (")");
  return (pos);
}

static int
run_otf (int depth,
	 MFLTOtfSpec *otf_spec, int from, int to, FontLayoutContext *ctx)
{
  MFLTFont *font = ctx->font;
  int from_idx = ctx->out->used;

  if (MDEBUG_FLAG () > 2)
    MDEBUG_PRINT3 ("\n [FLT] %*s%s", depth, "", MSYMBOL_NAME (otf_spec->sym));

  font->get_glyph_id (font, ctx->in, from, to);
  if (! font->drive_otf)
    {
      if (ctx->out->used + (to - from) > ctx->out->allocated)
	return -2;
      font->get_metrics (font, ctx->in, from, to);
      GCPY (ctx->in, from, to - from, ctx->out, ctx->out->used);
      ctx->out->used += to - from;
    }
  else
    {
      MFLTGlyphAdjustment *adjustment;
      int out_len;
      int i;

      adjustment = alloca ((sizeof *adjustment)
			   * (ctx->out->allocated - ctx->out->used));
      if (! adjustment)
	MERROR (MERROR_FLT, -1);
      memset (adjustment, 0,
	      (sizeof *adjustment) * (ctx->out->allocated - ctx->out->used));
      to = font->drive_otf (font, otf_spec, ctx->in, from, to, ctx->out,
			    adjustment);
      if (to < 0)
	return to;
      out_len = ctx->out->used - from_idx;
      if (otf_spec->features[1])
	{
	  MFLTGlyphAdjustment *a;
	  MFLTGlyph *g;
	  
	  for (i = 0, a = adjustment; i < out_len; i++, a++)
	    if (a->set)
	      break;
	  if (i < out_len)
	    {
	      font->get_metrics (font, ctx->out, from_idx, ctx->out->used);
	      for (i = 0, g = GREF (ctx->out, from_idx + i), a = adjustment;
		   i < out_len; i++, a++, g = NEXT (ctx->out, g))
		{
		  SET_MEASURED (g, 1);
		  if (a->advance_is_absolute)
		    {
		      g->xadv = a->xadv;
		      g->yadv = a->yadv;
		    }
		  else if (a->xadv || a->yadv)
		    {
		      g->xadv += a->xadv;
		      g->yadv += a->yadv;
		    }
		  if (a->xoff || a->yoff)
		    {
		      int j;
		      MFLTGlyph *gg = PREV (ctx->out, g);
		      MFLTGlyphAdjustment *aa = a;

		      g->xoff = a->xoff;
		      g->yoff = a->yoff;
		      while (aa->back > 0)
			{
			  for (j = 0; j < aa->back;
			       j++, gg = PREV (ctx->out, gg))
			    g->xoff -= gg->xadv;
			  aa = aa - aa->back;
			  g->xoff += aa->xoff;
			  g->yoff += aa->yoff;
			}
		    }
		}
	    }
	}
    }

  if (ctx->cluster_begin_idx >= 0)
    for (; from_idx < ctx->out->used; from_idx++)
      {
	MFLTGlyph *g = GREF (ctx->out, from_idx);
	UPDATE_CLUSTER_RANGE (ctx, g);
      }
  return to;
}

static char work[16];

static char *
dump_combining_code (int code)
{
  char *vallign = "tcbB";
  char *hallign = "lcr";
  char *p;
  int off_x, off_y;

  if (! code)
    return "none";
  work[0] = vallign[COMBINING_CODE_BASE_Y (code)];
  work[1] = hallign[COMBINING_CODE_BASE_X (code)];
  off_y = COMBINING_CODE_OFF_Y (code);
  off_x = COMBINING_CODE_OFF_X (code);
  if (off_y > 0)
    sprintf (work + 2, "+%d", off_y);
  else if (off_y < 0)
    sprintf (work + 2, "%d", off_y);
  else if (off_x == 0)
    sprintf (work + 2, ".");
  p = work + strlen (work);
  if (off_x > 0)
    sprintf (p, ">%d", off_x);
  else if (off_x < 0)
    sprintf (p, "<%d", -off_x);
  p += strlen (p);
  p[0] = vallign[COMBINING_CODE_ADD_Y (code)];
  p[1] = hallign[COMBINING_CODE_ADD_X (code)];
  p[2] = '\0';
  return work;
}

static int
run_command (int depth, int id, int from, int to, FontLayoutContext *ctx)
{
  MFLTGlyph *g;

  if (id >= 0)
    {
      int i;

      /* Direct code (== ctx->code_offset + id) output.
	 The source is not consumed.  */
      if (MDEBUG_FLAG () > 2)
	MDEBUG_PRINT3 ("\n [FLT] %*s(DIRECT 0x%X", depth, "",
		       ctx->code_offset + id);
      i = (from < to || from == 0) ? from : from - 1;
      GDUP (ctx, i);
      g = GREF (ctx->out, ctx->out->used - 1);
      g->code = ctx->code_offset + id;
      SET_ENCODED (g, 0);
      SET_MEASURED (g, 0);
      if (ctx->combining_code)
	SET_COMBINING_CODE (g, ctx, ctx->combining_code);
      if (ctx->left_padding)
	SET_LEFT_PADDING (g, ctx, LeftPaddingMask);
      for (i = from; i < to; i++)
	{
	  MFLTGlyph *tmp = GREF (ctx->in, i);

	  if (g->from > tmp->from)
	    g->from = tmp->from;
	  else if (g->to < tmp->to)
	    g->to = tmp->to;
	}
      UPDATE_CLUSTER_RANGE (ctx, g);
      ctx->code_offset = ctx->combining_code = ctx->left_padding = 0;
      if (MDEBUG_FLAG () > 2)
	MDEBUG_PRINT (")");
      return (from);
    }

  if (id <= CMD_ID_OFFSET_INDEX)
    {
      int idx = CMD_ID_TO_INDEX (id);
      FontLayoutCmd *cmd;

      if (idx >= ctx->stage->used)
	MERROR (MERROR_DRAW, -1);
      cmd = ctx->stage->cmds + idx;
      if (cmd->type == FontLayoutCmdTypeRule)
	to = run_rule (depth, &cmd->body.rule, from, to, ctx);
      else if (cmd->type == FontLayoutCmdTypeCond)
	to = run_cond (depth, &cmd->body.cond, from, to, ctx);
      else if (cmd->type == FontLayoutCmdTypeOTF)
	to = run_otf (depth, &cmd->body.otf, from, to, ctx);
      return to;
    }

  if (id <= CMD_ID_OFFSET_COMBINING)
    {
      ctx->combining_code = CMD_ID_TO_COMBINING_CODE (id);
      if (MDEBUG_FLAG () > 2)
	MDEBUG_PRINT3 ("\n [FLT] %*s(CMB %s)", depth, "",
		       dump_combining_code (ctx->combining_code));
      return from;
    }

  switch (id)
    {
    case CMD_ID_COPY:
      {
	if (from >= to)
	  return from;
	GDUP (ctx, from);
	g = GREF (ctx->out, ctx->out->used - 1);
	if (ctx->combining_code)
	  SET_COMBINING_CODE (g, ctx, ctx->combining_code);
	if (ctx->left_padding)
	  SET_LEFT_PADDING (g, ctx, LeftPaddingMask);
	UPDATE_CLUSTER_RANGE (ctx, g);
	if (MDEBUG_FLAG () > 2)
	  {
	    if (g->c < 0)
	      MDEBUG_PRINT2 ("\n [FLT] %*s(COPY |)", depth, "");
	    else
	      MDEBUG_PRINT3 ("\n [FLT] %*s(COPY 0x%X)", depth, "", g->code);
	  }
	ctx->code_offset = ctx->combining_code = ctx->left_padding = 0;
	return (from + 1);
      }

    case CMD_ID_CLUSTER_BEGIN:
      if (ctx->cluster_begin_idx < 0)
	{
	  if (MDEBUG_FLAG () > 2)
	    MDEBUG_PRINT3 ("\n [FLT] %*s<%d", depth, "",
			   GREF (ctx->in, from)->from);
	  ctx->cluster_begin_idx = ctx->out->used;
	  ctx->cluster_begin_pos = GREF (ctx->in, from)->from;
	  ctx->cluster_end_pos = GREF (ctx->in, from)->to;
	}
      return from;

    case CMD_ID_CLUSTER_END:
      if (ctx->cluster_begin_idx >= 0
	  && ctx->cluster_begin_idx < ctx->out->used)
	{
	  int i;

	  if (MDEBUG_FLAG () > 2)
	    MDEBUG_PRINT1 (" %d>", ctx->cluster_end_pos + 1);
	  for (i = ctx->cluster_begin_idx; i < ctx->out->used; i++)
	    {
	      GREF (ctx->out, i)->from = ctx->cluster_begin_pos;
	      GREF (ctx->out, i)->to = ctx->cluster_end_pos;
	    }
	  ctx->cluster_begin_idx = -1;
	}
      return from;

    case CMD_ID_SEPARATOR:
      {
	int i;

	i = from < to ? from : from - 1;
	GDUP (ctx, i);
	g = GREF (ctx->out, ctx->out->used - 1);
	g->c = -1, g->code = 0;
	g->xadv = g->yadv = 0;
	SET_ENCODED (g, 0);
	SET_MEASURED (g, 0);
	return from;
      }

    case CMD_ID_LEFT_PADDING:
      if (MDEBUG_FLAG () > 2)
	MDEBUG_PRINT2 ("\n [FLT] %*s[", depth, "");
      ctx->left_padding = 1;
      return from;

    case CMD_ID_RIGHT_PADDING:
      if (ctx->out->used > 0)
	{
	  if (MDEBUG_FLAG () > 2)
	    MDEBUG_PRINT2 ("\n [FLT] %*s]", depth, "");
	  g = GREF (ctx->out, ctx->out->used - 1);
	  SET_RIGHT_PADDING (g, ctx, RightPaddingMask);
	}
      return from;
    }

  MERROR (MERROR_DRAW, -1);
}

static int
run_stages (MFLTGlyphString *gstring, int from, int to,
	    MFLT *flt, FontLayoutContext *ctx)
{
  MFLTGlyphString buf, *temp;
  int stage_idx = 0;
  int orig_from = from, orig_to = to;
  int from_pos, to_pos, len;
  int i, j;
  MFLTGlyph *g;
  MPlist *stages = flt->stages;

  from_pos = GREF (ctx->in, from)->from;
  to_pos = GREF (ctx->in, to - 1)->to;
  len = to_pos - from_pos;

  buf = *(ctx->in);
  buf.glyphs = NULL;
  GINIT (ctx->out, ctx->out->allocated);
  ctx->encoded = alloca (ctx->out->allocated);
  if (! ctx->out->glyphs || ! ctx->encoded)
    return -1;

  for (stage_idx = 0; 1; stage_idx++)
    {
      MCharTable *table;
      int result;

      ctx->stage = (FontLayoutStage *) MPLIST_VAL (stages);
      table = ctx->stage->category;
      ctx->code_offset = ctx->combining_code = ctx->left_padding = 0;
      for (i = from; i < to; i++)
	{
	  MFLTGlyph *g = GREF (ctx->in, i);
	  char enc = (GET_ENCODED (g)
		      ? (g->c > 0 ? (int) mchartable_lookup (table, g->c) : 1)
		      : g->code
		      ? (int) mchartable_lookup (table, g->code)
		      : ' ');

	  ctx->encoded[i] = enc;
	  if (! enc && stage_idx == 0)
	    {
	      to = i;
	      break;
	    }
	}
      ctx->encoded[i] = '\0';
      ctx->match_indices[0] = from;
      ctx->match_indices[1] = to;
      for (i = 2; i < NMATCH; i++)
	ctx->match_indices[i] = -1;

      if (MDEBUG_FLAG () > 2)
	{
	  MDEBUG_PRINT2 ("\n [FLT]   (STAGE %d \"%s\"", stage_idx,
			 ctx->encoded + from);
	  MDEBUG_PRINT (" (");
	  for (i = from; i < to; i++)
	    {
	      g = GREF (ctx->in, i);
	      if (g->c == -1)
		MDEBUG_PRINT2 ("%*s|", (i > 0), "");
	      else
		MDEBUG_PRINT3 ("%*s%04X", (i > 0), "", GREF (ctx->in, i)->code);
	    }
	  MDEBUG_PRINT (")");
	}
      result = run_command (4, INDEX_TO_CMD_ID (0), from, to, ctx);
      if (MDEBUG_FLAG () > 2)
	MDEBUG_PRINT (")");
      if (result < 0)
	return result;

      stages = MPLIST_NEXT (stages);
      /* If this is the last stage, break the loop. */
      if (MPLIST_TAIL_P (stages))
	break;

      /* Otherwise, prepare for the next stage.   */
      temp = ctx->in;
      ctx->in = ctx->out;
      if (buf.glyphs)
	ctx->out = temp;
      else
	{
	  GINIT (&buf, ctx->out->allocated);
	  ctx->out = &buf;
	}
      ctx->out->used = 0;

      from = 0;
      to = ctx->in->used;
    }

  if (ctx->out->used > 0)
    {
      int *g_indices;
      int x_ppem = ctx->font->x_ppem << 6, y_ppem = ctx->font->y_ppem << 6;

      /* Remove separator glyphs.  */
      for (i = 0; i < ctx->out->used;)
	{
	  g = GREF (ctx->out, i);
	  if (g->c < 0)
	    GREPLACE (NULL, 0, 0, ctx->out, i, i + 1);
	  else
	    i++;
	}

      /* Get actual glyph IDs of glyphs.  */
      ctx->font->get_glyph_id (ctx->font, ctx->out, 0, ctx->out->used);

      /* Check if all characters in the range are covered by some
	 glyph(s).  If not, change <from> and <to> of glyphs to cover
	 uncovered characters.  */
      g_indices = alloca (sizeof (int) * len);
      if (! g_indices)
	return -1;
      for (i = 0; i < len; i++) g_indices[i] = -1;
      for (i = 0; i < ctx->out->used; i++)
	{
	  int pos;

	  g = GREF (ctx->out, i);
	  for (pos = g->from; pos <= g->to; pos++)
	    if (g_indices[pos - orig_from] < 0)
	      g_indices[pos - orig_from] = i;
	}
      for (i = 0; i < len; i++)
	if (g_indices[i] < 0)
	  {
	    if (i == 0)
	      {
		int this_from;

		for (i++; i < len && g_indices[i] < 0; i++);
		j = g_indices[i];
		g = GREF (ctx->out, j);
		this_from = g->from;
		do {
		  g->from = orig_from + i;
		} while (++j < ctx->out->used
			 && (g = GREF (ctx->out, j))
			 && g->from == this_from);
	      }
	    else
	      {
		int this_to;

		j = g_indices[i - 1];
		g = GREF (ctx->out, j);
		this_to = g->to;
		do {
		  g->to = orig_from + i + 1;
		} while (--j >= 0
			 && (g = GREF (ctx->out, j))
			 && g->to == this_to);
	      }
	  }

      ctx->font->get_metrics (ctx->font, ctx->out, 0, ctx->out->used);

      /* Handle combining.  */
      if (ctx->check_mask & CombiningCodeMask)
	{
	  MFLTGlyph *base = GREF (ctx->out, 0);
	  int base_height = base->ascent + base->descent;
	  int combining_code;

	  for (i = 1; i < ctx->out->used; i++)
	    {
	      if ((g = GREF (ctx->out, i))
		  && (combining_code = GET_COMBINING_CODE (g)))
		{
		  int height = g->ascent + g->descent;
		  int base_x, base_y, add_x, add_y, off_x, off_y;

		  if (base->from > g->from)
		    base->from = g->from;
		  else if (base->to < g->to)
		    base->to = g->to;
		
		  base_x = COMBINING_CODE_BASE_X (combining_code);
		  base_y = COMBINING_CODE_BASE_Y (combining_code);
		  add_x = COMBINING_CODE_ADD_X (combining_code);
		  add_y = COMBINING_CODE_ADD_Y (combining_code);
		  off_x = COMBINING_CODE_OFF_X (combining_code);
		  off_y = COMBINING_CODE_OFF_Y (combining_code);

		  g->xoff = ((base->xadv * base_x - g->xadv * add_x) / 2
			     + x_ppem * off_x / 100 - base->xadv);
		  if (base_y < 3)
		    g->yoff = base_height * base_y / 2 - base->ascent;
		  else
		    g->yoff = 0;
		  if (add_y < 3)
		    g->yoff -= height * add_y / 2 - g->ascent;
		  g->yoff -= y_ppem * off_y / 100;
		  if (base->lbearing > base->xadv + g->lbearing + g->xoff)
		    base->lbearing = base->xadv + g->lbearing + g->xoff;
		  if (base->rbearing < base->xadv + g->xadv + g->xoff)
		    base->rbearing = base->xadv + g->xadv + g->xoff;
		  if (base->ascent < g->ascent - g->yoff)
		    base->ascent = g->ascent - g->yoff;
		  if (base->descent < g->descent - g->yoff)
		    base->descent = g->descent - g->yoff;
		  g->xadv = g->yadv = 0;
		  if (GET_RIGHT_PADDING (g))
		    SET_RIGHT_PADDING (base, ctx, RightPaddingMask);
		}
	      else
		{
		  base = g;
		  base_height = g->ascent + g->descent;
		}
	    }
	}

      /* Handle padding */
      if (ctx->check_mask & (LeftPaddingMask | RightPaddingMask))
	for (i = 0; i < ctx->out->used; i++)
	  {
	    g = GREF (ctx->out, i);
	    if (! GET_COMBINING_CODE (g))
	      {
		if (GET_RIGHT_PADDING (g) && g->rbearing > g->xadv)
		  {
		    g->xadv = g->rbearing;
		  }
		if (GET_LEFT_PADDING (g) && g->lbearing < 0)
		  {
		    g->xoff += - g->lbearing;
		    g->xadv += - g->lbearing;
		    g->rbearing += - g->lbearing;
		    g->lbearing = 0;
		  }
	      }
	  }
    }

  GREPLACE (ctx->out, 0, ctx->out->used, gstring, orig_from, orig_to);
  to = orig_from + ctx->out->used;
  return to;
}

static void
setup_combining_coverage (int from, int to, void *val, void *arg)
{
  int combining_class = (int) val;
  int category = 0;

  if (combining_class < 200)
    category = 'a';
  else if (combining_class <= 204)
    {
      if ((combining_class % 2) == 0)
	category = "bcd"[(combining_class - 200) / 2];
    }
  else if (combining_class <= 232)
    {
      if ((combining_class % 2) == 0)
	category = "efghijklmnopq"[(combining_class - 208) / 2];
    }
  else if (combining_class == 233)
    category = 'r';
  else if (combining_class == 234)
    category = 's';
  else if (combining_class == 240)
    category = 't';
  mchartable_set_range ((MCharTable *) arg, from, to, (void *) category);
}

static void
setup_combining_flt (MFLT *flt)
{
  MSymbol type;
  MCharTable *combininig_class_table
    = mchar_get_prop_table (Mcombining_class, &type);

  mchartable_set_range (flt->coverage, 0, 0x10FFFF, (void *) 'u');
  if (combininig_class_table)
    mchartable_map (combininig_class_table, (void *) 0,
		    setup_combining_coverage, flt->coverage);
}

#define CHECK_FLT_STAGES(flt) ((flt)->stages || load_flt (flt, NULL) == 0)


/* Internal API */

int m17n__flt_initialized;


/* External API */

/* The following two are actually not exposed to a user but concealed
   by the macro M17N_INIT (). */

void
m17n_init_flt (void)
{
  int mdebug_flag = MDEBUG_INIT;

  merror_code = MERROR_NONE;
  if (m17n__flt_initialized++)
    return;
  m17n_init_core ();
  if (merror_code != MERROR_NONE)
    {
      m17n__flt_initialized--;
      return;
    }

  MDEBUG_PUSH_TIME ();

  Mcond = msymbol ("cond");
  Mrange = msymbol ("range");
  Mfont = msymbol ("font");
  Mlayouter = msymbol ("layouter");
  Mcombining = msymbol ("combining");
  Mfont_facility = msymbol ("font-facility");
  Mgenerator = msymbol ("generator");
  Mend = msymbol ("end");

  MDEBUG_PRINT_TIME ("INIT", (stderr, " to initialize the flt modules."));
  MDEBUG_POP_TIME ();
}

void
m17n_fini_flt (void)
{
  int mdebug_flag = MDEBUG_FINI;

  if (m17n__flt_initialized == 0
      || --m17n__flt_initialized > 0)
    return;

  MDEBUG_PUSH_TIME ();
  free_flt_list ();
  MDEBUG_PRINT_TIME ("FINI", (stderr, " to finalize the flt modules."));
  MDEBUG_POP_TIME ();
  m17n_fini_core ();
}

/*** @} */ 
#endif /* !FOR_DOXYGEN || DOXYGEN_INTERNAL_MODULE */

/*** @addtogroup m17nFLT */
/*** @{ */
/*=*/

/*=*/
/***en
    @brief Return a FLT object whose name is NAME.

    The mflt_get () function returns a FLT object whose name is $NAME.

    @return
    If the operation was successfully, mflt_get () returns a pointer
    to a FLT object.  Otherwise, it returns @c NULL.  */

MFLT *
mflt_get (MSymbol name)
{
  MFLT *flt;

  if (! flt_list && list_flt () < 0)
    return NULL;
  flt = mplist_get (flt_list, name);
  if (! flt || ! CHECK_FLT_STAGES (flt))
    return NULL;
  if (flt->name == Mcombining
      && ! mchartable_lookup (flt->coverage, 0))
    setup_combining_flt (flt);

  return flt;
}

/*=*/
/***en
    @brief Find a FLT suitable for a specified character and font.

    The mflt_find () function returns the most appropriate FLT for
    rendering the character $C by font $FONT.

    @return
    If the operation was successfully, mflt_find () returns a pointer
    to a FLT object.  Otherwise, it returns @c NULL.  */

MFLT *
mflt_find (int c, MFLTFont *font)
{
  MPlist *plist;
  MFLT *flt;
  static MSymbol unicode_bmp = NULL, unicode_full = NULL;

  if (! unicode_bmp)
    {
      unicode_bmp = msymbol ("unicode-bmp");
      unicode_full = msymbol ("unicode-full");
    }

  if (! flt_list && list_flt () < 0)
    return NULL;
  if (font)
    {
      MFLT *best = NULL;

      MPLIST_DO (plist, flt_list)
	{
	  flt = MPLIST_VAL (plist);
	  if (flt->registry != unicode_bmp
	      && flt->registry != unicode_full)
	    continue;
	  if (flt->family && flt->family != font->family)
	    continue;
	  if (c >= 0
	      && ! mchartable_lookup (flt->coverage, c))
	    continue;
	  if (flt->otf.sym)
	    {
	      MFLTOtfSpec *spec = &flt->otf;

	      if (! font->check_otf)
		{
		  if ((spec->features[0] && spec->features[0][0] != 0xFFFFFFFF)
		      || (spec->features[1] && spec->features[1][0] != 0xFFFFFFFF))
		    continue;
		}
	      else if (! font->check_otf (font, spec))
		continue;
	      return flt;
	    }
	  best = flt;
	}
      return best;
    }
  if (c >= 0)
    {
      MPLIST_DO (plist, flt_list)
	{
	  flt = MPLIST_VAL (plist);
	  if (mchartable_lookup (flt->coverage, c))
	    return flt;
	}
    }
  return NULL;
}

/*=*/
/***en
    @brief Return a name of a FLT.

    The mflt_name () function returns the name of $FLT.  */

const char *
mflt_name (MFLT *flt)
{
  return MSYMBOL_NAME (flt->name);
}

/*=*/
/***en
    @brief Return a coverage of a FLT.

    The mflt_coverage () function returns a char-table that contains
    nonzero value for characters supported by $FLT.  */

MCharTable *
mflt_coverage (MFLT *flt)
{
  return flt->coverage;
}

/*=*/
/***en
    @brief Layout characters by Font Layout Table.

    The mflt_run () function layout characters in $GSTRING between
    $FROM (inclusive) and $TO (exclusive) by $FONT.  If $FLT is
    nonzero, it is used for all the charaters.  Otherwise, appropriate
    FLTs are automatically chosen.

    @retval >=0
    The operation was successful.  The value is an index to the
    $GSTRING->glyphs which was previously indexed by $TO.

    @retval -2
    $GSTRING->glyphs is too short to store the result.  A caller can
    call this fucntion again with the larger $GSTRING->glyphs.

    @retval -1
    Some other error occurred.  */

int
mflt_run (MFLTGlyphString *gstring, int from, int to,
	  MFLTFont *font, MFLT *flt)
{
  FontLayoutContext ctx;
  int match_indices[NMATCH];
  MFLTGlyph *g;
  MFLTGlyphString out;
  int auto_flt = ! flt;
  int c, i, j, k;
  int this_from, this_to;

  out = *gstring;
  out.glyphs = NULL;
  /* This is usually sufficient, but if not, we retry with the larger
     values at most 3 times.  This value is also used for the
     allocating size of ctx.encoded.  */
  out.allocated = (to - from) * 4;

  for (i = from; i < to; i++)
    {
      g = GREF (gstring, i);
      c = g->c;
      memset (g, 0, sizeof (MFLTGlyph));
      g->code = g->c = c;
      g->from = g->to = i;
    }

  for (this_from = from; this_from < to;)
    {
      if (! auto_flt)
	{
	  for (this_to = this_from; this_to < to; this_to++)
	    if (mchartable_lookup (flt->coverage, GREF (gstring, this_to)->c))
	      break;
	}
      else
	{
	  if (! flt_list && list_flt () < 0)
	    {
	      font->get_glyph_id (font, gstring, this_from, to);
	      font->get_metrics (font, gstring, this_from, to);
	      this_from = to;
	      break;
	    }
	  for (this_to = this_from; this_to < to; this_to++)
	    {
	      c = GREF (gstring, this_to)->c;
	      if (c >= flt_min_coverage && c <= flt_max_coverage)
		break;
	    }
	  for (; this_to < to; this_to++)
	    {
	      c = GREF (gstring, this_to)->c;
	      if (font->internal
		  && mchartable_lookup (((MFLT *) font->internal)->coverage, c))
		{
		  flt = font->internal;
		  break;
		}
	      flt = mflt_find (c, font);
	      if (flt)
		{
		  if (CHECK_FLT_STAGES (flt))
		    {
		      font->internal = flt;
		      break;
		    }
		}
	    }
	}

      if (this_from < this_to)
	{
	  font->get_glyph_id (font, gstring, this_from, this_to);
	  font->get_metrics (font, gstring, this_from, this_to);
	  this_from = this_to;
	}
      if (this_to == to)
	break;

      MDEBUG_PRINT1 (" [FLT] (%s", MSYMBOL_NAME (flt->name));

      for (; this_to < to; this_to++)
	if (! mchartable_lookup (flt->coverage, GREF (gstring, this_to)->c))
	  break;

      if (MDEBUG_FLAG ())
	{
	  if (font->family)
	    MDEBUG_PRINT1 (" (%s)", MSYMBOL_NAME (font->family));
	  MDEBUG_PRINT ("\n [FLT]   (SOURCE");
	  for (i = this_from, j = 0; i < this_to; i++, j++)
	    {
	      if (j > 0 && j % 8 == 0)
		MDEBUG_PRINT ("\n [FLT]          ");
	      MDEBUG_PRINT1 (" %04X", GREF (gstring, i)->c);
	    }
	  MDEBUG_PRINT (")");
	}

      for (i = 0; i < 3; i++)
	{
	  /* Setup CTX.  */
	  memset (&ctx, 0, sizeof ctx);
	  ctx.match_indices = match_indices;
	  ctx.font = font;
	  ctx.cluster_begin_idx = -1;
	  ctx.in = gstring;
	  ctx.out = &out;
	  j = run_stages (gstring, this_from, this_to, flt, &ctx);
	  if (j != -2)
	    break;
	  out.allocated *= 2;
	}

      if (j < 0)
	return j;

      to += j - this_to;
      this_to = j;

      if (MDEBUG_FLAG ())
	{
	  MDEBUG_PRINT ("\n [FLT]   (RESULT");
	  if (MDEBUG_FLAG () > 1)
	    for (i = 0; this_from < this_to; this_from++, i++)
	      {
		if (i > 0 && i % 4 == 0)
		  MDEBUG_PRINT ("\n [FLT]          ");
		g = GREF (gstring, this_from);
		MDEBUG_PRINT4 (" (%04X %d %d %d)",
			       g->code, g->xadv, g->xoff, g->yoff);
	      }
	  else
	    for (; this_from < this_to; this_from++)
	      MDEBUG_PRINT1 (" %04X", GREF (gstring, this_from)->code);
	  MDEBUG_PRINT ("))\n");
	}
      this_from = this_to;
    }

  if (gstring->r2l)
    {
      int len = to - from;

      GINIT (&out, len);
      memcpy (((char *) out.glyphs),
	      ((char *) gstring->glyphs) + gstring->glyph_size * from,
	      gstring->glyph_size * len);
      for (i = from, j = to; i < to;)
	{
	  for (k = i + 1, j--; k < to && GREF (&out, k)->xadv == 0;
	       k++, j--);
	  GCPY (&out, i, (k - i), gstring, j);
	  i = k;
	}
    }

  return to;
}


/* for debugging... */

static void
dump_flt_cmd (FontLayoutStage *stage, int id, int indent)
{
  char *prefix = (char *) alloca (indent + 1);

  memset (prefix, 32, indent);
  prefix[indent] = 0;

  if (id >= 0)
    fprintf (stderr, "0x%02X", id);
  else if (id <= CMD_ID_OFFSET_INDEX)
    {
      int idx = CMD_ID_TO_INDEX (id);
      FontLayoutCmd *cmd = stage->cmds + idx;

      if (cmd->type == FontLayoutCmdTypeRule)
	{
	  FontLayoutCmdRule *rule = &cmd->body.rule;
	  int i;

	  fprintf (stderr, "(rule ");
	  if (rule->src_type == SRC_REGEX)
	    fprintf (stderr, "\"%s\"", rule->src.re.pattern);
	  else if (rule->src_type == SRC_INDEX)
	    fprintf (stderr, "%d", rule->src.match_idx);
	  else if (rule->src_type == SRC_SEQ)
	    fprintf (stderr, "(seq)");
	  else if (rule->src_type == SRC_RANGE)
	    fprintf (stderr, "(range)");
	  else
	    fprintf (stderr, "(invalid src)");

	  for (i = 0; i < rule->n_cmds; i++)
	    {
	      fprintf (stderr, "\n%s  ", prefix);
	      dump_flt_cmd (stage, rule->cmd_ids[i], indent + 2);
	    }
	  fprintf (stderr, ")");
	}
      else if (cmd->type == FontLayoutCmdTypeCond)
	{
	  FontLayoutCmdCond *cond = &cmd->body.cond;
	  int i;

	  fprintf (stderr, "(cond");
	  for (i = 0; i < cond->n_cmds; i++)
	    {
	      fprintf (stderr, "\n%s  ", prefix);
	      dump_flt_cmd (stage, cond->cmd_ids[i], indent + 2);
	    }
	  fprintf (stderr, ")");
	}
      else if (cmd->type == FontLayoutCmdTypeOTF)
	{
	  fprintf (stderr, "(otf)");
	}
      else
	fprintf (stderr, "(error-command)");
    }
  else if (id <= CMD_ID_OFFSET_COMBINING)
    fprintf (stderr, "cominging-code");
  else
    fprintf (stderr, "(predefiend %d)", id);
}

void
mdebug_dump_flt (MFLT *flt, int indent)
{
  char *prefix = (char *) alloca (indent + 1);
  MPlist *plist;
  int stage_idx = 0;

  memset (prefix, 32, indent);
  prefix[indent] = 0;
  fprintf (stderr, "(flt");
  MPLIST_DO (plist, flt->stages)
    {
      FontLayoutStage *stage = (FontLayoutStage *) MPLIST_VAL (plist);
      int i;

      fprintf (stderr, "\n%s  (stage %d", prefix, stage_idx);
      for (i = 0; i < stage->used; i++)
	{
	  fprintf (stderr, "\n%s    ", prefix);
	  dump_flt_cmd (stage, INDEX_TO_CMD_ID (i), indent + 4);
	}
      fprintf (stderr, ")");
      stage_idx++;
    }
  fprintf (stderr, ")");
}

/*** @} */

/*
 Local Variables:
 coding: euc-japan
 End:
*/