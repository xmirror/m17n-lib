/* font-ft.c -- FreeType interface sub-module.
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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>

#include "m17n-gui.h"
#include "m17n-misc.h"
#include "internal.h"
#include "plist.h"
#include "internal-gui.h"
#include "font.h"
#include "face.h"

#ifdef HAVE_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H

#ifdef HAVE_OTF
#include <otf.h>
#endif /* HAVE_OTF */

static FT_Library ft_library;

typedef struct
{
  MSymbol ft_style;
  MSymbol weight, style, stretch;
} MFTtoProp;

static int ft_to_prop_size;
static MFTtoProp *ft_to_prop;

typedef struct
{
  M17NObject control;
  MFont font;
  char *filename;
  MPlist *charmap_list;
  FT_Face ft_face;
  int otf_flag;	/* This font 1: is OTF, 0: may be OTF, -1: is not OTF.  */
#ifdef HAVE_OTF
  OTF *otf;
#endif /* HAVE_OTF */
} MFTInfo;

/* List of FreeType fonts.  Keys are family names, values are (MFTInfo
   *) where MFTInfo->ft_face and MFTInfo->otf are NULL.  */
static MPlist *ft_font_list;

/** Return 1 iff the filename in DIRENTRY matches FreeType font file.
    We select only TrueType/OpenType/Type1 fonts.
    Used as the arg SELECT of scandir () in list_ft_in_dir ().  */

static int
check_filename (const char *name)
{
  int len = strlen (name);
  const char *ext = name + (len - 4);

  if (len < 5)
    return -1;
  if (! memcmp (ext, ".ttf", 4)
      || ! memcmp (ext, ".TTF", 4)
      || ! memcmp (ext, ".otf", 4)
      || ! memcmp (ext, ".OTF", 4))
    return 1;
  if (! memcmp (ext, ".PFA", 4)
      || ! memcmp (ext, ".pfa", 4)
      || ! memcmp (ext, ".PFB", 4)
      || ! memcmp (ext, ".pfb", 4))
    return 0;
  return -1;
}

static MSymbol
ft_set_property (MFont *font, char *family_name, char *style_name)
{
  MSymbol family;
  MSymbol style;
  int len;
  char *buf, *p;

  MFONT_INIT (font);
  font->property[MFONT_TYPE] = MFONT_TYPE_FT + 1;
  mfont__set_property (font, MFONT_ADSTYLE, msymbol (""));

  len = strlen (family_name) + 1;
  buf = (char *) alloca (len);
  memcpy (buf, family_name, len);
  for (p = buf; *p; p++)
    if (*p >= 'A' && *p <= 'Z')
      *p += 'a' - 'A';
  family = msymbol (buf);
  mfont__set_property (font, MFONT_FAMILY, family);

  if (style_name)
    {
      len = strlen (style_name) + 1;
      buf = (char *) alloca (len);
      memcpy (buf, style_name, len);
      for (p = buf; *p; p++)
	if (*p >= 'A' && *p <= 'Z')
	  *p += 'a' - 'A';
      style = msymbol (buf);
    }
  else
    style = Mnil;

  if (style != Mnil)
    {
      int i;

      for (i = 0; i < ft_to_prop_size; i++)
	if (ft_to_prop[i].ft_style == style)
	  {
	    mfont__set_property (font, MFONT_WEIGHT, ft_to_prop[i].weight);
	    mfont__set_property (font, MFONT_STYLE, ft_to_prop[i].style);
	    mfont__set_property (font, MFONT_STRETCH, ft_to_prop[i].stretch);
	    return family;
	  }
    }      
  mfont__set_property (font, MFONT_WEIGHT, msymbol ("medium"));
  mfont__set_property (font, MFONT_STYLE, msymbol ("r"));
  mfont__set_property (font, MFONT_STRETCH, msymbol ("normal"));
  return family;
}

