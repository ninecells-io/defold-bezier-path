local CACHE_FORMAT = 2
local MANIFEST_FILE = "bezierpath-texture-cache-manifest"
local FILE_PREFIX = "bezierpath-texture-"
local FILE_SUFFIX = ".rgba"
local RECORD_MAGIC = "BEZIERPATH-RGBA-1\n"

local config = {
    enabled = false,
    folder = nil,
    version = nil,
}

local manifest
local total_size = 0

local M = {}

local function non_negative_integer(value)
    return type(value) == "number" and value >= 0 and value < math.huge and value == math.floor(value)
end

local function default_max_size()
    local system_name = sys.get_sys_info().system_name
    local mobile = system_name == "Android" or system_name == "iPhone OS"
    return (mobile and 256 or 512) * 1024 * 1024
end

local function assert_non_empty_string(value, label)
    assert(type(value) == "string" and value ~= "", label .. " must be a non-empty string")
end

local function cache_path(filename)
    return sys.get_save_file(config.folder, filename)
end

local function remove_file(filename)
    local ok, removed, _, code = pcall(os.remove, cache_path(filename))
    return ok and (removed == true or code == 2) -- An already missing file is also removed.
end

local function save_manifest()
    if not config.enabled or manifest == nil then return false end
    local ok, result = pcall(sys.save, cache_path(MANIFEST_FILE), manifest)
    return ok and result ~= false
end

local function fresh_manifest()
    return {
        format = CACHE_FORMAT,
        version = config.version,
        files = {},
        access_sequence = 0,
    }
end

local function purge_files(files)
    if type(files) ~= "table" then return true end
    local removed = true
    for filename in pairs(files) do
        if type(filename) == "string" then
            if not remove_file(filename .. ".tmp") or not remove_file(filename) then removed = false end
        end
    end
    return removed
end

local function valid_manifest(stored)
    if stored.format ~= CACHE_FORMAT or stored.version ~= config.version
        or type(stored.files) ~= "table" or not non_negative_integer(stored.access_sequence) then
        return false
    end
    for filename, entry in pairs(stored.files) do
        if type(filename) ~= "string" or type(entry) ~= "table"
            or not non_negative_integer(entry.size) or not non_negative_integer(entry.last_used)
            or entry.last_used > stored.access_sequence then return false end
    end
    return true
end

local function load_manifest()
    if not config.enabled then return false end
    if manifest ~= nil then return true end

    local ok, stored = pcall(sys.load, cache_path(MANIFEST_FILE))
    if not ok or type(stored) ~= "table" then
        manifest = fresh_manifest()
        save_manifest()
        return true
    end

    if not valid_manifest(stored) then
        -- Keep the old manifest until all files are removed, so a failed delete
        -- cannot leave untracked textures while we start filling a new cache.
        if not purge_files(stored.files) then return false end
        manifest = fresh_manifest()
        save_manifest()
        return true
    end

    manifest = stored
    for _, entry in pairs(manifest.files) do
        total_size = total_size + entry.size
    end
    return true
end

local function remove_entry(filename)
    if not remove_file(filename .. ".tmp") or not remove_file(filename) then return false end
    total_size = total_size - manifest.files[filename].size
    manifest.files[filename] = nil
    return true
end

