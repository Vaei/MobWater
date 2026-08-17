// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"

/**
 * Where water time comes from.
 *
 * Bind a project's own synchronised clock here. Unbound, this falls back to
 * AGameStateBase::GetServerWorldTimeSeconds, which is enough to see water move and not enough to keep
 * two machines in phase: it is corrected in steps, and a step in the clock is a step in every wave at
 * once - the whole surface jumps. A project that needs a server and a client to agree on where the
 * surface is supplies a clock that is corrected by rate instead, and binds it here.
 *
 * Return seconds. Anything monotonic and shared will do; it does not have to start at zero, because
 * the subsystem folds it into the loop period before anything reads it.
 */
DECLARE_DELEGATE_RetVal(double, FMobWaterTimeSource);
