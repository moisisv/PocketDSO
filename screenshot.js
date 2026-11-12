#!/usr/bin/env node
/**
 * PocketDSO screen capture (Node.js, no dependencies)
 * ===================================================
 * Takes screenshots of the Waveshare ESP32-C6-Touch-LCD-1.47 running the
 * oscilloscope sketch, over Wi-Fi. Requires the sketch (with the built-in
 * /api/screenshot endpoint) to be flashed and connected to Wi-Fi.
 *
 * Usage
 * -----
 *   node screenshot.js                          # 1 shot from pocketdso.local -> PNG
 *   node screenshot.js --host 192.168.1.50      # use the device IP (see Serial Monitor)
 *   node screenshot.js --out shot.png           # explicit output file
 *   node screenshot.js --watch 2                # capture every 2 s (Ctrl-C to stop)
 *   node screenshot.js --rot 0                  # keep raw portrait orientation
 *   node screenshot.js --flip-h / --flip-v      # mirror the image
 *   node screenshot.js --raw screen.bin         # save the undecoded firmware payload
 *
 * Notes
 * -----
 *   * PNG is encoded with Node's built-in zlib — no packages to install.
 *   * If "pocketdso.local" does not resolve (mDNS), use the device IP instead
 *     (shown in the Serial Monitor / on the SYS page).
 */

'use strict';

const fs = require('fs');
const zlib = require('zlib');

const MAGIC = 'DSOS';

// ---------------------------------------------------------------- transport --
async function fetchRaw(host, timeoutMs = 10000) {
  const url = `http://${host.replace(/^https?:\/\//, '').replace(/\/+$/, '')}/api/screenshot`;
  let buf;
  try {
    const res = await fetch(url, { signal: AbortSignal.timeout(timeoutMs) });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    buf = Buffer.from(await res.arrayBuffer());
  } catch (e) {
    throw new Error(
      `Could not reach ${url} (${e.message}).\n` +
      'Check that the ESP32 is on Wi-Fi and use its IP address:\n' +
      '  node screenshot.js --host <device-ip>'
    );
  }
  if (buf.length < 12 || buf.toString('latin1', 0, 4) !== MAGIC) {
    throw new Error(
      `Unexpected response from device (${buf.length} bytes). ` +
      'Is the sketch with the /api/screenshot endpoint flashed?'
    );
  }
  const fbw = buf.readUInt16LE(4);
  const fbh = buf.readUInt16LE(6);
  const rot = buf.readUInt16LE(8);
  const palCount = buf.readUInt16LE(10);
  const palette = [];
  for (let i = 0; i < palCount; ++i) palette.push(buf.readUInt16LE(12 + i * 2));
  const off = 12 + palCount * 2;
  const fb = buf.subarray(off, off + fbw * fbh);
  if (fb.length < fbw * fbh) {
    throw new Error(`Truncated framebuffer: got ${fb.length} of ${fbw * fbh} bytes.`);
  }
  return { fbw, fbh, rot, palette, fb };
}

// ---------------------------------------------------------------- decoding --
/** RGB565 -> 3-byte RGB LUT (256 entries x 3). */
function buildLut(palette) {
  const lut = Buffer.alloc(palette.length * 3);
  palette.forEach((c, i) => {
    const r5 = (c >> 11) & 0x1f, g6 = (c >> 5) & 0x3f, b5 = c & 0x1f;
    lut[i * 3 + 0] = (r5 * 255 + 15) / 31 | 0;
    lut[i * 3 + 1] = (g6 * 255 + 31) / 63 | 0;
    lut[i * 3 + 2] = (b5 * 255 + 15) / 31 | 0;
  });
  return lut;
}

/** Indexed framebuffer -> array of top-down RGB row Buffers. */
function toRgbRows(fbw, fbh, palette, fb) {
  const lut = buildLut(palette);
  const rows = [];
  for (let y = 0; y < fbh; ++y) {
    const row = Buffer.allocUnsafe(fbw * 3);
    for (let x = 0; x < fbw; ++x) {
      const idx = fb[y * fbw + x] * 3;
      lut.copy(row, x * 3, idx, idx + 3);
    }
    rows.push(row);
  }
  return rows;
}

