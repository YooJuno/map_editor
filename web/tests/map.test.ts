import { describe, expect, it } from 'vitest';
import { strToU8, zipSync, unzipSync } from 'fflate';
import { readFileSync } from 'node:fs';
import {
  readPgm,
  encodePgm,
  readYaml,
  defaults,
  importFiles,
  exportFiles,
  pack,
  unpack,
  crop,
  rotate,
  worldAt,
  originAt,
  stroke,
  polygon,
  flood,
  safeName,
  type MapDoc,
} from '../src/map';
const map = (): MapDoc => ({
  name: 'Test map',
  width: 4,
  height: 3,
  pixels: Uint8Array.from([0, 10, 32, 255, 10, 20, 30, 40, 50, 60, 70, 80]),
  meta: {
    ...defaults(),
    origin: [2, -3, 0.4],
    custom: { source: 'slam', number: 1.12345678901234 },
  },
  keepout: Uint8Array.from([0, 100, 100, 0, 0, 0, 0, 0, 100, 0, 0, 255]),
  speed: Uint8Array.from([0, 30, 30, 0, 0, 60, 60, 0, 0, 0, 0, 255]),
});
describe('PGM / YAML', () => {
  it('preserves every binary value, including header-like whitespace and #', () => {
    const pixels = Uint8Array.from([10, 32, 13, 35, 255, 0]);
    expect(readPgm(encodePgm(3, 2, pixels)).pixels).toEqual(pixels);
  });
  it('supports CRLF binary headers without consuming pixel whitespace', () => {
    const head = strToU8('P5\r\n2 1\r\n255\r\n');
    const bytes = new Uint8Array(head.length + 2);
    bytes.set(head);
    bytes.set([10, 32], head.length);
    expect(readPgm(bytes).pixels).toEqual(Uint8Array.of(10, 32));
  });
  it('reads P2 comments and validates integers', () => {
    expect(readPgm(strToU8('P2\n# demo\n3 1\n255\n0 205 # gray\n255\n')).pixels).toEqual(
      Uint8Array.of(0, 205, 255),
    );
    expect(() => readPgm(strToU8('P2 1 1 255 2.5'))).toThrow();
  });
  it.each([
    'P5 2 2 255\nabc',
    'P2 1 1 255 256',
    'P2 1 1 255 -1',
    'P2 1 1 255 2 3',
    'P5 1 1 65535\n00',
    'P5 9999999999 10 255\n',
  ])('rejects invalid pixels and dimensions: %s', (text) =>
    expect(() => readPgm(strToU8(text))).toThrow(),
  );
  it('preserves optional YAML fields and double precision', () => {
    const d = map();
    const result = unpack(pack(d));
    expect(result.meta).toEqual({ ...d.meta, image: 'Test map.pgm' });
    expect(result.pixels).toEqual(d.pixels);
  });
  it('rejects invalid metadata and unsupported map modes', () => {
    for (const text of [
      'resolution: 0',
      'origin: [0, 0]',
      'free_thresh: .9',
      'mode: raw',
      'negate: 2',
      'resolution: .nan',
      '- test',
    ])
      expect(() => readYaml(strToU8(text))).toThrow();
  });
  it('refuses YAML alias expansion', () =>
    expect(() => readYaml(strToU8('a: &a [1, 2]\nb: *a'))).toThrow());
  it('can open the actual repository example', () => {
    const doc = importFiles(
      ['yaml', 'pgm'].map((ext) => ({
        name: `office.${ext}`,
        bytes: new Uint8Array(
          readFileSync(new URL(`../../examples/office.${ext}`, import.meta.url)),
        ),
      })),
    );
    expect(doc.width * doc.height).toBe(doc.pixels.length);
    expect(doc.meta.resolution).toBe(0.05);
  });
  it('requires the exact YAML-referenced image', () =>
    expect(() =>
      importFiles([
        { name: 'a.yaml', bytes: strToU8('image: missing.pgm') },
        { name: 'other.pgm', bytes: encodePgm(1, 1, Uint8Array.of(0)) },
      ]),
    ).toThrow());
  it('uses documented defaults when opening only PGM', () => {
    const doc = importFiles([{ name: 'only.pgm', bytes: encodePgm(1, 1, Uint8Array.of(10)) }]);
    expect(doc.meta).toEqual(defaults());
  });
  it('downloads standard file pairs and raw masks', () => {
    const d = map(),
      files = exportFiles(d);
    expect(Object.keys(files)).toHaveLength(7);
    expect(readYaml(files['Test map_keepout.yaml'], true).mode).toBe('raw');
    const result = unpack(pack(d));
    expect(result.keepout).toEqual(d.keepout);
    expect(result.speed).toEqual(d.speed);
    expect(result.meta.origin).toEqual(d.meta.origin);
  });
  it('rejects masks with mismatched grids and invalid pixel values', () => {
    const files = exportFiles(map());
    files['Test map_speed.pgm'] = encodePgm(4, 3, new Uint8Array(12).fill(201));
    expect(() =>
      importFiles(Object.entries(files).map(([name, bytes]) => ({ name, bytes }))),
    ).toThrow();
  });
  it('rejects ZIP path traversal and ambiguous file pairs', () => {
    expect(() => unpack(zipSync({ '../x.pgm': encodePgm(1, 1, Uint8Array.of(0)) }))).toThrow();
    expect(() =>
      importFiles([
        { name: 'a.yaml', bytes: strToU8('image: a.pgm') },
        { name: 'b.yaml', bytes: strToU8('image: b.pgm') },
      ]),
    ).toThrow();
  });
  it('sanitizes download names without changing map data', () =>
    expect(safeName('../../bad:map')).toBe('____bad_map'));
  it('can reopen ZIPs after sanitizing unusual map names', () => {
    const d = map();
    d.name = '../../bad:map';
    expect(unpack(pack(d)).pixels).toEqual(d.pixels);
  });
  it('ZIP export includes no absolute filesystem paths', () => {
    const files = unzipSync(pack(map()));
    expect(Object.keys(files).every((name) => !name.includes('/') && !name.includes('\\'))).toBe(
      true,
    );
  });
});
describe('geometry keeps all layers aligned in world coordinates', () => {
  it('crop offsets the origin and crops both masks', () => {
    const d = map(),
      out = crop(d, { x: 1, y: 1, width: 2, height: 2 });
    expect([...out.pixels]).toEqual([20, 30, 60, 70]);
    expect([...out.speed!]).toEqual([60, 60, 0, 0]);
    for (const p of [
      { x: 0, y: 0 },
      { x: 2, y: 2 },
    ]) {
      const before = worldAt({ x: p.x + 1, y: p.y + 1 }, d),
        after = worldAt(p, out);
      expect(after.x).toBeCloseTo(before.x, 10);
      expect(after.y).toBeCloseTo(before.y, 10);
    }
  });
  it('origin placement makes the chosen point world (0,0)', () => {
    const d = map(),
      p = { x: 2.5, y: 1.5 };
    const result = worldAt(p, { ...d, meta: originAt(p, d) });
    expect(result.x).toBeCloseTo(0, 10);
    expect(result.y).toBeCloseTo(0, 10);
  });
  it('90 degree rotation preserves exact pixels and corresponding world positions', () => {
    const d = map(),
      out = rotate(d, 90);
    expect([out.width, out.height]).toEqual([3, 4]);
    expect([...out.pixels]).toEqual([50, 10, 0, 60, 20, 10, 70, 30, 32, 80, 40, 255]);
    for (let y = 0; y < d.height; y++)
      for (let x = 0; x < d.width; x++) {
        const before = worldAt({ x: x + 0.5, y: y + 0.5 }, d),
          after = worldAt({ x: d.height - y - 0.5, y: x + 0.5 }, out);
        expect(after.x).toBeCloseTo(before.x, 10);
        expect(after.y).toBeCloseTo(before.y, 10);
      }
    expect(out.keepout!.filter((v) => v === 100).length).toBe(3);
  });
  it('full turns preserve dimensions and data', () => {
    const d = map();
    expect(rotate(d, 360).pixels).toEqual(d.pixels);
  });
  it('does not mutate the original document', () => {
    const d = map(),
      before = pack(d);
    crop(d, { x: 1, y: 1, width: 2, height: 1 });
    rotate(d, 31);
    expect(pack(d)).toEqual(before);
  });
});
describe('pixel editing and connected mask selection', () => {
  it('draws a continuous stroke at fast pointer speeds', () => {
    const pixels = new Uint8Array(100);
    stroke(pixels, 10, 10, { x: 0.5, y: 5.5 }, { x: 9.5, y: 5.5 }, 1, 100);
    expect([...pixels.slice(50, 60)]).toEqual(new Array(10).fill(100));
  });
  it('selects four-connected same-value pixels only', () => {
    const pixels = Uint8Array.of(30, 30, 60, 0, 0, 60, 30, 0, 255);
    const result = flood(pixels, 3, 3, { x: 0, y: 0 }, 0, true)!;
    expect([...result.mask]).toEqual([1, 1, 0, 0, 0, 0, 0, 0, 0]);
    expect(result.bounds).toEqual({ x: 0, y: 0, width: 2, height: 1 });
    expect([...pixels]).toEqual([30, 30, 60, 0, 0, 60, 30, 0, 255]);
  });
  it('never selects no-restriction or unknown cells', () => {
    expect(flood(Uint8Array.of(0, 255), 2, 1, { x: 0, y: 0 }, 0, true)).toBeNull();
    expect(flood(Uint8Array.of(0, 255), 2, 1, { x: 1, y: 0 }, 0, true)).toBeNull();
  });
  it('fills only a connected component and handles no-ops', () => {
    const p = Uint8Array.of(0, 0, 1, 1, 0, 1, 0, 1, 1);
    flood(p, 3, 3, { x: 0, y: 0 }, 7);
    expect([...p]).toEqual([7, 7, 1, 1, 7, 1, 0, 1, 1]);
    expect(flood(p, 3, 3, { x: 0, y: 0 }, 7)).toBeNull();
  });
  it('fills a concave polygon with clipped scanlines', () => {
    const p = new Uint8Array(25);
    polygon(
      p,
      5,
      5,
      [
        { x: 1, y: 1 },
        { x: 4, y: 1 },
        { x: 4, y: 2 },
        { x: 2, y: 2 },
        { x: 2, y: 4 },
        { x: 1, y: 4 },
      ],
      100,
    );
    expect(p.filter((v) => v === 100)).toHaveLength(5);
  });
  it('does not paint unrelated rows when a polygon is outside the canvas', () => {
    const p = new Uint8Array(25);
    polygon(
      p,
      5,
      5,
      [
        { x: -10, y: 0 },
        { x: -5, y: 0 },
        { x: -5, y: 5 },
        { x: -10, y: 5 },
      ],
      100,
    );
    expect(p.some(Boolean)).toBe(false);
  });
  it('region deletion can restore the exact original snapshot', () => {
    const d = map(),
      before = d.speed!.slice(),
      result = flood(d.speed!, 4, 3, { x: 1, y: 0 }, 0, true)!;
    const edited = d.speed!.slice();
    for (let i = 0; i < edited.length; i++) if (result.mask[i]) edited[i] = 0;
    expect(edited[1]).toBe(0);
    expect(edited[5]).toBe(60);
    expect(d.speed).toEqual(before);
  });
});
