#include <debug.h>
#include <err.h>
#include <stdlib.h>
#include <dev/fbcon.h>
#include <string.h>
#include "font8x16.h"
#include <splash.h>

/* Cartesian coordinate system */
struct pos {
	int x;
	int y;
};

/* fbconfig, retrieved using fbcon_display() in device instants */
static struct fbcon_config *config = NULL;

#define RGB565_RED		    0xf800
#define RGB565_GREEN		0x07e0
#define RGB565_BLUE		    0x001f
#define RGB565_YELLOW		0xffe0
#define RGB565_CYAN		    0x07ff
#define RGB565_MAGENTA		0xf81f
#define RGB565_WHITE		0xffff
#define RGB565_BLACK		0x0000
#define RGB565_lboot		0x02E0


#define RGB888_BLACK            0x000000
#define RGB888_WHITE            0xffffff


#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 800

static uint16_t			BGCOLOR;
static uint16_t			FGCOLOR;
static uint16_t			TGCOLOR;


static uint16_t         F1COLOR;
static uint16_t         T1COLOR;

static struct pos		cur_pos;
static struct pos		max_pos;


static bool			scrolled;
static bool			forcedtg;

uint16_t *pixor;

static unsigned reverse_fnt_byte (unsigned x)
	{
		unsigned y = 0;
		for (uint8_t i = 0; i < 9; ++i)
		{
			y <<= 1;
			y |= (x & 1);
			x >>= 1;
		}
		return y;
	}
	
static void fbcon_drawglyph_helper (uint16_t *pixels, unsigned stride, unsigned data, bool dtg)
	{
		for (unsigned y = 0; y < (FONT_HEIGHT / FONT_PPCHAR); y++)
		{
			data = reverse_fnt_byte(data);
			for (unsigned x = 0; x < FONT_WIDTH; x++)
			{
				if (data & 1) *pixor = FGCOLOR;
				else if(dtg) *pixor = TGCOLOR;
				data >>= 1;
				pixor++;
			}
			pixor += stride;
		}
		return;
	}
void fbcon_forcetg(bool flag_boolean)
{
	forcedtg = flag_boolean;
}

static void ijustscrolled(void){
	scrolled = true;
}
static void cleanedyourcrap(void){
	scrolled = false;
}
bool didyouscroll(void){
	return scrolled;
}
void fill_screen(uint16_t COLOR)
{
	memset(config->base, COLOR, (((config->width) * (config->height)) * (config->bpp /8)));
	return;
}
void fbcon_clear_region(int start_y, int end_y){

    unsigned area_size = (((end_y - start_y) * FONT_HEIGHT) * config->width) * ((config->bpp) / 8);
    unsigned start_offset = ((start_y * FONT_HEIGHT) * config->width) * ((config->bpp) / 8);

    memset(config->base + start_offset, BGCOLOR, area_size);
}

int fbcon_get_y_cord(void){
    return cur_pos.y;
}
void fbcon_set_y_cord(int offset){
    cur_pos.y = offset;
}
int fbcon_get_x_cord(void){
    return cur_pos.x;
}
void fbcon_set_x_cord(int offset){
    cur_pos.x = offset;
}

static void fbcon_drawglyph (uint16_t *pixels, unsigned stride,
			    unsigned *glyph)
{
	stride -=FONT_WIDTH;
	bool dtg=false;
	if((BGCOLOR!=TGCOLOR)||(forcedtg)){dtg=true;}

	pixor = pixels;
	for(unsigned i = 0; i < FONT_PPCHAR; i++)
	{
		fbcon_drawglyph_helper(pixor, stride, glyph[i], dtg);
	}
}
void fbcon_flush(void)
{
	/* Send update command and hold pointer till really done */
	if (config->update_start)
		config->update_start();
	if (config->update_done)
		while (!config->update_done());
}
void fbcon_push(void)
{
	/* Send update command and return immediately */
	if (config->update_start)
		config->update_start();
	return (void)config->update_done;
}
/* TODO: Do not scroll the Androids */
static void fbcon_scroll_up(void)
{
	ijustscrolled();
	unsigned buffer_size = (config->width * (config->height - SPLASH_IMAGE_HEIGHT)) * (config->bpp /8);
	unsigned line_size = (config->width * FONT_HEIGHT) * (config->bpp / 8);
	// copy framebuffer from base + 1 line
	memmove(config->base, config->base + line_size, buffer_size-line_size);
	// clear last framebuffer line
	memset(config->base+buffer_size-line_size,BGCOLOR,line_size);
	// Flush holds the control till the Display is REALLY updated, now we update the display data and move on instead of blocking the pointer there and save some time as this function is HIGHLY time critical.
	fbcon_push();
}
/* TODO: take stride into account */
void fbcon_clear(void)
 {
 	/* Clear the console, only till the part where logo is displayed */
    unsigned image_base = (((((config->height) - (SPLASH_IMAGE_HEIGHT ))) *
			    (config->width)) + (config->width/2 - (SPLASH_IMAGE_WIDTH / 2)));
	/* Set the LCD to BGCOLOR till the image base */
	memset(config->base, BGCOLOR, image_base * ((config->bpp) / 8));
	/* memset ( TARGET, DATA, Sizeof ); */
 }
