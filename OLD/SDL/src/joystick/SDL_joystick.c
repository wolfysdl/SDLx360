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
 "@(#) $Id: SDL_joystick.c,v 1.1 2003/07/18 15:19:33 lantus Exp $";
#endif

/* This is the joystick API for Simple DirectMedia Layer */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL_error.h"
#include "SDL_events.h"
#ifndef DISABLE_EVENTS
#include "SDL_events_c.h"
#endif
#include "SDL_joystick_c.h"
#include "SDL_sysjoystick.h"

/* This is used for Quake III Arena */
#ifdef DISABLE_EVENTS
#define SDL_Lock_EventThread()
#define SDL_Unlock_EventThread()
#endif

Uint8 SDL_numjoysticks = 0;
SDL_Joystick **SDL_joysticks = NULL;
static SDL_Joystick *default_joystick = NULL;

int SDL_JoystickInit(void)
{
	int arraylen;
	int status;

	SDL_numjoysticks = 0;
	status = SDL_SYS_JoystickInit();
	if ( status >= 0 ) {
		arraylen = (status+1)*sizeof(*SDL_joysticks);
		SDL_joysticks = (SDL_Joystick **)malloc(arraylen);
		if ( SDL_joysticks == NULL ) {
			SDL_numjoysticks = 0;
		} else {
			memset(SDL_joysticks, 0, arraylen);
		}
		SDL_numjoysticks = status;
		status = 0;
	}
	default_joystick = NULL;
	return(status);
}

/*
 * Count the number of joysticks attached to the system
 */
int SDL_NumJoysticks(void)
{
	return SDL_numjoysticks;
}

/*
 * Get the implementation dependent name of a joystick
 */
const char *SDL_JoystickName(int device_index)
{
	if ( (device_index < 0) || (device_index >= SDL_numjoysticks) ) {
		SDL_SetError("There are %d joysticks available",
		             SDL_numjoysticks);
		return(NULL);
	}
	return(SDL_SYS_JoystickName(device_index));
}

/*
 * Open a joystick for use - the index passed as an argument refers to
 * the N'th joystick on the system.  This index is the value which will
 * identify this joystick in future joystick events.
 *
 * This function returns a joystick identifier, or NULL if an error occurred.
 */
SDL_Joystick *SDL_JoystickOpen(int device_index)
{
	int i;
	SDL_Joystick *joystick;

	if ( (device_index < 0) || (device_index >= SDL_numjoysticks) ) {
		SDL_SetError("There are %d joysticks available",
		             SDL_numjoysticks);
		return(NULL);
	}

	/* If the joystick is already open, return it */
	for ( i=0; SDL_joysticks[i]; ++i ) {
		if ( device_index == SDL_joysticks[i]->index ) {
			joystick = SDL_joysticks[i];
			++joystick->ref_count;
			return(joystick);
		}
	}

	/* Create and initialize the joystick */
	joystick = (SDL_Joystick *)malloc((sizeof *joystick));
	if ( joystick != NULL ) {
		memset(joystick, 0, (sizeof *joystick));
		joystick->index = device_index;
		if ( SDL_SYS_JoystickOpen(joystick) < 0 ) {
			free(joystick);
			joystick = NULL;
		} else {
			if ( joystick->naxes > 0 ) {
				joystick->axes = (Sint16 *)malloc
					(joystick->naxes*sizeof(Sint16));
			}
			if ( joystick->nhats > 0 ) {
				joystick->hats = (Uint8 *)malloc
					(joystick->nhats*sizeof(Uint8));
			}
			if ( joystick->nballs > 0 ) {
				joystick->balls = (struct balldelta *)malloc
				  (joystick->nballs*sizeof(*joystick->balls));
			}
			if ( joystick->nbuttons > 0 ) {
				joystick->buttons = (Uint8 *)malloc
					(joystick->nbuttons*sizeof(Uint8));
			}
			if ( ((joystick->naxes > 0) && !joystick->axes)
			  || ((joystick->nhats > 0) && !joystick->hats)
			  || ((joystick->nballs > 0) && !joystick->balls)
			  || ((joystick->nbuttons > 0) && !joystick->buttons)) {
				SDL_OutOfMemory();
				SDL_JoystickClose(joystick);
				joystick = NULL;
			}
			if ( joystick->axes ) {
				memset(joystick->axes, 0,
					joystick->naxes*sizeof(Sint16));
			}
			if ( joystick->hats ) {
				memset(joystick->hats, 0,
					joystick->nhats*sizeof(Uint8));
			}
			if ( joystick->balls ) {
				memset(joystick->balls, 0,
				  joystick->nballs*sizeof(*joystick->balls));
			}
			if ( joystick->buttons ) {
				memset(joystick->buttons, 0,
					joystick->nbuttons*sizeof(Uint8));
			}
		}
	}
	if ( joystick ) {
		/* Add joystick to list */
		++joystick->ref_count;
		SDL_Lock_EventThread();
		for ( i=0; SDL_joysticks[i]; ++i )
			/* Skip to next joystick */;
		SDL_joysticks[i] = joystick;
		SDL_Unlock_EventThread();
	}
	return(joystick);
}

