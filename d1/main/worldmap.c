#include <math.h>
#include "gr.h"
#include "newmenu.h"
#include "pcx.h"
#include "dxxerror.h"
#include "window.h"
#include "ogl_init.h"
#include "timer.h"
#include "gamefont.h"
#include "screens.h"
#include "key.h"
#include "config.h"
#include "gameseq.h"
#include "physfsx.h"
#include "game.h"
#include "args.h"
#include "worldmap.h"
#include "u_mem.h"
#include "player.h"
#include "menu.h"

//Level selection screen, based on Arne's reverse engineering of the solar map from PSX Descent.

#define TRANSITION_TIME (1.f/6)

static int ease(int a, int b, float time)
{
	float timesq = time * time;
	return (int)(a + (b - a) * (3 * timesq - 2 * timesq * time));
	//return (int)(a + (b - a) * time);
}

typedef struct {
	short level, map, x, y;
	const char* name;
} mapentry_t;

typedef enum
{
	BG_MAP01,
	BG_MAP02,
	BG_MAP03,
	NUMBGS
} bg_e;

typedef enum
{
	ICON_CURSOR,
	ICON_LOCATION,
	NUMICONS
} icon_e;

mapentry_t mapentries[] = {
	{ 1, 0, 286, 143, "Lunar Outpost" },
	{ 2, 0, 249, 136, "Lunar Scilab" },
	{ 3, 0, 208, 120, "Lunar Military Base" },
	{ 4, 0, 157, 110, "Venus Atmospheric Lab" },
	{ 5, 0, 130, 115, "Venus Nickel-Iron Mine" },
	{ 6, 0, 97, 140, "Mercury Solar Lab" },
	{ 7, 0, 82, 168, "Mercury Core" },
	{ 8, 1, 26, 105, "Mars Processing Station" },
	{ 9, 1, 53, 72, "Mars Military Dig" },
	{ 10, 1, 83, 64, "Mars Military Base" },
	{ -1, 1, 94, 69, "Asteroid Secret Base" },
	{ 11, 1, 106, 74, "Io Sulfur Mine" },
	{ 12, 1, 125, 100, "Callisto Tower Colony" },
	{ 13, 1, 145, 112, "Europa Mining Colony" },
	{ 14, 1, 177, 119, "Europa CO2 Mine" },
	{ 15, 1, 205, 135, "Titan Mine" },
	{ 16, 1, 225, 159, "Hyperion Methane Mine" },
	{ 17, 1, 262, 182, "Tethys H2O Mine" },
	{ 18, 2, 15, 48, "Miranda Mine" },
	{ 19, 2, 19, 76, "Oberon Mine" },
	{ 20, 2, 25, 110, "Oberon Iron Mine" },
	{ 21, 2, 65, 131, "Oberon Platnium Mine" },
	{ -2, 2, 86, 125, "Asteroid Military Depot" },
	{ 22, 2, 107, 120, "Neptune Storage Depot" },
	{ 23, 2, 144, 96, "Triton Storage Depot" },
	{ 24, 2, 188, 90, "Nereid Volatile Mine" },
	{ -3, 2, 211, 96, "Asteroid Robot Factory" },
	{ 25, 2, 235, 102, "Pluto Outpost" },
	{ 26, 2, 267, 108, "Pluto Military Base" },
	{ 27, 2, 296, 98, "Charon Volatile Mine" } };

#define NUM_MAP_ENTRIES (sizeof(mapentries) / sizeof(mapentries[0]))

typedef struct worldmap_s
{
	struct window* window;

	//transitiontime is used to animate the transition between oldindex to index. 
	//The current background page will also be shifted if the definition changes. 
	int index, oldindex;
	int bg, oldbg;
	float transitiontime; //yeah it's a float because ease is easier to write this way

	grs_bitmap bgpics[NUMBGS];
	ubyte bgpals[NUMBGS][768];

	grs_bitmap iconpics[NUMICONS];
	ubyte iconpals[NUMICONS][768];

	fix64 last_frame;
	fix frame_time;
} worldmap_t;

