#include "coroutine.h"
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define STACK_SIZE (8*1024*1024)
#define DEFAULT_COROUTINE 128
#define MAX_COROUTINE 8192

struct coroutine;

struct schedule {
	char stack[STACK_SIZE]; // 保存当前运行coroutine的栈
	ucontext_t main;
	int nco;
	int cap;
	int running;
	struct coroutine **co;
    int last_co_id;
    int last_check_idx;
};

struct coroutine {
	struct coroutine_callbacks_t callbacks;
	ucontext_t ctx;
	struct schedule * sch;
	ptrdiff_t cap;
	ptrdiff_t size;
	int status;
	char *stack;
    time_t create_time;
    int fatal;
};

int coroutine_delete( struct schedule * sched, int id );

struct coroutine * 
_co_new(struct schedule *sched, struct coroutine_callbacks_t callbacks) {
	struct coroutine * co = malloc(sizeof(*co));
	co->callbacks = callbacks;
	co->sch = sched;
	co->cap = 0;
	co->size = 0;
	co->status = COROUTINE_READY;
	co->stack = NULL;
    co->create_time = time(NULL);
    co->fatal = 0;
	return co;
}

void
_co_delete(struct coroutine *co) {
	free(co->stack);
	free(co);
}

struct schedule * 
coroutine_open(void) {
	struct schedule *sched = malloc(sizeof(*sched));
	sched->nco = 0;
	sched->cap = DEFAULT_COROUTINE;
	sched->running = -1;
	sched->co = malloc(sizeof(struct coroutine *) * sched->cap);
    sched->last_co_id = -1;
    sched->last_check_idx = 0;
	memset(sched->co, 0, sizeof(struct coroutine *) * sched->cap);
	return sched;
}
// coroutine_close的时候是强制关闭的，可能部分coroutine还未执行完
void 
coroutine_close(struct schedule *sched) {
	int i;
	for (i=0;i<sched->cap;i++) {
		struct coroutine * co = sched->co[i];
		if (co) {
			_co_delete(co);
		}
	}
	free(sched->co);
	sched->co = NULL;
	free(sched);
}

int 
coroutine_new(struct schedule *sched, struct coroutine_callbacks_t callbacks) {
    if (sched->nco >= sched->cap && sched->cap >= MAX_COROUTINE) {
        // full
        return -1;
    }
    
	struct coroutine *co = _co_new(sched, callbacks);
	if (sched->nco >= sched->cap) {
		int id = sched->cap;
		sched->co = realloc(sched->co, sched->cap * 2 * sizeof(struct coroutine *));
		memset(sched->co + sched->cap , 0 , sizeof(struct coroutine *) * sched->cap);
		sched->co[sched->cap] = co;
		sched->cap *= 2;
		++sched->nco;
        sched->last_co_id = id;
		return id;
	} else {
		int i;
		for (i=0;i<sched->cap;i++) {
            int id = (i + 1 + sched->last_co_id) % sched->cap;
			if (sched->co[id] == NULL) {
				sched->co[id] = co;
				++sched->nco;
                sched->last_co_id = id;
				return id;
			}
		}
	}
	assert(0);
	return -1;
}

static void
mainfunc(uint32_t low32, uint32_t hi32) {
	uintptr_t ptr = (uintptr_t)low32 | ((uintptr_t)hi32 << 32);
	struct schedule *sched = (struct schedule *)ptr;
	int id = sched->running;
    struct coroutine *C = sched->co[id];
    C->callbacks.main_(sched,C->callbacks.ud_);
    coroutine_delete(sched, id);
	sched->running = -1;
}

