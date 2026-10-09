/* Copyright (c) 2013-2016 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "util/test/suite.h"

#include <mgba/core/core.h>
#ifdef ENABLE_DEBUGGERS
#include <mgba/debugger/debugger.h>
#endif
#include <mgba/gba/core.h>
#ifdef ENABLE_DEBUGGERS
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/arm/debugger/debugger.h>
#include <mgba/internal/arm/isa-inlines.h>
#include <mgba/internal/gba/memory.h>

static int _watchpointHits;

static void _watchpointHit(struct mDebuggerModule* module, enum mDebuggerEntryReason reason, struct mDebuggerEntryInfo* info) {
	UNUSED(module);
	UNUSED(info);
	if (reason == DEBUGGER_ENTER_WATCHPOINT) {
		++_watchpointHits;
	}
}
#endif

M_TEST_DEFINE(create) {
	struct mCore* core = GBACoreCreate();
	assert_non_null(core);
	assert_true(core->init(core));
	core->deinit(core);
}

M_TEST_DEFINE(platform) {
	struct mCore* core = GBACoreCreate();
	assert_non_null(core);
	assert_true(core->platform(core) == mPLATFORM_GBA);
	assert_true(core->init(core));
	core->deinit(core);
}

M_TEST_DEFINE(reset) {
	struct mCore* core = GBACoreCreate();
	assert_non_null(core);
	assert_true(core->init(core));
	mCoreInitConfig(core, NULL);
	core->reset(core);
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
}

M_TEST_DEFINE(loadNullROM) {
	struct mCore* core = GBACoreCreate();
	assert_non_null(core);
	assert_true(core->init(core));
	assert_false(core->loadROM(core, NULL));
	mCoreInitConfig(core, NULL);
	core->reset(core);
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
}

#ifdef ENABLE_DEBUGGERS
M_TEST_DEFINE(watchpointFastPath) {
	struct mCore* core = GBACoreCreate();
	assert_non_null(core);
	assert_true(core->init(core));
	mCoreInitConfig(core, NULL);
	core->reset(core);

	struct mDebugger debugger;
	mDebuggerInit(&debugger);
	mDebuggerAttach(&debugger, core);

	struct mDebuggerModule module = {
		.entered = _watchpointHit,
	};
	mDebuggerAttachModule(&debugger, &module);

	struct ARMCore* cpu = core->cpu;
	const uint32_t base = GBA_BASE_IWRAM;

	// Read-only watchpoint: fetch shim must stay absent.
	struct mWatchpoint wp = {
		.minAddress = base,
		.maxAddress = base + 4,
		.segment = 0,
		.type = WATCHPOINT_READ,
	};
	ssize_t id = debugger.platform->setWatchpoint(debugger.platform, &module, &wp);
	assert_true(id > 0);
	assert_null(cpu->memory.fetch);

	// Outside the read range should skip the slow path entirely.
	_watchpointHits = 0;
	core->busRead8(core, base + 0x100);
	assert_int_equal(_watchpointHits, 0);

	// At the start boundary it should hit.
	core->busRead8(core, base);
	assert_int_equal(_watchpointHits, 1);

	// Just past the end should not hit (half-open range).
	core->busRead8(core, base + 4);
	assert_int_equal(_watchpointHits, 1);

	// Wide read that straddles the boundary should hit.
	core->busRead32(core, base + 2);
	assert_int_equal(_watchpointHits, 2);

	// Remove the read watchpoint.
	assert_true(debugger.platform->clearBreakpoint(debugger.platform, id));
	assert_null(cpu->memory.fetch);

	// Add a fetch watchpoint and verify the shim is installed only now.
	wp.type = WATCHPOINT_FETCH;
	id = debugger.platform->setWatchpoint(debugger.platform, &module, &wp);
	assert_true(id > 0);
	assert_non_null(cpu->memory.fetch);

	// Outside the fetch range should not hit.
	_watchpointHits = 0;
	cpu->gprs[ARM_PC] = base + 0x100;
	ARMWritePC(cpu);
	assert_int_equal(_watchpointHits, 0);

	// Inside the fetch range should hit at least once (exact count is
	// mode/width dependent; we only assert that the fast path let it through).
	cpu->gprs[ARM_PC] = base;
	ARMWritePC(cpu);
	assert_true(_watchpointHits > 0);

	// Removing the fetch watchpoint should drop the shim again.
	assert_true(debugger.platform->clearBreakpoint(debugger.platform, id));
	assert_null(cpu->memory.fetch);

	// Overlapping read ranges should each report on their overlap.
	wp.type = WATCHPOINT_READ;
	wp.minAddress = base;
	wp.maxAddress = base + 4;
	id = debugger.platform->setWatchpoint(debugger.platform, &module, &wp);
	wp.minAddress = base + 2;
	wp.maxAddress = base + 6;
	ssize_t id2 = debugger.platform->setWatchpoint(debugger.platform, &module, &wp);
	assert_true(id2 > id);

	_watchpointHits = 0;
	core->busRead8(core, base);        // only first range
	assert_int_equal(_watchpointHits, 1);
	core->busRead8(core, base + 2);    // both ranges
	assert_int_equal(_watchpointHits, 3);
	core->busRead8(core, base + 4);    // only second range
	assert_int_equal(_watchpointHits, 4);
	core->busRead8(core, base + 6);    // outside both
	assert_int_equal(_watchpointHits, 4);

	// Removing one overlapping watchpoint must shrink the bounds.
	assert_true(debugger.platform->clearBreakpoint(debugger.platform, id));
	_watchpointHits = 0;
	core->busRead8(core, base);        // now outside remaining [base+2, base+6)
	assert_int_equal(_watchpointHits, 0);
	core->busRead8(core, base + 3);    // inside remaining
	assert_int_equal(_watchpointHits, 1);

	mDebuggerDeinit(&debugger);
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
}
#endif

M_TEST_SUITE_DEFINE(GBACore,
	cmocka_unit_test(create),
	cmocka_unit_test(platform),
	cmocka_unit_test(reset),
	cmocka_unit_test(loadNullROM)
#ifdef ENABLE_DEBUGGERS
	, cmocka_unit_test(watchpointFastPath)
#endif
)
