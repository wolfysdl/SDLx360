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
 "@(#) $Id: SDL_thread.c,v 1.1 2003/07/18 15:19:33 lantus Exp $";
#endif

/* System independent thread management routines for SDL */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL_error.h"
#include "SDL_mutex.h"
#include "SDL_thread.h"
#include "SDL_thread_c.h"
#include "SDL_systhread.h"

#define ARRAY_CHUNKSIZE	32
/* The array of threads currently active in the application
   (except the main thread)
   The manipulation of an array here is safer than using a linked list.
*/
static int SDL_maxthreads = 0;
static int SDL_numthreads = 0;
static SDL_Thread **SDL_Threads = NULL;
static SDL_mutex *thread_lock = NULL;
int _creating_thread_lock = 0;

int SDL_ThreadsInit(void)
{
	int retval;

	retval = 0;
	/* Set the thread lock creation flag so that we can reuse an
	   existing lock on the system - since this mutex never gets
	   destroyed (see SDL_ThreadsQuit()), we want to reuse it.
	*/
	_creating_thread_lock = 1;
	thread_lock = SDL_CreateMutex();
	_creating_thread_lock = 0;
	if ( thread_lock == NULL ) {
		retval = -1;
	}
	return(retval);
}

/* This should never be called...
   If this is called by SDL_Quit(), we don't know whether or not we should
   clean up threads here.  If any threads are still running after this call,
   they will no longer have access to any per-thread data.
 */
void SDL_ThreadsQuit()
{
	SDL_mutex *mutex;

	mutex = thread_lock;
	thread_lock = NULL;
	if ( mutex != NULL ) {
		SDL_DestroyMutex(mutex);
	}
}

/* Routines for manipulating the thread list */
static void SDL_AddThread(SDL_Thread *thread)
{
	SDL_Thread **threads;

	/* WARNING:
	   If the very first threads are created simultaneously, then
	   there could be a race condition causing memory corruption.
	   In practice, this isn't a problem because by definition there
	   is only one thread running the first time this is called.
	*/
	if ( thread_lock == NULL ) {
		if ( SDL_ThreadsInit() < 0 ) {
			return;
		}
	}
	SDL_mutexP(thread_lock);

	/* Expand the list of threads, if necessary */
#ifdef DEBUG_THREADS
	printf("Adding thread (%d already - %d max)\n",
			SDL_numthreads, SDL_maxthreads);
#endif
	if ( SDL_numthreads == SDL_maxthreads ) {
		threads=(SDL_Thread **)malloc((SDL_maxthreads+ARRAY_CHUNKSIZE)*
		                              (sizeof *threads));
		if ( threads == NULL ) {
			SDL_OutOfMemory();
			goto done;
		}
		memcpy(threads, SDL_Threads, SDL_numthreads*(sizeof *threads));
		SDL_maxthreads += ARRAY_CHUNKSIZE;
		if ( SDL_Threads ) {
			free(SDL_Threads);
		}
		SDL_Threads = threads;
	}
	SDL_Threads[SDL_numthreads++] = thread;
done:
	SDL_mutexV(thread_lock);
}

static void SDL_DelThread(SDL_Thread *thread)
{
	int i;

	if ( thread_lock ) {
		SDL_mutexP(thread_lock);
		for ( i=0; i<SDL_numthreads; ++i ) {
			if ( thread == SDL_Threads[i] ) {
				break;
			}
		}
		if ( i < SDL_numthreads ) {
			if ( --SDL_numthreads > 0 ) {
				while ( i < SDL_numthreads ) {
					SDL_Threads[i] = SDL_Threads[i+1];
					++i;
				}
			} else {
				SDL_maxthreads = 0;
				free(SDL_Threads);
				SDL_Threads = NULL;
			}
#ifdef DEBUG_THREADS
			printf("Deleting thread (%d left - %d max)\n",
					SDL_numthreads, SDL_maxthreads);
#endif
		}
		SDL_mutexV(thread_lock);
	}
}

/* The default (non-thread-safe) global error variable */
static SDL_error SDL_global_error;

/* Routine to get the thread-specific error variable */
SDL_error *SDL_GetErrBuf(void)
{
	SDL_error *errbuf;

	errbuf = &SDL_global_error;
	if ( SDL_Threads ) {
		int i;
		Uint32 this_thread;

		this_thread = SDL_ThreadID();
		SDL_mutexP(thread_lock);
		for ( i=0; i<SDL_numthreads; ++i ) {
			if ( this_thread == SDL_Threads[i]->threadid ) {
				errbuf = &SDL_Threads[i]->errbuf;
				break;
			}
		}
		SDL_mutexV(thread_lock);
	}
	return(errbuf);
}


/* Arguments and callback to setup and run the user thread function */
typedef struct {
	int (*func)(void *);
	void *data;
	SDL_Thread *info;
	SDL_sem *wait;
} thread_args;

void SDL_RunThread(void *data)
{
	thread_args *args;
	int (*userfunc)(void *);
	void *userdata;
	int *statusloc;

	/* Perform any system-dependent setup
	   - this function cannot fail, and cannot use SDL_SetError()
	 */
	SDL_SYS_SetupThread();

	/* Get the thread id */
	args = (thread_args *)data;
	args->info->threadid = SDL_ThreadID();

	/* Figure out what function to run */
	userfunc = args->func;
	userdata = args->data;
	statusloc = &args->info->status;

	/* Wake up the parent thread */
	SDL_SemPost(args->wait);

	/* Run the function */
	*statusloc = userfunc(userdata);
}

SDL_Thread *SDL_CreateThread(int (*fn)(void *), void *data)
{
	SDL_Thread *thread;
	thread_args *args;
	int ret;

	/* Allocate memory for the thread info structure */
	thread = (SDL_Thread *)malloc(sizeof(*thread));
	if ( thread == NULL ) {
		SDL_OutOfMemory();
		return(NULL);
	}
	memset(thread, 0, (sizeof *thread));
	thread->status = -1;

	/* Set up the arguments for the thread */
	args = (thread_args *)malloc(sizeof(*args));
	if ( args == NULL ) {
		SDL_OutOfMemory();
		free(thread);
		return(NULL);
	}
	args->func = fn;
	args->data = data;
	args->info = thread;
	args->wait = SDL_CreateSemaphore(0);
	if ( args->wait == NULL ) {
		free(thread);
		free(args);
		return(NULL);
	}

	/* Add the thread to the list of available threads */
	SDL_AddThread(thread);

	/* Create the thread and go! */
	ret = SDL_SYS_CreateThread(thread, args);
	if ( ret >= 0 ) {
		/* Wait for the thread function to use arguments */
		SDL_SemWait(args->wait);
	} else {
		/* Oops, failed.  Gotta free everything */
		SDL_DelThread(thread);
		free(thread);
		thread = NULL;
	}
	SDL_DestroySemaphore(args->wait);
	free(args);

	/* Everything is running now */
	return(thread);
}

void SDL_WaitThread(SDL_Thread *thread, int *status)
{
	if ( thread ) {
		SDL_SYS_WaitThread(thread);
		if ( status ) {
			*status = thread->status;
		}
		SDL_DelThread(thread);
		free(thread);
	}
}

Uint32 SDL_GetThreadID(SDL_Thread *thread)
{
	Uint32 id;

	if ( thread ) {
		id = thread->threadid;
	} else {
		id = SDL_ThreadID();
	}
	return(id);
}

void SDL_KillThread(SDL_Thread *thread)
{
	if ( thread ) {
		SDL_SYS_KillThread(thread);
		SDL_WaitThread(thread, NULL);
	}
}


