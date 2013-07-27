/*
 *   Creation Date: <2002/10/23 20:26:40 samuel>
 *   Time-stamp: <2004/01/07 19:39:15 samuel>
 *
 *     <video_common.c>
 *
 *     Shared video routines
 *
 *   Copyright (C) 2002, 2003, 2004 Samuel Rydh (samuel@ibrium.se)
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *
 */

#include "config.h"
#include "libopenbios/bindings.h"
#include "libopenbios/console.h"
#include "libopenbios/fontdata.h"
#include "libopenbios/ofmem.h"
#include "libopenbios/video.h"
#include "packages/video.h"
#include "drivers/vga.h"
#define NO_QEMU_PROTOS
#include "arch/common/fw_cfg.h"

struct video_info video;

unsigned long
video_get_color( int col_ind )
{
	unsigned long col;
	if( !video.has_video || col_ind < 0 || col_ind > 255 )
		return 0;
	if( VIDEO_DICT_VALUE(video.depth) == 8 )
		return col_ind;
	col = video.pal[col_ind];
	if( VIDEO_DICT_VALUE(video.depth) == 24 || VIDEO_DICT_VALUE(video.depth) == 32 )
		return col;
	if( VIDEO_DICT_VALUE(video.depth) == 15 )
		return ((col>>9) & 0x7c00) | ((col>>6) & 0x03e0) | ((col>>3) & 0x1f);
	return 0;
}

void
video_set_color( int ind, unsigned long color )
{
	xt_t hw_xt = 0;

	if( !video.has_video || ind < 0 || ind > 255 )
		return;
	video.pal[ind] = color;

	/* Call the low-level hardware setter in the
	   display package */
	hw_xt = find_ih_method("hw-set-color", VIDEO_DICT_VALUE(video.ih));
	if (hw_xt) {
		PUSH((color >> 16) & 0xff);  // Red
		PUSH((color >> 8) & 0xff);  // Green
		PUSH(color & 0xff);  // Blue
		PUSH(ind);
		PUSH(hw_xt);
		fword("execute");
	}

	/* Call the low-level palette update if required */
	hw_xt = find_ih_method("hw-refresh-palette", VIDEO_DICT_VALUE(video.ih));
	if (hw_xt) {
		PUSH(hw_xt);
		fword("execute");
	}
}

/* ( fbaddr maskaddr width height fgcolor bgcolor -- ) */

void
video_mask_blit(void)
{
	ucell bgcolor = POP();
	ucell fgcolor = POP();
	ucell height = POP();
	ucell width = POP();
	unsigned char *mask = (unsigned char *)POP();
	unsigned char *fbaddr = (unsigned char *)POP();

	ucell color;
	unsigned char *dst, *rowdst;
	int x, y, m, b, d, depthbytes;

	fgcolor = video_get_color(fgcolor);
	bgcolor = video_get_color(bgcolor);
	d = VIDEO_DICT_VALUE(video.depth);
	depthbytes = (d + 1) >> 3;

	dst = fbaddr;
	for( y = 0; y < height; y++) {
		rowdst = dst;
		for( x = 0; x < (width + 1) >> 3; x++ ) {
			for (b = 0; b < 8; b++) {
				m = (1 << (7 - b));

				if (*mask & m) {
					color = fgcolor;
				} else {
					color = bgcolor;
				}

				if( d >= 24 )
					*((uint32_t*)dst) = color;
				else if( d >= 15 )
					*((uint16_t*)dst) = color;
				else
					*dst = color;

				dst += depthbytes;
			}
			mask++;
		}
		dst = rowdst;
		dst += VIDEO_DICT_VALUE(video.rb);
	}
}

/* ( x y w h fgcolor bgcolor -- ) */

void
video_invert_rect( void )
{
	ucell bgcolor = POP();
	ucell fgcolor = POP();
	int h = POP();
	int w = POP();
	int y = POP();
	int x = POP();
	char *pp;

	bgcolor = video_get_color(bgcolor);
	fgcolor = video_get_color(fgcolor);

	if (!video.has_video || x < 0 || y < 0 || w <= 0 || h <= 0 ||
		x + w > VIDEO_DICT_VALUE(video.w) || y + h > VIDEO_DICT_VALUE(video.h))
		return;

	pp = (char*)VIDEO_DICT_VALUE(video.mvirt) + VIDEO_DICT_VALUE(video.rb) * y;
	for( ; h--; pp += *(video.rb) ) {
		int ww = w;
		if( VIDEO_DICT_VALUE(video.depth) == 24 || VIDEO_DICT_VALUE(video.depth) == 32 ) {
			uint32_t *p = (uint32_t*)pp + x;
			while( ww-- ) {
				if (*p == fgcolor) {
					*p++ = bgcolor;
				} else if (*p == bgcolor) {
					*p++ = fgcolor;
				}
			}
		} else if( VIDEO_DICT_VALUE(video.depth) == 16 || VIDEO_DICT_VALUE(video.depth) == 15 ) {
			uint16_t *p = (uint16_t*)pp + x;
			while( ww-- ) {
				if (*p == (uint16_t)fgcolor) {
					*p++ = bgcolor;
				} else if (*p == (uint16_t)bgcolor) {
					*p++ = fgcolor;
				}
			}
		} else {
			char *p = (char *)(pp + x);

			while( ww-- ) {
				if (*p == (char)fgcolor) {
					*p++ = bgcolor;
				} else if (*p == (char)bgcolor) {
					*p++ = fgcolor;
				}
			}
		}
	}
}