/*
 * Returns 1 if the joystick has been opened, or 0 if it has not.
 */
int SDL_JoystickOpened(int device_index)
{
	int i, opened;

	opened = 0;
	for ( i=0; SDL_joysticks[i]; ++i ) {
		if ( SDL_joysticks[i]->index == (Uint8)device_index ) {
			opened = 1;
			break;
		}
	}
	return(opened);
}

static int ValidJoystick(SDL_Joystick **joystick)
{
	int valid;

	if ( *joystick == NULL ) {
		*joystick = default_joystick;
	}
	if ( *joystick == NULL ) {
		SDL_SetError("Joystick hasn't been opened yet");
		valid = 0;
	} else {
		valid = 1;
	}
	return valid;
}

/*
 * Get the device index of an opened joystick.
 */
int SDL_JoystickIndex(SDL_Joystick *joystick)
{
	if ( ! ValidJoystick(&joystick) ) {
		return(-1);
	}
	return(joystick->index);
}

/*
 * Get the number of multi-dimensional axis controls on a joystick
 */
int SDL_JoystickNumAxes(SDL_Joystick *joystick)
{
	if ( ! ValidJoystick(&joystick) ) {
		return(-1);
	}
	return(joystick->naxes);
}

/*
 * Get the number of hats on a joystick
 */
int SDL_JoystickNumHats(SDL_Joystick *joystick)
{
	if ( ! ValidJoystick(&joystick) ) {
		return(-1);
	}
	return(joystick->nhats);
}

/*
 * Get the number of trackballs on a joystick
 */
int SDL_JoystickNumBalls(SDL_Joystick *joystick)
{
	if ( ! ValidJoystick(&joystick) ) {
		return(-1);
	}
	return(joystick->nballs);
}

/*
 * Get the number of buttons on a joystick
 */
int SDL_JoystickNumButtons(SDL_Joystick *joystick)
{
	if ( ! ValidJoystick(&joystick) ) {
		return(-1);
	}
	return(joystick->nbuttons);
}

/*
 * Get the current state of an axis control on a joystick
 */
Sint16 SDL_JoystickGetAxis(SDL_Joystick *joystick, int axis)
{
	Sint16 state;

	if ( ! ValidJoystick(&joystick) ) {
		return(0);
	}
	if ( axis < joystick->naxes ) {
		state = joystick->axes[axis];
	} else {
		SDL_SetError("Joystick only has %d axes", joystick->naxes);
		state = 0;
	}
	return(state);
}

/*
 * Get the current state of a hat on a joystick
 */
Uint8 SDL_JoystickGetHat(SDL_Joystick *joystick, int hat)
{
	Uint8 state;

	if ( ! ValidJoystick(&joystick) ) {
		return(0);
	}
	if ( hat < joystick->nhats ) {
		state = joystick->hats[hat];
	} else {
		SDL_SetError("Joystick only has %d hats", joystick->nhats);
		state = 0;
	}
	return(state);
}

