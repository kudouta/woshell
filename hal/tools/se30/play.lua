-- 調査用: 決めたフレームでキーを押し・離しながら、画面の保存と変数の記録をする
-- 環境変数:
--   KEYS        "フレーム=ポート|フィールド|1 or 0;..."
--               例: "1200=:macadb:KEY2|l  L|1;1204=:macadb:KEY2|l  L|0"
--   SNAP_FRAMES "1290,1450" (画面を保存するフレーム)
--   TRACE_OUT / TRACE_VARS / TRACE_EVERY  (trace_vars.lua と同じ。省略可)
--   POKES       "フレーム=16進アドレス:値;..." (32 ビットで書き込む。調査用)
--   END_FRAME   終了するフレーム
local keys = {}
for frame, port, field, value in string.gmatch(os.getenv("KEYS") or "", "(%d+)=([^|]+)|([^|]+)|(%d)") do
  table.insert(keys, { frame = tonumber(frame), port = port, field = field, value = tonumber(value) })
end
local pokes = {}
for frame, addr, value in string.gmatch(os.getenv("POKES") or "", "(%d+)=(%x+):(%d+)") do
  table.insert(pokes, { frame = tonumber(frame), addr = tonumber(addr, 16), value = tonumber(value) })
end
-- TYPE_FILE: 1 行に「フレーム番号 タブ 文字列」。文字列を MAME の自然なキーボード入力で打つ
--            (文字列の中の \n は Return)。シェルの調査用
local types = {}
if os.getenv("TYPE_FILE") then
  for line in io.lines(os.getenv("TYPE_FILE")) do
    local frame, text = string.match(line, "^(%d+)\t(.*)$")
    if frame then
      text = string.gsub(text, "\\n", "\n")
      table.insert(types, { frame = tonumber(frame), text = text })
    end
  end
end
local nmi_frames = {}
for n in string.gmatch(os.getenv("NMI_FRAMES") or "", "%d+") do nmi_frames[tonumber(n)] = true end
local nmi_entry = os.getenv("NMI_ENTRY") and tonumber(os.getenv("NMI_ENTRY"), 16)
local nmi_burst = tonumber(os.getenv("NMI_BURST") or "1")   -- 1 フレームに積む回数 (続けて入る NMI のまね)
local snaps = {}
for n in string.gmatch(os.getenv("SNAP_FRAMES") or "", "%d+") do snaps[tonumber(n)] = true end
local end_frame = tonumber(os.getenv("END_FRAME") or "3000")

local out, vars, every = nil, {}, tonumber(os.getenv("TRACE_EVERY") or "10")
if os.getenv("TRACE_OUT") then
  out = io.open(os.getenv("TRACE_OUT"), "w")
  for name, addr, size in string.gmatch(os.getenv("TRACE_VARS") or "", "(%w+)=(%x+):(%d)") do
    table.insert(vars, { name = name, addr = tonumber(addr, 16), size = tonumber(size) })
  end
  local header = "frame"
  for _, v in ipairs(vars) do header = header .. " " .. v.name end
  out:write(header .. "\n")
end

local mem = manager.machine.devices[":maincpu"].spaces["program"]
local frames = 0

-- ASC_OUT: ASC の FIFO A に書いたバイトをファイルに記録する (音の比較用。MAME の wav は 48kHz に
-- 変換されるため、補充の時刻が 1 サンプルずれただけで一致しなくなる)
local asc_out, asc_taps = nil, {}
if os.getenv("ASC_OUT") then
  asc_out = io.open(os.getenv("ASC_OUT"), "wb")
  local function tap(offset, data, mask)
    for lane = 3, 0, -1 do
      local m = 0xFF << (lane * 8)
      if mask & m ~= 0 then asc_out:write(string.char((data >> (lane * 8)) & 0xFF)) end
    end
  end
  -- 起動中にメモリの割り当てが変わるとタップが外れるので、フレームごとに付け直す
  function asc_retap()
    for _, t in ipairs(asc_taps) do t:remove() end
    asc_taps = {}
    for _, base in ipairs({ 0x50014000, 0x50F14000 }) do
      table.insert(asc_taps, mem:install_write_tap(base, base + 0x3FF, "asc_fifo_a", tap))
    end
  end
  asc_retap()
end
emu.register_frame_done(function()
  frames = frames + 1
  if asc_out then asc_retap() end
  for _, k in ipairs(keys) do
    if k.frame == frames then
      local f = manager.machine.ioport.ports[k.port].fields[k.field]
      if k.value ~= 0 then f:set_value(1) else f:clear_value() end
    end
  end
  for _, p in ipairs(pokes) do
    if p.frame == frames then mem:write_u32(p.addr, p.value) end
  end
  for _, t in ipairs(types) do
    if t.frame == frames then manager.machine.natkeyboard:post(t.text) end
  end
  -- NMI_FRAMES / NMI_ENTRY: MAME の SE/30 には割り込みボタンがないので、決めたフレームで
  -- レベル7 の例外を作って入れる (スタックに形式 0 のフレーム: SR, PC, ベクタの位置 $7C を積み、
  -- SR を割り込み禁止の 7 にして NMI_ENTRY (16 進) へ飛ぶ)。NMI_BURST 回積むと、間隔ほぼ 0 で続けて入る
  if nmi_frames[frames] and nmi_entry then
    local st = manager.machine.devices[":maincpu"].state
    for i = 1, nmi_burst do
      local sp, sr, pc = st["SP"].value, st["SR"].value, st["PC"].value
      sp = sp - 8
      mem:write_u16(sp, sr)
      mem:write_u32(sp + 2, pc)
      mem:write_u16(sp + 6, 0x007C)
      st["SP"].value = sp
      st["SR"].value = (sr & 0xF8FF) | 0x2700
      st["PC"].value = nmi_entry
    end
  end
  if snaps[frames] then manager.machine.video:snapshot() end
  if out and frames % every == 0 then
    local line = tostring(frames)
    for _, v in ipairs(vars) do
      local val
      if v.size == 1 then val = mem:read_u8(v.addr)
      elseif v.size == 2 then val = mem:read_u16(v.addr)
      else val = mem:read_i32(v.addr) end
      line = line .. " " .. tostring(val)
    end
    out:write(line .. "\n")
    out:flush()
  end
  if frames == end_frame then
    if out then out:close() end
    -- DUMP_OUT / DUMP_ADDR (16 進) / DUMP_LEN: 終了時にメモリをファイルへ書き出す (調査用)
    if os.getenv("DUMP_OUT") then
      local d = io.open(os.getenv("DUMP_OUT"), "wb")
      local a = tonumber(os.getenv("DUMP_ADDR"), 16)
      for i = 0, tonumber(os.getenv("DUMP_LEN")) - 1 do d:write(string.char(mem:read_u8(a + i))) end
      d:close()
    end
    if asc_out then asc_out:close() end
    -- IMAGES_OUT: 終了時に、各ドライブにイメージが入っているかを書き出す (取り出しの確認用)
    if os.getenv("IMAGES_OUT") then
      local o = io.open(os.getenv("IMAGES_OUT"), "w")
      for tag, img in pairs(manager.machine.images) do
        o:write(tag .. " " .. (img.exists and img.filename or "(empty)") .. "\n")
      end
      o:close()
    end
    manager.machine:exit()
  end
end)