static void
add_font_list (char *filename, int otf_flag)
{
  FT_Face ft_face;
  MFTInfo *ft_info;
  MSymbol family;
  char registry_buf[16];
  int i;

  if (FT_New_Face (ft_library, filename, 0, &ft_face))
    return;

  if (ft_face->family_name && ((char *) ft_face->family_name)[0])
    {
      int unicode_charmap_bmp = -1, unicode_charmap_full = -1;

      MSTRUCT_CALLOC (ft_info, MERROR_FONT_FT);
      family = ft_set_property (&ft_info->font,
				(char *) ft_face->family_name,
				(char *) ft_face->style_name);
      ft_info->filename = strdup (filename);
      ft_info->charmap_list = mplist ();
      mplist_add (ft_info->charmap_list, Mt, (void *) -1);
      for (i = 0; i < ft_face->num_charmaps; i++)
	{
	  sprintf (registry_buf, "%d-%d",
		   ft_face->charmaps[i]->platform_id,
		   ft_face->charmaps[i]->encoding_id);
	  mplist_add (ft_info->charmap_list, msymbol (registry_buf),
		      (void *) i);
	  if (ft_face->charmaps[i]->platform_id == 0)
	    {
	      if (ft_face->charmaps[i]->encoding_id == 3)
		unicode_charmap_bmp = i;
	      else if (ft_face->charmaps[i]->encoding_id == 4)
		unicode_charmap_full = i;
	    }
	  else if (ft_face->charmaps[i]->platform_id == 3)
	    {
	      if (ft_face->charmaps[i]->encoding_id == 1)
		unicode_charmap_bmp = i;
	      else if (ft_face->charmaps[i]->encoding_id == 10)
		unicode_charmap_full = i;
	    }
	  else if (ft_face->charmaps[i]->platform_id == 1
		   && ft_face->charmaps[i]->encoding_id == 0)
	    mplist_add (ft_info->charmap_list, msymbol ("apple-roman"),
			(void *) i);
	}
      if (unicode_charmap_bmp >= 0)
	mplist_add (ft_info->charmap_list, msymbol ("unicode-bmp"),
		    (void *) unicode_charmap_bmp);
      if (unicode_charmap_full >= 0)
	mplist_add (ft_info->charmap_list, msymbol ("unicode-full"),
		    (void *) unicode_charmap_full);
      mplist_add (ft_font_list, family, ft_info);
    }
  FT_Done_Face (ft_face);
}

static void
build_font_list ()
{
  MPlist *plist;
  struct stat buf;
  char *pathname;

  ft_font_list = mplist ();

  MPLIST_DO (plist, mfont_freetype_path)
    if (MPLIST_STRING_P (plist)
	&& (pathname = MPLIST_STRING (plist))
	&& stat (pathname, &buf) == 0)
      {
	if (S_ISREG (buf.st_mode))
	  {
	    int result = check_filename (pathname);

	    add_font_list (pathname, result > 0 ? 0 : -1);
	  }
	else if (S_ISDIR (buf.st_mode))
	  {
	    int len = strlen (pathname);
	    char path[PATH_MAX];
	    DIR *dir = opendir (pathname);
	    struct dirent *dp;
	    int result;

	    if (dir)
	      {
		strcpy (path, pathname);
		strcpy (path + len, "/");
		len++;
		while ((dp = readdir (dir)) != NULL)
		  if ((result = check_filename (dp->d_name)) >= 0)
		    {
		      strcpy (path + len, dp->d_name);
		      add_font_list (path, result > 0 ? 0 : -1);
		    }
		closedir (dir);
	      }
	  }
      }
}

static MRealizedFont *ft_select (MFrame *, MFont *, MFont *, int);
static int ft_open (MRealizedFont *);
static void ft_close (MRealizedFont *);
static void ft_find_metric (MRealizedFont *, MGlyph *);
static unsigned ft_encode_char (MRealizedFont *, int, unsigned);
static void ft_render (MDrawWindow, int, int, MGlyphString *,
		       MGlyph *, MGlyph *, int, MDrawRegion);

MFontDriver ft_driver =
  { ft_select, ft_open, ft_close,
    ft_find_metric, ft_encode_char, ft_render };

/* The FreeType font driver function LIST.  */

