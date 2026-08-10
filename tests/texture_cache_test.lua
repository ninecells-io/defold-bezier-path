package.path = "./?.lua;./?/init.lua;" .. package.path

local base = "/private/tmp/bezierpath-texture-cache-test"
local saved = {}
local files = {}

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
        local chunks = {}
        return {
            write = function(_, value) table.insert(chunks, value); return true end,
            flush = function() return true end,
            close = function() files[path] = table.concat(chunks); return true end,
        }
    end,
}

os = {
    remove = function(path) files[path] = nil; return true end,
    rename = function(from, to)
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
    get_save_file = function(folder, filename)
        if folder == "unwritable" then return "/missing/bezierpath-cache/" .. filename end
        return base .. "-" .. folder .. "-" .. filename
    end,
    save = function(path, value)
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

print("texture cache tests passed")
