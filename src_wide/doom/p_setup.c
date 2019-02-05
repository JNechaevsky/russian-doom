//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
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
// DESCRIPTION:
//	Do all the WAD I/O, get map description,
//	set up initial state and misc. LUTs.
//

// Russian Doom (C) 2016-2018 Julian Nechaevsky


#include <math.h>

#include "z_zone.h"

#include "deh_main.h"
#include "i_swap.h"
#include "m_argv.h"
#include "m_bbox.h"

#include "g_game.h"

#include "i_system.h"
#include "w_wad.h"

#include "doomdef.h"
#include "p_local.h"

#include "s_sound.h"

#include "doomstat.h"

#include "crispy.h"
#include "jn.h"
#include "p_fix.h"


void	P_SpawnMapThing (mapthing_t*	mthing);


//
// MAP related Lookup tables.
// Store VERTEXES, LINEDEFS, SIDEDEFS, etc.
//
int		numvertexes;
vertex_t*	vertexes;

int		numsegs;
seg_t*		segs;

int		numsectors;
sector_t*	sectors;

int		numsubsectors;
subsector_t*	subsectors;

int		numnodes;
node_t*		nodes;

int		numlines;
line_t*		lines;

int		numsides;
side_t*		sides;

int     detailLevel; // [JN] & [crispy] Необходимо для WiggleFix

static int      totallines;

boolean canmodify;

gameaction_t    gameaction;

// BLOCKMAP
// Created from axis aligned bounding box
// of the map, a rectangular array of
// blocks of size ...
// Used to speed up collision detection
// by spatial subdivision in 2D.
//
// Blockmap size.
int		bmapwidth;
int		bmapheight;	// size in mapblocks
long*		blockmap;	// int for larger maps
// offsets in blockmap are from here
long*		blockmaplump;		
// origin of block map
fixed_t		bmaporgx;
fixed_t		bmaporgy;
// for thing chains
mobj_t**	blocklinks;		


// REJECT
// For fast sight rejection.
// Speeds up enemy AI by skipping detailed
//  LineOf Sight calculation.
// Without special effect, this could be
//  used as a PVS lookup as well.
//
byte*		rejectmatrix;


// Maintain single and multi player starting spots.
#define MAX_DEATHMATCH_STARTS	10

mapthing_t	deathmatchstarts[MAX_DEATHMATCH_STARTS];
mapthing_t*	deathmatch_p;
mapthing_t	playerstarts[MAXPLAYERS];
boolean     playerstartsingame[MAXPLAYERS];

// [crispy] recalculate seg offsets
// adapted from prboom-plus/src/p_setup.c:474-482
static fixed_t GetOffset(vertex_t *v1, vertex_t *v2)
{
    fixed_t dx, dy;
    fixed_t r;

    dx = (v1->x - v2->x)>>FRACBITS;
    dy = (v1->y - v2->y)>>FRACBITS;
    r = (fixed_t)(sqrt(dx*dx + dy*dy))<<FRACBITS;

    return r;
}



//
// P_LoadVertexes
//
void P_LoadVertexes (int lump)
{
    byte*		data;
    int			i;
    mapvertex_t*	ml;
    vertex_t*		li;

    // Determine number of lumps:
    //  total lump length / vertex record length.
    numvertexes = W_LumpLength (lump) / sizeof(mapvertex_t);

    // Allocate zone memory for buffer.
    vertexes = Z_Malloc (numvertexes*sizeof(vertex_t),PU_LEVEL,0);	

    // Load data into cache.
    data = W_CacheLumpNum (lump, PU_STATIC);
	
    ml = (mapvertex_t *)data;
    li = vertexes;

    // Copy and convert vertex coordinates,
    // internal representation as fixed.
    for (i=0 ; i<numvertexes ; i++, li++, ml++)
    {
	li->x = SHORT(ml->x)<<FRACBITS;
	li->y = SHORT(ml->y)<<FRACBITS;
    
    // [BH] Apply any map-specific fixes.
    if (canmodify && fix_map_errors)
    {
        for (int j = 0; vertexfix[j].mission != -1; j++)
        {
            if (i == vertexfix[j].vertex && gamemission == vertexfix[j].mission
                && gameepisode == vertexfix[j].epsiode && gamemap == vertexfix[j].map
                && vertexes[i].x == SHORT(vertexfix[j].oldx) << FRACBITS
                && vertexes[i].y == SHORT(vertexfix[j].oldy) << FRACBITS)
            {
                vertexes[i].x = SHORT(vertexfix[j].newx) << FRACBITS;
                vertexes[i].y = SHORT(vertexfix[j].newy) << FRACBITS;

                break;
            }
        }
    }

	if ((flip_levels || flip_levels_cmdline) && singleplayer)
	    li->x = -li->x;
    
    // [crispy] initialize pseudovertexes with actual vertex coordinates
    li->px = li->x;
    li->py = li->y;
    li->moved = false;
    }

    // Free buffer memory.
    W_ReleaseLumpNum(lump);
}

//
// GetSectorAtNullAddress
//
sector_t* GetSectorAtNullAddress(void)
{
    static boolean null_sector_is_initialized = false;
    static sector_t null_sector;

    if (!null_sector_is_initialized)
    {
        memset(&null_sector, 0, sizeof(null_sector));
        I_GetMemoryValue(0, &null_sector.floorheight, 4);
        I_GetMemoryValue(4, &null_sector.ceilingheight, 4);
        null_sector_is_initialized = true;
    }

    return &null_sector;
}

