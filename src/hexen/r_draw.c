//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 1993-2008 Raven Software
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2016-2021 Julian Nechaevsky
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//



#include "h2def.h"
#include "i_system.h"
#include "i_video.h"
#include "r_local.h"
#include "v_video.h"

/*

All drawing to the view buffer is accomplished in this file.  The other refresh
files only know about ccordinates, not the architecture of the frame buffer.

*/

byte *viewimage;
int viewwidth, scaledviewwidth, viewheight, viewwindowx, viewwindowy;
byte *ylookup[SCREENHEIGHT];
int columnofs[WIDESCREENWIDTH];
//byte translations[3][256]; // color tables for different players

/*
==================
=
= R_DrawColumn
=
= Source is the top of the column to scale
=
==================
*/

lighttable_t *dc_colormap;
int dc_x;
int dc_yl;
int dc_yh;
fixed_t dc_iscale;
fixed_t dc_texturemid;
int dc_texheight;
byte *dc_source;                // first pixel in a column (possibly virtual)


void R_DrawColumn(void)
{
    int              count;
    register byte    *dest;           // killough
    register fixed_t frac;            // killough
    fixed_t          fracstep;

    count = dc_yh - dc_yl + 1;

    if (count <= 0)    // Zero length, column does not exceed a pixel.
    return;

#ifdef RANGECHECK
    if ((unsigned)dc_x >= screenwidth || dc_yl < 0 || dc_yh >= SCREENHEIGHT)
        I_Error (english_language ?
                 "R_DrawColumn: %i to %i at %i" :
                 "R_DrawColumn: %i к %i в %i",
                 dc_yl, dc_yh, dc_x);
#endif

    // Framebuffer destination address.
    // Use ylookup LUT to avoid multiply with ScreenWidth.
    // Use columnofs LUT for subwindows?

    dest = ylookup[dc_yl] + columnofs[flipwidth[dc_x]];

    // Determine scaling, which is the only mapping to be done.

    fracstep = dc_iscale;
    frac = dc_texturemid + (dc_yl-centery)*fracstep;

    // Inner loop that does the actual texture mapping,
    //  e.g. a DDA-lile scaling.
    // This is as fast as it gets.       (Yeah, right!!! -- killough)
    //
    // killough 2/1/98: more performance tuning

    {
        register const byte *source = dc_source;
        register const lighttable_t *colormap = dc_colormap;
        register int heightmask = dc_texheight-1;
        if (dc_texheight & heightmask)   // not a power of 2 -- killough
        {
            heightmask++;
            heightmask <<= FRACBITS;

            if (frac < 0)
                while ((frac += heightmask) < 0);
            else
                while (frac >= heightmask)
                frac -= heightmask;

            do
            {
                // Re-map color indices from wall texture column
                //  using a lighting/special effects LUT.

                // heightmask is the Tutti-Frutti fix -- killough

                *dest = colormap[source[frac>>FRACBITS]];
                dest += screenwidth;                     // killough 11/98
                if ((frac += fracstep) >= heightmask)
                    frac -= heightmask;
            }
            while (--count);
        }
        else
        {
            while ((count-=2)>=0)   // texture height is a power of 2 -- killough
            {
                *dest = colormap[source[(frac>>FRACBITS) & heightmask]];
                dest += screenwidth;   // killough 11/98
                frac += fracstep;
                *dest = colormap[source[(frac>>FRACBITS) & heightmask]];
                dest += screenwidth;   // killough 11/98
                frac += fracstep;
            }
            if (count & 1)
                *dest = colormap[source[(frac>>FRACBITS) & heightmask]];
        }
    }
}

void R_DrawColumnLow(void)
{
    int count;
    byte *dest;
    fixed_t frac, fracstep;

    count = dc_yh - dc_yl;
    if (count < 0)
        return;

#ifdef RANGECHECK
    if ((unsigned) dc_x >= screenwidth || dc_yl < 0 || dc_yh >= SCREENHEIGHT)
        I_Error(english_language ?
                "R_DrawColumn: %i to %i at %i" :
                "R_DrawColumn: %i к %i в %i",
                dc_yl, dc_yh, dc_x);
#endif

    dest = ylookup[dc_yl] + columnofs[flipwidth[dc_x]];

    fracstep = dc_iscale;
    frac = dc_texturemid + (dc_yl - centery) * fracstep;

    do
    {
        *dest = dc_colormap[dc_source[(frac >> FRACBITS) & 127]];
        dest += screenwidth;
        frac += fracstep;
    }
    while (count--);
}

