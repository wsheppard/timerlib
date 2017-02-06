#ifndef TIMERLIB_H_
#define TIMERLIB_H_

enum timerlib_state
{
	NET_TIMER_EXPIRED,
	NET_TIMER_CANCELLED,
	NET_TIMER_RUNNING,
	NET_TIMER_ERROR
};

struct timerlib_ctx;

typedef void timerlib_cb( enum timerlib_state state, int exp_count, void *cookie );
int timerlib_activate( struct timerlib_ctx *ctx );
struct timerlib_ctx *timerlib_create(int secs, timerlib_cb *cb, void *cookie);
int timerlib_free( struct timerlib_ctx *ctx );
int timerlib_cancel( struct timerlib_ctx *ctx );
int timerlib_is_active( struct timerlib_ctx *ctx );
int timerlib_reset( struct timerlib_ctx *ctx, int secs );
int timerlib_get_time( struct timerlib_ctx *ctx );

#endif