//
// P_LoadSegs
//
void P_LoadSegs (int lump)
{
    byte*		data;
    int			i;
    mapseg_t*		ml;
    seg_t*		li;
    line_t*		ldef;
    int			linedef;
    int			side;
    int                 sidenum;
	
    numsegs = W_LumpLength (lump) / sizeof(mapseg_t);
    segs = Z_Malloc (numsegs*sizeof(seg_t),PU_LEVEL,0);	
    memset (segs, 0, numsegs*sizeof(seg_t));
    data = W_CacheLumpNum (lump,PU_STATIC);
	
    ml = (mapseg_t *)data;
    li = segs;
    for (i=0 ; i<numsegs ; i++, li++, ml++)
    {
    li->v1 = &vertexes[(unsigned short)SHORT(ml->v1)]; // [crispy] extended nodes
    li->v2 = &vertexes[(unsigned short)SHORT(ml->v2)]; // [crispy] extended nodes

	if ((flip_levels || flip_levels_cmdline) && singleplayer)
	{
            vertex_t* tmp = li->v1;
            li->v1 = li->v2;
            li->v2 = tmp;
	}
    
	li->angle = (SHORT(ml->angle))<<FRACBITS;
    
	if ((flip_levels || flip_levels_cmdline) && singleplayer)
            li->angle = -li->angle;
    
//	li->offset = (SHORT(ml->offset))<<FRACBITS; // [crispy] recalculated below
	linedef = (unsigned short)SHORT(ml->linedef); // [crispy] extended nodes
	ldef = &lines[linedef];
	li->linedef = ldef;
	side = SHORT(ml->side);

        // e6y: check for wrong indexes
        if ((unsigned)ldef->sidenum[side] >= (unsigned)numsides)
        {
            I_Error(english_language ?
                    "P_LoadSegs: linedef %d for seg %d references a non-existent sidedef %d" :
                    "P_LoadSegs: линия %d для сегмента %d указывает на несуществующую сторону %d",
                    linedef, i, (unsigned)ldef->sidenum[side]);
        }

	li->sidedef = &sides[ldef->sidenum[side]];
	li->frontsector = sides[ldef->sidenum[side]].sector;
	// [crispy] recalculate
    if (singleplayer)
    li->offset = GetOffset(li->v1, ((ml->side ^ (flip_levels || flip_levels_cmdline) ? ldef->v2 : ldef->v1)));

        if (ldef-> flags & ML_TWOSIDED)
        {
            sidenum = ldef->sidenum[side ^ 1];

            // If the sidenum is out of range, this may be a "glass hack"
            // impassible window.  Point at side #0 (this may not be
            // the correct Vanilla behavior; however, it seems to work for
            // OTTAWAU.WAD, which is the one place I've seen this trick
            // used).

            if (sidenum < 0 || sidenum >= numsides)
            {
                li->backsector = GetSectorAtNullAddress();
            }
            else
            {
                li->backsector = sides[sidenum].sector;
            }
        }
        else
        {
	    li->backsector = 0;
        }
        
        // [BH] Apply any map-specific fixes.
        if (canmodify && fix_map_errors)
        {
            for (int j = 0; linefix[j].mission != -1; j++)
            {
                if (linedef == linefix[j].linedef && gamemission == linefix[j].mission
                    && gameepisode == linefix[j].epsiode && gamemap == linefix[j].map
                    && side == linefix[j].side)
                {
                    if (*linefix[j].toptexture)
                    {
                        li->sidedef->toptexture = R_TextureNumForName(linefix[j].toptexture);
                    }

                    if (*linefix[j].middletexture)
                    {
                        li->sidedef->midtexture = R_TextureNumForName(linefix[j].middletexture);
                    }

                    if (*linefix[j].bottomtexture)
                    {
                        li->sidedef->bottomtexture = R_TextureNumForName(linefix[j].bottomtexture);
                    }

                    if (linefix[j].offset != DEFAULT && !flip_levels && !flip_levels_cmdline )
                    {
                        li->offset = SHORT(linefix[j].offset) << FRACBITS;
                        li->sidedef->textureoffset = 0;
                    }

                    if (linefix[j].rowoffset != DEFAULT)
                    {
                        li->sidedef->rowoffset = SHORT(linefix[j].rowoffset) << FRACBITS;
                    }

                    if (linefix[j].flags != DEFAULT)
                    {
                        if (li->linedef->flags & linefix[j].flags)
                            li->linedef->flags &= ~linefix[j].flags;
                        else
                            li->linedef->flags |= linefix[j].flags;
                    }
                    if (linefix[j].special != DEFAULT)
                    {
                        li->linedef->special = linefix[j].special;
                    }

                    if (linefix[j].tag != DEFAULT)
                    {
                        li->linedef->tag = linefix[j].tag;
                    }

                    break;
                }
            }
        }
    }
	
    W_ReleaseLumpNum(lump);
}

// [crispy] fix long wall wobble
void P_SegLengths (void)
{
    int i;
    seg_t *li;
    fixed_t dx, dy;

    for (i = 0; i < numsegs; i++)
    {
	li = &segs[i];
	dx = li->v2->px - li->v1->px;
	dy = li->v2->py - li->v1->py;
	li->length = (fixed_t)sqrt((double)dx*dx + (double)dy*dy);
    }
}