void R_DrawTLColumn(void)
{
    int count;
    byte *dest;
    fixed_t frac, fracstep;

    if (!dc_yl)
        dc_yl = 1;
    if (dc_yh == viewheight - 1)
        dc_yh = viewheight - 2;

    count = dc_yh - dc_yl;
    if (count < 0)
        return;

#ifdef RANGECHECK
    if ((unsigned) dc_x >= screenwidth || dc_yl < 0 || dc_yh >= SCREENHEIGHT)
        I_Error(english_language ?
                "R_DrawTLColumn: %i to %i at %i" :
                "R_DrawTLColumn: %i к %i в %i",
                dc_yl, dc_yh, dc_x);
#endif

    dest = ylookup[dc_yl] + columnofs[flipwidth[dc_x]];

    fracstep = dc_iscale;
    frac = dc_texturemid + (dc_yl - centery) * fracstep;

    do
    {
        *dest = tinttable[*dest +
                          (dc_colormap[dc_source[(frac >> FRACBITS) & 127]] <<
                           8)];
        dest += screenwidth;
        frac += fracstep;
    }
    while (count--);
}

//============================================================================
//
// R_DrawAltTLColumn
//
//============================================================================

void R_DrawAltTLColumn(void)
{
    int count;
    byte *dest;
    fixed_t frac, fracstep;

    if (!dc_yl)
        dc_yl = 1;
    if (dc_yh == viewheight - 1)
        dc_yh = viewheight - 2;

    count = dc_yh - dc_yl;
    if (count < 0)
        return;

#ifdef RANGECHECK
    if ((unsigned) dc_x >= screenwidth || dc_yl < 0 || dc_yh >= SCREENHEIGHT)
        I_Error(english_language ?
                "R_DrawAltTLColumn: %i to %i at %i" :
                "R_DrawAltTLColumn: %i к %i в %i",
                dc_yl, dc_yh, dc_x);
#endif

    dest = ylookup[dc_yl] + columnofs[flipwidth[dc_x]];

    fracstep = dc_iscale;
    frac = dc_texturemid + (dc_yl - centery) * fracstep;

    do
    {
        *dest = tinttable[((*dest) << 8)
                          + dc_colormap[dc_source[(frac >> FRACBITS) & 127]]];
        dest += screenwidth;
        frac += fracstep;
    }
    while (count--);
}

/*
========================
=
= R_DrawTranslatedColumn
=
========================
*/

byte *dc_translation;
byte *translationtables;

void R_DrawTranslatedColumn(void)
{
    int count;
    byte *dest;
    fixed_t frac, fracstep;

    count = dc_yh - dc_yl;
    if (count < 0)
        return;

#ifdef RANGECHECK
    if ((unsigned) dc_x >= screenwidth || dc_yl < 0 || dc_yh >= SCREENHEIGHT)
        I_Error(english_language ?
                "R_DrawColumn: %i to %i at %i" :
                "R_DrawColumn: %i к %i в %i",
                dc_yl, dc_yh, dc_x);
#endif

    dest = ylookup[dc_yl] + columnofs[flipwidth[dc_x]];

    fracstep = dc_iscale;
    frac = dc_texturemid + (dc_yl - centery) * fracstep;

    do
    {
        *dest = dc_colormap[dc_translation[dc_source[frac >> FRACBITS]]];
        dest += screenwidth;
        frac += fracstep;
    }
    while (count--);
}

//============================================================================
//
// R_DrawTranslatedTLColumn
//
//============================================================================

void R_DrawTranslatedTLColumn(void)
{
    int count;
    byte *dest;
    fixed_t frac, fracstep;

    count = dc_yh - dc_yl;
    if (count < 0)
        return;

#ifdef RANGECHECK
    if ((unsigned) dc_x >= screenwidth || dc_yl < 0 || dc_yh >= SCREENHEIGHT)
        I_Error(english_language ?
                "R_DrawColumn: %i to %i at %i" :
                "R_DrawColumn: %i к %i в %i",
                dc_yl, dc_yh, dc_x);
#endif

    dest = ylookup[dc_yl] + columnofs[flipwidth[dc_x]];

    fracstep = dc_iscale;
    frac = dc_texturemid + (dc_yl - centery) * fracstep;

    do
    {
        *dest = tinttable[((*dest) << 8)
                          +
                          dc_colormap[dc_translation
                                      [dc_source[frac >> FRACBITS]]]];
        dest += screenwidth;
        frac += fracstep;
    }
    while (count--);
}

//============================================================================
//
// R_DrawTranslatedAltTLColumn
//
//============================================================================