local function evict_to_fit(size)
    if size > config.max_size_bytes then return false end
    if total_size + size <= config.max_size_bytes then return true end
    local oldest_first = {}
    for filename in pairs(manifest.files) do oldest_first[#oldest_first + 1] = filename end
    table.sort(oldest_first, function(a, b)
        local left, right = manifest.files[a].last_used, manifest.files[b].last_used
        return left < right or (left == right and a < b)
    end)
    for _, filename in ipairs(oldest_first) do
        if not remove_entry(filename) then return false end
        if total_size + size <= config.max_size_bytes then return true end
    end
    return total_size + size <= config.max_size_bytes
end

local function mark_used(entry)
    manifest.access_sequence = manifest.access_sequence + 1
    entry.last_used = manifest.access_sequence
end

local function identity(kind, key, width, height, factor)
    assert_non_empty_string(key, "texture cache key")
    return table.concat({
        "bezierpath",
        tostring(CACHE_FORMAT),
        config.version or "uncached",
        kind,
        tostring(width),
        tostring(height),
        tostring(factor or 1),
        key,
    }, "\31")
end

local function record_prefix(width, height)
    return RECORD_MAGIC .. tostring(width) .. "\n" .. tostring(height) .. "\nrgba\n"
end

local function read_all(path)
    local ok, data = pcall(function()
        local file = io.open(path, "rb")
        if not file then return nil end
        local bytes = file:read("*a")
        file:close()
        return bytes
    end)
    return ok and data or nil
end

local function write_all(path, data)
    local temporary_path = path .. ".tmp"
    local file
    local ok = pcall(function()
        file = assert(io.open(temporary_path, "wb"))
        assert(file:write(data))
        assert(file:flush())
        assert(file:close())
        file = nil
        assert(os.rename(temporary_path, path))
    end)
    if not ok then
        if file then pcall(function() file:close() end) end
        pcall(os.remove, temporary_path)
    end
    return ok
end

function M.configure(options)
    options = options or {}
    local enabled = options.enabled == true
    if enabled then
        assert_non_empty_string(options.folder, "texture cache folder")
        assert_non_empty_string(options.version, "texture cache version")
        if options.max_size_bytes ~= nil then
            assert(non_negative_integer(options.max_size_bytes), "texture cache max_size_bytes must be a non-negative integer")
        end
    end

    config = {
        enabled = enabled,
        folder = enabled and options.folder or nil,
        version = enabled and options.version or nil,
        max_size_bytes = enabled and (options.max_size_bytes or default_max_size()) or 0,
    }
    manifest = nil
    total_size = 0
    if enabled and load_manifest() then
        evict_to_fit(0)
        save_manifest()
    end
end

function M.clear()
    if not config.enabled then return false end
    if not load_manifest() then return false end
    local removed = true
    for filename in pairs(manifest.files) do
        if not remove_entry(filename) then removed = false end
    end
    return save_manifest() and removed
end

function M.describe(kind, key, width, height, factor)
    local id = identity(kind, key, width, height, factor)
    local digest = hash_to_hex(hash(id))
    return {
        runtime_name = "bezierpath_" .. digest,
        filename = FILE_PREFIX .. digest .. FILE_SUFFIX,
        width = width,
        height = height,
    }
end

function M.load(record)
    if not config.enabled then return nil end
    if not load_manifest() or manifest.files[record.filename] == nil then return nil end

    local prefix = record_prefix(record.width, record.height)
    local data = read_all(cache_path(record.filename))
    local expected_size = record.width * record.height * 4
    if data and string.sub(data, 1, #prefix) == prefix and #data == #prefix + expected_size then
        mark_used(manifest.files[record.filename])
        save_manifest()
        return string.sub(data, #prefix + 1)
    end

    remove_entry(record.filename)
    save_manifest()
    return nil
end

-- Runtime GUI texture reuse also counts as use of its persistent cache entry.
function M.touch(record)
    if not config.enabled or not load_manifest() then return false end
    local entry = manifest.files[record.filename]
    if not entry then return false end
    mark_used(entry)
    return save_manifest()
end

function M.store(record, pixels)
    if not config.enabled then return false end
    if type(pixels) ~= "string" or #pixels ~= record.width * record.height * 4 then
        return false
    end

    if not load_manifest() then return false end
    local data = record_prefix(record.width, record.height) .. pixels
    -- Oversized textures still render, but must not empty the cache or be saved.
    if #data > config.max_size_bytes then return false end
    if manifest.files[record.filename] and not remove_entry(record.filename) then return false end
    local fits = evict_to_fit(#data)
    if not fits then
        save_manifest()
        return false
    end

    -- Reserve the file in the manifest before writing, including its temporary
    -- path. An interrupted write stays tracked and counts against the budget.
    local entry = { size = #data }
    mark_used(entry)
    manifest.files[record.filename] = entry
    total_size = total_size + entry.size
    if not save_manifest() then
        remove_entry(record.filename)
        return false
    end
    if not write_all(cache_path(record.filename), data) then
        remove_entry(record.filename)
        save_manifest()
        return false
    end
    return true
end

return M
