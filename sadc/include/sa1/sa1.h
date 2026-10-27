#ifndef SA1_SA1_H
#define SA1_SA1_H

/* All shared engine/player headers (include/sa1/). Conventions:
 * docs/portability.md. sa1/pending.h (canonical names of the shared types
 * that unmigrated src files still define locally, sa1_<name> here) is
 * deliberately not included: see docs/header_migration.md. Neither is
 * sa1/protos.h (canonical prototypes of the functions that src files still
 * declare with different signatures, build/verify/canonical_protos.json):
 * a src file includes it once its own declarations agree with it. */

#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/ninja_types.h"
#include "sa1/ninja_funcs.h"
#include "sa1/shc_intrinsics.h"
#include "sa1/engine.h"
#include "sa1/motion.h"
#include "sa1/collision.h"
#include "sa1/ccl.h"
#include "sa1/player.h"
#include "sa1/input.h"

#endif