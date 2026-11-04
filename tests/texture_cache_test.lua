package.path = "./?.lua;./?/init.lua;" .. package.path

local base = "/private/tmp/bezierpath-texture-cache-test"
local saved = {}
local files = {}
local undeletable = {}
local before_write
local save_failure_countdown
local fail_rename = false
local system_name = "Darwin"

io = {
    open = function(path, mode)
        if string.sub(path, 1, 9) == "/missing/" then return nil, "missing directory" end
        if mode == "rb" then
            if files[path] == nil then return nil, "not found" end
            return {
                read = function(_, format) assert(format == "*a"); return files[path] end,
                close = function() return true end,
            }
        end
        assert(mode == "wb")
        if before_write then before_write(path) end
        local chunks = {}
        return {
            write = function(_, value) table.insert(chunks, value); return true end,
            flush = function() return true end,
            close = function() files[path] = table.concat(chunks); return true end,
        }
    end,
}

os = {
    remove = function(path)
        if undeletable[path] then return nil, "permission denied", 13 end
        if files[path] == nil then return nil, "not found", 2 end
        files[path] = nil
        return true
    end,
    rename = function(from, to)
        if fail_rename then return nil, "rename failed" end
        if files[from] == nil then return nil, "not found" end
        files[to] = files[from]
        files[from] = nil
        return true
    end,
}

local function copy(value)
    if type(value) ~= "table" then return value end
    local result = {}
    for key, item in pairs(value) do result[copy(key)] = copy(item) end
    return result
end

sys = {
    get_sys_info = function() return { system_name = system_name } end,
    get_save_file = function(folder, filename)
        if folder == "unwritable" then return "/missing/bezierpath-cache/" .. filename end
        return base .. "-" .. folder .. "-" .. filename
    end,
    save = function(path, value)
        if save_failure_countdown then
            save_failure_countdown = save_failure_countdown - 1
            if save_failure_countdown == 0 then
                save_failure_countdown = nil
                return false
            end
        end
        saved[path] = copy(value)
        return true
    end,
    load = function(path)
        return copy(saved[path] or {})
    end,
}

function hash(value)
    local result = 7
    for index = 1, #value do result = (result * 131 + string.byte(value, index)) % 4294967291 end
    return result
end

function hash_to_hex(value)
    return tostring(value)
end

local cache = require("bezierpath.texture_cache")

local disabled = cache.describe("texture", "button", 2, 2, 1)
assert(cache.load(disabled) == nil)
assert(cache.store(disabled, string.rep("a", 16)) == false)
assert(disabled.runtime_name ~= cache.describe("texture", "button", 4, 2, 1).runtime_name)
assert(disabled.runtime_name ~= cache.describe("region", "button", 2, 2, 1).runtime_name)

assert(not pcall(cache.configure, { enabled = true, folder = "", version = "1" }))
assert(not pcall(cache.configure, { enabled = true, folder = "test", version = "" }))

cache.configure({ enabled = true, folder = "test", version = "1" })
local record = cache.describe("texture", "button", 2, 2, 1)
local pixels = string.rep("p", 16)
assert(cache.load(record) == nil)
assert(cache.store(record, pixels))
assert(cache.load(record) == pixels)

-- Reconfiguration reloads the manifest and represents a new process/module lifecycle.
cache.configure({ enabled = true, folder = "test", version = "1" })
assert(cache.load(record) == pixels)

local file_path = sys.get_save_file("test", record.filename)
local corrupt = assert(io.open(file_path, "wb"))
corrupt:write("short")
corrupt:close()
assert(cache.load(record) == nil)
assert(cache.store(record, pixels))

cache.configure({ enabled = true, folder = "test", version = "2" })
assert(io.open(file_path, "rb") == nil, "version changes must remove files from the old manifest")
local version_two = cache.describe("texture", "button", 2, 2, 1)
assert(version_two.runtime_name ~= record.runtime_name)
assert(cache.store(version_two, pixels))
assert(cache.clear())
assert(cache.load(version_two) == nil)

cache.configure({ enabled = true, folder = "unwritable", version = "1" })
local unwritable = cache.describe("texture", "button", 2, 2, 1)
assert(cache.store(unwritable, pixels) == false)

local function configure(folder, limit)
    cache.configure({ enabled = true, folder = folder, version = "lru", max_size_bytes = limit })