void 
coroutine_resume(struct schedule * sched, int id) {
	assert(sched->running == -1);
	assert(id >=0 && id < sched->cap);
	struct coroutine *C = sched->co[id];
	if (C == NULL)
		return;
	int status = C->status;
	switch(status) {
	case COROUTINE_READY:
		getcontext(&C->ctx);
		C->ctx.uc_stack.ss_sp = sched->stack;
		C->ctx.uc_stack.ss_size = STACK_SIZE;
		C->ctx.uc_link = &sched->main;
		sched->running = id;
		C->status = COROUTINE_RUNNING;
		uintptr_t ptr = (uintptr_t)sched;
		makecontext(&C->ctx, (void (*)(void)) mainfunc, 2, (uint32_t)ptr, (uint32_t)(ptr>>32));
		swapcontext(&sched->main, &C->ctx);
		break;
	case COROUTINE_SUSPEND:
		memcpy(sched->stack + STACK_SIZE - C->size, C->stack, C->size);
		sched->running = id;
		C->status = COROUTINE_RUNNING;
		swapcontext(&sched->main, &C->ctx);
		break;
	default:
		assert(0);
	}
}

static void
_save_stack(struct coroutine *C, char *top) {
	char dummy = 0;
	assert(top - &dummy <= STACK_SIZE);
	if (C->cap < top - &dummy) {
		free(C->stack);
		C->cap = top-&dummy;
		C->stack = malloc(C->cap);  // 创建栈空间
	}
	C->size = top - &dummy;
	memcpy(C->stack, &dummy, C->size);  // 保存
}

void
coroutine_yield(struct schedule * sched) {
	int id = sched->running;
	assert(id >= 0);
	struct coroutine * C = sched->co[id];
	assert((char *)&C > sched->stack);
	_save_stack(C,sched->stack + STACK_SIZE);
	C->status = COROUTINE_SUSPEND;
	sched->running = -1;
	swapcontext(&C->ctx , &sched->main);
    // 这里resume/delete的时候一定要把原来的函数context执行完，不然函数里声明类的析构函数不会被执行！
}

int 
coroutine_status(struct schedule * sched, int id) {
	assert(id>=0 && id < sched->cap);
	if (sched->co[id] == NULL) {
		return COROUTINE_DEAD;
	}
	return sched->co[id]->status;
}

int 
coroutine_running(struct schedule * sched) {
	return sched->running;
}

int coroutine_delete( struct schedule * sched, int id ) {
    assert(id>=0 && id < sched->cap);
    struct coroutine *C = sched->co[id];
    if (NULL == C) {
        return -1;
    }
    
    C->callbacks.ondelete_(sched, C->callbacks.ud_);
    _co_delete(C);
    sched->co[id] = NULL;
    --sched->nco;
    return 0;
}

int coroutine_check_timeout( struct schedule * sched, int max_check_num, int life ) {
    int del_num = 0;
    time_t now = time(NULL);
    int check_num = 0;
    int i = -1;

    while (check_num < max_check_num && check_num <= sched->cap) {
        ++check_num;
        i = sched->last_check_idx;
        i = (i >= sched->cap || i < 0)? 0: i;
        sched->last_check_idx = i + 1;
        
        struct coroutine * C = sched->co[i];
        if (NULL == C) {
            continue;
        }

        if (now - C->create_time >= life) {
            C->callbacks.ontimeout_(sched, i, C->callbacks.ud_);
            C->fatal = 1;
            // 让他执行完, coroutine要检查fatal字段
            coroutine_resume(sched, i);
            ++del_num;
        }
    }
    
    return del_num;
    
}

int coroutine_id( struct schedule * sched ) {
    return sched->running;
}

void* coroutine_get_ud( struct schedule * sched, int id ) {
    assert(id>=0 && id < sched->cap);
    struct coroutine *C = sched->co[id];
    if (NULL == C) {
        return NULL;
    }

    return C->callbacks.ud_;
}

int coroutine_check( struct schedule * sched, int id ) {
    if (id < 0 || id >= sched->cap) {
        return -1;
    }

    struct coroutine *C = sched->co[id];
    if (NULL == C) {
        return -1;
    }

    return 0;
}

int coroutine_fatal( struct schedule * sched, int id ) {
    assert(id>=0 && id < sched->cap);
    struct coroutine *C = sched->co[id];
    if (NULL == C) {
        return 0;
    }

    return C->fatal;
}


int coroutine_get_sched_cap( struct schedule * sched ) {
    return sched->cap;
}

int coroutine_get_sched_cur( struct schedule * sched ) {
    return sched->nco;
}


#ifdef __cplusplus
}

#endif
