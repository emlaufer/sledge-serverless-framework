#pragma once

#include <assert.h>
#include <errno.h>
#include <stdint.h>

#include "client_socket.h"
#include "cache_protection.h"
#include "current_sandbox.h"
#include "gang_scheduler.h"
#include "global_request_scheduler.h"
#include "global_request_scheduler_deque.h"
#include "global_request_scheduler_minheap.h"
#include "local_runqueue.h"
#include "local_runqueue_minheap.h"
#include "local_runqueue_list.h"
#include "panic.h"
#include "sandbox_request.h"
#include "sandbox_functions.h"
#include "sandbox_types.h"
#include "sandbox_set_as_preempted.h"
#include "sandbox_set_as_runnable.h"
#include "sandbox_set_as_running_sys.h"
#include "sandbox_set_as_running_user.h"
#include "scheduler_execute_epoll_loop.h"

#define LOG_CONTEXT_SWITCHES

enum SCHEDULER
{
	SCHEDULER_FIFO = 0,
	SCHEDULER_EDF  = 1,
    SCHEDULER_GANG = 2,
};

extern enum SCHEDULER scheduler;

static inline struct sandbox *
scheduler_edf_get_next(bool preemptive)
{
	/* Get the deadline of the sandbox at the head of the local request queue */
	struct sandbox *        local          = local_runqueue_get_next(preemptive);
	uint64_t                local_deadline = local == NULL ? UINT64_MAX : local->absolute_deadline;
	struct sandbox_request *request        = NULL;

	uint64_t global_deadline = global_request_scheduler_peek();

	/* Try to pull and allocate from the global queue if earlier
	 * This will be placed at the head of the local runqueue */
	if (global_deadline < local_deadline) {
		if (global_request_scheduler_remove_if_earlier(&request, local_deadline) == 0) {
			assert(request != NULL);
			assert(request->absolute_deadline < local_deadline);
			struct sandbox *global = sandbox_allocate(request);
			if (!global) goto err_allocate;

			assert(global->state == SANDBOX_INITIALIZED);
			sandbox_set_as_runnable(global, SANDBOX_INITIALIZED);
		}
	}

/* Return what is at the head of the local runqueue or NULL if empty */
done:
	return local_runqueue_get_next(preemptive);
err_allocate:
	client_socket_send(request->socket_descriptor, 503);
	client_socket_close(request->socket_descriptor, &request->socket_address);
	free(request);
	goto done;
}

static inline struct sandbox *
scheduler_fifo_get_next(bool preemptive)
{
    // TODO: get rid of preemptive bool later...
	struct sandbox *sandbox = local_runqueue_get_next(preemptive);

	struct sandbox_request *sandbox_request = NULL;

	if (sandbox == NULL) {
		/* If the local runqueue is empty, pull from global request scheduler */
		if (global_request_scheduler_remove(&sandbox_request) < 0) goto err;

		sandbox = sandbox_allocate(sandbox_request);
		if (!sandbox) goto err_allocate;

		sandbox_set_as_runnable(sandbox, SANDBOX_INITIALIZED);
	} else if (sandbox == current_sandbox_get()) {
		/* Execute Round Robin Scheduling Logic if the head is the current sandbox */
		local_runqueue_list_rotate();
		sandbox = local_runqueue_get_next(preemptive);
	}


done:
	return sandbox;
err_allocate:
	client_socket_send(sandbox_request->socket_descriptor, 503);
	client_socket_close(sandbox_request->socket_descriptor, &sandbox->client_address);
	free(sandbox_request);
err:
	sandbox = NULL;
	goto done;
}

static inline struct sandbox *
scheduler_gang_get_next(bool preemptive)
{
    // if not preemptive:
    //  get next sandbox from same domain
    //  if there is none, try to get one from global scheduler
    // TODO: assumption: preemtion only happens on timer interrupt
    //
    // cooperative case first
    // get the next sandbox... should be from this domain

    // swap to the next domain for gang scheduling...
    if (preemptive) {
#ifdef LOG_DOMAIN_SWITCH
        uint32_t current_domain = local_runqueue_gang_current_domain();
#endif
        local_runqueue_gang_next_domain();
#ifdef LOG_DOMAIN_SWITCH
        uint32_t next_domain = local_runqueue_gang_current_domain();
		debuglog("Swapping domains: %s > %s\n", current_domain, next_domain); 
#endif
    }

	struct sandbox *sandbox = local_runqueue_get_next(preemptive);
	struct sandbox_request *sandbox_request = NULL;

	if (sandbox == NULL) {
		/* If the local runqueue is empty, pull from global request scheduler */
        // TODO: change global_request_scheduler to respect domains...
		if (global_request_scheduler_remove(&sandbox_request) < 0) goto err;

		sandbox = sandbox_allocate(sandbox_request);
		if (!sandbox) goto err_allocate;

		sandbox_set_as_runnable(sandbox, SANDBOX_INITIALIZED);
	} else if (sandbox == current_sandbox_get()) {
		/* Execute Round Robin Scheduling Logic if the head is the current sandbox */
		local_runqueue_gang_rotate();
		sandbox = local_runqueue_get_next(preemptive);
	}


done:
	return sandbox;
err_allocate:
	client_socket_send(sandbox_request->socket_descriptor, 503);
	client_socket_close(sandbox_request->socket_descriptor, &sandbox->client_address);
	free(sandbox_request);
err:
	sandbox = NULL;
	goto done;

}