/** Rotate top-down RGB rows (w x h) by 0/90/180/270 degrees CCW. */
function rotateRows(rows, w, h, degCcW) {
  if (degCcW === 0) return { rows, w, h };
  if (degCcW === 180) {
    return { rows: rows.map(r => Buffer.from(r).reverse()), w, h };
  }
  if (degCcW !== 90 && degCcW !== 270) {
    throw new Error('--rot must be one of 0, 90, 180, 270');
  }
  const nw = h, nh = w;
  const out = [];
  for (let y = 0; y < nh; ++y) out.push(Buffer.allocUnsafe(nw * 3));
  for (let r = 0; r < h; ++r) {
    const row = rows[r];
    for (let c = 0; c < w; ++c) {
      const px = row.subarray(c * 3, c * 3 + 3);
      if (degCcW === 90) {           // CCW: (c, r) -> (r, wOld-1-c)
        px.copy(out[w - 1 - c], r * 3);
      } else {                       // 270 CCW (CW): (c, r) -> (hOld-1-r, c)
        px.copy(out[c], (h - 1 - r) * 3);
      }
    }
  }
  return { rows: out, w: nw, h: nh };
}

/** Device rotation value (canvas rotation as configured in the sketch) -> deg CCW. */
function defaultRotationCcW(rot) {
  return ({ 0: 0, 1: 90, 2: 180, 3: 270 })[rot] ?? 90;
}

// ------------------------------------------------------------------ saving --
// Minimal PNG encoder (8-bit RGB, filter 0), using built-in zlib.
const CRC_TABLE = (() => {
  const t = new Int32Array(256);
  for (let n = 0; n < 256; ++n) {
    let c = n;
    for (let k = 0; k < 8; ++k) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
    t[n] = c;
  }
  return t;
})();

function crc32(buf) {
  let c = -1;
  for (let i = 0; i < buf.length; ++i) c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return (c ^ -1) >>> 0;
}

function pngChunk(type, data) {
  const out = Buffer.alloc(12 + data.length);
  out.writeUInt32BE(data.length, 0);
  out.write(type, 4, 'latin1');
  data.copy(out, 8);
  out.writeUInt32BE(crc32(out.subarray(4, 8 + data.length)), 8 + data.length);
  return out;
}

