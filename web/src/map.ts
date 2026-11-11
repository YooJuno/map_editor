import { parse, stringify } from 'yaml';
import { zipSync, unzipSync, strToU8, strFromU8 } from 'fflate';

export const MAX_PIXELS = 32 * 1024 * 1024;
export const MAX_CLOUD_BYTES = 20 * 1024 * 1024;
export type Layer = 'map' | 'keepout' | 'speed';
export type Point = { x: number; y: number };
export type Rect = Point & { width: number; height: number };
export type Meta = {
  resolution: number;
  origin: [number, number, number];
  negate: number;
  occupied_thresh: number;
  free_thresh: number;
  mode: string;
  [key: string]: unknown;
};
export type MapDoc = {
  name: string;
  width: number;
  height: number;
  pixels: Uint8Array;
  keepout?: Uint8Array;
  speed?: Uint8Array;
  meta: Meta;
};
export type NamedFile = { name: string; bytes: Uint8Array };
export const defaults = (): Meta => ({
  resolution: 0.05,
  origin: [0, 0, 0],
  negate: 0,
  occupied_thresh: 0.65,
  free_thresh: 0.196,
  mode: 'trinary',
});
export const clone = (doc: MapDoc): MapDoc => structuredClone(doc);
export const layerBytes = (doc: MapDoc, layer: Layer) =>
  layer === 'map' ? doc.pixels : doc[layer];
