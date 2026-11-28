-- fawwk.lua
-- part of fawwk menu, made by fawwktrustdigital (MIT licence).
-- settings sdk for fawwk menu. put it in moonloader/lib and require 'fawwk'.
-- if the menu (fawwkmenu.asi) isn't installed your mod just runs on its defaults.

local M = {}
M.API = 1
M.VERSION = '1.0.0'

local floor, abs, min, max = math.floor, math.abs, math.min, math.max

-- ---------------------------------------------------------------- wire stuff
-- moonloader scripts can only hand each other plain strings/numbers, so
-- everything crossing between the menu and a mod is a flat string.

local function enc(s)
  return (tostring(s):gsub('[^%w%-%._~]', function(c)
    return ('%%%02X'):format(c:byte())
  end))
end

local function dec(s)
  return (s:gsub('%%(%x%x)', function(h) return string.char(tonumber(h, 16)) end))
end

local function pack(t)
  local keys = {}
  for k in pairs(t) do keys[#keys + 1] = k end
  table.sort(keys)
  local out = {}
  for i, k in ipairs(keys) do out[i] = enc(k) .. '=' .. enc(t[k]) end
  return table.concat(out, '&')
end

local function unpackstr(s)
  local t = {}
  if not s or s == '' then return t end
  for pair in s:gmatch('[^&]+') do
    local k, v = pair:match('^([^=]*)=(.*)$')
    if k then t[dec(k)] = dec(v) end
  end
  return t
end

local function lines(s)
  local out = {}
  for l in (s .. '\n'):gmatch('(.-)\n') do out[#out + 1] = l end
  return out
end

local function numstr(n)
  if n == floor(n) and abs(n) < 1e15 then return ('%d'):format(n) end
  return ('%.10g'):format(n)
end

M.enc, M.dec, M.pack, M.unpack, M.lines, M.numstr = enc, dec, pack, unpackstr, lines, numstr

-- ------------------------------------------------------------------- schema

local ALIAS = {
  toggle = 'bool', checkbox = 'bool', boolean = 'bool',
  slider = 'float', number = 'float',
  combo = 'enum', dropdown = 'enum', select = 'enum',
  hotkey = 'key', string = 'text', input = 'text',
  label = 'note', info = 'note', description = 'note',
  divider = 'separator', heading = 'header', section = 'header',
}

local VALUED = { bool = true, int = true, float = true, enum = true, color = true, key = true, text = true, list = true }
local LAYOUT = { button = true, header = true, separator = true, note = true }
M.VALUED = VALUED

local function clamp(n, lo, hi)
  if n < lo then return lo end
  if n > hi then return hi end
  return n
end

local function str(v, fallback)
  if v == nil then return fallback or '' end
  return tostring(v)
end

-- turns whatever the mod author wrote into a clean spec, or nil + reason
function M.spec(raw)
  if type(raw) ~= 'table' then return nil, 'setting needs to be a table' end
  local t = str(raw.type, 'bool'):lower()
  t = ALIAS[t] or t
  if not VALUED[t] and not LAYOUT[t] then return nil, 'unknown type "' .. t .. '"' end

  local s = { type = t }
  s.id = str(raw.id)
  if VALUED[t] or t == 'button' then
    if not s.id:match('^[%w_%.%-]+$') or #s.id > 64 then
      return nil, 'bad setting id "' .. s.id .. '" (letters, digits, _ . - only, 64 max)'
    end
  end
  s.label = str(raw.label, s.id)
  s.page = str(raw.page, 'General')
  if s.page == '' then s.page = 'General' end
  s.group = str(raw.group)
  s.desc = str(raw.desc)
  s.tag = str(raw.tag)
  s.show = str(raw.show)
  s.text = str(raw.text)  -- button caption
  -- translations: label_pt = '...', desc_pt = '...', options_pt = {...} and so on
  s.tr = {}
  for k, v in pairs(raw) do
    if type(k) == 'string' then
      local base, code = k:match('^(%a+)_(%a%a)$')
      if base == 'label' or base == 'desc' or base == 'group' or base == 'page' or base == 'text' or base == 'tag' then
        s.tr[k] = tostring(v)
      elseif (base == 'options' or base == 'actions') and type(v) == 'table' then
        local o = {}
        for i, name in ipairs(v) do o[i] = enc(tostring(name)) end
        s.tr['options_' .. code] = table.concat(o, '|')
      end
    end
  end

  if t == 'int' or t == 'float' then
    s.min = tonumber(raw.min) or 0
    s.max = tonumber(raw.max) or (t == 'int' and 100 or 1)
    if s.max < s.min then s.min, s.max = s.max, s.min end
    s.step = tonumber(raw.step) or (t == 'int' and 1 or 0)
    if s.step < 0 then s.step = 0 end
    if t == 'int' and s.step < 1 then s.step = 1 end
    s.fmt = raw.fmt and tostring(raw.fmt) or ''
    s.widget = (raw.widget == 'input' or raw.widget == 'meter') and raw.widget or 'slider'
  elseif t == 'enum' then
    local opts = {}
    if type(raw.options) == 'table' then
      for i, o in ipairs(raw.options) do opts[i] = tostring(o) end
    end
    if #opts == 0 then return nil, 'combo "' .. s.id .. '" needs at least one option' end
    s.options = opts
    s.widget = raw.widget == 'radio' and 'radio' or 'dropdown'
  elseif t == 'color' then
    s.alpha = raw.alpha == true or raw.alpha == '1'
  elseif t == 'text' then
    s.maxlen = clamp(floor(tonumber(raw.maxlen) or 64), 1, 250)
  elseif t == 'list' then
    -- captions of the buttons at the end of every row (4 at most)
    s.options = {}
    local acts = raw.actions or raw.options
    if type(acts) == 'table' then
      for i, a in ipairs(acts) do if i <= 4 then s.options[i] = tostring(a) end end
    end
  end

  if VALUED[t] then
    local d = M.coerce(s, raw.default)
    if d == nil then
      if t == 'bool' then d = false
      elseif t == 'int' or t == 'float' then d = clamp(0, s.min, s.max)
      elseif t == 'enum' then d = 1
      elseif t == 'color' then d = 0xFFFFFFFF
      elseif t == 'key' then d = 0
      else d = '' end
    end
    s.default = d
  end
  return s
end

-- forces a value into shape for this spec. returns nil + reason if it can't
function M.coerce(s, v)
  local t = s.type
  if t == 'bool' then
    if type(v) == 'boolean' then return v end
    if type(v) == 'number' then return v ~= 0 end
    if type(v) == 'string' then
      local l = v:lower()
      if l == '1' or l == 'true' or l == 'on' or l == 'yes' then return true end
      if l == '0' or l == 'false' or l == 'off' or l == 'no' or l == '' then return false end
    end
    return nil, 'expected true/false'
  elseif t == 'list' then
    if type(v) ~= 'string' then return nil, 'expected rows as text' end
    if #v > 32768 then v = v:sub(1, 32768) end
    return v
  elseif t == 'text' then
    if type(v) ~= 'string' and type(v) ~= 'number' then return nil, 'expected text' end
    v = tostring(v)
    if #v > s.maxlen then v = v:sub(1, s.maxlen) end
    return v
  elseif t == 'int' or t == 'float' or t == 'enum' or t == 'key' or t == 'color' then
    local n = tonumber(v)
    if n == nil or n ~= n or n == math.huge or n == -math.huge then return nil, 'expected a number' end
    if t == 'int' then
      n = clamp(floor(n + 0.5), s.min, s.max)
      if s.step > 1 then n = clamp(s.min + floor((n - s.min) / s.step + 0.5) * s.step, s.min, s.max) end
      return n
    elseif t == 'float' then
      n = clamp(n, s.min, s.max)
      if s.step > 0 then n = clamp(s.min + floor((n - s.min) / s.step + 0.5) * s.step, s.min, s.max) end
      return tonumber(('%.10g'):format(n))
    elseif t == 'enum' then
      return clamp(floor(n + 0.5), 1, #s.options)
    elseif t == 'key' then
      return clamp(floor(n + 0.5), 0, 255)
    else
      n = clamp(floor(n), 0, 0xFFFFFFFF)
      if not s.alpha then n = n % 0x1000000 + 0xFF000000 end
      return n
    end
  end
  return nil, 'this setting has no value'
end

function M.tostr(s, v)
  if s.type == 'bool' then return v and '1' or '0' end
  if s.type == 'text' or s.type == 'list' then return v end
  return numstr(v)
end

function M.same(s, a, b)
  if s.type == 'float' then return abs(a - b) < 1e-6 end
  return a == b
end

function M.specToWire(s)
  local w = {
    type = s.type, id = s.id, label = s.label, page = s.page, group = s.group,
    desc = s.desc, tag = s.tag, show = s.show, text = s.text,
  }
  if s.type == 'int' or s.type == 'float' then
    w.min, w.max, w.step = numstr(s.min), numstr(s.max), numstr(s.step)
    w.fmt, w.widget = s.fmt, s.widget
  elseif s.type == 'enum' or s.type == 'list' then
    local o = {}
    for i, name in ipairs(s.options) do o[i] = enc(name) end
    w.options = table.concat(o, '|')
    w.widget = s.widget
  elseif s.type == 'color' then
    w.alpha = s.alpha and '1' or '0'
  elseif s.type == 'text' then
    w.maxlen = numstr(s.maxlen)
  end
  if VALUED[s.type] then w.default = M.tostr(s, s.default) end
  if s.tr then for k, v in pairs(s.tr) do w[k] = v end end
  return w
end

function M.specFromWire(str)
  local w = unpackstr(str)
  if w.options then
    local o = {}
    for part in w.options:gmatch('[^|]+') do o[#o + 1] = dec(part) end
    w.options = o
  end
  return M.spec(w)
end

-- name_pt / description_pt on the mod itself
function M._metaTr(opts)
  local t = {}
  for k, v in pairs(opts) do
    if type(k) == 'string' then
      local base = k:match('^(%a+)_%a%a$')
      if base == 'name' or base == 'description' then t[k] = tostring(v) end
    end
  end
  return t
end

-- an icon is optional. a relative path is looked up next to the script that asked for it
function M._iconPath(p)
  if type(p) ~= 'string' or p == '' then return '' end
  if p:match('^%a:[\\/]') or p:match('^[\\/][\\/]') then return p end
  local ts = rawget(_G, 'thisScript')
  if ts then
    local ok, path = pcall(function() return ts().path end)
    if ok and path then
      local dir = path:match('^(.*)[\\/][^\\/]*$')
      if dir then return dir .. '\\' .. p end
    end
  end
  return p
end

-- --------------------------------------------------------------- the client

local host = { fns = nil, nextTry = 0, backoff = 1, open = false, lang = 'en' }
local mods = {}
local toggleHandlers = {}
local threadStarted = false

local function now() return os.clock() end

local function say(msg) print('[fawwk] ' .. msg) end

-- the menu is a native plugin (fawwkmenu.asi). we reach its exports through ffi.
local NATIVE_NAMES = { 'fawwk_hello', 'fawwk_register', 'fawwk_unregister', 'fawwk_poll',
                       'fawwk_dump', 'fawwk_set', 'fawwk_save', 'fawwk_show' }

local function nativeHost()
  local okf, ffi = pcall(require, 'ffi')
  if not okf then return nil end
  pcall(ffi.cdef, [[
    const char* fawwk_hello(void);
    const char* fawwk_register(const char* meta, const char* specs, const char* seeds);
    const char* fawwk_unregister(const char* id);
    const char* fawwk_poll(const char* id, const char* last);
    const char* fawwk_dump(const char* id);
    const char* fawwk_set(const char* id, const char* sid, const char* val);
    const char* fawwk_save(const char* id);
    const char* fawwk_show(const char* id);
  ]])

  local function wrap(lib, names)
    local ex = {}
    for _, fname in ipairs(NATIVE_NAMES) do
      local f = lib[fname]
      ex[fname] = function(...)
        local r = f(...)
        if r == nil then return nil end
        return ffi.string(r)
      end
    end
    return ex
  end

  -- the asi loader already loaded the plugin, wherever it keeps its files. asking windows for the
  -- module that is in memory works no matter which folder it sits in
  pcall(ffi.cdef, [[
    void* GetModuleHandleA(const char* name);
    void* GetProcAddress(void* module, const char* name);
  ]])
  local okh, handle = pcall(function() return ffi.C.GetModuleHandleA('fawwkmenu.asi') end)
  if okh and handle ~= nil then
    local sigs = {
      fawwk_hello = 'const char*(*)(void)',
      fawwk_register = 'const char*(*)(const char*, const char*, const char*)',
      fawwk_unregister = 'const char*(*)(const char*)',
      fawwk_poll = 'const char*(*)(const char*, const char*)',
      fawwk_dump = 'const char*(*)(const char*)',
      fawwk_set = 'const char*(*)(const char*, const char*, const char*)',
      fawwk_save = 'const char*(*)(const char*)',
      fawwk_show = 'const char*(*)(const char*)',
    }
    local lib, complete = {}, true
    for fname, sig in pairs(sigs) do
      local addr = ffi.C.GetProcAddress(handle, fname)
      if addr == nil then complete = false break end
      lib[fname] = ffi.cast(sig, addr)
    end
    if complete then
      local ok2, hello = pcall(function() return ffi.string(lib.fawwk_hello()) end)
      if ok2 and hello:match('^%d+|') then return wrap(lib) end
    end
  end

  -- fall back to loading it by name or path
  local candidates = { 'fawwkmenu.asi' }
  local gd = rawget(_G, 'getGameDirectory')
  if gd then
    local ok, dir = pcall(gd)
    if ok and dir then
      candidates[#candidates + 1] = dir .. '\\fawwkmenu.asi'
      candidates[#candidates + 1] = dir .. '\\scripts\\fawwkmenu.asi'
    end
  end
  for _, name in ipairs(candidates) do
    local ok, lib = pcall(ffi.load, name)
    if ok and lib then
      local ok2, hello = pcall(function() return ffi.string(lib.fawwk_hello()) end)
      if ok2 and hello:match('^%d+|') then return wrap(lib) end
    end
  end
  return nil
end

local finder = nativeHost
local function findHost() return finder() end

local function hcall(name, ...)
  local f = host.fns and host.fns[name]
  if not f then return nil, 'no host' end
  local ok, r = pcall(f, ...)
  if not ok or type(r) ~= 'string' then
    host.fns = nil
    return nil, tostring(r)
  end
  return r
end

local Mod = {}
Mod.__index = Mod

local function safely(fn, ...)
  local ok, err = pcall(fn, ...)
  if not ok then say('callback error: ' .. tostring(err)) end
end

function M.mod(opts)
  if type(opts) ~= 'table' or not opts.id then error('fawwk.mod needs { id = "...", name = "..." }', 2) end
  local id = tostring(opts.id):lower()
  if not id:match('^[a-z0-9_%.%-]+$') or #id < 3 or #id > 64 then
    error('fawwk: bad mod id "' .. id .. '" (lowercase letters, digits, _ . - , 3 to 64 chars)', 2)
  end
  if mods[id] then return mods[id] end

  local m = setmetatable({
    id = id,
    name = str(opts.name, id),
    category = str(opts.category, 'General'),
    version = str(opts.version),
    author = str(opts.author),
    description = str(opts.description),
    icon = M._iconPath(opts.icon),
    tr = M._metaTr(opts),
    specs = {}, byId = {}, values = {},
    handlers = {}, anyHandlers = {}, readyHandlers = {},
    seeds = {}, seq = 0, online = false, dirty = true, everReady = false,
    nextRegister = 0, lastError = nil,
    _page = 'General', _group = '',
  }, Mod)

  -- m.v.something reads the live value, m.v.something = x sets it
  m.v = setmetatable({}, {
    __index = m.values,
    __newindex = function(_, k, val) m:set(k, val) end,
  })

  mods[id] = m
  if not opts.manual then M._startThread() end
  return m
end

function Mod:add(raw)
  local copy = {}
  for k, v in pairs(raw) do copy[k] = v end
  if copy.page == nil then copy.page = self._page end
  if copy.group == nil then copy.group = self._group end
  if copy.page == self._page and self._pageTr then
    for code, text in pairs(self._pageTr) do if copy['page_' .. code] == nil then copy['page_' .. code] = text end end
  end
  if copy.group == self._group and self._groupTr then
    for code, text in pairs(self._groupTr) do if copy['group_' .. code] == nil then copy['group_' .. code] = text end end
  end
  local s, err = M.spec(copy)
  if not s then error('fawwk [' .. self.id .. ']: ' .. err, 3) end
  if s.id == '' then s.id = '~' .. (#self.specs + 1) end
  if self.byId[s.id] then error('fawwk [' .. self.id .. ']: duplicate setting id "' .. s.id .. '"', 3) end
  self.specs[#self.specs + 1] = s
  self.byId[s.id] = s
  if VALUED[s.type] then self.values[s.id] = s.default end
  self.dirty = true
  return self
end

local function merge(o, base)
  if o then for k, v in pairs(o) do base[k] = v end end
  return base
end

function Mod:toggle(id, label, default, o)
  return self:add(merge(o, { type = 'bool', id = id, label = label, default = default }))
end
function Mod:slider(id, label, default, lo, hi, o)
  return self:add(merge(o, { type = 'float', id = id, label = label, default = default, min = lo, max = hi }))
end
-- a bar the player can only look at. the mod fills it with cfg:set(id, value); it is never stored
function Mod:meter(id, label, default, lo, hi, o)
  return self:add(merge(o, { type = 'float', widget = 'meter', id = id, label = label, default = default, min = lo, max = hi }))
end
-- a list the player can look through, with a button per action on every row.
-- fill it with cfg:rows(id, { {'title', 'detail'}, ... }) and react with cfg:onRow(id, function(row, action) end)
function Mod:list(id, label, o)
  o = o or {}
  local handler = o.onAction
  o.onAction = nil
  self:add(merge(o, { type = 'list', id = id, label = label, default = '' }))
  if handler then self:onRow(id, handler) end
  return self
end

function Mod:rows(id, rows)
  local out = {}
  for i, r in ipairs(rows or {}) do
    local title, detail = r, ''
    if type(r) == 'table' then title, detail = r[1], r[2] or '' end
    title = tostring(title):gsub('[%c]', ' ')
    detail = tostring(detail):gsub('[%c]', ' ')
    out[i] = detail ~= '' and (title .. '\t' .. detail) or title
  end
  return self:set(id, table.concat(out, '\n'))
end

function Mod:onRow(id, fn)
  self.rowHandlers = self.rowHandlers or {}
  self.rowHandlers[id] = self.rowHandlers[id] or {}
  table.insert(self.rowHandlers[id], fn)
  return self
end

function Mod:int(id, label, default, lo, hi, o)
  return self:add(merge(o, { type = 'int', id = id, label = label, default = default, min = lo, max = hi }))
end
function Mod:combo(id, label, default, options, o)
  return self:add(merge(o, { type = 'enum', id = id, label = label, default = default, options = options }))
end
function Mod:color(id, label, default, o)
  return self:add(merge(o, { type = 'color', id = id, label = label, default = default }))
end
function Mod:key(id, label, default, o)
  return self:add(merge(o, { type = 'key', id = id, label = label, default = default }))
end
function Mod:text(id, label, default, o)
  return self:add(merge(o, { type = 'text', id = id, label = label, default = default }))
end
function Mod:button(id, label, fn, o)
  self:add(merge(o, { type = 'button', id = id, label = label }))
  if fn then self:on(id, fn) end
  return self
end
function Mod:header(label, o) return self:add(merge(o, { type = 'header', label = label })) end
function Mod:separator(o) return self:add(merge(o, { type = 'separator' })) end
function Mod:note(text, o) return self:add(merge(o, { type = 'note', label = text })) end

-- the second argument is optional: cfg:page('Keys', {pt = 'Teclas', es = 'Teclas'})
function Mod:page(name, tr) self._page = name; self._group = ''; self._pageTr = tr; self._groupTr = nil; return self end
function Mod:group(name, tr) self._group = name or ''; self._groupTr = tr; return self end

function Mod:get(id) return self.values[id] end

function Mod:option(id)
  local s = self.byId[id]
  if s and s.type == 'enum' then return s.options[self.values[id]] end
end

-- 0..255 each, handy for renderDrawBox and friends
function Mod:rgba(id)
  local c = self.values[id] or 0
  return floor(c / 0x10000) % 256, floor(c / 0x100) % 256, c % 256, floor(c / 0x1000000) % 256
end

function Mod:on(id, fn)
  if id == '*' then
    self.anyHandlers[#self.anyHandlers + 1] = fn
  else
    local list = self.handlers[id] or {}
    list[#list + 1] = fn
    self.handlers[id] = list
  end
  return self
end

function Mod:onReady(fn)
  if self.everReady then safely(fn) else self.readyHandlers[#self.readyHandlers + 1] = fn end
  return self
end

-- legacy config import. only used the first time the menu sees this mod
function Mod:seed(tbl)
  for id, v in pairs(tbl) do
    local s = self.byId[id]
    if s and VALUED[s.type] then
      local nv = M.coerce(s, v)
      if nv ~= nil then
        self.seeds[id] = M.tostr(s, nv)
        if not self.online then self.values[id] = nv end
      end
    end
  end
  self.dirty = true
  return self
end

function Mod:_apply(id, nv, quiet)
  local s = self.byId[id]
  local old = self.values[id]
  if old ~= nil and M.same(s, old, nv) then return false end
  self.values[id] = nv
  if not quiet then
    local list = self.handlers[id]
    if list then for _, fn in ipairs(list) do safely(fn, nv, old) end end
    for _, fn in ipairs(self.anyHandlers) do safely(fn, id, nv, old) end
  end
  return true
end

function Mod:set(id, v, quiet)
  local s = self.byId[id]
  if not s or not VALUED[s.type] then return false, 'unknown setting' end
  local nv, err = M.coerce(s, v)
  if nv == nil then return false, err end
  if (s.widget == 'meter' or s.type == 'list') and self.values[id] == nv then return true end  -- nothing to tell the menu
  self:_apply(id, nv, quiet)
  if self.online then hcall('fawwk_set', self.id, id, M.tostr(s, nv)) end
  return true
end

function Mod:reset(id)
  local s = self.byId[id]
  if s and VALUED[s.type] then return self:set(id, s.default) end
  return false
end

function Mod:isOnline() return self.online end

function Mod:open() hcall('fawwk_show', self.id) end

-- shows one page of the mod in a small window of its own instead of the whole menu
function Mod:popup(page) hcall('fawwk_show', self.id .. '\n' .. (page or '')) end

function Mod:save() hcall('fawwk_save', self.id) end

-- hotkey helpers. ignored while the menu is open so typing doesn't trigger stuff
function Mod:pressed(id)
  local vk = self.values[id]
  local f = rawget(_G, 'wasKeyPressed')
  if not vk or vk == 0 or host.open or not f then return false end
  return f(vk)
end

function Mod:down(id)
  local vk = self.values[id]
  local f = rawget(_G, 'isKeyDown')
  if not vk or vk == 0 or host.open or not f then return false end
  return f(vk)
end

function Mod:unregister()
  if self.online then hcall('fawwk_unregister', self.id) end
  self.online = false
  mods[self.id] = nil
end

function Mod:_press(id)
  local list = self.handlers[id]
  if list then for _, fn in ipairs(list) do safely(fn) end end
  for _, fn in ipairs(self.anyHandlers) do safely(fn, id) end
end

function Mod:_applyValues(packed)
  for id, sv in pairs(unpackstr(packed)) do
    local s = self.byId[id]
    if s and VALUED[s.type] then
      local nv = M.coerce(s, sv)
      if nv ~= nil then self:_apply(id, nv) end
    end
  end
end

function Mod:_register()
  local t = now()
  if t < self.nextRegister then return end
  local metaT = {
    id = self.id, name = self.name, category = self.category,
    version = self.version, author = self.author, description = self.description, icon = self.icon,
  }
  for k, v in pairs(self.tr or {}) do metaT[k] = v end
  local meta = pack(metaT)
  local specLines = {}
  for i, s in ipairs(self.specs) do specLines[i] = pack(M.specToWire(s)) end
  local r = hcall('fawwk_register', meta, table.concat(specLines, '\n'), pack(self.seeds))
  if not r then return end
  local ls = lines(r)
  if ls[1] ~= 'ok' then
    local msg = r:gsub('^err:%s*', '')
    if msg == 'menu not ready' then self.nextRegister = t + 1; return end
    if msg ~= self.lastError then say(self.id .. ': ' .. msg); self.lastError = msg end
    self.nextRegister = t + 10
    return
  end
  self.lastError = nil
  self.seq = tonumber(ls[2]) or 0
  self.online = true
  self.dirty = false
  self:_applyValues(ls[3] or '')
  if not self.everReady then
    self.everReady = true
    for _, fn in ipairs(self.readyHandlers) do safely(fn) end
    self.readyHandlers = {}
  end
end

function Mod:_tick()
  if not self.online or self.dirty then
    if host.fns then self:_register() end
    return
  end
  local r = hcall('fawwk_poll', self.id, numstr(self.seq))
  if not r then self.online = false; return end
  if r == 'unknown' then self.online = false; self.dirty = true; return end

  local ls = lines(r)
  local _, open, head, resync, lang = (ls[1] or ''):match('^([^,]*),(%d),(%d+),(%d),?([%a%-]*)$')
  if not open then return end
  if lang and lang ~= '' then host.lang = lang end
  local wasOpen = host.open
  host.open = (open == '1')
  if wasOpen ~= host.open then
    for _, fn in ipairs(toggleHandlers) do safely(fn, host.open) end
  end

  if resync == '1' then
    local d = hcall('fawwk_dump', self.id)
    if d then
      local dl = lines(d)
      self.seq = tonumber(dl[1]) or self.seq
      self:_applyValues(dl[2] or '')
    end
    return
  end

  for i = 2, #ls do
    local seq, kind, id, val = ls[i]:match('^(%d+)|(%a)|([^|]*)|(.*)$')
    if seq then
      local s = self.byId[id]
      if s then
        if kind == 'v' and VALUED[s.type] then
          local nv = M.coerce(s, dec(val))
          if nv ~= nil then self:_apply(id, nv) end
        elseif kind == 'b' and s.type == 'list' then
          local row, action = dec(val):match('^(%d+),(%d+)$')
          local hs = self.rowHandlers and self.rowHandlers[id]
          if row and hs then
            for _, fn in ipairs(hs) do safely(fn, tonumber(row), tonumber(action)) end
          end
        elseif kind == 'b' then
          self:_press(id)
        end
      end
    end
  end
  self.seq = tonumber(head) or self.seq
end

-- ----------------------------------------------------------- module level api

function M.poll()
  local t = now()
  if not host.fns then
    if t < host.nextTry then return end
    local ex = findHost()
    if ex then
      host.fns = ex
      say('connected to fawwk menu')
      host.backoff = 1
      for _, m in pairs(mods) do m.online = false; m.dirty = true end
    else
      host.nextTry = t + host.backoff
      host.backoff = min(host.backoff * 2, 15)
      if not host.warned and t > 20 then
        host.warned = true
        say('fawwkmenu.asi not found in the game (is the plugin installed and loaded?), settings stay on their defaults')
      end
      return
    end
  end
  for _, m in pairs(mods) do m:_tick() end
end

function M._startThread()
  if threadStarted then return end
  threadStarted = true
  local lt = rawget(_G, 'lua_thread')
  if not lt then
    local ok, r = pcall(require, 'lua_thread')
    if ok then lt = r end
  end
  local w = rawget(_G, 'wait')
  if lt and lt.create and w then
    lt.create(function()
      while true do
        w(0)
        local ok, err = pcall(M.poll)
        if not ok then say('poll error: ' .. tostring(err)) end
      end
    end)
  else
    say('no lua_thread available, call fawwk.poll() from your main loop')
  end
end

function M.isOpen() return host.open end

-- the language the player picked in the menu: 'en', 'pt', 'es', 'ru'. use it for your own messages
function M.language() return host.lang end
function M.isConnected() return host.fns ~= nil end
function M.onToggle(fn) toggleHandlers[#toggleHandlers + 1] = fn end

-- clean up when the owning script dies so the menu doesn't keep a dead entry
local addHandler = rawget(_G, 'addEventHandler')
if addHandler and rawget(_G, 'thisScript') then
  local me = thisScript()
  addHandler('onScriptTerminate', function(scr)
    if scr == me then
      for _, m in pairs(mods) do
        if m.online then pcall(m.unregister, m) end
      end
    end
  end)
end

-- test hooks, the test suite swaps the way we find the menu
function M._setFinder(fn) finder = fn end
M._host = host
M._mods = mods

return M