//
// P_LoadSubsectors
//
void P_LoadSubsectors (int lump)
{
    byte*		data;
    int			i;
    mapsubsector_t*	ms;
    subsector_t*	ss;
	
    numsubsectors = W_LumpLength (lump) / sizeof(mapsubsector_t);
    subsectors = Z_Malloc (numsubsectors*sizeof(subsector_t),PU_LEVEL,0);	
    data = W_CacheLumpNum (lump,PU_STATIC);
	
    ms = (mapsubsector_t *)data;
    memset (subsectors,0, numsubsectors*sizeof(subsector_t));
    ss = subsectors;
    
    for (i=0 ; i<numsubsectors ; i++, ss++, ms++)
    {
    ss->numlines = (unsigned short)SHORT(ms->numsegs);
    ss->firstline = (unsigned short)SHORT(ms->firstseg);
    }
	
    W_ReleaseLumpNum(lump);
}



//
// P_LoadSectors
//
void P_LoadSectors (int lump)
{
    byte*		data;
    int			i;
    mapsector_t*	ms;
    sector_t*		ss;
	
    numsectors = W_LumpLength (lump) / sizeof(mapsector_t);
    sectors = Z_Malloc (numsectors*sizeof(sector_t),PU_LEVEL,0);	
    memset (sectors, 0, numsectors*sizeof(sector_t));
    data = W_CacheLumpNum (lump,PU_STATIC);
	
    ms = (mapsector_t *)data;
    ss = sectors;
    for (i=0 ; i<numsectors ; i++, ss++, ms++)
    {
	ss->floorheight = SHORT(ms->floorheight)<<FRACBITS;
	ss->ceilingheight = SHORT(ms->ceilingheight)<<FRACBITS;
	ss->floorpic = R_FlatNumForName(ms->floorpic);
	ss->ceilingpic = R_FlatNumForName(ms->ceilingpic);
	ss->lightlevel = SHORT(ms->lightlevel);
	ss->special = SHORT(ms->special);
	ss->tag = SHORT(ms->tag);
	ss->thinglist = NULL;
        if (!detailLevel)
        {
           // [crispy] WiggleFix: [kb] for R_FixWiggle()
            ss->cachedheight = 0;
        }

    // [AM] Sector interpolation.  Even if we're
    //      not running uncapped, the renderer still
    //      uses this data.
    ss->oldfloorheight = ss->floorheight;
    ss->interpfloorheight = ss->floorheight;
    ss->oldceilingheight = ss->ceilingheight;
    ss->interpceilingheight = ss->ceilingheight;
    // [crispy] inhibit sector interpolation during the 0th gametic
    ss->oldgametic = -1;

        // [BH] Apply any level-specific fixes.
        if (canmodify && fix_map_errors)
        {
            for (int j = 0; sectorfix[j].mission != -1; j++)
            {
                if (i == sectorfix[j].sector && gamemission == sectorfix[j].mission
                    && gameepisode == sectorfix[j].epsiode && gamemap == sectorfix[j].map)
                {
                    if (*sectorfix[j].floorpic)
                    {
                        ss->floorpic = R_FlatNumForName(sectorfix[j].floorpic);
                    }
    
                    if (*sectorfix[j].ceilingpic)
                    {
                        ss->ceilingpic = R_FlatNumForName(sectorfix[j].ceilingpic);
                    }
    
                    if (sectorfix[j].floorheight != DEFAULT)
                    {
                        ss->floorheight = SHORT(sectorfix[j].floorheight) << FRACBITS;
                    }
    
                    if (sectorfix[j].ceilingheight != DEFAULT)
                    {
                        ss->ceilingheight = SHORT(sectorfix[j].ceilingheight) << FRACBITS;
                    }
    
                    if (sectorfix[j].special != DEFAULT)
                    {
                        ss->special = SHORT(sectorfix[j].special);
                    }
    
                    if (sectorfix[j].newtag != DEFAULT && (sectorfix[j].oldtag == DEFAULT
                        || sectorfix[j].oldtag == ss->tag))
                    {
                        ss->tag = SHORT(sectorfix[j].newtag) << FRACBITS;
                    }
    
                    break;
                }
            }
        }
    }
	
    W_ReleaseLumpNum(lump);
}


//
// P_LoadNodes
//
void P_LoadNodes (int lump)
{
    byte*	data;
    int		i;
    int		j;
    int		k;
    mapnode_t*	mn;
    node_t*	no;
	
    numnodes = W_LumpLength (lump) / sizeof(mapnode_t);
    nodes = Z_Malloc (numnodes*sizeof(node_t),PU_LEVEL,0);	
    data = W_CacheLumpNum (lump,PU_STATIC);
	
    mn = (mapnode_t *)data;
    no = nodes;
    
    for (i=0 ; i<numnodes ; i++, no++, mn++)
    {
	no->x = SHORT(mn->x)<<FRACBITS;
	no->y = SHORT(mn->y)<<FRACBITS;
	no->dx = SHORT(mn->dx)<<FRACBITS;
	no->dy = SHORT(mn->dy)<<FRACBITS;
    
	if ((flip_levels || flip_levels_cmdline) && singleplayer)
	{
	    no->x += no->dx;
	    no->y += no->dy;
	    no->x = -no->x;
	    no->dy = -no->dy;
	}
    
	for (j=0 ; j<2 ; j++)
	{
	    no->children[j] = (unsigned short)SHORT(mn->children[j]);
        
        // [crispy] support for extended nodes
  	    if (no->children[j] == 0xFFFF)
  		no->children[j] = -1;
  	    else
  	    if (no->children[j] & 0x8000)
  	    {
  		no->children[j] &= ~0x8000;
  
  		if (no->children[j] >= numsubsectors)
  		    no->children[j] = 0;
  
  		no->children[j] |= NF_SUBSECTOR;
  	    }
        
	    for (k=0 ; k<4 ; k++)
		no->bbox[j][k] = SHORT(mn->bbox[j][k])<<FRACBITS;
    
	    if ((flip_levels || flip_levels_cmdline) && singleplayer)
	    {
		fixed_t tmp = no->bbox[j][2];
		no->bbox[j][2] = -no->bbox[j][3];
		no->bbox[j][3] = -tmp;
	    }    
	}
    }
	
    W_ReleaseLumpNum(lump);
}