end
local function path(folder, item) return sys.get_save_file(folder, item.filename) end
local function disk_size(folder)
    local prefix = sys.get_save_file(folder, "")
    local size = 0
    for filename, bytes in pairs(files) do
        if filename:sub(1, #prefix) == prefix then size = size + #bytes end
    end
    return size
end

configure("lru", 1024)
local a = cache.describe("texture", "a", 2, 2, 1)
local b = cache.describe("texture", "b", 2, 2, 1)
local c = cache.describe("texture", "c", 2, 2, 1)
assert(cache.store(a, pixels))
local record_size = #files[path("lru", a)]
configure("lru", record_size * 2)
assert(cache.store(b, pixels))
assert(disk_size("lru") == record_size * 2, "the exact byte limit must be usable")
assert(cache.load(a) == pixels)
configure("lru", record_size * 2) -- Access order and byte counts survive a restart.
before_write = function()
    assert(files[path("lru", b)] == nil, "evict the oldest file before writing new bytes")
end
assert(cache.store(c, pixels))
before_write = nil
assert(cache.load(b) == nil and cache.load(a) == pixels and cache.load(c) == pixels)
assert(cache.store(a, pixels), "replacement must reclaim its old bytes")
assert(cache.load(c) == pixels, "replacement must not double-count its old bytes")

assert(cache.touch(a))
configure("lru", record_size * 2)
assert(cache.store(b, pixels))
assert(files[path("lru", c)] == nil, "runtime reuse must refresh persisted recency")
assert(disk_size("lru") == record_size * 2)

local oversized = cache.describe("texture", "huge", 100, 100, 1)
assert(not cache.store(oversized, string.rep("x", 40000)))
assert(disk_size("lru") == record_size * 2, "oversized entries must not evict useful textures")
assert(not cache.store(c, "invalid pixels"))
assert(disk_size("lru") == record_size * 2)

local larger = cache.describe("region", "larger", 4, 2, 1)
assert(cache.store(larger, string.rep("x", 32)))
assert(cache.load(a) == nil and cache.load(b) == nil, "evict multiple entries when necessary")
assert(disk_size("lru") <= record_size * 2)
assert(cache.clear())
assert(cache.store(a, pixels) and cache.store(b, pixels))
assert(cache.load(a) == pixels)
configure("lru", record_size)
assert(cache.load(a) == pixels and cache.load(b) == nil, "lower limits trim immediately on startup")
configure("lru", 0)
assert(disk_size("lru") == 0 and not cache.store(a, pixels))

configure("failures", record_size)
assert(cache.store(a, pixels))
undeletable[path("failures", a)] = true
assert(not cache.store(b, pixels), "a failed eviction must prevent a new write")
assert(files[path("failures", b)] == nil)
assert(not cache.clear(), "failed deletions must stay tracked")
configure("failures", record_size)
assert(not cache.store(b, pixels))
undeletable[path("failures", a)] = nil
files[path("failures", a)] = nil -- A file missing outside the cache must not block eviction.
assert(cache.store(b, pixels))
assert(disk_size("failures") == record_size)
assert(cache.clear())
save_failure_countdown = 1
assert(not cache.store(a, pixels), "do not write if eviction metadata cannot be persisted")
assert(disk_size("failures") == 0)
fail_rename = true
assert(not cache.store(a, pixels), "failed writes must remove the new texture and temporary file")
fail_rename = false
assert(disk_size("failures") == 0)
assert(cache.store(a, pixels))

assert(cache.clear())
fail_rename = true
undeletable[path("failures", a) .. ".tmp"] = true
assert(not cache.store(a, pixels))
configure("failures", record_size)
assert(not cache.store(b, pixels), "unremovable temporary files must remain in the budget after restart")
assert(disk_size("failures") == record_size)
fail_rename = false
undeletable[path("failures", a) .. ".tmp"] = nil
assert(cache.store(b, pixels))
assert(disk_size("failures") == record_size)

-- Platform defaults without allocating hundreds of MiB: seed two sparse-file
-- metadata records, each representing a 256 MiB texture.
local mib = 1024 * 1024
for _, platform in ipairs({ "Android", "iPhone OS", "Darwin", "Windows", "Linux" }) do
    system_name = platform
    local folder = "default-" .. platform
    local manifest_path = sys.get_save_file(folder, "bezierpath-texture-cache-manifest")
    saved[manifest_path] = { format = 2, version = "lru", access_sequence = 2, files = {
        [a.filename] = { size = 256 * mib, last_used = 1 },
        [b.filename] = { size = 256 * mib, last_used = 2 },
    } }
    files[path(folder, a)], files[path(folder, b)] = "sparse", "sparse"
    configure(folder)
    assert(files[path(folder, b)], "most recent texture must survive")
    local mobile = platform == "Android" or platform == "iPhone OS"
    assert((files[path(folder, a)] == nil) == mobile, "incorrect default limit for " .. platform)
end

local legacy_folder = "legacy"
local legacy_path = sys.get_save_file(legacy_folder, "bezierpath-texture-cache-manifest")
saved[legacy_path] = { format = 1, version = "lru", files = { [a.filename] = true } }
files[path(legacy_folder, a)] = "old-format"
undeletable[path(legacy_folder, a)] = true
configure(legacy_folder, record_size)
assert(not cache.store(b, pixels), "failed old-format cleanup must prevent untracked growth")
undeletable[path(legacy_folder, a)] = nil
assert(cache.store(b, pixels))
assert(files[path(legacy_folder, a)] == nil)

for _, limit in ipairs({ -1, 0.5, math.huge, 0 / 0, "invalid" }) do
    assert(not pcall(configure, "invalid", limit))
end

-- Exercise same-scene reuse through the public API: it skips disk loads but
-- must still protect the recently used texture from the next eviction.
package.preload["helpers.color_helper"] = function() return {} end
package.preload["helpers.gui_helper"] = function() return {} end
local runtime_textures = {}
gui = {
    RESULT_TEXTURE_ALREADY_EXISTS = 1,
    new_texture = function(name)
        if runtime_textures[name] then return false, 1 end
        runtime_textures[name] = true
        return true
    end,
    delete_texture = function(name) runtime_textures[name] = nil end,
}
image = { TYPE_RGB = "rgb", TYPE_RGBA = "rgba" }
local api = require("bezierpath.bezierpath")
api.draw_texture = function() return pixels end
api.draw_texture_region = function(bounds) return pixels, bounds end
for _, kind in ipairs({ "texture", "region" }) do
    local folder = "runtime-" .. kind
    configure(folder, record_size * 2)
    local function create(key)
        if kind == "texture" then return api.create_texture(key, 2, 2, function() end, nil, 1) end
        return api.create_texture_region(key, { width = 2, height = 2 }, function() end)
    end
    create("a")
    create("b")
    create("a")
    configure(folder, record_size * 2)
    create("c")
    local oldest = cache.describe(kind, "b", 2, 2, 1)
    local reused = cache.describe(kind, "a", 2, 2, 1)
    assert(cache.load(oldest) == nil, kind .. " runtime reuse must update the disk LRU")
    assert(cache.load(reused) == pixels)
end

print("texture cache tests passed")