static void draw_icon(grs_bitmap* bmp, int x, int y, float scale)
{
	ogl_ubitmapm_cs(x * scale, y * scale, bmp->bm_w * scale, bmp->bm_h * scale, bmp, -1, F1_0);
}

void worldmap_draw(worldmap_t* self)
{
	const float w = (float)SWIDTH / 320;
	const float h = (float)SHEIGHT / 240;
	const float scale = (w < h) ? w : h;

	gr_clear_canvas(BM_XRGB(0, 0, 0));

	grs_canvas* old_canv = grd_curcanv;
	grs_canvas* sub_canv = gr_create_sub_canvas(old_canv, 0, 20 * scale, 320 * scale, 200 * scale);
	grd_curcanv = sub_canv;

	//Draw the background.
	//If there is no animation active, this just draws the current bg. 
	//However, if there is animation, this will draw a sliding window of all 3 background. 
	if (self->transitiontime == 0 || self->bg == self->oldbg) //this will explicitly be zeroed, so a direct equals should be safe
	{
		memcpy(gr_palette, self->bgpals[self->bg], sizeof(gr_palette));
		gr_palette_load(gr_palette);

		show_fullscr(&self->bgpics[self->bg]);
	}
	else
	{
		float frac = 1.f - self->transitiontime / TRANSITION_TIME;
		int xoffset = ease(self->oldbg * 320, self->bg * 320, frac);
		for (int i = 0; i < 3; i++)
		{
			memcpy(gr_palette, self->bgpals[i], sizeof(gr_palette));
			gr_palette_load(gr_palette);
			ogl_ubitmapm_cs((i * 320 - xoffset) * scale, 0, 320 * scale, 200 * scale, &self->bgpics[i], -1, F1_0);
		}
	}

	//Draw the map positions
	memcpy(gr_palette, self->iconpals[ICON_LOCATION], sizeof(gr_palette));
	gr_palette_load(gr_palette);
	if (self->transitiontime == 0 || self->bg == self->oldbg)
	{
		for (int i = 0; i < NUM_MAP_ENTRIES; i++)
		{
			mapentry_t* entry = &mapentries[i];
			if (entry->map == self->bg)
			{
				draw_icon(&self->iconpics[ICON_LOCATION], entry->x - 8, entry->y - 8, scale);
			}
		}
	}
	else
	{
		float frac = 1.f - self->transitiontime / TRANSITION_TIME;
		int xoffset = ease(self->oldbg * 320, self->bg * 320, frac);
		for (int i = 0; i < NUM_MAP_ENTRIES; i++)
		{
			mapentry_t* entry = &mapentries[i];
			draw_icon(&self->iconpics[ICON_LOCATION], (entry->x - 8 + entry->map * 320) - xoffset, entry->y - 8, scale);
		}
	}

	//Draw the cursor in screen-space
	int cursorx = mapentries[self->index].x;
	int cursory = mapentries[self->index].y;

	if (self->transitiontime > 0)
	{
		float frac = 1.f - self->transitiontime / TRANSITION_TIME;
		cursorx = ease(mapentries[self->oldindex].x, cursorx, frac);
		cursory = ease(mapentries[self->oldindex].y, cursory, frac);
	}

	memcpy(gr_palette, self->iconpals[ICON_CURSOR], sizeof(gr_palette));
	gr_palette_load(gr_palette);

	draw_icon(&self->iconpics[ICON_CURSOR], cursorx - 10, cursory - 10, scale);

	//Draw the currently selected level name
	gr_set_curfont(MEDIUM1_FONT);
	gr_string(0x8000, 4 * scale, mapentries[self->index].name);

	grd_curcanv = old_canv;
	gr_free_sub_canvas(sub_canv);
}

static void do_delay(fix64* t1, fix* frame_time) 
{
	fix64 t2 = timer_query();
	const int vsync = GameCfg.VSync;
	const fix bound = F1_0 / (vsync ? MAXIMUM_FPS : GameArg.SysMaxFPS);
	const int may_sleep = GameArg.SysUseNiceFPS && !vsync;
	while (t2 - *t1 < bound)
	{
		if (may_sleep)
			timer_delay(F1_0 >> 8);
		timer_update();
		t2 = timer_query();
	}
	*frame_time = t2 - *t1;
	*t1 = t2;
}

