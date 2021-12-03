
#include "gang_scheduler.h"

#include <threads.h>
#include <stdbool.h>
#include <stdint.h>

#include "current_sandbox.h"
#include "debuglog.h"
#include "local_runqueue.h"
#include "panic.h"

thread_local static struct runqueue_gang runqueue_gang;

// TODO: this should be domain specific...so get if current domain is empty...
bool
local_runqueue_gang_domain_is_empty()
{
	return ps_list_head_empty(&runqueue_gang.domain_lists[domain]);
}

/* Get the sandbox at the head of the thread local runqueue */
struct sandbox *
local_runqueue_gang_get_head()
{
    uint32_t domain = runqueue_gang.current_domain;
	return ps_list_head_first_d(&runqueue_gang.domain_lists[domain], struct sandbox);
}

/**
 * Removes the sandbox from the thread-local runqueue
 * @param sandbox sandbox
 */
void
local_runqueue_gang_remove(struct sandbox *sandbox_to_remove)
{
	ps_list_rem_d(sandbox_to_remove);
}

struct sandbox *
local_runqueue_gang_remove_and_return()
{
    uint32_t domain = runqueue_gang.current_domain;
	struct sandbox *sandbox_to_remove = ps_list_head_first_d(&runqueue_gang.domain_lists[domain], struct sandbox);
	ps_list_rem_d(sandbox_to_remove);
	return sandbox_to_remove;
}

/**
 * Append a sandbox to the tail of the runqueue
 * @returns the appended sandbox
 */
void
local_runqueue_gang_append(struct sandbox *sandbox_to_append)
{
	assert(sandbox_to_append != NULL);
	assert(ps_list_singleton_d(sandbox_to_append));

    uint32_t domain = sandbox_to_append->module->domain;
    assert(domain < MAX_DOMAINS);
    assert(domain < runqueue_gang.num_domains + 1);

    // if its a new domain, increment num_domains...
    runqueue_gang.num_domains += (domain == runqueue_gang.num_domains);
    ps_list_head_append_d(&runqueue_gang.domain_lists[domain], sandbox_to_append);
    runqueue_gang.num_sandboxes++;
}


/* Remove sandbox from head of runqueue and add it to tail */
void
local_runqueue_gang_rotate()
{
    // TODO
	/* If runqueue is size one, skip round robin logic since tail equals head */
    uint32_t domain = runqueue_gang.current_domain;
	if (ps_list_head_one_node(&runqueue_gang.domain_lists[domain])) return;

	struct sandbox *sandbox_at_head = local_runqueue_gang_remove_and_return();
	assert(sandbox_at_head->state == SANDBOX_RUNNING_SYS || sandbox_at_head->state == SANDBOX_RUNNABLE
	       || sandbox_at_head->state == SANDBOX_PREEMPTED);
	local_runqueue_gang_append(sandbox_at_head);
}

/**
 * Get the next sandbox
 * @return the sandbox to execute or NULL if none are available
 */
struct sandbox *
local_runqueue_gang_get_next()
{
	if (local_runqueue_gang_domain_is_empty()) return NULL;

	return local_runqueue_gang_get_head();
}

/**
 * Swap to the next domain
 */
void
local_runqueue_gang_next_domain()
{
    runqueue_gang.current_domain = (runqueue_gang.current_domain + 1) % runqueue_gang.num_domains;
}

void
local_runqueue_gang_initialize()
{
    // TODO...
    for (uint32_t i = 0; i < MAX_DOMAINS; i++) {
        ps_list_head_init(&runqueue_gang.domain_lists[i]);
    }

    // TODO: lets just ignore scheudling api for now, and just call 
    //  gang function directly...
	/* Register Function Pointers for Abstract Scheduling API */
	struct local_runqueue_config config = { .add_fn      = local_runqueue_gang_append,
		                                .is_empty_fn = local_runqueue_gang_is_empty,
		                                .delete_fn   = local_runqueue_gang_remove,
		                                .get_next_fn = local_runqueue_gang_get_next };
	local_runqueue_initialize(&config);
};
