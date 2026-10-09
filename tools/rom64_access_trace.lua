-- PokemonStart ROM64 CPU data-read, DMA-read and instruction-fetch trace.
-- Edit these two values before loading the script. Forward slashes work on Windows.
ROM64_SCENE = "unspecified"
ROM64_OUTPUT = "rom64_access_trace.csv"

local FIRST = 0x0A000000
local LAST = 0x0C000000 -- exclusive
local MAX_ROWS = 100000
local FLUSH_EVERY = 100

local function csv(value)
    return '"' .. tostring(value):gsub('"', '""') .. '"'
end

-- Reloading the script in the same Lua state should not leave two watchpoints.
if type(rom64_trace_stop) == "function" then
    rom64_trace_stop()
end

local file, err = io.open(ROM64_OUTPUT, "a+")
assert(file, "Cannot open ROM64 trace CSV: " .. tostring(err))
local size = assert(file:seek("end"))
if size == 0 then
    assert(file:write("scene,kind,pc,address,width,notes\n"))
    assert(file:flush())
end

local watch_ids = {}
local rows = 0
local active = true

function rom64_trace_stop()
    if not active then return end
    active = false
    for _, id in ipairs(watch_ids) do
        emu:clearBreakpoint(id)
    end
    watch_ids = {}
    if file then
        file:flush()
        file:close()
        file = nil
    end
    print("ROM64 trace stopped: " .. tostring(rows) .. " rows; " .. ROM64_OUTPUT)
end

local function on_access(info)
    if not active or not file or not info then return end
    local address = tonumber(info.address)
    local width = tonumber(info.width)
    if not address or address < FIRST or address >= LAST then return end
    if width ~= 1 and width ~= 2 and width ~= 4 then return end

    local kind = "read"
    if info.accessType == C.WATCHPOINT_TYPE.FETCH then
        kind = "fetch"
    elseif info.accessSource == C.MEMORY_ACCESS_SOURCE.DMA then
        kind = "dma"
    end
    local pc = kind == "fetch" and address or emu:readRegister("pc")
    local pc_text = ""
    if type(pc) == "number" then
        pc_text = string.format("0x%08X", pc % 0x100000000)
    end
    local line = table.concat({
        csv(ROM64_SCENE), kind, pc_text,
        string.format("0x%08X", address), tostring(width), csv("")
    }, ",") .. "\n"
    local ok, write_err = file:write(line)
    if not ok then
        print("ROM64 trace write failed: " .. tostring(write_err))
        rom64_trace_stop()
        return
    end
    rows = rows + 1
    if rows % FLUSH_EVERY == 0 then
        local flushed, flush_err = file:flush()
        if not flushed then
            print("ROM64 trace flush failed: " .. tostring(flush_err))
            rom64_trace_stop()
            return
        end
    end
    if rows >= MAX_ROWS then
        print("ROM64 trace reached the safety limit of " .. MAX_ROWS .. " rows")
        rom64_trace_stop()
    end
end

local ok, result = pcall(function()
    return emu:setRangeWatchpoint(on_access, FIRST, LAST, C.WATCHPOINT_TYPE.READ)
end)
if not ok or type(result) ~= "number" or result <= 0 then
    rom64_trace_stop()
    error("ROM64 read watchpoint unavailable; use an mGBA build with Lua scripting and debugger support: " .. tostring(result))
end
watch_ids[#watch_ids + 1] = result
ok, result = pcall(function()
    return emu:setRangeWatchpoint(on_access, FIRST, LAST, C.WATCHPOINT_TYPE.FETCH)
end)
if not ok or type(result) ~= "number" or result <= 0 then
    rom64_trace_stop()
    error("ROM64 fetch watchpoint unavailable; use an mGBA build with the ROM64 fetch API: " .. tostring(result))
end
watch_ids[#watch_ids + 1] = result
print("ROM64 read/DMA/fetch trace active: 0x0A000000-0x0BFFFFFF -> " .. ROM64_OUTPUT)
