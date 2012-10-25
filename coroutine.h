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

typedef void (*coroutine_main_func)(struct schedule *, void *ud);
typedef void (*coroutine_ontimeout_func)(struct schedule *, int id, void *ud);

struct coroutine_callbacks_t
{
    coroutine_main_func main_;
    coroutine_main_func ondelete_;
    coroutine_ontimeout_func ontimeout_;
    void* ud_;
};

struct schedule * coroutine_open(void);
void coroutine_close(struct schedule *);

int coroutine_new(struct schedule *, struct coroutine_callbacks_t callbacks);
void coroutine_resume(struct schedule *, int id);
int coroutine_status(struct schedule *, int id);
int coroutine_running(struct schedule *);
int coroutine_id(struct schedule *);
void coroutine_yield(struct schedule *);
int coroutine_check_timeout( struct schedule *, int max_check_num, int life );
void* coroutine_get_ud( struct schedule * sched, int id );
int coroutine_check( struct schedule * sched, int id );
int coroutine_fatal( struct schedule * sched, int id );
int coroutine_get_sched_cap( struct schedule * sched );
int coroutine_get_sched_cur( struct schedule * sched );

#ifdef __cplusplus
}
#endif

#endif
