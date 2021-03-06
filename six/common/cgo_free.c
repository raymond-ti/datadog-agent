// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2019 Datadog, Inc.
#include "cgo_free.h"

// these must be set by the Agent
static cb_cgo_free_t cb_cgo_free = NULL;

void _set_cgo_free_cb(cb_cgo_free_t cb) {
    cb_cgo_free = cb;
}

// On windows we cannot free memory block from another DLL. Agent's Callbacks
// will return memory block to free, this is why we need to a pointer to CGO
// free method to release memory allocated in the agent once we're done with
// them.
void cgo_free(void *ptr) {
    if (cb_cgo_free == NULL || ptr == NULL) {
        return;
    }
    cb_cgo_free(ptr);
}