static MRealizedFont *
ft_select (MFrame *frame, MFont *spec, MFont *request, int limited_size)
{
  MPlist *plist;
  MFTInfo *best_font;
  int best_score;
  MRealizedFont *rfont;
  MSymbol family, registry;

  if (! ft_font_list)
    build_font_list ();
  best_font = NULL;
  best_score = 0;
  family = FONT_PROPERTY (spec, MFONT_FAMILY);
  registry = FONT_PROPERTY (spec, MFONT_REGISTRY);
  if (registry == Mnil)
    registry = Mt;

  MPLIST_DO (plist, ft_font_list)
    {
      MFTInfo *ft_info;
      int score;

      if (family)
	{
	  plist = mplist_find_by_key (plist, family);
	  if (! plist)
	    break;
	}
      ft_info = (MFTInfo *) MPLIST_VAL (plist);
      if (! mplist_find_by_key (ft_info->charmap_list, registry))
	continue;

      /* We always ignore FOUNDRY.  */
      ft_info->font.property[MFONT_FOUNDRY] = spec->property[MFONT_FOUNDRY];
      score = mfont__score (&ft_info->font, spec, request, limited_size);
      if (score >= 0
	  && (! best_font
	      || best_score > score))
	{
	  best_font = ft_info;
	  best_score = score;
	  if (score == 0)
	    break;
	}
    }
  if (! best_font)
    return NULL;

  MSTRUCT_CALLOC (rfont, MERROR_FONT_FT);
  rfont->frame = frame;
  rfont->spec = *spec;
  rfont->request = *request;
  rfont->font = best_font->font;
  rfont->font.property[MFONT_SIZE] = request->property[MFONT_SIZE];
  rfont->font.property[MFONT_REGISTRY] = spec->property[MFONT_REGISTRY];
  rfont->score = best_score;
  rfont->info = best_font;
  rfont->driver = &ft_driver;
  return rfont;
}

static void
close_ft (void *object)
{
  MFTInfo *ft_info = (MFTInfo *) object;

  if (ft_info->ft_face)
    FT_Done_Face (ft_info->ft_face);
#ifdef HAVE_OTF
  if (ft_info->otf)
    OTF_close (ft_info->otf);
#endif /* HAVE_OTF */
  free (object);
}

/* The FreeType font driver function OPEN.  */

static int
ft_open (MRealizedFont *rfont)
{
  MFTInfo *ft_info;
  MSymbol registry = FONT_PROPERTY (&rfont->font, MFONT_REGISTRY);
  int i;
  int mdebug_mask = MDEBUG_FONT;

  M17N_OBJECT (ft_info, close_ft, MERROR_FONT_FT);
  ft_info->font = ((MFTInfo *) rfont->info)->font;
  ft_info->otf_flag = ((MFTInfo *) rfont->info)->otf_flag;
  ft_info->filename = ((MFTInfo *) rfont->info)->filename;
  ft_info->charmap_list = ((MFTInfo *) rfont->info)->charmap_list;
  rfont->info = ft_info;

  rfont->status = -1;
  if (FT_New_Face (ft_library, ft_info->filename, 0, &ft_info->ft_face))
    goto err;
  if (registry == Mnil)
    registry = Mt;
  i = (int) mplist_get (((MFTInfo *) rfont->info)->charmap_list, registry);
  if (i >= 0
      && FT_Set_Charmap (ft_info->ft_face, ft_info->ft_face->charmaps[i]))
    goto err;
  if (FT_Set_Pixel_Sizes (ft_info->ft_face, 0,
			  rfont->font.property[MFONT_SIZE] / 10))
    goto err;

  MDEBUG_PRINT1 (" [FT-FONT] o %s\n", ft_info->filename);
  rfont->status = 1;
  rfont->ascent = ft_info->ft_face->ascender >> 6;
  rfont->descent = ft_info->ft_face->descender >> 6;
  return 0;

 err:
  MDEBUG_PRINT1 (" [FT-FONT] x %s\n", ft_info->filename);
  return -1;
}

/* The FreeType font driver function CLOSE.  */

static void
ft_close (MRealizedFont *rfont)
{
  M17N_OBJECT_UNREF (rfont->info);
}

