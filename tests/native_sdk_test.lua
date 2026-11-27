-- the lua sdk against the real c++ core. run through tests/run_native.sh
-- globals from the bridge: HOST (the string exports), T (stand-in for the ui), ROOT
package.path = ROOT .. '/lua/?.lua;' .. package.path
unpack = unpack or table.unpack

local fails, passes = 0, 0
local function check(name, cond, extra)
  if cond then passes = passes + 1 else fails = fails + 1; print('FAIL: ' .. name .. (extra and (' -> ' .. tostring(extra)) or '')) end
end

local clock = 0
os.clock = function() return clock end
local fawwk = require 'fawwk'

-- only strings/numbers cross, same as the real thing
local alive = true
local function strict()
  local w = {}
  for k, fn in pairs(HOST) do
    w[k] = function(...)
      if not alive then error('plugin is gone') end
      for i = 1, select('#', ...) do
        local t = type((select(i, ...)))
        assert(t == 'string' or t == 'number', 'arg ' .. i .. ' of ' .. k .. ' is ' .. t)
      end
      local r = fn(...)
      assert(type(r) == 'string', k .. ' returned ' .. type(r))
      return r
    end
  end
  return w
end

local hostUp = true
fawwk._setFinder(function() if hostUp then return strict() end end)

local function step(n)
  for _ = 1, n or 1 do clock = clock + 0.016; T.clock(clock); fawwk.poll() end
end
local function ui(mod, id, v) return T.set(mod, id, tostring(v)) end

-- ---------------------------------------------------------------- offline
local cfg = fawwk.mod { id = 'test.mod', name = 'Test Mod', category = 'Tests', version = '2', author = 'me' }
cfg:toggle('enabled', 'Enabled', true)
cfg:slider('power', 'Power', 1.5, 0, 5, { step = 0.1 })
cfg:int('count', 'Count', 3, 1, 9)
cfg:combo('mode', 'Mode', 2, { 'a', 'b', 'c' })
cfg:color('tint', 'Tint', 0xFF336699)
cfg:key('hotkey', 'Key', 0x4E)
cfg:text('name', 'Name', 'bob')
cfg:button('boom', 'Boom')
cfg:toggle('adv', 'Adv', false)
cfg:slider('hidden', 'Hidden', 1, 0, 2, { show = 'adv' })
cfg:page('Second'):int('later', 'Later', 1, 0, 5)

hostUp = false
step(3)
check('offline gives defaults', cfg.v.power == 1.5 and cfg.v.enabled == true and cfg.v.name == 'bob')
check('offline not online', not cfg:isOnline())
cfg.v.count = 700
check('offline set clamps', cfg.v.count == 9)
cfg.v.count = 3

