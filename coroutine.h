#ifndef C_COROUTINE_H
#define C_COROUTINE_H

#ifdef __cplusplus
extern "C"
{
#endif

#define COROUTINE_DEAD 0
#define COROUTINE_READY 1
#define COROUTINE_RUNNING 2
#define COROUTINE_SUSPEND 3

struct schedule;

typedef void (*coroutine_func)(struct schedule *, void *ud);
typedef void (*coroutine_timeout_func)(struct schedule *, int id, void *ud);

struct schedule * coroutine_open(void);
void coroutine_close(struct schedule *);

int coroutine_new(struct schedule *, coroutine_func, coroutine_func, coroutine_timeout_func, void *ud);
int coroutine_delete(struct schedule *, int id);
void coroutine_resume(struct schedule *, int id);
int coroutine_status(struct schedule *, int id);
int coroutine_running(struct schedule *);
int coroutine_id(struct schedule *);
void coroutine_yield(struct schedule *);
int coroutine_check_timeout( struct schedule *, int life );
void* coroutine_get_ud( struct schedule * S, int id );

#ifdef __cplusplus
}
#endif

#endif