/*
void R_DrawTranslatedAltTLColumn (void)
{
	int			count;
	byte		*dest;
	fixed_t		frac, fracstep;	

	count = dc_yh - dc_yl;
	if (count < 0)
		return;
				
#ifdef RANGECHECK
	if ((unsigned)dc_x >= screenwidth || dc_yl < 0 || dc_yh >= SCREENHEIGHT)
		I_Error ("R_DrawColumn: %i to %i at %i", dc_yl, dc_yh, dc_x);
#endif

	dest = ylookup[dc_yl] + columnofs[dc_x];
	
	fracstep = dc_iscale;
	frac = dc_texturemid + (dc_yl-centery)*fracstep;

	do
	{
		*dest = tinttable[*dest
			+(dc_colormap[dc_translation[dc_source[frac>>FRACBITS]]]<<8)];
		dest += screenwidth;
		frac += fracstep;
	} while (count--);
}
*/

//--------------------------------------------------------------------------
//
// PROC R_InitTranslationTables
//
//--------------------------------------------------------------------------

void R_InitTranslationTables(void)
{
    int i;
    byte *transLump;
    int lumpnum;

    V_LoadTintTable();

    // Allocate translation tables
    translationtables = Z_Malloc(256 * 3 * (maxplayers - 1), PU_STATIC, 0);

    for (i = 0; i < 3 * (maxplayers - 1); i++)
    {
        lumpnum = W_GetNumForName("trantbl0") + i;
        transLump = W_CacheLumpNum(lumpnum, PU_STATIC);
        memcpy(translationtables + i * 256, transLump, 256);
        W_ReleaseLumpNum(lumpnum);
    }
}

/*
================
=
= R_DrawSpan
=
================
*/

int ds_y;
int ds_x1;
int ds_x2;
lighttable_t *ds_colormap;
fixed_t ds_xfrac;
fixed_t ds_yfrac;
fixed_t ds_xstep;
fixed_t ds_ystep;
byte *ds_source;                // start of a 64*64 tile image


void R_DrawSpan(void)
{
    fixed_t xfrac, yfrac;
    byte *dest;
    int count, spot;

#ifdef RANGECHECK
    if (ds_x2 < ds_x1 || ds_x1 < 0 || ds_x2 >= screenwidth
        || (unsigned) ds_y > SCREENHEIGHT)
        I_Error(english_language ?
                "R_DrawSpan: %i to %i at %i" :
                "R_DrawSpan: %i к %i в %i",
                ds_x1, ds_x2, ds_y);
#endif

    xfrac = ds_xfrac;
    yfrac = ds_yfrac;

    // dest = ylookup[ds_y] + columnofs[ds_x1];
    count = ds_x2 - ds_x1;
    do
    {
        spot = ((yfrac >> (16 - 6)) & (63 * 64)) + ((xfrac >> 16) & 63);
        // *dest++ = ds_colormap[ds_source[spot]];
        dest = ylookup[ds_y] + columnofs[flipwidth[ds_x1++]];
        *dest = ds_colormap[ds_source[spot]];
        xfrac += ds_xstep;
        yfrac += ds_ystep;
    }
    while (count--);
}

void R_DrawSpanLow(void)
{
    fixed_t xfrac, yfrac;
    byte *dest;
    int count, spot;

#ifdef RANGECHECK
    if (ds_x2 < ds_x1 || ds_x1 < 0 || ds_x2 >= screenwidth
        || (unsigned) ds_y > SCREENHEIGHT)
        I_Error(english_language ?
                "R_DrawSpan: %i to %i at %i" :
                "R_DrawSpan: %i к %i в %i",
                ds_x1, ds_x2, ds_y);
#endif

    xfrac = ds_xfrac;
    yfrac = ds_yfrac;

    // dest = ylookup[ds_y] + columnofs[ds_x1];
    count = ds_x2 - ds_x1;
    do
    {
        spot = ((yfrac >> (16 - 6)) & (63 * 64)) + ((xfrac >> 16) & 63);
        dest = ylookup[ds_y] + columnofs[flipwidth[ds_x1++]];
        *dest = ds_colormap[ds_source[spot]];
        xfrac += ds_xstep;
        yfrac += ds_ystep;
    }
    while (count--);
}



/*
================
=
= R_InitBuffer
=
=================
*/

void R_InitBuffer(int width, int height)
{
    int i;

    viewwindowx = (screenwidth - width) >> 1;
    for (i = 0; i < width; i++)
        columnofs[i] = viewwindowx + i;
    if (width == screenwidth)
        viewwindowy = 0;
    else
        viewwindowy = (SCREENHEIGHT - SBARHEIGHT - height) >> 1;
    for (i = 0; i < height; i++)
        ylookup[i] = I_VideoBuffer + (i + viewwindowy) * screenwidth;
}


/*
==================
=
= R_DrawViewBorder
=
= Draws the border around the view for different size windows
==================
*/

boolean BorderNeedRefresh;