static inline struct sandbox *
scheduler_get_next(bool preemptive)
{
#ifdef LOG_DEFERRED_SIGALRM_MAX
	if (unlikely(software_interrupt_deferred_sigalrm
	             > software_interrupt_deferred_sigalrm_max[worker_thread_idx])) {
		software_interrupt_deferred_sigalrm_max[worker_thread_idx] = software_interrupt_deferred_sigalrm;
	}
#endif

	atomic_store(&software_interrupt_deferred_sigalrm, 0);
	switch (scheduler) {
	case SCHEDULER_EDF:
		return scheduler_edf_get_next(preemptive);
	case SCHEDULER_FIFO:
		return scheduler_fifo_get_next(preemptive);
    case SCHEDULER_GANG:
        return scheduler_gang_get_next(preemptive);
	default:
		panic("Unimplemented\n");
	}
}

static inline void
scheduler_initialize()
{
	switch (scheduler) {
	case SCHEDULER_EDF:
		global_request_scheduler_minheap_initialize();
		break;
	case SCHEDULER_FIFO:
		global_request_scheduler_deque_initialize();
		break;
    case SCHEDULER_GANG:
        // TODO: What is the global policy for gang...?
        global_request_scheduler_deque_initialize();
        break;
	default:
		panic("Invalid scheduler policy: %u\n", scheduler);
	}
}

static inline void
scheduler_runqueue_initialize()
{
	switch (scheduler) {
	case SCHEDULER_EDF:
		local_runqueue_minheap_initialize();
		break;
	case SCHEDULER_FIFO:
		local_runqueue_list_initialize();
		break;
    case SCHEDULER_GANG:
		//local_runqueue_list_initialize();
        local_runqueue_gang_initialize();
        break;
	default:
		panic("Invalid scheduler policy: %u\n", scheduler);
	}
}

static inline char *
scheduler_print(enum SCHEDULER variant)
{
	switch (variant) {
	case SCHEDULER_FIFO:
		return "FIFO";
	case SCHEDULER_EDF:
		return "EDF";
    case SCHEDULER_GANG:
        return "GANG";
	}
}

static inline void
scheduler_log_sandbox_switch(struct sandbox *current_sandbox, struct sandbox *next_sandbox)
{
#ifdef LOG_CONTEXT_SWITCHES
	if (current_sandbox == NULL) {
		/* Switching from "Base Context" */
		debuglog("Base Context (@%p) (%s) > Sandbox %lu (@%p) (%s)\n", &worker_thread_base_context,
		         arch_context_variant_print(worker_thread_base_context.variant), next_sandbox->id,
		         &next_sandbox->ctxt, arch_context_variant_print(next_sandbox->ctxt.variant));
	} else if (next_sandbox == NULL) {
		debuglog("Sandbox %lu (@%p) (%s) > Base Context (@%p) (%s)\n", current_sandbox->id,
		         &current_sandbox->ctxt, arch_context_variant_print(current_sandbox->ctxt.variant),
		         &worker_thread_base_context, arch_context_variant_print(worker_thread_base_context.variant));
	} else {
		debuglog("Sandbox %lu (@%p) (%s) > Sandbox %lu (@%p) (%s)\n", current_sandbox->id,
		         &current_sandbox->ctxt, arch_context_variant_print(current_sandbox->ctxt.variant),
		         next_sandbox->id, &next_sandbox->ctxt, arch_context_variant_print(next_sandbox->ctxt.variant));
	}
    if (runtime_domains) {
        char current_domain[32] = {0};
        char next_domain[32] = {0};
        if (current_sandbox) {
            snprintf(current_domain, 32, "Domain %d", current_sandbox->module->domain);
        } else {
            strcpy(current_domain, "Base Ctx Domain");
        }
        if (next_sandbox) {
            snprintf(next_domain, 32, "Domain %d", next_sandbox->module->domain);
        } else {
            strcpy(next_domain, "Base Ctx Domain");
        }
		debuglog("%s > %s\n", current_domain, next_domain); 
    }
#endif
}