/* ( color_ind x y width height -- ) (?) */
void
video_fill_rect(void)
{
	int h = POP();
	int w = POP();
	int y = POP();
	int x = POP();
	int col_ind = POP();

	char *pp;
	unsigned long col = video_get_color(col_ind);

        if (!video.has_video || x < 0 || y < 0 || w <= 0 || h <= 0 ||
            x + w > VIDEO_DICT_VALUE(video.w) || y + h > VIDEO_DICT_VALUE(video.h))
		return;

	pp = (char*)VIDEO_DICT_VALUE(video.mvirt) + VIDEO_DICT_VALUE(video.rb) * y;
	for( ; h--; pp += VIDEO_DICT_VALUE(video.rb) ) {
		int ww = w;
		if( VIDEO_DICT_VALUE(video.depth) == 24 || VIDEO_DICT_VALUE(video.depth) == 32 ) {
			uint32_t *p = (uint32_t*)pp + x;
			while( ww-- )
				*p++ = col;
		} else if( VIDEO_DICT_VALUE(video.depth) == 16 || VIDEO_DICT_VALUE(video.depth) == 15 ) {
			uint16_t *p = (uint16_t*)pp + x;
			while( ww-- )
				*p++ = col;
		} else {
                        char *p = (char *)(pp + x);

			while( ww-- )
				*p++ = col;
		}
	}
}

void setup_video(phys_addr_t phys, ucell virt)
{
	/* Make everything inside the video_info structure point to the
	   values in the Forth dictionary. Hence everything is always in
	   sync. */

	video.mphys = phys;

	feval("['] display-ih cell+");
	video.ih = cell2pointer(POP());

	feval("['] qemu-video-addr cell+");
	video.mvirt = cell2pointer(POP());
	feval("['] qemu-video-width cell+");
	video.w = cell2pointer(POP());
	feval("['] qemu-video-height cell+");
	video.h = cell2pointer(POP());
	feval("['] depth-bits cell+");
	video.depth = cell2pointer(POP());
	feval("['] line-bytes cell+");
	video.rb = cell2pointer(POP());
	feval("['] color-palette cell+");
	video.pal = cell2pointer(POP());

	/* Set global variables ready for fb8-install */
	PUSH( pointer2cell(video_mask_blit) );
	fword("is-noname-cfunc");
	feval("to fb8-blitmask");
	PUSH( pointer2cell(video_fill_rect) );
	fword("is-noname-cfunc");
	feval("to fb8-fillrect");
	PUSH( pointer2cell(video_invert_rect) );
	fword("is-noname-cfunc");
	feval("to fb8-invertrect");

	/* Static information */
	PUSH((ucell)fontdata);
	feval("to (romfont)");
	PUSH(FONT_HEIGHT);
	feval("to (romfont-height)");
	PUSH(FONT_WIDTH);
	feval("to (romfont-width)");

	/* Initialise the structure */
	VIDEO_DICT_VALUE(video.mvirt) = virt;
	VIDEO_DICT_VALUE(video.w) = VGA_DEFAULT_WIDTH;
	VIDEO_DICT_VALUE(video.h) = VGA_DEFAULT_HEIGHT;
	VIDEO_DICT_VALUE(video.depth) = VGA_DEFAULT_DEPTH;
	VIDEO_DICT_VALUE(video.rb) = VGA_DEFAULT_LINEBYTES;

#if defined(CONFIG_QEMU) && (defined(CONFIG_PPC) || defined(CONFIG_SPARC32) || defined(CONFIG_SPARC64))
	/* If running from QEMU, grab the parameters from the firmware interface */
	int w, h, d;

	w = fw_cfg_read_i16(FW_CFG_ARCH_WIDTH);
        h = fw_cfg_read_i16(FW_CFG_ARCH_HEIGHT);
        d = fw_cfg_read_i16(FW_CFG_ARCH_DEPTH);
	if (w && h && d) {
		VIDEO_DICT_VALUE(video.w) = w;
		VIDEO_DICT_VALUE(video.h) = h;
		VIDEO_DICT_VALUE(video.depth) = d;
		VIDEO_DICT_VALUE(video.rb) = (w * ((d + 7) / 8));
	}
#endif
}

void
init_video(void)
{
#if defined(CONFIG_OFMEM) && defined(CONFIG_DRIVER_PCI)
        int size;
#endif
	phandle_t ph=0, saved_ph=0;

	saved_ph = get_cur_dev();
	while( (ph=dt_iterate_type(ph, "display")) ) {
		video.has_video = 1;

		set_int_property( ph, "width", VIDEO_DICT_VALUE(video.w) );
		set_int_property( ph, "height", VIDEO_DICT_VALUE(video.h) );
		set_int_property( ph, "depth", VIDEO_DICT_VALUE(video.depth) );
		set_int_property( ph, "linebytes", VIDEO_DICT_VALUE(video.rb) );
		set_int_property( ph, "address", VIDEO_DICT_VALUE(video.mvirt) );

		activate_dev(ph);

		molvideo_init();
	}
	activate_dev(saved_ph);

#if defined(CONFIG_OFMEM) && defined(CONFIG_DRIVER_PCI)
        size = ((VIDEO_DICT_VALUE(video.h) * VIDEO_DICT_VALUE(video.rb))  + 0xfff) & ~0xfff;

	ofmem_claim_phys( video.mphys, size, 0 );
	ofmem_claim_virt( VIDEO_DICT_VALUE(video.mvirt), size, 0 );
	ofmem_map( video.mphys, VIDEO_DICT_VALUE(video.mvirt), size, ofmem_arch_io_translation_mode(video.mphys) );
#endif
}