//
// P_LoadThings
//
void P_LoadThings (int lump)
{
    byte               *data;
    int			i;
    mapthing_t         *mt;
    mapthing_t          spawnthing;
    int			numthings;
    boolean		spawn;

    data = W_CacheLumpNum (lump,PU_STATIC);
    numthings = W_LumpLength (lump) / sizeof(mapthing_t);
	
    mt = (mapthing_t *)data;
    for (i=0 ; i<numthings ; i++, mt++)
    {
	spawn = true;

	// Do not spawn cool, new monsters if !commercial
	if (gamemode != commercial)
	{
	    switch (SHORT(mt->type))
	    {
	      case 68:	// Arachnotron
	      case 64:	// Archvile
	      case 88:	// Boss Brain
	      case 89:	// Boss Shooter
	      case 69:	// Hell Knight
	      case 67:	// Mancubus
	      case 71:	// Pain Elemental
	      case 65:	// Former Human Commando
	      case 66:	// Revenant
	      case 84:	// Wolf SS
		spawn = false;
		break;
	    }
	}
	if (spawn == false)
	    continue;   // [BH] Fix <https://doomwiki.org/wiki/Doom_II_monster_exclusion_bug>.

	// [crispy] minor fixes to prevent users from getting stuck in levels with mapping errors
	if (singleplayer)
	{
	    // [crispy] spawn Former Human instead of Wolf SS in BFG Edition
	    if (gamevariant == bfgedition && mt->type == 84)
	    {
	        mt->type = 3004;
	    }
	    // [crispy] TNT MAP31 has a yellow key that is erroneously marked as multi-player only
	    if (gamemission == pack_tnt && gamemap == 31 && mt->type == 6)
	    {
	        mt->options &= ~16;
	    }
	    // [JN] Replace static candles and candelabras with animated ones.
        // It's *very* unsafe for internal demos, so there is also "reversive" condition.
	    if (canmodify)
	    {
            if (gameaction == ga_newgame || gameaction == ga_loadgame || gameaction == ga_worlddone)
            {
                if (mt->type == 34)     // Candle
                    mt->type = 4000;    // Animated candle
        
                if (mt->type == 35)     // Candelabra
                    mt->type = 4001;    // Animated candelabra
            }
            else
            {
                if (mt->type == 4000)   // Animated candle
                    mt->type = 34;      // Candle
            
                if (mt->type == 4001)   // Animated candelabra
                    mt->type = 35;      // Candelabra
            }
	    }
	}

	// Do spawn all other stuff. 
	spawnthing.x = SHORT(mt->x);
	spawnthing.y = SHORT(mt->y);
	spawnthing.angle = SHORT(mt->angle);
	spawnthing.type = SHORT(mt->type);
	spawnthing.options = SHORT(mt->options);

    // [BH] Apply any level-specific fixes.
    if (canmodify && fix_map_errors)
    {
        for (int j = 0; thingfix[j].mission != -1; j++)
        {
            if (gamemission == thingfix[j].mission && gameepisode == thingfix[j].epsiode
                && gamemap == thingfix[j].map && i == thingfix[j].thing && spawnthing.type == thingfix[j].type
                && spawnthing.x == SHORT(thingfix[j].oldx) && spawnthing.y == SHORT(thingfix[j].oldy))
            {
                if (thingfix[j].newx == REMOVE && thingfix[j].newy == REMOVE)
                    spawn = false;
                else
                {
                    spawnthing.x = SHORT(thingfix[j].newx);
                    spawnthing.y = SHORT(thingfix[j].newy);
                }
    
                if (thingfix[j].angle != DEFAULT)
                {
                    spawnthing.angle = SHORT(thingfix[j].angle);
                }
    
                if (thingfix[j].options != DEFAULT)
                {
                    spawnthing.options = thingfix[j].options;
                }
    
                break;
            }
        }
    }

	if ((flip_levels || flip_levels_cmdline) && singleplayer)
	{
	    spawnthing.x = -spawnthing.x;
	    spawnthing.angle = 180 - spawnthing.angle;
	}
	
	P_SpawnMapThing(&spawnthing);
    }

    if (!deathmatch)
        for (i = 0; i < MAXPLAYERS; i++)
            if (playeringame[i] && !playerstartsingame[i])
                I_Error(english_language ? 
                        "P_LoadThings: Player %d start missing" :
                        "P_LoadThings: Отсутствует стартовая точка игрока №%d", i + 1);

    W_ReleaseLumpNum(lump);
}