void R_DrawViewBorder(void)
{
    byte *src, *dest;
    int x, y;

    if (scaledviewwidth == screenwidth)
        return;

    src = W_CacheLumpName("F_022", PU_CACHE);
    dest = I_VideoBuffer;

    for (y = 0; y < SCREENHEIGHT - SBARHEIGHT; y++)
    {
        for (x = 0; x < screenwidth / 64; x++)
        {
            memcpy(dest, src + ((y & 63) << 6), 64);
            dest += 64;
        }
        if (screenwidth & 63)
        {
            memcpy(dest, src + ((y & 63) << 6), screenwidth & 63);
            dest += (screenwidth & 63);
        }
    }
    for (x = (viewwindowx >> hires); x < (viewwindowx >> hires) + (viewwidth >> hires); x += 16)
    {
        V_DrawPatch(x, (viewwindowy >> hires) - 4, W_CacheLumpName("bordt", PU_CACHE));
        V_DrawPatch(x, (viewwindowy >> hires) + (viewheight >> hires), W_CacheLumpName("bordb",
                                                                 PU_CACHE));
    }
    for (y = (viewwindowy >> hires); y < (viewwindowy >> hires) + (viewheight >> hires); y += 16)
    {
        V_DrawPatch((viewwindowx >> hires) - 4, y, W_CacheLumpName("bordl", PU_CACHE));
        V_DrawPatch((viewwindowx >> hires) + (viewwidth >> hires), y, W_CacheLumpName("bordr",
                                                                PU_CACHE));
    }
    V_DrawPatch((viewwindowx >> hires) - 4, (viewwindowy >> hires) - 4, W_CacheLumpName("bordtl",
                                                                  PU_CACHE));
    V_DrawPatch((viewwindowx >> hires) + (viewwidth >> hires), (viewwindowy >> hires) - 4,
                W_CacheLumpName("bordtr", PU_CACHE));
    V_DrawPatch((viewwindowx >> hires) + (viewwidth >> hires), (viewwindowy >> hires) + (viewheight >> hires),
                W_CacheLumpName("bordbr", PU_CACHE));
    V_DrawPatch((viewwindowx >> hires) - 4, (viewwindowy >> hires) + (viewheight >> hires),
                W_CacheLumpName("bordbl", PU_CACHE));
}

/*
==================
=
= R_DrawTopBorder
=
= Draws the top border around the view for different size windows
==================
*/

boolean BorderTopRefresh;

void R_DrawTopBorder(void)
{
    byte *src, *dest;
    int x, y;

    if (scaledviewwidth == screenwidth)
        return;

/*	if(gamemode == shareware)
	{
		src = W_CacheLumpName ("FLOOR04", PU_CACHE);
	}
	else
	{
		src = W_CacheLumpName ("FLAT513", PU_CACHE);
	}
*/
    src = W_CacheLumpName("F_022", PU_CACHE);
    dest = I_VideoBuffer;

    for (y = 0; y < (34 << hires); y++)
    {
        for (x = 0; x < screenwidth / 64; x++)
        {
            memcpy(dest, src + ((y & 63) << 6), 64);
            dest += 64;
        }
        if (screenwidth & 63)
        {
            memcpy(dest, src + ((y & 63) << 6), screenwidth & 63);
            dest += (screenwidth & 63);
        }
    }
    if (viewwindowy < 35)
    {
        for (x = (viewwindowx >> hires); x < (viewwindowx >> hires) + (viewwidth >> hires); x += 16)
        {
            V_DrawPatch(x, (viewwindowy >> hires) - 4,
                        W_CacheLumpName("bordt", PU_CACHE));
        }
        V_DrawPatch((viewwindowx >> hires) - 4, (viewwindowy >> hires), W_CacheLumpName("bordl",
                                                                  PU_CACHE));
        V_DrawPatch((viewwindowx >> hires) + (viewwidth >> hires), (viewwindowy >> hires),
                    W_CacheLumpName("bordr", PU_CACHE));
        V_DrawPatch((viewwindowx >> hires) - 4, (viewwindowy >> hires) + 16,
                    W_CacheLumpName("bordl", PU_CACHE));
        V_DrawPatch((viewwindowx >> hires) + (viewwidth >> hires), (viewwindowy >> hires) + 16,
                    W_CacheLumpName("bordr", PU_CACHE));

        V_DrawPatch((viewwindowx >> hires) - 4, (viewwindowy >> hires) - 4,
                    W_CacheLumpName("bordtl", PU_CACHE));
        V_DrawPatch((viewwindowx >> hires) + (viewwidth >> hires), (viewwindowy >> hires) - 4,
                    W_CacheLumpName("bordtr", PU_CACHE));
    }
}
