/* Power-on self-test (FR-10). SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include "node.h"

/* Runs every check, prints one "[CHECK] ..." line per check with the real
 * result, and fills ctx->boot_dht and ctx->selftest_fault. A check prints
 * OK only if it actually ran and passed (closes AR-2). */
void selftest_run(node_ctx_t *ctx);