//
// P_LoadLineDefs
// Also counts secret lines for intermissions.
//
void P_LoadLineDefs (int lump)
{
    byte*		data;
    int			i;
    maplinedef_t*	mld;
    line_t*		ld;
    vertex_t*		v1;
    vertex_t*		v2;
	
    numlines = W_LumpLength (lump) / sizeof(maplinedef_t);
    lines = Z_Malloc (numlines*sizeof(line_t),PU_LEVEL,0);	
    memset (lines, 0, numlines*sizeof(line_t));
    data = W_CacheLumpNum (lump,PU_STATIC);
	
    mld = (maplinedef_t *)data;
    ld = lines;
    for (i=0 ; i<numlines ; i++, mld++, ld++)
    {
	ld->flags = (unsigned short)SHORT(mld->flags);
	ld->special = SHORT(mld->special);
	ld->tag = SHORT(mld->tag);

	if ((flip_levels || flip_levels_cmdline) && singleplayer)
	{
	    v1 = ld->v2 = &vertexes[(unsigned short)SHORT(mld->v2)]; // [crispy] extended nodes
	    v2 = ld->v1 = &vertexes[(unsigned short)SHORT(mld->v1)]; // [crispy] extended nodes
	}
	else
	{
	v1 = ld->v1 = &vertexes[(unsigned short)SHORT(mld->v1)]; // [crispy] extended nodes
	v2 = ld->v2 = &vertexes[(unsigned short)SHORT(mld->v2)]; // [crispy] extended nodes
	}

	ld->dx = v2->x - v1->x;
	ld->dy = v2->y - v1->y;
	
	if (!ld->dx)
	    ld->slopetype = ST_VERTICAL;
	else if (!ld->dy)
	    ld->slopetype = ST_HORIZONTAL;
	else
	{
	    if (FixedDiv (ld->dy , ld->dx) > 0)
		ld->slopetype = ST_POSITIVE;
	    else
		ld->slopetype = ST_NEGATIVE;
	}
		
	if (v1->x < v2->x)
	{
	    ld->bbox[BOXLEFT] = v1->x;
	    ld->bbox[BOXRIGHT] = v2->x;
	}
	else
	{
	    ld->bbox[BOXLEFT] = v2->x;
	    ld->bbox[BOXRIGHT] = v1->x;
	}

	if (v1->y < v2->y)
	{
	    ld->bbox[BOXBOTTOM] = v1->y;
	    ld->bbox[BOXTOP] = v2->y;
	}
	else
	{
	    ld->bbox[BOXBOTTOM] = v2->y;
	    ld->bbox[BOXTOP] = v1->y;
	}

    // [crispy] calculate sound origin of line to be its midpoint
    ld->soundorg.x = ld->bbox[BOXLEFT] / 2 + ld->bbox[BOXRIGHT] / 2;
    ld->soundorg.y = ld->bbox[BOXTOP] / 2 + ld->bbox[BOXBOTTOM] / 2;

	ld->sidenum[0] = SHORT(mld->sidenum[0]);
	ld->sidenum[1] = SHORT(mld->sidenum[1]);

	// [crispy] substitute dummy sidedef for missing right side
	if (ld->sidenum[0] == NO_INDEX)
	{
	    ld->sidenum[0] = 0;
	    fprintf(stderr, english_language ?
                        "P_LoadSegs: Linedef %d has two-sided flag set, but no second sidedef\n" :
                        "P_LoadLineDefs: у линии %d не назначена первая сторона!\n", i);
	}

	if (ld->sidenum[0] != NO_INDEX) // [crispy] extended nodes
	    ld->frontsector = sides[ld->sidenum[0]].sector;
	else
	    ld->frontsector = 0;

	if (ld->sidenum[1] != NO_INDEX) // [crispy] extended nodes
	    ld->backsector = sides[ld->sidenum[1]].sector;
	else
	    ld->backsector = 0;
    

    // [crispy] fix common wad errors (missing sidedefs)
    // adapted from prboom-plus/src/p_setup.c:1426
    {
    
        // linedef has out-of-range sidedef number
        if (ld->sidenum[0] != NO_INDEX && ld->sidenum[0] >= numsides)
        ld->sidenum[0] = NO_INDEX;
        
        if (ld->sidenum[1] != NO_INDEX && ld->sidenum[1] >= numsides)
        ld->sidenum[1] = NO_INDEX;
    
        // linedef missing first sidedef
        if (ld->sidenum[0] == NO_INDEX)
        ld->sidenum[0] = 0; // Substitute dummy sidedef for missing right side
    
        // linedef has two-sided flag set, but no second sidedef
        if ((ld->sidenum[1] == NO_INDEX) && (ld->flags & ML_TWOSIDED))
        {
        // e6y: ML_TWOSIDED flag shouldn't be cleared for compatibility purposes
        if (singleplayer)
            ld->flags &= ~ML_TWOSIDED; // Clear 2s flag for missing left side
        }
    }
    
    }

    W_ReleaseLumpNum(lump);
}


//
// P_LoadSideDefs
//
void P_LoadSideDefs (int lump)
{
    byte*		data;
    int			i;
    mapsidedef_t*	msd;
    side_t*		sd;
	
    numsides = W_LumpLength (lump) / sizeof(mapsidedef_t);
    sides = Z_Malloc (numsides*sizeof(side_t),PU_LEVEL,0);	
    memset (sides, 0, numsides*sizeof(side_t));
    data = W_CacheLumpNum (lump,PU_STATIC);
	
    msd = (mapsidedef_t *)data;
    sd = sides;
    for (i=0 ; i<numsides ; i++, msd++, sd++)
    {
	sd->textureoffset = SHORT(msd->textureoffset)<<FRACBITS;
	sd->rowoffset = SHORT(msd->rowoffset)<<FRACBITS;
	sd->toptexture = R_TextureNumForName(msd->toptexture);
	sd->bottomtexture = R_TextureNumForName(msd->bottomtexture);
	sd->midtexture = R_TextureNumForName(msd->midtexture);
	sd->sector = &sectors[SHORT(msd->sector)];
	// [crispy] smooth texture scrolling
	sd->oldtextureoffset = sd->textureoffset;
    }

    W_ReleaseLumpNum(lump);
}