-- ------------------------------------------------------------- connects
local seen = {}
cfg:on('power', function(v, old) seen[#seen + 1] = { v, old } end)
local readyCalls = 0
cfg:onReady(function() readyCalls = readyCalls + 1 end)

hostUp = true
clock = clock + 20
step(2)
check('connects', cfg:isOnline())
check('ready fired once', readyCalls == 1)
check('mod in store', T.hasmod('test.mod') and T.name('test.mod') == 'Test Mod')
check('first page', T.pages('test.mod') == 'General')

ui('test.mod', 'power', 2.5)
step(1)
check('ui change reaches mod', cfg.v.power == 2.5, cfg.v.power)
check('onChange value+old', seen[#seen] and seen[#seen][1] == 2.5 and seen[#seen][2] == 1.5)

ui('test.mod', 'power', 2.26)
step(1)
check('host snaps to step', cfg.v.power == 2.3, cfg.v.power)

cfg.v.count = 5
check('mod set reaches store', T.value('test.mod', 'count') == '5', T.value('test.mod', 'count'))
cfg.v.count = 99
check('host also clamps', T.value('test.mod', 'count') == '9')
cfg.v.mode = 3
check('enum set', T.value('test.mod', 'mode') == '3')
cfg.v.enabled = false
check('bool set', T.value('test.mod', 'enabled') == '0')
cfg.v.enabled = true

local pressed = 0
cfg:on('boom', function() pressed = pressed + 1 end)
T.press('test.mod', 'boom')
step(1)
check('button fires', pressed == 1)

for i = 1, 50 do ui('test.mod', 'power', i / 10) end
step(1)
check('coalesced to last', cfg.v.power == 5, cfg.v.power)
check('one power event kept', T.ringcount('test.mod', 'power') == 1, T.ringcount('test.mod', 'power'))

for i = 1, 400 do
  ui('test.mod', 'name', 'n' .. i)
  ui('test.mod', 'count', (i % 9) + 1)
  T.press('test.mod', 'boom')
end
step(1)
check('resync lands on final state', cfg.v.name == 'n400' and tostring(cfg.v.count) == T.value('test.mod', 'count'), cfg.v.name)

T.open(true)
step(1)
check('open flag visible', fawwk.isOpen() == true)
check('pressed ignored while open', cfg:pressed('hotkey') == false)
local toggled
fawwk.onToggle(function(o) toggled = o end)
T.open(false)
step(1)
check('toggle handler', toggled == false)

check('hidden when cond false', T.visible('test.mod', 'hidden') == false)
ui('test.mod', 'adv', 1)
check('shown when cond true', T.visible('test.mod', 'hidden') == true)

-- bad input from the ui side
check('bad number rejected', ui('test.mod', 'power', 'abc') == false)
check('unknown setting rejected', ui('test.mod', 'nope', 1) == false)

-- ------------------------------------------------------------- persistence
ui('test.mod', 'name', 'multi\nline = x')
ui('test.mod', 'tint', 0xFF112233)
T.flush()
local file = T.file('test.mod')
check('file written', file ~= false)
check('defaults not stored', file and not file:find('hotkey=', 1, true))
check('color stored', file and file:find('tint=4279312947', 1, true) ~= nil, file)
check('text escaped', file and file:find('name=multi\\nline = x', 1, true) ~= nil, file)

T.restart()
step(1)
check('notices restart', not cfg:isOnline())
step(2)
check('re-registers', cfg:isOnline())
check('values come back from disk', cfg.v.tint == 0xFF112233 and cfg.v.name == 'multi\nline = x', cfg.v.name)
check('untouched stays default', cfg.v.hotkey == 0x4E)

-- autosave after a delay
ui('test.mod', 'count', 4)
clock = clock + 0.3; T.clock(clock); T.tick()
local before = T.file('test.mod')
clock = clock + 1; T.clock(clock); T.tick()
local after = T.file('test.mod')
check('autosave waits then writes', before ~= after and after:find('count=4', 1, true) ~= nil)

-- changes are live but only reach the disk when saved (autosave off)
T.autosave(false)
T.flush()
ui('test.mod', 'count', 7)
clock = clock + 5; T.clock(clock); T.tick(); step(1)
check('no autosave: value live', cfg.v.count == 7, cfg.v.count)
check('no autosave: marked unsaved', T.unsaved('test.mod') == true)
check('no autosave: file untouched', T.file('test.mod'):find('count=7', 1, true) == nil)
check('revert returns changed count', T.revert('test.mod') >= 1)
step(1)
check('revert restores disk value', cfg.v.count == 4, cfg.v.count)
check('revert clears unsaved', T.unsaved('test.mod') == false)
ui('test.mod', 'count', 8)
T.flush()
check('explicit save writes', T.file('test.mod'):find('count=8', 1, true) ~= nil and not T.unsaved('test.mod'))
T.autosave(true)

-- default reset removes the stored line
T.reset('test.mod', 'count')
T.flush()
check('reset drops the line', not T.file('test.mod'):find('count=', 1, true))

-- ------------------------------------------------------------------ seeds
local legacy = fawwk.mod { id = 'legacy.mod', name = 'Legacy' }
legacy:slider('gain', 'Gain', 1, 0, 10)
legacy:toggle('on', 'On', true)
legacy:seed({ gain = 7, on = false })
clock = clock + 30
step(3)
check('seed applied on first run', legacy.v.gain == 7 and legacy.v.on == false, legacy.v.gain)
check('host holds the seeded value', T.value('legacy.mod', 'gain') == '7')
T.flush()
check('seed file written', T.file('legacy.mod') ~= false)
T.reset('legacy.mod', 'gain')
T.flush()
T.restart()
step(1); step(3)
check('seed not reapplied after file exists', legacy.v.gain == 1, legacy.v.gain)

-- a mod registered while another file exists on disk for a removed setting keeps that line
T.disk_set('cfg/keep.mod.ini', '[mod]\nid=keep.mod\n\n[values]\nold_setting=5\nkept=1\n')
local keep = fawwk.mod { id = 'keep.mod', name = 'Keep' }
keep:toggle('kept', 'Kept', false)
step(3)
check('reads existing file', keep.v.kept == true)
ui('keep.mod', 'kept', 0)
T.flush()
check('unknown saved lines survive', T.file('keep.mod'):find('old_setting=5', 1, true) ~= nil)

-- meter and radio
local mm = fawwk.mod { id = 'meter.mod', name = 'Meter mod' }
mm:meter('level', 'Level', 0, 0, 100)
mm:combo('style', 'Style', 1, { 'a', 'b' }, { widget = 'radio' })
step(3)
mm:set('level', 70)
step(3)
check('meter holds the pushed value', mm.v.level == 70)
check('meter change is not "unsaved"', T.unsaved('meter.mod') == false)
T.flush()
check('meter is never written to disk', (T.file('meter.mod') or ''):find('level', 1, true) == nil)
mm:set('style', 2)
step(3)
check('radio combo still stores', mm.v.style == 2)
mm:list('stash', 'Stash', { actions = { 'Use', 'Drop' } })
local got = {}
mm:onRow('stash', function(r, a) got[#got + 1] = r .. ':' .. a end)
step(3)
mm:rows('stash', { { 'Pistol', '9mm' }, 'Sandwich' })
step(3)
check('list text reaches the host', (HOST.fawwk_dump('meter.mod') or ''):find('Pistol', 1, true) ~= nil)
check('list is not stored', (T.file('meter.mod') or ''):find('stash', 1, true) == nil)
check('list keeps its rows', mm.v.stash == 'Pistol\t9mm\nSandwich')
T.pressRow('meter.mod', 'stash', 2, 1)
step(3)
check('row action reaches the mod', got[1] == '2:1', got[1])
mm:unregister()

-- host side validation
local bad = HOST.fawwk_register(fawwk.pack({ id = 'x' }), 'type=bool&id=bad%20id', '')
check('short id is an err string', bad:sub(1, 3) == 'err', bad)
bad = HOST.fawwk_register(fawwk.pack({ id = 'good.id' }), 'type=bool&id=bad%20id', '')
check('bad setting id is an err string', bad:sub(1, 3) == 'err', bad)
bad = HOST.fawwk_register(fawwk.pack({ id = 'good.id' }), 'type=combo&id=c', '')
check('combo without options errs', bad:sub(1, 3) == 'err', bad)
check('poll unknown mod', HOST.fawwk_poll('nobody.here', '0') == 'unknown')
check('hello looks right', HOST.fawwk_hello():match('^1|') ~= nil)

-- sdk side validation
check('bad mod id throws', not pcall(function() fawwk.mod({ id = 'Bad ID' }) end))
local m2 = fawwk.mod { id = 'dupe.test' }
m2:toggle('a', 'A', true)
check('duplicate id throws', not pcall(function() m2:toggle('a', 'A', true) end))

-- translations
local tr = fawwk.mod { id = 'test.lang', name = 'Nitro', name_pt = 'Nitro PT', description = 'd', category = 'Cars' }
tr:page('Keys', {pt = 'Teclas'})
tr:toggle('on', 'Enabled', true, {desc = 'Turns it on', label_pt = 'Ligado', desc_pt = 'Liga'})
tr:combo('mode', 'Mode', 1, {'Slow', 'Fast'}, {options_pt = {'Lento', 'Rápido'}})
tr:group('Tuning', {pt = 'Ajustes finos'})
tr:int('lvl', 'Level', 3, 1, 9, {})
step(4)
check('lang mod registered', T.hasmod('test.lang'))
check('lang default english', T.lang('en') == 'en' and T.shown('test.lang', 'on', 'label') == 'Enabled')
check('lang pt label', T.lang('pt') == 'pt' and T.shown('test.lang', 'on', 'label') == 'Ligado', T.shown('test.lang', 'on', 'label'))
check('lang pt desc', T.shown('test.lang', 'on', 'desc') == 'Liga')
check('lang pt page', T.shown('test.lang', 'on', 'page') == 'Teclas', T.shown('test.lang', 'on', 'page'))
check('lang pt option', T.shown('test.lang', 'mode', 'option1') == 'Rápido', T.shown('test.lang', 'mode', 'option1'))
check('lang pt group', T.shown('test.lang', 'lvl', 'group') == 'Ajustes finos', T.shown('test.lang', 'lvl', 'group'))
check('lang pt mod name', T.modname('test.lang') == 'Nitro PT', T.modname('test.lang'))
check('lang falls back for missing text', T.shown('test.lang', 'lvl', 'label') == 'Level')
check('lang es falls back to the mod original', T.lang('es') == 'es' and T.shown('test.lang', 'on', 'label') == 'Enabled')
check('menu text pt', T.lang('pt') == 'pt' and T.T('Store') == 'Guardar', T.T('Store'))
check('menu text unknown key stays', T.T('no such sentence') == 'no such sentence')
check('lang file adds text', T.langfile('pt', '# c\nStore => Salvar\n\nnot a line\n') == 1 and T.T('Store') == 'Salvar', T.T('Store'))
check('unknown language is english', T.lang('xx') == 'en')
step(4)
T.lang('ru'); step(4)
check('mods learn the language', fawwk.language() == 'ru', fawwk.language())
T.lang('en'); step(4)
check('and back', fawwk.language() == 'en', fawwk.language())
tr:unregister()

-- unregister removes it
cfg:unregister()
check('unregister removes mod', not T.hasmod('test.mod'))

-- plugin missing mid game: sdk keeps last values and doesn't throw
alive = false
step(3)
check('survives dead host', legacy.v.gain == 1)

print(('native sdk tests: %d passed, %d failed'):format(passes, fails))
if fails > 0 then os.exit(1) end