/* The FreeType font driver function FIND_METRIC.  */

static void
ft_find_metric (MRealizedFont *rfont, MGlyph *g)
{
  MFTInfo *ft_info = (MFTInfo *) rfont->info;
  FT_Face ft_face = ft_info->ft_face;

  if (g->code == MCHAR_INVALID_CODE)
    {
      unsigned unitsPerEm = ft_face->units_per_EM;
      int size = rfont->font.property[MFONT_SIZE] / 10;

      g->lbearing = 0;
      g->rbearing = ft_face->max_advance_width * size / unitsPerEm;
      g->width = ft_face->max_advance_width * size / unitsPerEm;
      g->ascent = ft_face->ascender * size / unitsPerEm;
      g->descent = (- ft_face->descender) * size / unitsPerEm;
    }
  else
    {
      FT_Glyph_Metrics *metrics;
      FT_UInt code;

      if (g->otf_encoded)
	code = g->code;
      else
	code = FT_Get_Char_Index (ft_face, (FT_ULong) g->code);

      FT_Load_Glyph (ft_face, code, FT_LOAD_RENDER | FT_LOAD_MONOCHROME);
      metrics = &ft_face->glyph->metrics;
      g->lbearing = metrics->horiBearingX >> 6;
      g->rbearing = (metrics->horiBearingX + metrics->width) >> 6;
      g->width = metrics->horiAdvance >> 6;
      g->ascent = metrics->horiBearingY >> 6;
      g->descent = (metrics->height - metrics->horiBearingY) >> 6;
    }
}

/* The FreeType font driver function ENCODE_CHAR.  */

static unsigned
ft_encode_char (MRealizedFont *rfont, int c, unsigned ignored)
{
  MFTInfo *ft_info;
  FT_UInt code;

  if (rfont->status == 0)
    {
      if (ft_open (rfont) < 0)
	return -1;
    }
  ft_info = (MFTInfo *) rfont->info;
  code = FT_Get_Char_Index (ft_info->ft_face, (FT_ULong) c);
  if (! code)
    return MCHAR_INVALID_CODE;
#if 0
  FT_Load_Glyph (ft_info->ft_face, code, FT_LOAD_NO_SCALE);
  return (ft_info->ft_face->glyph->metrics.width > 0
	  ? (unsigned) c : MCHAR_INVALID_CODE);
#endif
  return ((unsigned) c);

}

/* The FreeType font driver function RENDER.  */

static void
ft_render (MDrawWindow win, int x, int y,
	   MGlyphString *gstring, MGlyph *from, MGlyph *to,
	   int reverse, MDrawRegion region)
{
  MRealizedFace *rface;
  MFrame *frame;
  MFTInfo *ft_info;
  FT_Face ft_face = NULL;
  MGlyph *g;

  if (from == to)
    return;

  /* It is assured that the all glyphs in the current range use the
     same realized face.  */
  rface = from->rface;
  frame = rface->frame;
  ft_info = (MFTInfo *) rface->rfont->info;
  ft_face = ft_info->ft_face;

  g = from;
  while (g < to)
    {
      if (g->type == GLYPH_CHAR)
	{
	  FT_UInt code;

	  if (g->otf_encoded)
	    code = g->code;
	  else
	    code = FT_Get_Char_Index (ft_face, (FT_ULong) g->code);
#ifdef FT_LOAD_TARGET_MONO
	  FT_Load_Glyph (ft_face, code, FT_LOAD_RENDER | FT_LOAD_TARGET_MONO);
#else
	  FT_Render_Glyph (ft_face->glyph, FT_LOAD_RENDER | FT_LOAD_MONOCHROME);
#endif
	  mwin__draw_bitmap (frame, win, rface, reverse,
			     x + ft_face->glyph->bitmap_left + g->xoff,
			     y - ft_face->glyph->bitmap_top + g->yoff,
			     ft_face->glyph->bitmap.width,
			     ft_face->glyph->bitmap.rows,
			     ft_face->glyph->bitmap.pitch,
			     ft_face->glyph->bitmap.buffer,
			     region);
	}
      x += g++->width;
    }

  return;
}



