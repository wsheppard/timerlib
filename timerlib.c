
#include <sys/timerfd.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include "timerlib.h"

#define log_dev(...) printf(__VA_ARGS__)
#define log_error(...) printf(__VA_ARGS__)

struct timerlib_args
{
	int secs;
	timerlib_cb *cb;
	void *cookie;
}; 

#define timerlib_defaults .secs = 0, cb = NULL, cookie = NULL 

struct timerlib_ctx 
{
	int fd;
	int quit;
	int active;
	pthread_t thread;
	pthread_mutex_t mut;
	pthread_cond_t cond;
	timerlib_cb *cb;
	int secs;
	void *cookie;
	struct timerlib_args args;
};

#define timerlib_create(...) timerlib_create_args( (struct timerlib_args) {timerlib_defaults, __VA_ARGS__ } )

#if 0

// The following is kept here to show example usage.

static void net_test_cb( enum timerlib_state state, int exp_count, void *cookie )
{
	log_dev("TIMER TEST CALLBACK WITH STATE: %d, EXPIRIES %d.", state, exp_count);
}


int timerlib_test()
{
	struct timerlib_ctx *ctx;

	log_dev("TIMER TEST STARTED.");

	ctx = timerlib_create( 20, net_test_cb, ctx );
	
	timerlib_activate( ctx );

	sleep(2);

	timerlib_cancel( ctx );

	timerlib_free( ctx );

	return 0;
}

#endif

int timerlib_get_time( struct timerlib_ctx *ctx )
{
	int secs_left = 0;
	struct itimerspec interval;

	pthread_mutex_lock( &ctx->mut );

	if(ctx->active)
	{
		if(timerfd_gettime( ctx->fd, &interval ))
			log_error("ERROR getting timer value for %p", ctx );
		else
			secs_left = (int)interval.it_value.tv_sec;
	}
	
	pthread_mutex_unlock( &ctx->mut );

	return secs_left;
}


int timerlib_reset( struct timerlib_ctx *ctx, int secs )
{
	timerlib_cancel( ctx );
	pthread_mutex_lock( &ctx->mut );
	ctx->secs = secs;
	pthread_mutex_unlock( &ctx->mut );

	return 0;
}





static int timerlib_set_timeout(struct timerlib_ctx *ctx, int secs) 
{
	struct itimerspec interval;
	int               result;

	interval.it_interval.tv_sec = 0;
	interval.it_interval.tv_nsec = 0;
	interval.it_value.tv_sec = (long)secs;
	interval.it_value.tv_nsec = 0;

	result = timerfd_settime( ctx->fd, 0, &interval, NULL);
	if (result)
		return errno;

	return 0;
}

static void *timerlib_timeout( void *arg )
{

	struct timerlib_ctx *ctx  = arg;
	uint64_t count;
	int ret;

	/* Wait for running */
	pthread_mutex_lock( &ctx->mut );
	
	while(1)
	{

		/* Locked test for quit; if set, we exit the thread */
		if(ctx->quit)
		{
			pthread_mutex_unlock( &ctx->mut );
			break;
		}

		/* If active is set, we read the fd, if not, we wait
		 * for the signal */
		if(!ctx->active)
		{
			log_dev("Primed timer %p with secs %d", ctx, ctx->secs );
			pthread_cond_wait( &ctx->cond, &ctx->mut );
			continue;
		}

		log_dev("Waiting on timer %p with secs %d", ctx, ctx->secs );
		
		pthread_mutex_unlock( &ctx->mut );
		
		/* We block wait here for expiration, and at expiration,
		 * the buffer is filled with the number of expirations.
		 * If the timer has already expired X number of times, that
		 * buffer will be filled with X */
	

		ret = read( ctx->fd, &count, sizeof count );

		pthread_mutex_lock( &ctx->mut );

		/* Error on read? Bail out! */
		if(ret == -1)
		{
			log_error("Error on timer %p with secs %d", ctx, ctx->secs );
			//ctx->cb( NET_TIMER_ERROR, 0, ctx->cookie );
			ctx->quit = 1;
			continue;
		}
		else
		{
			log_dev("Expired! on timer %p with secs %d", ctx, ctx->secs );
		
			/* If we deactivated the timer whilst it was waiting for expiry, we
			 * don't treat this expiry as "official" so we don't ping the user.
			 * Instead, we just go to sleep, with the likely expectation that we 
			 * will quit immedately after */
			if(ctx->active)
			{
				ctx->cb( NET_TIMER_EXPIRED, (int)count, ctx->cookie );
				ctx->active = 0;
			}
			else
			{
				ctx->cb( NET_TIMER_CANCELLED, (int)count, ctx->cookie );
			}
			continue;
		}

	}
	
	log_dev("THREAD EXIT on timer %p with secs %d", ctx, ctx->secs );

	return NULL;
}



int timerlib_activate( struct timerlib_ctx *ctx )
{
	if(!ctx)
		return -1;

	pthread_mutex_lock(&ctx->mut);
	if(!ctx->active)
	{
		log_dev("Activating timer %p with secs %d", ctx, ctx->secs );
		timerlib_set_timeout( ctx, ctx->secs );
		ctx->active = 1;
		pthread_cond_broadcast( &ctx->cond );
	}
	pthread_mutex_unlock(&ctx->mut);

	return 0;
}



struct timerlib_ctx *timerlib_create_args( struct timerlib_args args )
{

	struct timerlib_ctx *ctx = NULL;

	ctx = calloc( 1, sizeof *ctx );

	if(ctx == NULL)
		return NULL;

	pthread_mutex_init( &ctx->mut, NULL );
	pthread_cond_init( &ctx->cond, NULL );

	ctx->fd = timerfd_create( CLOCK_MONOTONIC, TFD_CLOEXEC);
	if( ctx->fd < 0 )
	{
		log_error("Timer creation failed with err: [%d] %m", errno);
		goto err;
	}

	ctx->secs = args.secs;
	ctx->cb = args.cb;
	ctx->cookie = args.cookie;
	
	if(pthread_create(&ctx->thread, NULL, timerlib_timeout, ctx ))
		goto err;

	log_dev("Created timer %p with secs %d", ctx, ctx->secs );

	return ctx;

err:
	free(ctx);
	return NULL;

}

int timerlib_is_active( struct timerlib_ctx *ctx )
{
	int active = 0;

	pthread_mutex_lock( &ctx->mut );
	active = ctx->active;
	pthread_mutex_unlock( &ctx->mut );

	return active;
}


int timerlib_cancel( struct timerlib_ctx *ctx )
{
	log_dev("Cancelling timer %p with secs %d", ctx, ctx->secs );
	
	pthread_mutex_lock( &ctx->mut );

	if(ctx->active)
		ctx->active = 0;
		
	timerlib_set_timeout( ctx, 1 );
	
	pthread_mutex_unlock( &ctx->mut);
	

	return 0;
}


int timerlib_free( struct timerlib_ctx *ctx )
{
	if(!ctx)
		return 0;

	pthread_mutex_lock( &ctx->mut );

	if(!ctx->quit)
	{
		ctx->quit = 1;
		pthread_cond_broadcast( &ctx->cond);
	}
		
	pthread_mutex_unlock( &ctx->mut);
	
	/* If already exited, this will be ok */	
	pthread_join( ctx->thread, NULL );

	close(ctx->fd);

	log_dev("Freed timer %p with secs %d", ctx, ctx->secs );
	
	free(ctx);
	
	return 0;
}




