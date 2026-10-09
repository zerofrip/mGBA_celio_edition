/* Copyright (c) 2013-2014 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/arm/debugger/memory-debugger.h>

#include <mgba/internal/arm/debugger/debugger.h>
#include <mgba/internal/debugger/parser.h>

#include <mgba-util/math.h>

#include <string.h>

static void _checkWatchpoints(struct ARMDebugger* debugger, uint32_t address, enum mWatchpointType type, uint32_t newValue, int width);

#define CREATE_SHIM(NAME, RETURN, TYPES, ...) \
	static RETURN DebuggerShim_ ## NAME TYPES { \
		struct ARMDebugger* debugger = cpu->debuggerShim; \
		return debugger->originalMemory.NAME(cpu, __VA_ARGS__); \
	}

#define CREATE_WATCHPOINT_READ_SHIM(NAME, WIDTH, RETURN, TYPES, ...) \
	static RETURN DebuggerShim_ ## NAME TYPES { \
		struct ARMDebugger* debugger = cpu->debuggerShim; \
		if (_watchpointMayHit(debugger, address, WATCHPOINT_READ, WIDTH)) { \
			_checkWatchpoints(debugger, address, WATCHPOINT_READ, 0, WIDTH); \
		} \
		return debugger->originalMemory.NAME(cpu, __VA_ARGS__); \
	}

#define CREATE_WATCHPOINT_WRITE_SHIM(NAME, WIDTH, RETURN, TYPES, ...) \
	static RETURN DebuggerShim_ ## NAME TYPES { \
		struct ARMDebugger* debugger = cpu->debuggerShim; \
		if (_watchpointMayHit(debugger, address, WATCHPOINT_WRITE, WIDTH)) { \
			_checkWatchpoints(debugger, address, WATCHPOINT_WRITE, value, WIDTH); \
		} \
		return debugger->originalMemory.NAME(cpu, __VA_ARGS__); \
	}

static bool _rangeMayContain(uint32_t accessMin, uint32_t accessMax, uint32_t rangeMin, uint32_t rangeMax) {
	if (rangeMin >= rangeMax) {
		return false;
	}
	return accessMax > rangeMin && accessMin < rangeMax;
}

static bool _watchpointMayHit(struct ARMDebugger* debugger, uint32_t address, enum mWatchpointType type, int width) {
	uint32_t minAddress = address & ~(width - 1);
	uint32_t maxAddress = minAddress + width;
	switch (type) {
	case WATCHPOINT_READ:
		return _rangeMayContain(minAddress, maxAddress, debugger->wpRead.min, debugger->wpRead.max);
	case WATCHPOINT_WRITE:
		return _rangeMayContain(minAddress, maxAddress, debugger->wpWrite.min, debugger->wpWrite.max);
	case WATCHPOINT_FETCH:
		return _rangeMayContain(minAddress, maxAddress, debugger->wpFetch.min, debugger->wpFetch.max);
	default:
		return true;
	}
}

CREATE_WATCHPOINT_READ_SHIM(load32, 4, uint32_t, (struct ARMCore* cpu, uint32_t address, int* cycleCounter), address, cycleCounter)
CREATE_WATCHPOINT_READ_SHIM(load16, 2, uint32_t, (struct ARMCore* cpu, uint32_t address, int* cycleCounter), address, cycleCounter)
CREATE_WATCHPOINT_READ_SHIM(load8, 1, uint32_t, (struct ARMCore* cpu, uint32_t address, int* cycleCounter), address, cycleCounter)
CREATE_WATCHPOINT_WRITE_SHIM(store32, 4, void, (struct ARMCore* cpu, uint32_t address, int32_t value, int* cycleCounter), address, value, cycleCounter)
CREATE_WATCHPOINT_WRITE_SHIM(store16, 2, void, (struct ARMCore* cpu, uint32_t address, int16_t value, int* cycleCounter), address, value, cycleCounter)
CREATE_WATCHPOINT_WRITE_SHIM(store8, 1, void, (struct ARMCore* cpu, uint32_t address, int8_t value, int* cycleCounter), address, value, cycleCounter)
CREATE_SHIM(setActiveRegion, void, (struct ARMCore* cpu, uint32_t address), address)

static uint32_t DebuggerShim_loadMultiple(struct ARMCore* cpu, uint32_t address, int mask, enum LSMDirection direction, int* cycleCounter) {
	struct ARMDebugger* debugger = cpu->debuggerShim;
	uint32_t popcount = popcount32(mask);
	int offset = 4;
	int base = address;
	if (direction & LSM_D) {
		offset = -4;
		base -= (popcount << 2) - 4;
	}
	if (direction & LSM_B) {
		base += offset;
	}
	uint32_t end = (uint32_t) base + (popcount << 2);
	if (end < (uint32_t) base || _rangeMayContain(base, end, debugger->wpRead.min, debugger->wpRead.max)) {
		unsigned i;
		for (i = 0; i < popcount; ++i) {
			_checkWatchpoints(debugger, base + 4 * i, WATCHPOINT_READ, 0, 4);
		}
	}
	return debugger->originalMemory.loadMultiple(cpu, address, mask, direction, cycleCounter);
}

static uint32_t DebuggerShim_storeMultiple(struct ARMCore* cpu, uint32_t address, int mask, enum LSMDirection direction, int* cycleCounter) {
	struct ARMDebugger* debugger = cpu->debuggerShim;
	uint32_t popcount = popcount32(mask);
	int offset = 4;
	int base = address;
	if (direction & LSM_D) {
		offset = -4;
		base -= (popcount << 2) - 4;
	}
	if (direction & LSM_B) {
		base += offset;
	}
	uint32_t end = (uint32_t) base + (popcount << 2);
	if (end < (uint32_t) base || _rangeMayContain(base, end, debugger->wpWrite.min, debugger->wpWrite.max)) {
		unsigned i;
		for (i = 0; i < popcount; ++i) {
			_checkWatchpoints(debugger, base + 4 * i, WATCHPOINT_WRITE, 0, 4);
		}
	}
	return debugger->originalMemory.storeMultiple(cpu, address, mask, direction, cycleCounter);
}

static void _checkWatchpoints(struct ARMDebugger* debugger, uint32_t address, enum mWatchpointType type, uint32_t newValue, int width) {
	struct mWatchpoint* watchpoint;
	size_t i;
	uint32_t minAddress = address & ~(width - 1);
	uint32_t maxAddress = minAddress + width;
	for (i = 0; i < mWatchpointListSize(&debugger->watchpoints); ++i) {
		watchpoint = mWatchpointListGetPointer(&debugger->watchpoints, i);
		if (watchpoint->type & type && watchpoint->minAddress < maxAddress && minAddress < watchpoint->maxAddress) {
			if (watchpoint->disabled) {
				continue;
			}
			if (watchpoint->condition) {
				int32_t value;
				int segment;
				if (!mDebuggerEvaluateParseTree(debugger->d.p, watchpoint->condition, &value, &segment) || !(value || segment >= 0)) {
					continue;
				}
			}

			uint32_t oldValue;
			if (type == WATCHPOINT_FETCH) {
				oldValue = 0;
			} else {
				switch (width) {
				case 1:
					oldValue = debugger->originalMemory.load8(debugger->cpu, address, 0);
					break;
				case 2:
					oldValue = debugger->originalMemory.load16(debugger->cpu, address, 0);
					break;
				case 4:
					oldValue = debugger->originalMemory.load32(debugger->cpu, address, 0);
					break;
				default:
					continue;
				}
			}
			if ((watchpoint->type & WATCHPOINT_CHANGE) && newValue == oldValue) {
				continue;
			}

			struct mDebuggerEntryInfo info;
			info.type.wp.oldValue = oldValue;
			info.type.wp.newValue = newValue;
			info.type.wp.watchType = watchpoint->type;
			info.type.wp.accessType = type;
			info.type.wp.accessSource = type == WATCHPOINT_FETCH ? mACCESS_PROGRAM : debugger->cpu->memory.accessSource;
			info.address = address;
			info.segment = 0;
			info.width = width;
			info.pointId = watchpoint->id;
			info.target = TableLookup(&debugger->d.p->pointOwner, watchpoint->id);
			mDebuggerEnter(debugger->d.p, DEBUGGER_ENTER_WATCHPOINT, &info);
		}
	}
}

static void DebuggerShim_fetch(struct ARMCore* cpu, uint32_t address, int width) {
	struct ARMDebugger* debugger = cpu->debuggerShim;
	if (_watchpointMayHit(debugger, address, WATCHPOINT_FETCH, width)) {
		_checkWatchpoints(debugger, address, WATCHPOINT_FETCH, 0, width);
	}
	if (debugger->originalMemory.fetch) {
		debugger->originalMemory.fetch(cpu, address, width);
	}
}

static void _rebuildWatchpointBounds(struct ARMDebugger* debugger) {
	debugger->wpRead.min = debugger->wpRead.max = 0;
	debugger->wpWrite.min = debugger->wpWrite.max = 0;
	debugger->wpFetch.min = debugger->wpFetch.max = 0;

	struct mWatchpoint* watchpoint;
	size_t i;
	for (i = 0; i < mWatchpointListSize(&debugger->watchpoints); ++i) {
		watchpoint = mWatchpointListGetPointer(&debugger->watchpoints, i);
		if (watchpoint->disabled) {
			continue;
		}
		if (watchpoint->minAddress >= watchpoint->maxAddress) {
			continue;
		}

		if (watchpoint->type & WATCHPOINT_READ) {
			if (debugger->wpRead.min >= debugger->wpRead.max) {
				debugger->wpRead.min = watchpoint->minAddress;
				debugger->wpRead.max = watchpoint->maxAddress;
			} else {
				if (watchpoint->minAddress < debugger->wpRead.min) {
					debugger->wpRead.min = watchpoint->minAddress;
				}
				if (watchpoint->maxAddress > debugger->wpRead.max) {
					debugger->wpRead.max = watchpoint->maxAddress;
				}
			}
		}
		if (watchpoint->type & WATCHPOINT_WRITE) {
			if (debugger->wpWrite.min >= debugger->wpWrite.max) {
				debugger->wpWrite.min = watchpoint->minAddress;
				debugger->wpWrite.max = watchpoint->maxAddress;
			} else {
				if (watchpoint->minAddress < debugger->wpWrite.min) {
					debugger->wpWrite.min = watchpoint->minAddress;
				}
				if (watchpoint->maxAddress > debugger->wpWrite.max) {
					debugger->wpWrite.max = watchpoint->maxAddress;
				}
			}
		}
		if (watchpoint->type & WATCHPOINT_FETCH) {
			if (debugger->wpFetch.min >= debugger->wpFetch.max) {
				debugger->wpFetch.min = watchpoint->minAddress;
				debugger->wpFetch.max = watchpoint->maxAddress;
			} else {
				if (watchpoint->minAddress < debugger->wpFetch.min) {
					debugger->wpFetch.min = watchpoint->minAddress;
				}
				if (watchpoint->maxAddress > debugger->wpFetch.max) {
					debugger->wpFetch.max = watchpoint->maxAddress;
				}
			}
		}
	}
}

static void _updateFetchShim(struct ARMDebugger* debugger) {
	if (!debugger->shimsInstalled) {
		return;
	}
	if (debugger->wpFetch.min < debugger->wpFetch.max) {
		debugger->cpu->memory.fetch = DebuggerShim_fetch;
	} else {
		debugger->cpu->memory.fetch = debugger->originalMemory.fetch;
	}
}

void ARMDebuggerRebuildWatchpointBounds(struct ARMDebugger* debugger) {
	_rebuildWatchpointBounds(debugger);
	_updateFetchShim(debugger);
}

void ARMDebuggerInstallMemoryShim(struct ARMDebugger* debugger) {
	debugger->originalMemory = debugger->cpu->memory;
	debugger->cpu->memory.store32 = DebuggerShim_store32;
	debugger->cpu->memory.store16 = DebuggerShim_store16;
	debugger->cpu->memory.store8 = DebuggerShim_store8;
	debugger->cpu->memory.load32 = DebuggerShim_load32;
	debugger->cpu->memory.load16 = DebuggerShim_load16;
	debugger->cpu->memory.load8 = DebuggerShim_load8;
	debugger->cpu->memory.storeMultiple = DebuggerShim_storeMultiple;
	debugger->cpu->memory.loadMultiple = DebuggerShim_loadMultiple;
	debugger->cpu->memory.setActiveRegion = DebuggerShim_setActiveRegion;
	debugger->cpu->debuggerShim = debugger;
	debugger->shimsInstalled = true;
	ARMDebuggerRebuildWatchpointBounds(debugger);
}

void ARMDebuggerRemoveMemoryShim(struct ARMDebugger* debugger) {
	debugger->cpu->memory.store32 = debugger->originalMemory.store32;
	debugger->cpu->memory.store16 = debugger->originalMemory.store16;
	debugger->cpu->memory.store8 = debugger->originalMemory.store8;
	debugger->cpu->memory.load32 = debugger->originalMemory.load32;
	debugger->cpu->memory.load16 = debugger->originalMemory.load16;
	debugger->cpu->memory.load8 = debugger->originalMemory.load8;
	debugger->cpu->memory.storeMultiple = debugger->originalMemory.storeMultiple;
	debugger->cpu->memory.loadMultiple = debugger->originalMemory.loadMultiple;
	debugger->cpu->memory.setActiveRegion = debugger->originalMemory.setActiveRegion;
	debugger->cpu->memory.fetch = debugger->originalMemory.fetch;
	debugger->cpu->debuggerShim = NULL;
	debugger->shimsInstalled = false;
}