int
mfont__ft_init ()
{
  struct {
    char *ft_style;
    char *weight, *style, *stretch;
  } ft_to_prop_name[] =
    { { "regular", "medium", "r", "normal" },
      { "italic", "medium", "i", "normal" },
      { "bold", "bold", "r", "normal" },
      { "bold italic", "bold", "i", "normal" },
      { "narrow", "medium", "r", "condensed" },
      { "narrow italic", "medium", "i", "condensed" },
      { "narrow bold", "bold", "r", "condensed" },
      { "narrow bold italic", "bold", "i", "condensed" },
      { "black", "black", "r", "normal" },
      { "black italic", "black", "i", "normal" } };
  int i;

  if (FT_Init_FreeType (&ft_library) != 0)
    MERROR (MERROR_FONT_FT, -1);

  ft_to_prop_size = sizeof (ft_to_prop_name) / sizeof (ft_to_prop_name[0]);
  MTABLE_MALLOC (ft_to_prop, ft_to_prop_size, MERROR_FONT_FT);
  for (i = 0; i < ft_to_prop_size; i++)
    {
      ft_to_prop[i].ft_style = msymbol (ft_to_prop_name[i].ft_style);
      ft_to_prop[i].weight = msymbol (ft_to_prop_name[i].weight);
      ft_to_prop[i].style = msymbol (ft_to_prop_name[i].style);
      ft_to_prop[i].stretch = msymbol (ft_to_prop_name[i].stretch);
    }

  mfont__driver_list[MFONT_TYPE_FT] = &ft_driver;

  return 0;
}

void
mfont__ft_fini ()
{
  MPlist *plist;

  if (ft_font_list)
    {
      MPLIST_DO (plist, ft_font_list)
	{
	  MFTInfo *ft_info = (MFTInfo *) MPLIST_VAL (plist);
	  free (ft_info->filename);
	  M17N_OBJECT_UNREF (ft_info->charmap_list);
	  free (ft_info);
	}
      M17N_OBJECT_UNREF (ft_font_list);
      ft_font_list = NULL;
    }
  free (ft_to_prop);
  FT_Done_FreeType (ft_library);
}