function encodePng(width, height, rows) {
  const stride = width * 3;
  const raw = Buffer.alloc((stride + 1) * height);
  for (let y = 0; y < height; ++y) {
    raw[y * (stride + 1)] = 0;                 // filter: none
    rows[y].copy(raw, y * (stride + 1) + 1);
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8;   // bit depth
  ihdr[9] = 2;   // color type: truecolor RGB
  const signature = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
  return Buffer.concat([
    signature,
    pngChunk('IHDR', ihdr),
    pngChunk('IDAT', zlib.deflateSync(raw, { level: 9 })),
    pngChunk('IEND', Buffer.alloc(0)),
  ]);
}

function savePng(pathOut, w, h, rows) {
  fs.writeFileSync(pathOut, encodePng(w, h, rows));
  return pathOut;
}

// -------------------------------------------------------------------- args --
function parseArgs(argv) {
  const args = { host: 'pocketdso.local', rot: -1, watch: 0, flipH: false, flipV: false, raw: null, out: null };
  for (let i = 2; i < argv.length; ++i) {
    const a = argv[i];
    const need = (name) => { if (++i >= argv.length) throw new Error(`--${name} needs a value`); return argv[i]; };
    if (a === '--host') args.host = need('host');
    else if (a === '--out') args.out = need('out');
    else if (a === '--raw') args.raw = need('raw');
    else if (a === '--watch') args.watch = parseFloat(need('watch'));
    else if (a === '--rot') {
      args.rot = parseInt(need('rot'), 10);
      if (![0, 90, 180, 270].includes(args.rot)) throw new Error('--rot must be 0, 90, 180 or 270');
    } else if (a === '--flip-h') args.flipH = true;
    else if (a === '--flip-v') args.flipV = true;
    else if (a === '--help' || a === '-h') { console.log(fs.readFileSync(__filename, 'utf8').split('*/')[0] + '*/'); process.exit(0); }
    else throw new Error(`Unknown argument: ${a}`);
  }
  return args;
}

function timestamp() {
  const d = new Date(), p = (n, w = 2) => String(n).padStart(w, '0');
  return `${d.getFullYear()}${p(d.getMonth() + 1)}${p(d.getDate())}-${p(d.getHours())}${p(d.getMinutes())}${p(d.getSeconds())}`;
}

function applyFlips(rows, flipH, flipV) {
  let out = rows;
  if (flipH) out = out.map(r => {
    const n = Buffer.allocUnsafe(r.length);
    for (let x = 0; x < r.length / 3; ++x) r.copy(n, x * 3, r.length - (x + 1) * 3, r.length - x * 3);
    return n;
  });
  if (flipV) out = out.slice().reverse();
  return out;
}

const sleep = (ms) => new Promise((res) => setTimeout(res, ms));

// -------------------------------------------------------------------- main --
async function main() {
  const args = parseArgs(process.argv);
  const { fbw, fbh, rot, palette, fb } = await fetchRaw(args.host);

  if (args.raw) {
    const hdr = Buffer.alloc(12);
    hdr.write(MAGIC, 0, 'latin1');
    hdr.writeUInt16LE(fbw, 4);
    hdr.writeUInt16LE(fbh, 6);
    hdr.writeUInt16LE(rot, 8);
    hdr.writeUInt16LE(palette.length, 10);
    const pal = Buffer.alloc(palette.length * 2);
    palette.forEach((v, i) => pal.writeUInt16LE(v, i * 2));
    fs.writeFileSync(args.raw, Buffer.concat([hdr, pal, fb]));
    console.log(`Raw payload saved to ${args.raw}`);
    return;
  }

  const deg = args.rot >= 0 ? args.rot : defaultRotationCcW(rot);
  let { rows, w, h } = rotateRows(toRgbRows(fbw, fbh, palette, fb), fbw, fbh, deg);
  rows = applyFlips(rows, args.flipH, args.flipV);

  if (args.watch > 0) {
    console.log(`Capturing every ${args.watch}s from ${args.host} — press Ctrl-C to stop.`);
    const stamp = timestamp();
    for (let n = 0; ; ++n) {
      const out = args.out
        ? (n === 0 ? args.out : args.out.replace(/(\.png)?$/i, `-${n}.png`))
        : `dso_${stamp}-${String(n).padStart(3, '0')}.png`;
      if (n > 0) {
        const s2 = await fetchRaw(args.host);
        const r2 = rotateRows(toRgbRows(s2.fbw, s2.fbh, s2.palette, s2.fb), s2.fbw, s2.fbh, deg);
        savePng(out, r2.w, r2.h, applyFlips(r2.rows, args.flipH, args.flipV));
      } else {
        savePng(out, w, h, rows);
      }
      console.log(`  ${out}`);
      await sleep(args.watch * 1000);
    }
  } else {
    const out = args.out || `dso_${timestamp()}.png`;
    const pathOut = /\.png$/i.test(out) ? out : `${out}.png`;
    savePng(pathOut, w, h, rows);
    const kb = (fs.statSync(pathOut).size / 1024).toFixed(1);
    console.log(`Saved ${pathOut} (${w}x${h} px, ${kb} KB)`);
  }
}

// allow unit-testing from other scripts
module.exports = { fetchRaw, toRgbRows, rotateRows, defaultRotationCcW, encodePng, applyFlips };

if (require.main === module) {
  main().catch((e) => { console.error(String(e.message || e)); process.exit(1); });
}