/*
 * Get the ball axis change since the last poll
 */
int SDL_JoystickGetBall(SDL_Joystick *joystick, int ball, int *dx, int *dy)
{
	int retval;

	if ( ! ValidJoystick(&joystick) ) {
		return(-1);
	}

	retval = 0;
	if ( ball < joystick->nballs ) {
		if ( dx ) {
			*dx = joystick->balls[ball].dx;
		}
		if ( dy ) {
			*dy = joystick->balls[ball].dy;
		}
		joystick->balls[ball].dx = 0;
		joystick->balls[ball].dy = 0;
	} else {
		SDL_SetError("Joystick only has %d balls", joystick->nballs);
		retval = -1;
	}
	return(retval);
}

/*
 * Get the current state of a button on a joystick
 */
Uint8 SDL_JoystickGetButton(SDL_Joystick *joystick, int button)
{
	Uint8 state;

	if ( ! ValidJoystick(&joystick) ) {
		return(0);
	}
	if ( button < joystick->nbuttons ) {
		state = joystick->buttons[button];
	} else {
		SDL_SetError("Joystick only has %d buttons",joystick->nbuttons);
		state = 0;
	}
	return(state);
}

/*
 * Close a joystick previously opened with SDL_JoystickOpen()
 */
void SDL_JoystickClose(SDL_Joystick *joystick)
{
	int i;

	if ( ! ValidJoystick(&joystick) ) {
		return;
	}

	/* First decrement ref count */
	if ( --joystick->ref_count > 0 ) {
		return;
	}

	/* Lock the event queue - prevent joystick polling */
	SDL_Lock_EventThread();

	if ( joystick == default_joystick ) {
		default_joystick = NULL;
	}
	SDL_SYS_JoystickClose(joystick);

	/* Remove joystick from list */
	for ( i=0; SDL_joysticks[i]; ++i ) {
		if ( joystick == SDL_joysticks[i] ) {
			memcpy(&SDL_joysticks[i], &SDL_joysticks[i+1],
			       (SDL_numjoysticks-i)*sizeof(joystick));
			break;
		}
	}

	/* Let the event thread keep running */
	SDL_Unlock_EventThread();

	/* Free the data associated with this joystick */
	if ( joystick->axes ) {
		free(joystick->axes);
	}
	if ( joystick->hats ) {
		free(joystick->hats);
	}
	if ( joystick->balls ) {
		free(joystick->balls);
	}
	if ( joystick->buttons ) {
		free(joystick->buttons);
	}
	free(joystick);
}

void SDL_JoystickQuit(void)
{
	/* Stop the event polling */
	SDL_Lock_EventThread();
	SDL_numjoysticks = 0;
	SDL_Unlock_EventThread();

	/* Quit the joystick setup */
	SDL_SYS_JoystickQuit();
	if ( SDL_joysticks ) {
		free(SDL_joysticks);
		SDL_joysticks = NULL;
	}
}


/* These are global for SDL_sysjoystick.c and SDL_events.c */

int SDL_PrivateJoystickAxis(SDL_Joystick *joystick, Uint8 axis, Sint16 value)
{
	int posted;

	/* Update internal joystick state */
	joystick->axes[axis] = value;

	/* Post the event, if desired */
	posted = 0;
#ifndef DISABLE_EVENTS
	if ( SDL_ProcessEvents[SDL_JOYAXISMOTION] == SDL_ENABLE ) {
		SDL_Event event;
		event.type = SDL_JOYAXISMOTION;
		event.jaxis.which = joystick->index;
		event.jaxis.axis = axis;
		event.jaxis.value = value;
		if ( (SDL_EventOK == NULL) || (*SDL_EventOK)(&event) ) {
			posted = 1;
			SDL_PushEvent(&event);
		}
	}
#endif /* !DISABLE_EVENTS */
	return(posted);
}