int
mfont__ft_drive_otf (MGlyphString *gstring, int from, int to,
		     MSymbol script, MSymbol langsys, 
		     MSymbol gsub_features, MSymbol gpos_features)
{
  int len = to - from;
  MGlyph g;
  int i;
#ifdef HAVE_OTF
  MFTInfo *ft_info;
  OTF *otf;
  OTF_GlyphString otf_gstring;
  OTF_Glyph *otfg;
  char *script_name, *language_name;
  char *gsub_feature_names, *gpos_feature_names;
  int from_pos, to_pos;
  int unitsPerEm;

  if (len == 0)
    return from;

  ft_info = (MFTInfo *) gstring->glyphs[from].rface->rfont->info;
  if (ft_info->otf_flag < 0)
    goto simple_copy;
  otf = ft_info->otf;
  if (! otf && (otf = OTF_open (ft_info->filename)))
    {
      if (OTF_get_table (otf, "head") < 0
	  || (OTF_check_table (otf, "GSUB") < 0
	      && OTF_check_table (otf, "GPOS") < 0))
	{
	  OTF_close (otf);
	  ft_info->otf_flag = -1;
	  ft_info->otf = NULL;
	  goto simple_copy;
	}
      ft_info->otf = otf;
    }

  script_name = msymbol_name (script);
  language_name = langsys != Mnil ? msymbol_name (langsys) : NULL;
  gsub_feature_names
    = (gsub_features == Mt ? "*"
       : gsub_features == Mnil ? NULL
       : msymbol_name (gsub_features));
  gpos_feature_names
    = (gpos_features == Mt ? "*"
       : gpos_features == Mnil ? NULL
       : msymbol_name (gpos_features));

  g = gstring->glyphs[from];
  from_pos = g.pos;
  to_pos = g.to;
  for (i = from + 1; i < to; i++)
    {
      if (from_pos > gstring->glyphs[i].pos)
	from_pos = gstring->glyphs[i].pos;
      if (to_pos < gstring->glyphs[i].to)
	to_pos = gstring->glyphs[i].to;
    }

  unitsPerEm = otf->head->unitsPerEm;
  otf_gstring.size = otf_gstring.used = len;
  otf_gstring.glyphs = (OTF_Glyph *) alloca (sizeof (OTF_Glyph) * len);
  memset (otf_gstring.glyphs, 0, sizeof (OTF_Glyph) * len);
  for (i = 0; i < len; i++)
    {
      if (gstring->glyphs[from + i].otf_encoded)
	{
	  otf_gstring.glyphs[i].c = gstring->glyphs[from + i].c;
	  otf_gstring.glyphs[i].glyph_id = gstring->glyphs[from + i].code;
	}
      else
	{
	  otf_gstring.glyphs[i].c = gstring->glyphs[from + i].code;
	}
    }
      
  if (OTF_drive_tables (otf, &otf_gstring, script_name, language_name,
			gsub_feature_names, gpos_feature_names) < 0)
    goto simple_copy;
  g.pos = from_pos;
  g.to = to_pos;
  for (i = 0, otfg = otf_gstring.glyphs; i < otf_gstring.used; i++, otfg++)
    {
      g.combining_code = 0;
      g.c = otfg->c;
      if (otfg->glyph_id)
	{
	  g.code = otfg->glyph_id;
	  switch (otfg->positioning_type)
	    {
	    case 1: case 2:
	      {
		int off_x = 128, off_y = 128;

		if (otfg->f.f1.format & OTF_XPlacement)
		  off_x = ((double) (otfg->f.f1.value->XPlacement)
			   * 100 / unitsPerEm + 128);
		if (otfg->f.f1.format & OTF_YPlacement)
		  off_y = ((double) (otfg->f.f1.value->YPlacement)
			   * 100 / unitsPerEm + 128);
		g.combining_code
		  = MAKE_COMBINING_CODE (3, 2, 3, 0, off_y, off_x);
		if ((otfg->f.f1.format & OTF_XAdvance)
		    || (otfg->f.f1.format & OTF_YAdvance))
		  off_y--;
	      }
	      break;
	    case 3:
	      /* Not yet supported.  */
	      break;
	    case 4:
	      {
		int off_x, off_y;

		off_x = ((double) (otfg->f.f4.base_anchor->XCoordinate
				   - otfg->f.f4.mark_anchor->XCoordinate)
			 * 100 / unitsPerEm + 128);
		off_y = ((double) (otfg->f.f4.base_anchor->YCoordinate
				   - otfg->f.f4.mark_anchor->YCoordinate)
			 * 100 / unitsPerEm + 128);
		g.combining_code
		  = MAKE_COMBINING_CODE (3, 0, 3, 0, off_y, off_x);
	      }
	      break;
	    case 5:
	      /* Not yet supported.  */
	      break;
	    default:		/* i.e case 6 */
	      /* Not yet supported.  */
	      break;
	    }
	  g.otf_encoded = 1;
	}
      else
	{
	  g.code = otfg->c;
	  g.otf_encoded = 0;
	}
      MLIST_APPEND1 (gstring, glyphs, g, MERROR_FONT_OTF);
    }
  return to;

 simple_copy:
#endif	/* HAVE_OTF */
  for (i = 0; i < len; i++)
    {
      g = gstring->glyphs[from + i];
      MLIST_APPEND1 (gstring, glyphs, g, MERROR_FONT_OTF);
    }
  return to;
}

int
mfont__ft_decode_otf (MGlyph *g)
{
#ifdef HAVE_OTF
  MFTInfo *ft_info = (MFTInfo *) g->rface->rfont->info;
  int c = OTF_get_unicode (ft_info->otf, (OTF_GlyphID) g->code);

  return (c ? c : -1);
#else  /* not HAVE_OTF */
  return -1;
#endif	/* not HAVE_OTF */
}

#else /* not HAVE_FREETYPE */

int
mfont__ft_init ()
{
  return 0;
}

void
mfont__ft_fini ()
{
}

#endif /* HAVE_FREETYPE */