//
// P_LoadBlockMap
//
// [crispy] remove BLOCKMAP limit
// adapted from boom202s/P_SETUP.C:1025-1076
void P_LoadBlockMap (int lump)
{
    int i;
    int count;
    int lumplen;
    short *wadblockmaplump;

    lumplen = W_LumpLength(lump);
    count = lumplen / 2;
	
    wadblockmaplump = Z_Malloc(lumplen, PU_LEVEL, NULL);
    W_ReadLump(lump, wadblockmaplump);
    blockmaplump = Z_Malloc(sizeof(*blockmaplump) * count, PU_LEVEL, NULL);
    blockmap = blockmaplump + 4;

    blockmaplump[0] = SHORT(wadblockmaplump[0]);
    blockmaplump[1] = SHORT(wadblockmaplump[1]);
    blockmaplump[2] = (long)(SHORT(wadblockmaplump[2])) & 0xffff;
    blockmaplump[3] = (long)(SHORT(wadblockmaplump[3])) & 0xffff;
    
    // Swap all short integers to native byte ordering.
  
    for (i=4; i<count; i++)
    {
	short t = SHORT(wadblockmaplump[i]);
	blockmaplump[i] = (t == -1) ? -1l : (long) t & 0xffff;
    }
    
    Z_Free(wadblockmaplump);
		
    // Read the header

    bmaporgx = blockmaplump[0]<<FRACBITS;
    bmaporgy = blockmaplump[1]<<FRACBITS;
    bmapwidth = blockmaplump[2];
    bmapheight = blockmaplump[3];
    
    if ((flip_levels || flip_levels_cmdline) && singleplayer)
    {
	int x, y;
	long* rowoffset; // [crispy] BLOCKMAP limit

	bmaporgx += bmapwidth * 128 * FRACUNIT;
	bmaporgx = -bmaporgx;

	for (y = 0; y < bmapheight; y++)
	{
	    rowoffset = blockmap + y * bmapwidth;

	    for (x = 0; x < bmapwidth / 2; x++)
	    {
	        long tmp; // [crispy] BLOCKMAP limit

	        tmp = rowoffset[x];
	        rowoffset[x] = rowoffset[bmapwidth-1-x];
	        rowoffset[bmapwidth-1-x] = tmp;
	    }
	}
    }
	
    // Clear out mobj chains

    count = sizeof(*blocklinks) * bmapwidth * bmapheight;
    blocklinks = Z_Malloc(count, PU_LEVEL, 0);
    memset(blocklinks, 0, count);
}



//
// P_GroupLines
// Builds sector line lists and subsector sector numbers.
// Finds block bounding boxes for sectors.
//
void P_GroupLines (void)
{
    line_t**		linebuffer;
    int			i;
    int			j;
    line_t*		li;
    sector_t*		sector;
    subsector_t*	ss;
    seg_t*		seg;
    fixed_t		bbox[4];
    int			block;
	
    // look up sector number for each subsector
    ss = subsectors;
    for (i=0 ; i<numsubsectors ; i++, ss++)
    {
	seg = &segs[ss->firstline];
	ss->sector = seg->sidedef->sector;
    }

    // count number of lines in each sector
    li = lines;
    totallines = 0;
    for (i=0 ; i<numlines ; i++, li++)
    {
	totallines++;
	li->frontsector->linecount++;

	if (li->backsector && li->backsector != li->frontsector)
	{
	    li->backsector->linecount++;
	    totallines++;
	}
    }

    // build line tables for each sector	
    linebuffer = Z_Malloc (totallines*sizeof(line_t *), PU_LEVEL, 0);

    for (i=0; i<numsectors; ++i)
    {
        // Assign the line buffer for this sector

        sectors[i].lines = linebuffer;
        linebuffer += sectors[i].linecount;

        // Reset linecount to zero so in the next stage we can count
        // lines into the list.

        sectors[i].linecount = 0;
    }

    // Assign lines to sectors

    for (i=0; i<numlines; ++i)
    { 
        li = &lines[i];

        if (li->frontsector != NULL)
        {
            sector = li->frontsector;

            sector->lines[sector->linecount] = li;
            ++sector->linecount;
        }

        if (li->backsector != NULL && li->frontsector != li->backsector)
        {
            sector = li->backsector;

            sector->lines[sector->linecount] = li;
            ++sector->linecount;
        }
    }
    
    // Generate bounding boxes for sectors
	
    sector = sectors;
    for (i=0 ; i<numsectors ; i++, sector++)
    {
	M_ClearBox (bbox);

	for (j=0 ; j<sector->linecount; j++)
	{
            li = sector->lines[j];

            M_AddToBox (bbox, li->v1->x, li->v1->y);
            M_AddToBox (bbox, li->v2->x, li->v2->y);
	}

	// set the degenmobj_t to the middle of the bounding box
	sector->soundorg.x = (bbox[BOXRIGHT]+bbox[BOXLEFT])/2;
	sector->soundorg.y = (bbox[BOXTOP]+bbox[BOXBOTTOM])/2;
		
	// adjust bounding box to map blocks
    block = (bbox[BOXTOP]-bmaporgy+MAXRADIUS)>>MAPBLOCKSHIFT;
	block = block >= bmapheight ? bmapheight-1 : block;
	sector->blockbox[BOXTOP]=block;

    block = (bbox[BOXBOTTOM]-bmaporgy-MAXRADIUS)>>MAPBLOCKSHIFT;
	block = block < 0 ? 0 : block;
	sector->blockbox[BOXBOTTOM]=block;

    block = (bbox[BOXRIGHT]-bmaporgx+MAXRADIUS)>>MAPBLOCKSHIFT;
	block = block >= bmapwidth ? bmapwidth-1 : block;
	sector->blockbox[BOXRIGHT]=block;

    block = (bbox[BOXLEFT]-bmaporgx-MAXRADIUS)>>MAPBLOCKSHIFT;
	block = block < 0 ? 0 : block;
	sector->blockbox[BOXLEFT]=block;
    }
	
}

