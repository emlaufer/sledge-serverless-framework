
#pragma once

#include <stdint.h>
#include "ps_list.h"

#define MAX_DOMAINS 256

// Gang runqueue implementation
// It is a list of domain-specific runqueues
struct runqueue_gang {
    // TODO: lets just say 256 max domains for now
    //       can replace with hash-map for higher number or resize based on
    //       max number??
    struct ps_list_head domain_lists[MAX_DOMAINS];
    uint32_t num_sandboxes;
    uint32_t num_domains;
    uint32_t current_domain;
};

void local_runqueue_gang_initialize();
void local_runqueue_gang_next_domain();
void local_runqueue_gang_rotate();
uint32_t local_runqueue_gang_current_domain();