int SDL_PrivateJoystickHat(SDL_Joystick *joystick, Uint8 hat, Uint8 value)
{
	int posted;

	/* Update internal joystick state */
	joystick->hats[hat] = value;

	/* Post the event, if desired */
	posted = 0;
#ifndef DISABLE_EVENTS
	if ( SDL_ProcessEvents[SDL_JOYHATMOTION] == SDL_ENABLE ) {
		SDL_Event event;
		event.jhat.type = SDL_JOYHATMOTION;
		event.jhat.which = joystick->index;
		event.jhat.hat = hat;
		event.jhat.value = value;
		if ( (SDL_EventOK == NULL) || (*SDL_EventOK)(&event) ) {
			posted = 1;
			SDL_PushEvent(&event);
		}
	}
#endif /* !DISABLE_EVENTS */
	return(posted);
}

int SDL_PrivateJoystickBall(SDL_Joystick *joystick, Uint8 ball,
					Sint16 xrel, Sint16 yrel)
{
	int posted;

	/* Update internal mouse state */
	joystick->balls[ball].dx += xrel;
	joystick->balls[ball].dy += yrel;

	/* Post the event, if desired */
	posted = 0;
#ifndef DISABLE_EVENTS
	if ( SDL_ProcessEvents[SDL_JOYBALLMOTION] == SDL_ENABLE ) {
		SDL_Event event;
		event.jball.type = SDL_JOYBALLMOTION;
		event.jball.which = joystick->index;
		event.jball.ball = ball;
		event.jball.xrel = xrel;
		event.jball.yrel = yrel;
		if ( (SDL_EventOK == NULL) || (*SDL_EventOK)(&event) ) {
			posted = 1;
			SDL_PushEvent(&event);
		}
	}
#endif /* !DISABLE_EVENTS */
	return(posted);
}

int SDL_PrivateJoystickButton(SDL_Joystick *joystick, Uint8 button, Uint8 state)
{
	int posted;
#ifndef DISABLE_EVENTS
	SDL_Event event;

	switch ( state ) {
		case SDL_PRESSED:
			event.type = SDL_JOYBUTTONDOWN;
			break;
		case SDL_RELEASED:
			event.type = SDL_JOYBUTTONUP;
			break;
		default:
			/* Invalid state -- bail */
			return(0);
	}
#endif /* !DISABLE_EVENTS */

	/* Update internal joystick state */
	joystick->buttons[button] = state;

	/* Post the event, if desired */
	posted = 0;
#ifndef DISABLE_EVENTS
	if ( SDL_ProcessEvents[event.type] == SDL_ENABLE ) {
		event.jbutton.which = joystick->index;
		event.jbutton.button = button;
		event.jbutton.state = state;
		if ( (SDL_EventOK == NULL) || (*SDL_EventOK)(&event) ) {
			posted = 1;
			SDL_PushEvent(&event);
		}
	}
#endif /* !DISABLE_EVENTS */
	return(posted);
}

void SDL_JoystickUpdate(void)
{
	int i;

	for ( i=0; SDL_joysticks[i]; ++i ) {
		SDL_SYS_JoystickUpdate(SDL_joysticks[i]);
	}
}

int SDL_JoystickEventState(int state)
{
#ifdef DISABLE_EVENTS
	return SDL_IGNORE;
#else
	const Uint8 event_list[] = {
		SDL_JOYAXISMOTION, SDL_JOYBALLMOTION, SDL_JOYHATMOTION,
		SDL_JOYBUTTONDOWN, SDL_JOYBUTTONUP,
	};
	int i;

	switch (state) {
		case SDL_QUERY:
			state = SDL_IGNORE;
			for ( i=0; i<SDL_TABLESIZE(event_list); ++i ) {
				state = SDL_EventState(event_list[i],SDL_QUERY);
				if ( state == SDL_ENABLE ) {
					break;
				}
			}
			break;
		default:
			for ( i=0; i<SDL_TABLESIZE(event_list); ++i ) {
				SDL_EventState(event_list[i], state);
			}
			break;
	}
	return(state);
#endif /* DISABLE_EVENTS */
}
