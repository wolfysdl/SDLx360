/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002  Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Sam Lantinga
    slouken@libsdl.org
*/

#ifdef SAVE_RCSID
static char rcsid =
 "@(#) $Id: SDL_yuvfuncs.h,v 1.1 2003/07/18 15:19:33 lantus Exp $";
#endif

/* This is the definition of the YUV video surface function structure */

#include "SDL_video.h"
#include "SDL_sysvideo.h"

#ifndef _THIS
#define _THIS	SDL_VideoDevice *_this
#endif
struct private_yuvhwfuncs {
	int (*Lock)(_THIS, SDL_Overlay *overlay);
	void (*Unlock)(_THIS, SDL_Overlay *overlay);
	int (*Display)(_THIS, SDL_Overlay *overlay, SDL_Rect *src, SDL_Rect *dst);
	void (*FreeHW)(_THIS, SDL_Overlay *overlay);
};