// [crispy] remove slime trails
// mostly taken from Lee Killough's implementation in mbfsrc/P_SETUP.C:849-924,
// with the exception that not the actual vertex coordinates are modified,
// but pseudovertexes which are dummies that are *only* used in rendering,
// i.e. r_bsp.c:R_AddLine()

static void P_RemoveSlimeTrails(void)
{
    int i;

    for (i = 0; i < numsegs; i++)
    {
	const line_t *l = segs[i].linedef;
	vertex_t *v = segs[i].v1;

	// [crispy] ignore exactly vertical or horizontal linedefs
	if (l->dx && l->dy)
	{
	    do
	    {
		// [crispy] vertex wasn't already moved
		if (!v->moved)
		{
		    v->moved = true;
		    // [crispy] ignore endpoints of linedefs
		    if (v != l->v1 && v != l->v2)
		    {
			// [crispy] move the vertex towards the linedef
			// by projecting it using the law of cosines
			int64_t dx2 = (l->dx >> FRACBITS) * (l->dx >> FRACBITS);
			int64_t dy2 = (l->dy >> FRACBITS) * (l->dy >> FRACBITS);
			int64_t dxy = (l->dx >> FRACBITS) * (l->dy >> FRACBITS);
			int64_t s = dx2 + dy2;
			int x0 = v->x, y0 = v->y, x1 = l->v1->x, y1 = l->v1->y;

			// [crispy] MBF actually overrides v->x and v->y here
            v->px = (fixed_t)((dx2 * x0 + dy2 * x1 + dxy * (y0 - y1)) / s);
            v->py = (fixed_t)((dy2 * y0 + dx2 * y1 + dxy * (x0 - x1)) / s);
		    }
		}
	    // [crispy] if v doesn't point to the second vertex of the seg already, point it there
	    } while ((v != segs[i].v2) && (v = segs[i].v2));
	}
    }
}


// Pad the REJECT lump with extra data when the lump is too small,
// to simulate a REJECT buffer overflow in Vanilla Doom.

static void PadRejectArray(byte *array, unsigned int len)
{
    unsigned int i;
    unsigned int byte_num;
    byte *dest;
    unsigned int padvalue;

    // Values to pad the REJECT array with:

    unsigned int rejectpad[4] =
    {
        ((totallines * 4 + 3) & ~3) + 24,     // Size
        0,                                    // Part of z_zone block header
        50,                                   // PU_LEVEL
        0x1d4a11                              // DOOM_CONST_ZONEID
    };

    // Copy values from rejectpad into the destination array.

    dest = array;

    for (i=0; i<len && i<sizeof(rejectpad); ++i)
    {
        byte_num = i % 4;
        *dest = (rejectpad[i / 4] >> (byte_num * 8)) & 0xff;
        ++dest;
    }

    // We only have a limited pad size.  Print a warning if the
    // REJECT lump is too small.

    if (len > sizeof(rejectpad))
    {
        fprintf(stderr, english_language ?
                        "PadRejectArray: REJECT lump too short to pad! (%i > %i)\n" :
                        "PadRejectArray: блок REJECT слишком мал для заполнения! (%i > %i)\n",
                        len, (int) sizeof(rejectpad));

        // Pad remaining space with 0 (or 0xff, if specified on command line).

        if (M_CheckParm("-reject_pad_with_ff"))
        {
            padvalue = 0xff;
        }
        else
        {
            padvalue = 0xf00;
        }

        memset(array + sizeof(rejectpad), padvalue, len - sizeof(rejectpad));
    }
}

static void P_LoadReject(int lumpnum)
{
    int minlength;
    int lumplen;

    // Calculate the size that the REJECT lump *should* be.

    minlength = (numsectors * numsectors + 7) / 8;

    // If the lump meets the minimum length, it can be loaded directly.
    // Otherwise, we need to allocate a buffer of the correct size
    // and pad it with appropriate data.

    lumplen = W_LumpLength(lumpnum);

    if (lumplen >= minlength)
    {
        rejectmatrix = W_CacheLumpNum(lumpnum, PU_LEVEL);
    }
    else
    {
        rejectmatrix = Z_Malloc(minlength, PU_LEVEL, &rejectmatrix);
        W_ReadLump(lumpnum, rejectmatrix);

        PadRejectArray(rejectmatrix + lumplen, minlength - lumplen);
    }
}