void worldmap_setlevel(worldmap_t* self, int index)
{
	//Keep in bounds
	while (index < 0)
		index += NUM_MAP_ENTRIES;
	//clamp above
	index = index % NUM_MAP_ENTRIES;

	if (index != self->index)
	{
		self->oldindex = self->index;
		self->index = index;
		self->oldbg = self->bg;
		self->bg = mapentries[index].map;
		self->transitiontime = TRANSITION_TIME;
	}
}

int worldmap_handler(struct window* wind, d_event* event, void* data)
{
	worldmap_t* self = (worldmap_t*)data;

	switch (event->type)
	{
	case EVENT_WINDOW_DRAW:
	{
		//Do think for the frame

		//If the cursor is moving, animate it. 
		if (self->transitiontime > 0)
		{
			self->transitiontime -= f2fl(self->frame_time);
			if (self->transitiontime < 0)
				self->transitiontime = 0;
		}
		worldmap_draw(self);
		do_delay(&self->last_frame, &self->frame_time);
	}
	return 1;

	case EVENT_KEY_COMMAND: 
	{
		if (self->transitiontime > 0)
			break;

		int key = event_key_get((d_event*)event);
		if (key == KEY_ESC)
		{
			window_close(self->window);
			return 1;
		}
		else if (key == KEY_UP || key == KEY_W)
		{
			worldmap_setlevel(self, self->index - 1);
		}
		else if (key == KEY_DOWN || key == KEY_S)
		{
			worldmap_setlevel(self, self->index + 1);
		}
	}
	break;

	case EVENT_WINDOW_CLOSE:
	{
		for (int i = 0; i < NUMBGS; i++)
		{
			gr_free_bitmap_data(&self->bgpics[i]);
		}

		for (int i = 0; i < NUMICONS; i++)
		{
			gr_free_bitmap_data(&self->iconpics[i]);
		}
	}
	break;
	}

	return 0;
}

static const char* bgfilenames[NUMBGS] = { "map01.pcx", "map02.pcx", "map03.pcx" };
static const char* iconfilenames[NUMICONS] = { "mapcur.pcx", "maploc.pcx" };

void worldmap_init(worldmap_t* self, grs_canvas* canv)
{
	self->window = window_create(canv, 0, 0, SWIDTH, SHEIGHT, &worldmap_handler, self);
	self->index = self->oldindex = 0;
	self->bg = self->oldbg = 0;
	self->transitiontime = 0;

	self->last_frame = timer_query();
	self->frame_time = FrameTime;

	//Load background bitmaps
	for (int i = 0; i < NUMBGS; i++)
	{
		memset(&self->bgpics[i], 0, sizeof(grs_bitmap));
		int pcx_error = pcx_read_bitmap((char*)bgfilenames[i], &self->bgpics[i], BM_LINEAR, self->bgpals[i]);
		if (pcx_error != PCX_ERROR_NONE)
		{
			Error("worldmap_init: Failed to load background %s\n%s", bgfilenames[i], pcx_errormsg(pcx_error));
		}
	}

	//Load cursor and other bitmaps
	for (int i = 0; i < NUMICONS; i++)
	{
		memset(&self->iconpics[i], 0, sizeof(grs_bitmap));
		int pcx_error = pcx_read_bitmap((char*)iconfilenames[i], &self->iconpics[i], BM_LINEAR, self->iconpals[i]);
		if (pcx_error != PCX_ERROR_NONE)
		{
			Error("worldmap_init: Failed to load background %s\n%s", iconfilenames[i], pcx_errormsg(pcx_error));
		}

		gr_set_transparent(&self->iconpics[i], 1);
	}
}

void do_world_map()
{
	worldmap_t worldmap;
	worldmap_init(&worldmap, grd_curcanv);

	//Modal window loop for the world map.
	//The game window will be a child of this. 
	while (window_exists(worldmap.window))
	{
		event_process();
	}
}