static inline void
scheduler_preemptive_switch_to(ucontext_t *interrupted_context, struct sandbox *next)
{
	/* Switch to next sandbox */
	switch (next->ctxt.variant) {
	case ARCH_CONTEXT_VARIANT_FAST: {
		assert(next->state == SANDBOX_RUNNABLE);
		arch_context_restore_fast(&interrupted_context->uc_mcontext, &next->ctxt);
		sandbox_set_as_running_sys(next, SANDBOX_RUNNABLE);
		break;
	}
	case ARCH_CONTEXT_VARIANT_SLOW: {
		assert(next->state == SANDBOX_PREEMPTED);
		arch_context_restore_slow(&interrupted_context->uc_mcontext, &next->ctxt);
		sandbox_set_as_running_user(next, SANDBOX_PREEMPTED);
		break;
	}
	default: {
		panic("Unexpectedly tried to switch to a context in %s state\n",
		      arch_context_variant_print(next->ctxt.variant));
	}
	}
}

/**
 * Called by the SIGALRM handler after a quantum
 * Assumes the caller validates that there is something to preempt
 * @param interrupted_context - The context of our user-level Worker thread
 * @returns the sandbox that the scheduler chose to run
 */
static inline void
scheduler_preemptive_sched(ucontext_t *interrupted_context)
{
	assert(interrupted_context != NULL);

	/* Process epoll to make sure that all runnable jobs are considered for execution */
	scheduler_execute_epoll_loop();

	struct sandbox *current = current_sandbox_get();
	assert(current != NULL);
	assert(current->state == SANDBOX_RUNNING_USER);

	sandbox_interrupt(current);

	struct sandbox *next = scheduler_get_next(true);
	/* Assumption: the current sandbox is on the runqueue, so the scheduler should always return something */
	assert(next != NULL);

	/* If current equals next, no switch is necessary, so resume execution */
	if (current == next) {
		sandbox_return(current);
		return;
	}

#ifdef LOG_PREEMPTION
	debuglog("Preempting sandbox %lu to run sandbox %lu\n", current->id, next->id);
#endif

	scheduler_log_sandbox_switch(current, next);

    if (!runtime_domains || current->module->domain == -1 
            || current->module->domain != next->module->domain) {
        // clear the cache via policy
        cache_protection_flush();
    }


	/* Preempt executing sandbox */
	sandbox_preempt(current);
	arch_context_save_slow(&current->ctxt, &interrupted_context->uc_mcontext);

	scheduler_preemptive_switch_to(interrupted_context, next);
}

/**
 * @brief Switches to the next sandbox
 * Assumption: only called by the "base context"
 * @param next_sandbox The Sandbox to switch to
 */
static inline void
scheduler_cooperative_switch_to(struct sandbox *next_sandbox)
{
	assert(current_sandbox_get() == NULL);

	struct arch_context *next_context = &next_sandbox->ctxt;

	scheduler_log_sandbox_switch(NULL, next_sandbox);

	/* Switch to next sandbox */
	switch (next_sandbox->state) {
	case SANDBOX_RUNNABLE: {
		assert(next_context->variant == ARCH_CONTEXT_VARIANT_FAST);
		sandbox_set_as_running_sys(next_sandbox, SANDBOX_RUNNABLE);
		break;
	}
	case SANDBOX_PREEMPTED: {
		assert(next_context->variant == ARCH_CONTEXT_VARIANT_SLOW);
		/* arch_context_switch triggers a SIGUSR1, which transitions next_sandbox to running_user */
		current_sandbox_set(next_sandbox);
		break;
	}
	default: {
		panic("Unexpectedly tried to switch to a sandbox in %s state\n",
		      sandbox_state_stringify(next_sandbox->state));
	}
	}

	arch_context_switch(&worker_thread_base_context, next_context);
}

/* A sandbox cannot execute the scheduler directly. It must yield to the base context, and then the context calls this
 * within its idle loop
 */
static inline void
scheduler_cooperative_sched()
{
	/* Assumption: only called by the "base context" */
	assert(current_sandbox_get() == NULL);

	/* Try to wakeup sleeping sandboxes */
	scheduler_execute_epoll_loop();

	/* Switch to a sandbox if one is ready to run */
	struct sandbox *next_sandbox = scheduler_get_next(false);

    // clear the cache via policy (TODO: always do on coop for now)
    cache_protection_flush();

	if (next_sandbox != NULL) scheduler_cooperative_switch_to(next_sandbox);

	/* Clear the completion queue */
	local_completion_queue_free();
}


static inline bool
scheduler_worker_would_preempt(int worker_idx)
{
	assert(scheduler == SCHEDULER_EDF);
	uint64_t local_deadline  = runtime_worker_threads_deadline[worker_idx];
	uint64_t global_deadline = global_request_scheduler_peek();
	return global_deadline < local_deadline;
}
