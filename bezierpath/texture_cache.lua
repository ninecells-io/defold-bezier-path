local CACHE_FORMAT = 1
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

local M = {}

local function assert_non_empty_string(value, label)
    assert(type(value) == "string" and value ~= "", label .. " must be a non-empty string")
end

local function cache_path(filename)
    return sys.get_save_file(config.folder, filename)
end

local function remove_file(filename)
    pcall(os.remove, cache_path(filename))
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
    }
end

local function purge_files(files)
    if type(files) ~= "table" then return end
    for filename in pairs(files) do
        if type(filename) == "string" then
            remove_file(filename)
        end
    end
end

local function load_manifest()
    if not config.enabled or manifest ~= nil then return end

    local ok, stored = pcall(sys.load, cache_path(MANIFEST_FILE))
    if not ok or type(stored) ~= "table" then
        manifest = fresh_manifest()
        save_manifest()
        return
    end

    if stored.format ~= CACHE_FORMAT or stored.version ~= config.version then
        purge_files(stored.files)
        manifest = fresh_manifest()
        save_manifest()
        return
    end

    manifest = stored
    if type(manifest.files) ~= "table" then
        manifest.files = {}
        save_manifest()
    end
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
    local ok = pcall(function()
        local file = assert(io.open(temporary_path, "wb"))
        assert(file:write(data))
        file:flush()
        file:close()
        assert(os.rename(temporary_path, path))
    end)
    if not ok then
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
    end

    config = {
        enabled = enabled,
        folder = enabled and options.folder or nil,
        version = enabled and options.version or nil,
    }
    manifest = nil
    if enabled then load_manifest() end
end

function M.clear()
    if not config.enabled then return false end
    load_manifest()
    purge_files(manifest.files)
    manifest = fresh_manifest()
    save_manifest()
    return true
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
    load_manifest()
    if manifest.files[record.filename] ~= true then return nil end

    local prefix = record_prefix(record.width, record.height)
    local data = read_all(cache_path(record.filename))
    local expected_size = record.width * record.height * 4
    if data and string.sub(data, 1, #prefix) == prefix and #data == #prefix + expected_size then
        return string.sub(data, #prefix + 1)
    end

    remove_file(record.filename)
    manifest.files[record.filename] = nil
    save_manifest()
    return nil
end

function M.store(record, pixels)
    if not config.enabled then return false end
    if type(pixels) ~= "string" or #pixels ~= record.width * record.height * 4 then
        return false
    end

    load_manifest()
    if not write_all(cache_path(record.filename), record_prefix(record.width, record.height) .. pixels) then
        return false
    end

    manifest.files[record.filename] = true
    if not save_manifest() then
        manifest.files[record.filename] = nil
        remove_file(record.filename)
        return false
    end
    return true
end

return M