export function setLayer(doc: MapDoc, layer: Layer, bytes: Uint8Array) {
  if (layer === 'map') doc.pixels = bytes;
  else doc[layer] = bytes;
}
const basename = (name: string) => name.replaceAll('\\', '/').split('/').pop()!;
export const safeName = (name: string) =>
  name
    .trim()
    .replace(/[<>:"/\\|?*\x00-\x1f]/g, '_')
    .replace(/\.\.+/g, '_')
    .replace(/^[. ]+|[. ]+$/g, '')
    .slice(0, 100) || 'map';
function dimensions(width: number, height: number) {
  if (
    !Number.isSafeInteger(width) ||
    !Number.isSafeInteger(height) ||
    width < 1 ||
    height < 1 ||
    width > 16384 ||
    height > 16384 ||
    width * height > MAX_PIXELS
  )
    throw Error('PGM: 한 변 16,384 px, 최대 32 Mi 픽셀의 지도를 지원합니다.');
}
export function readPgm(bytes: Uint8Array) {
  let p = 0;
  const ws = (c: number) => c === 32 || (c >= 9 && c <= 13);
  const token = () => {
    while (p < bytes.length) {
      if (ws(bytes[p])) {
        p++;
        continue;
      }
      if (bytes[p] === 35) {
        while (p < bytes.length && bytes[p] !== 10 && bytes[p] !== 13) p++;
        continue;
      }
      break;
    }
    const start = p;
    while (p < bytes.length && !ws(bytes[p]) && bytes[p] !== 35) p++;
    if (start === p) throw Error('PGM: 파일이 잘렸습니다.');
    return new TextDecoder('ascii').decode(bytes.subarray(start, p));
  };
  const uint = () => {
    const value = token();
    if (!/^\d+$/.test(value)) throw Error('PGM: 잘못된 숫자입니다.');
    return Number(value);
  };
  const magic = token();
  if (magic !== 'P5' && magic !== 'P2') throw Error('PGM P2/P5 파일을 선택하세요.');
  const width = uint(),
    height = uint();
  dimensions(width, height);
  if (uint() !== 255) throw Error('PGM: Maxval 255만 지원합니다.');
  const pixels = new Uint8Array(width * height);
  if (magic === 'P5') {
    if (!ws(bytes[p])) throw Error('PGM: 헤더 구분자가 없습니다.');
    if (bytes[p++] === 13 && bytes[p] === 10) p++;
    if (bytes.length - p !== pixels.length) throw Error('PGM: 이미지 크기와 픽셀 수가 다릅니다.');
    pixels.set(bytes.subarray(p));
  } else {
    for (let i = 0; i < pixels.length; i++) {
      const v = uint();
      if (v > 255) throw Error('PGM: 픽셀값 범위는 0~255입니다.');
      pixels[i] = v;
    }
    while (p < bytes.length) {
      if (ws(bytes[p])) p++;
      else if (bytes[p] === 35) {
        while (p < bytes.length && bytes[p] !== 10) p++;
      } else throw Error('PGM: 픽셀 데이터가 너무 많습니다.');
    }
  }
  return { width, height, pixels };
}
export function encodePgm(width: number, height: number, pixels: Uint8Array) {
  dimensions(width, height);
  if (pixels.length !== width * height) throw Error('픽셀 수가 맞지 않습니다.');
  const header = strToU8(`P5\n${width} ${height}\n255\n`),
    out = new Uint8Array(header.length + pixels.length);
  out.set(header);
  out.set(pixels, header.length);
  return out;
}
export function validateMeta(meta: Meta, mask = false) {
  if (
    !Number.isFinite(meta.resolution) ||
    meta.resolution <= 0 ||
    !Array.isArray(meta.origin) ||
    meta.origin.length !== 3 ||
    meta.origin.some((v) => !Number.isFinite(v))
  )
    throw Error('해상도와 원점을 확인하세요.');
  if (
    ![0, 1].includes(meta.negate) ||
    !(0 < meta.free_thresh && meta.free_thresh < meta.occupied_thresh && meta.occupied_thresh < 1)
  )
    throw Error('점유 임계값을 확인하세요.');
  if (meta.mode !== 'trinary' && !(mask && meta.mode === 'raw'))
    throw Error('지도는 trinary, 마스크는 raw 또는 trinary를 지원합니다.');
}
export function readYaml(bytes: Uint8Array, mask = false): Meta {
  if (bytes.length > 1024 * 1024) throw Error('YAML은 1 MiB 이하여야 합니다.');
  const data = parse(strFromU8(bytes), { maxAliasCount: 0, uniqueKeys: true });
  if (!data || Array.isArray(data) || typeof data !== 'object')
    throw Error('YAML 형식이 올바르지 않습니다.');
  const meta = { ...defaults(), ...data } as Meta;
  validateMeta(meta, mask);
  return meta;
}
export function unknownGray(meta: Meta) {
  const p = (meta.free_thresh + meta.occupied_thresh) / 2;
  return Math.round((meta.negate ? p : 1 - p) * 255);
}
export function exportFiles(doc: MapDoc): Record<string, Uint8Array> {
  validateMeta(doc.meta);
  const stem = safeName(doc.name),
    files: Record<string, Uint8Array> = {};
  for (const layer of ['map', 'keepout', 'speed'] as const) {
    const bytes = layerBytes(doc, layer);
    if (!bytes) continue;
    const name = stem + (layer === 'map' ? '' : `_${layer}`);
    files[`${name}.pgm`] = encodePgm(doc.width, doc.height, bytes);
    files[`${name}.yaml`] = strToU8(
      stringify({
        ...doc.meta,
        image: `${name}.pgm`,
        ...(layer === 'map' ? {} : { mode: 'raw', negate: 0 }),
      }),
    );
  }
  files['map-editor.json'] = strToU8(JSON.stringify({ version: 1, map: `${stem}.yaml` }));
  return files;
}
export const pack = (doc: MapDoc) => zipSync(exportFiles(doc), { level: 6 });
export function unpack(bytes: Uint8Array): MapDoc {
  if (bytes.length > 128 * 1024 * 1024) throw Error('ZIP 파일이 너무 큽니다.');
  let total = 0,
    count = 0;
  const files = unzipSync(bytes, {
    filter: (entry) => {
      count++;
      total += entry.originalSize;
      if (
        count > 12 ||
        total > 110 * 1024 * 1024 ||
        entry.name.includes('..') ||
        entry.name.startsWith('/')
      )
        throw Error('지원하지 않는 ZIP 구성입니다.');
      return /\.(pgm|ya?ml|json)$/i.test(entry.name);
    },
  });
  return importFiles(Object.entries(files).map(([name, bytes]) => ({ name, bytes })));
}
export function importFiles(input: NamedFile[]): MapDoc {
  if (input.length === 1 && /\.zip$/i.test(input[0].name)) return unpack(input[0].bytes);
  if (input.length > 12) throw Error('지도 한 쌍과 마스크를 선택하세요.');
  const files = new Map<string, NamedFile>();
  for (const file of input) {
    const key = basename(file.name).toLowerCase();
    if (files.has(key)) throw Error('같은 이름의 파일이 있습니다.');
    files.set(key, file);
  }
  const yamls = input.filter((f) => /\.ya?ml$/i.test(f.name));
  let mainYaml: NamedFile | undefined;
  const manifest = files.get('map-editor.json');
  if (manifest) {
    if (manifest.bytes.length > 1024 * 1024) throw Error('지도 목록 파일이 너무 큽니다.');
    const data = JSON.parse(strFromU8(manifest.bytes));
    if (data.version !== 1 || typeof data.map !== 'string')
      throw Error('지원하지 않는 지도 패키지입니다.');
    mainYaml = files.get(basename(data.map).toLowerCase());
    if (!mainYaml) throw Error('ZIP 안에 지도 YAML이 없습니다.');
  } else {
    const candidates = yamls.filter((f) => !/_(keepout|speed)\.ya?ml$/i.test(f.name));
    if (candidates.length > 1) throw Error('한 번에 지도 한 쌍만 선택하세요.');
    mainYaml = candidates[0];
  }
  const meta = mainYaml ? readYaml(mainYaml.bytes) : defaults();
  const pgms = input.filter(
    (f) => /\.pgm$/i.test(f.name) && !/_(keepout|speed)\.pgm$/i.test(f.name),
  );
  const mainPgm = mainYaml
    ? typeof meta.image === 'string'
      ? files.get(basename(meta.image).toLowerCase())
      : undefined
    : pgms.length === 1
      ? pgms[0]
      : undefined;
  if (!mainPgm) throw Error('YAML이 가리키는 PGM을 함께 선택하세요.');
  const image = readPgm(mainPgm.bytes);
  const doc: MapDoc = {
    ...image,
    meta,
    name: basename(mainYaml?.name || mainPgm.name).replace(/\.(pgm|ya?ml)$/i, '') || 'map',
  };
  for (const layer of ['keepout', 'speed'] as const) {
    const yaml =
      files.get(`${doc.name}_${layer}.yaml`.toLowerCase()) ||
      files.get(`${doc.name}_${layer}.yml`.toLowerCase());
    if (!yaml) continue;
    const m = readYaml(yaml.bytes, true);
    const file =
      typeof m.image === 'string' ? files.get(basename(m.image).toLowerCase()) : undefined;
    if (!file) throw Error('마스크 PGM을 함께 선택하세요.');
    const img = readPgm(file.bytes);
    if (
      img.width !== doc.width ||
      img.height !== doc.height ||
      m.resolution !== meta.resolution ||
      m.origin.some((v, i) => Math.abs(v - meta.origin[i]) > 1e-8)
    )
      throw Error('마스크 격자가 원본 지도와 다릅니다.');
    if (m.mode === 'trinary')
      img.pixels = img.pixels.map((v) => {
        const p = m.negate ? v / 255 : 1 - v / 255;
        return p > m.occupied_thresh ? 100 : p < m.free_thresh ? 0 : 255;
      });
    if (img.pixels.some((v) => v > 100 && v !== 255))
      throw Error('마스크 값은 0~100 또는 255여야 합니다.');
    doc[layer] = img.pixels;
  }
  return doc;
}
export function worldAt(p: Point, doc: MapDoc): Point {
  const [x, y, yaw] = doc.meta.origin,
    r = doc.meta.resolution,
    c = Math.cos(yaw),
    s = Math.sin(yaw);
  return {
    x: x + r * (p.x * c - (doc.height - p.y) * s),
    y: y + r * (p.x * s + (doc.height - p.y) * c),
  };
}
export function originAt(p: Point, doc: MapDoc): Meta {
  const r = doc.meta.resolution,
    a = doc.meta.origin[2],
    c = Math.cos(a),
    s = Math.sin(a);
  return {
    ...doc.meta,
    origin: [-r * (p.x * c - (doc.height - p.y) * s), -r * (p.x * s + (doc.height - p.y) * c), a],
  };
}
export function crop(doc: MapDoc, rect: Rect): MapDoc {
  const x = Math.max(0, Math.floor(rect.x)),
    y = Math.max(0, Math.floor(rect.y));
  const width = Math.min(doc.width - x, Math.floor(rect.width)),
    height = Math.min(doc.height - y, Math.floor(rect.height));
  dimensions(width, height);
  const out = { ...doc, width, height },
    origin = worldAt({ x, y: y + height }, doc);
  out.meta = { ...doc.meta, origin: [origin.x, origin.y, doc.meta.origin[2]] };
  for (const layer of ['map', 'keepout', 'speed'] as const) {
    const source = layerBytes(doc, layer);
    if (!source) continue;
    const bytes = new Uint8Array(width * height);
    for (let row = 0; row < height; row++)
      bytes.set(
        source.subarray((row + y) * doc.width + x, (row + y) * doc.width + x + width),
        row * width,
      );
    setLayer(out, layer, bytes);
  }
  return out;
}
export function rotate(doc: MapDoc, degrees: number): MapDoc {
  if (!Number.isFinite(degrees)) throw Error('회전 각도를 확인하세요.');
  const a = (degrees * Math.PI) / 180,
    c = Math.cos(a),
    s = Math.sin(a);
  const w = doc.width,
    h = doc.height;
  const corners = [
    [0, 0],
    [w, 0],
    [0, h],
    [w, h],
  ].map(([x, y]) => ({ x: c * x - s * y, y: s * x + c * y }));
  const minX = Math.min(...corners.map((p) => p.x)),
    minY = Math.min(...corners.map((p) => p.y));
  const width = Math.ceil(Math.max(...corners.map((p) => p.x)) - minX - 1e-9),
    height = Math.ceil(Math.max(...corners.map((p) => p.y)) - minY - 1e-9);
  dimensions(width, height);
  const bottom = { x: s * (height + minY) + c * minX, y: c * (height + minY) - s * minX },
    origin = worldAt(bottom, doc);
  const out: MapDoc = {
    ...doc,
    width,
    height,
    meta: { ...doc.meta, origin: [origin.x, origin.y, doc.meta.origin[2] + a] },
  };
  for (const layer of ['map', 'keepout', 'speed'] as const) {
    const source = layerBytes(doc, layer);
    if (!source) continue;
    const bytes = new Uint8Array(width * height).fill(layer === 'map' ? unknownGray(doc.meta) : 0);
    for (let y = 0; y < height; y++)
      for (let x = 0; x < width; x++) {
        const dx = x + 0.5 + minX,
          dy = y + 0.5 + minY,
          sx = Math.floor(c * dx + s * dy),
          sy = Math.floor(-s * dx + c * dy);
        if (sx >= 0 && sy >= 0 && sx < w && sy < h) bytes[y * width + x] = source[sy * w + sx];
      }
    setLayer(out, layer, bytes);
  }
  return out;
}
export function stroke(
  bytes: Uint8Array,
  w: number,
  h: number,
  a: Point,
  b: Point,
  size: number,
  value: number,
): Rect {
  const radius = Math.max(0.5, size / 2),
    steps = Math.max(1, Math.ceil(Math.hypot(b.x - a.x, b.y - a.y) * 2));
  const left = Math.max(0, Math.floor(Math.min(a.x, b.x) - radius)),
    top = Math.max(0, Math.floor(Math.min(a.y, b.y) - radius));
  const right = Math.min(w, Math.ceil(Math.max(a.x, b.x) + radius)),
    bottom = Math.min(h, Math.ceil(Math.max(a.y, b.y) + radius));
  for (let i = 0; i <= steps; i++) {
    const cx = a.x + ((b.x - a.x) * i) / steps,
      cy = a.y + ((b.y - a.y) * i) / steps;
    for (let y = Math.max(0, Math.floor(cy - radius)); y < Math.min(h, Math.ceil(cy + radius)); y++)
      for (
        let x = Math.max(0, Math.floor(cx - radius));
        x < Math.min(w, Math.ceil(cx + radius));
        x++
      )
        if ((x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 <= radius ** 2) bytes[y * w + x] = value;
  }
  return { x: left, y: top, width: right - left, height: bottom - top };
}
export function polygon(bytes: Uint8Array, w: number, h: number, points: Point[], value: number) {
  if (points.length < 3) return;
  const top = Math.max(0, Math.floor(Math.min(...points.map((p) => p.y)))),
    bottom = Math.min(h, Math.ceil(Math.max(...points.map((p) => p.y))));
  for (let y = top; y < bottom; y++) {
    const cuts: number[] = [],
      scan = y + 0.5;
    for (let i = 0; i < points.length; i++) {
      const a = points[i],
        b = points[(i + 1) % points.length];
      if ((a.y <= scan && b.y > scan) || (b.y <= scan && a.y > scan))
        cuts.push(a.x + ((scan - a.y) * (b.x - a.x)) / (b.y - a.y));
    }
    cuts.sort((a, b) => a - b);
    for (let i = 0; i + 1 < cuts.length; i += 2)
      bytes.fill(
        value,
        y * w + Math.max(0, Math.min(w, Math.ceil(cuts[i] - 0.5))),
        y * w + Math.max(0, Math.min(w, Math.ceil(cuts[i + 1] - 0.5))),
      );
  }
}
export function flood(
  bytes: Uint8Array,
  w: number,
  h: number,
  point: Point,
  value: number,
  select = false,
) {
  const x = Math.floor(point.x),
    y = Math.floor(point.y);
  if (x < 0 || y < 0 || x >= w || y >= h) return null;
  const initial = y * w + x,
    target = bytes[initial];
  if (select && (target === 0 || target === 255)) return null;
  if (!select && target === value) return null;
  const mask = new Uint8Array(bytes.length),
    stack = [initial];
  let left = x,
    right = x,
    top = y,
    bottom = y;
  while (stack.length) {
    const seed = stack.pop()!,
      row = Math.floor(seed / w);
    let px = seed % w;
    if (mask[seed] || bytes[seed] !== target) continue;
    while (px > 0 && bytes[row * w + px - 1] === target && !mask[row * w + px - 1]) px--;
    let above = false,
      below = false;
    for (; px < w && bytes[row * w + px] === target && !mask[row * w + px]; px++) {
      const index = row * w + px;
      mask[index] = 1;
      if (!select) bytes[index] = value;
      left = Math.min(left, px);
      right = Math.max(right, px);
      top = Math.min(top, row);
      bottom = Math.max(bottom, row);
      const up = row > 0 && !mask[index - w] && bytes[index - w] === target;
      const down = row + 1 < h && !mask[index + w] && bytes[index + w] === target;
      if (up && !above) stack.push(index - w);
      if (down && !below) stack.push(index + w);
      above = up;
      below = down;
    }
  }
  return { mask, bounds: { x: left, y: top, width: right - left + 1, height: bottom - top + 1 } };
}