static void fbcon_set_colors(unsigned bg, unsigned fg)
{
	BGCOLOR = bg;
	F1COLOR = FGCOLOR;
	FGCOLOR = fg;
}
void fbcon_setfg(unsigned fg)
{
	F1COLOR = FGCOLOR;
	FGCOLOR = fg;
}
void fbcon_setbg(unsigned bg)
{
	BGCOLOR = bg;
	fbcon_disp_logo();
}
void fbcon_settg(unsigned tg)
{
	T1COLOR = TGCOLOR;
	TGCOLOR = tg;
}
void fbcon_set_txt_colors(unsigned fgcolor, unsigned tgcolor){
	F1COLOR = FGCOLOR;
	T1COLOR = TGCOLOR;
	
	FGCOLOR = fgcolor;
	TGCOLOR = tgcolor;
}
uint16_t fbcon_get_bgcol(void)
{
	return BGCOLOR;
}
void fbcon_reset_colors_rgb555(void)
{
	uint16_t bg;
	uint16_t fg;
	fg = RGB565_BLACK;
	bg = RGB565_WHITE;
	fbcon_set_colors(bg, fg);
	T1COLOR = (TGCOLOR = bg);
	F1COLOR = fg;
}
void fbcon_putc(char c)
{
	uint16_t *pixels;

	if (!config)
		return;

	if((unsigned char)c > 127)
		return;
	if((unsigned char)c < 32) {
		if(c == '\n')
			goto newline;
		else if (c == '\r')
			cur_pos.x = 0;
		return;
	}

	pixels = config->base;
	pixels += cur_pos.y * FONT_HEIGHT * config->width;
	pixels += cur_pos.x * (FONT_WIDTH);
	
	fbcon_drawglyph(pixels, config->stride, font8x16 + (c - 32) * FONT_PPCHAR);

	cur_pos.x++;
	if (cur_pos.x < max_pos.x)
		return;

newline:
	cur_pos.y++;
	cur_pos.x = 0;
	if(cur_pos.y >= max_pos.y) {
		cur_pos.y = max_pos.y - 1;
		fbcon_scroll_up();
	} else
		fbcon_flush();
}
void fbcon_setup(struct fbcon_config *_config)
{
	uint32_t bg;
	uint32_t fg;

	ASSERT(_config);

	config = _config;

	switch (config->format) {
	case FB_FORMAT_RGB565:
		fg = RGB565_BLACK;
		bg = RGB565_WHITE;
		break;
    case FB_FORMAT_RGB888:
         fg = RGB888_WHITE;
         bg = RGB888_BLACK;
         break;
	default:
		dprintf(CRITICAL, "unknown framebuffer pixel format\n");
		ASSERT(0);
		break;
	}
	T1COLOR = (TGCOLOR = bg);
	F1COLOR = fg;
	//SGCOLOR = 0x001f;
	fbcon_set_colors(bg, fg);
	cur_pos.x = 0;
	cur_pos.y = 0;
	max_pos.x = config->width / (FONT_WIDTH);
	max_pos.y = (config->height - SPLASH_IMAGE_HEIGHT) / FONT_HEIGHT;
	cleanedyourcrap();
	fbcon_disp_logo();
#if !DISPLAY_SPLASH_SCREEN
	fbcon_clear();
#endif
}

struct fbcon_config* fbcon_display(void)
{
    return config;
}

void fbcon_resetdisp(void){
	fbcon_clear();
	cur_pos.x = 0;
	cur_pos.y = 0;
	cleanedyourcrap();
}
void fbcon_disp_logo(void)
{
    unsigned i = 0;
    unsigned bytes_per_bpp = ((config->bpp) / 8);
    unsigned image_base = ((((config->height)-SPLASH_IMAGE_HEIGHT) * (config->width)) + ((config->width/2)-(SPLASH_IMAGE_WIDTH/2)));
	
	//Set the LCD to BGCOLOR from the image base to image size
	memset(config->base + ((((config->height)-(SPLASH_IMAGE_HEIGHT))*(config->width))*bytes_per_bpp), BGCOLOR, (((SPLASH_IMAGE_HEIGHT) * config->width)*bytes_per_bpp));

    if (bytes_per_bpp == 3)
    {
        for (i = 0; i < SPLASH_IMAGE_HEIGHT; i++)
        {
            memcpy (config->base + ((image_base + (i * (config->width))) * bytes_per_bpp),
		    imageBuffer_rgb888 + (i * SPLASH_IMAGE_WIDTH * bytes_per_bpp),
		    SPLASH_IMAGE_WIDTH * bytes_per_bpp);
		}
    } 
	if (bytes_per_bpp == 2)
	{
		for (i = 0; i < SPLASH_IMAGE_HEIGHT; i++)
		{
			memcpy (config->base + ((image_base + (i * (config->width))) * 2),
			imageBuffer + (i * SPLASH_IMAGE_WIDTH * 2),
			SPLASH_IMAGE_WIDTH * 2);
		}
	}
}