//
// [JN] Check for non-standard nodes format, call I_Error in case if found one.
// Adapted from Crispy Doom (p_extnodes.c). Tested nodes so far:
//
// No nodes                        | not working
// BSP-W32 - Fast (no reject)      | working
// BSP-W32 - Normal                | working
// DeepBSP - Normal                | working (partially?)
// glBSP - Fast (no reject)        | not working (identified as uncompressed ZDBSP)
// glBSP - Normal                  | working
// ZDBSP - Compressed              | not working
// ZDBSP - Compressed (UDMF)       | not working (identified as uncompressed ZDBSP)
// ZDBSP - Normal (no reject)      | working (partially?)
// ZDBSP - Normal (0 reject)       | working (partially?)
// ZDBSP - UDMF Normal (no reject) | not working (identified as uncompressed ZDBSP)
// ZDBSP - UDMF Normal (0 reject)  | not working (identified as uncompressed ZDBSP)
// ZenNode - Fast (no reject)      | working
// ZenNode - Normal                | working
// 
void P_CheckMapFormat (int lumpnum)
{
    byte *nodes = NULL;
    int b;

    if ((b = lumpnum+ML_BLOCKMAP+1) < numlumps && !strncasecmp(lumpinfo[b]->name, "BEHAVIOR", 8))
    I_Error (english_language ?
             "Hexen map format is not supported" :
             "Уровни формата Hexen не поддерживаются");
    
    if (!((b = lumpnum+ML_NODES) < numlumps && (nodes = W_CacheLumpNum(b, PU_CACHE)) && W_LumpLength(b) > 0))
    {
        // fprintf(stderr, "no nodes.\n");
    }
    else if (!memcmp(nodes, "xNd4\0\0\0\0", 8))
    {
        I_Error (english_language ?
                 "DeePBSP nodes not supported" :
                 "Неподдерживаемый формат нодов: DeePBSP");
    }
    else if (!memcmp(nodes, "XNOD", 4))
    {
        I_Error (english_language ?
                 "Uncompressed ZDBSP nodes not supported" :
                 "Неподдерживаемый формат нодов: Uncompressed ZDBSP");
    }
    else if (!memcmp(nodes, "ZNOD", 4))
    {
        I_Error (english_language ?
                 "Compressed ZDBSP nodes not supported" :
                 "Неподдерживаемый формат нодов: Compressed ZDBSP");
    }

    if (nodes)
	W_ReleaseLumpNum(b);

    return;
}


//
// P_SetupLevel
//
void
P_SetupLevel
( int		episode,
  int		map,
  int		playermask,
  skill_t	skill)
{
    int		i;
    char	lumpname[9];
    int		lumpnum;
	
    totalkills = totalitems = totalsecret = wminfo.maxfrags = 0;
    wminfo.partime = 180;
    for (i=0 ; i<MAXPLAYERS ; i++)
    {
	players[i].killcount = players[i].secretcount = players[i].itemcount =  0;
    }

    // Initial height of PointOfView
    // will be set by player think.
    players[consoleplayer].viewz = 1; 

    // Make sure all sounds are stopped before Z_FreeTags.
    S_Start ();			

    Z_FreeTags (PU_LEVEL, PU_PURGELEVEL-1);

    // UNUSED W_Profile ();
    P_InitThinkers ();

    // if working with a devlopment map, reload it
    W_Reload ();

    // find map name
    if ( gamemode == commercial)
    {
	if (map<10)
	    DEH_snprintf(lumpname, 9, "map0%i", map);
	else
	    DEH_snprintf(lumpname, 9, "map%i", map);
    }
    else
    {
	lumpname[0] = 'E';
	lumpname[1] = '0' + episode;
	lumpname[2] = 'M';
	lumpname[3] = '0' + map;
	lumpname[4] = 0;
    }

    lumpnum = W_GetNumForName (lumpname);
	
    // [JN] Checking for multiple map lump names for allowing map fixes to work.
    // Adaptaken from Doom Retro, thanks Brad Harding!
    //  Fixes also should not work for: network game, shareware, IWAD versions below 1.9,
    //  vanilla game mode, Press Beta, Atari Jaguar, Freedoom and FreeDM.
    canmodify = (((W_CheckMultipleLumps(lumpname) == 1 || gamemission == pack_nerve)
        && (!netgame && !vanillaparm
        && gamemode != shareware
        && gameversion >= exe_doom_1_9
        && gamemode != pressbeta
        && gamemission != jaguar
        && gamevariant != freedoom && gamevariant != freedm)));

    leveltime = 0;
    
    // [crispy] check and log map and nodes format
    // [JN] No support for non-standard nodes for now, just call I_Error instead of lock up.
    P_CheckMapFormat(lumpnum);

    // note: most of this ordering is important	
    P_LoadBlockMap (lumpnum+ML_BLOCKMAP);
    P_LoadVertexes (lumpnum+ML_VERTEXES);
    P_LoadSectors (lumpnum+ML_SECTORS);
    P_LoadSideDefs (lumpnum+ML_SIDEDEFS);

    P_LoadLineDefs (lumpnum+ML_LINEDEFS);
    P_LoadSubsectors (lumpnum+ML_SSECTORS);
    P_LoadNodes (lumpnum+ML_NODES);
    P_LoadSegs (lumpnum+ML_SEGS);

    P_GroupLines ();
    P_LoadReject (lumpnum+ML_REJECT);
    
    // [crispy] remove slime trails
    P_RemoveSlimeTrails();
    // [crispy] fix long wall wobble
    P_SegLengths();
    // [crispy] blinking key or skull in the status bar
    memset(st_keyorskull, 0, sizeof(st_keyorskull));

    bodyqueslot = 0;
    deathmatch_p = deathmatchstarts;
    P_LoadThings (lumpnum+ML_THINGS);
    
    // if deathmatch, randomly spawn the active players
    if (deathmatch)
    {
	for (i=0 ; i<MAXPLAYERS ; i++)
	    if (playeringame[i])
	    {
		players[i].mo = NULL;
		G_DeathMatchSpawnPlayer (i);
	    }
			
    }

    // clear special respawning que
    iquehead = iquetail = 0;		
	
    // set up world state
    P_SpawnSpecials ();
	
    // build subsector connect matrix
    //	UNUSED P_ConnectSubsectors ();

    // preload graphics
    if (precache)
	R_PrecacheLevel ();

    //printf ("free memory: 0x%x\n", Z_FreeMemory());

}



//
// P_Init
//
void P_Init (void)
{
    P_InitSwitchList ();
    P_InitPicAnims ();
    R_InitSprites (sprnames);
}


