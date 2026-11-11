/// <reference lib="webworker" />
import {
  importFiles,
  pack,
  unpack,
  crop,
  rotate,
  flood,
  polygon,
  type MapDoc,
  type NamedFile,
  type Point,
  type Rect,
  type Layer,
  layerBytes,
  setLayer,
} from './map';
export type Job =
  | { action: 'import'; files: NamedFile[] }
  | { action: 'pack'; doc: MapDoc }
  | { action: 'unpack'; bytes: Uint8Array }
  | { action: 'crop'; doc: MapDoc; rect: Rect }
  | { action: 'rotate'; doc: MapDoc; angle: number }
  | { action: 'fill' | 'select'; doc: MapDoc; layer: Layer; point: Point; value: number }
  | { action: 'polygon'; doc: MapDoc; layer: Layer; points: Point[]; value: number };
self.onmessage = (event: MessageEvent<{ id: number; job: Job }>) => {
  const { id, job } = event.data;
  try {
    let result: unknown;
    switch (job.action) {
      case 'import':
        result = importFiles(job.files);
        break;
      case 'pack':
        result = pack(job.doc);
        break;
      case 'unpack':
        result = unpack(job.bytes);
        break;
      case 'crop':
        result = crop(job.doc, job.rect);
        break;
      case 'rotate':
        result = rotate(job.doc, job.angle);
        break;
      case 'fill':
      case 'select': {
        let bytes = layerBytes(job.doc, job.layer);
        if (!bytes) {
          bytes = new Uint8Array(job.doc.width * job.doc.height);
          setLayer(job.doc, job.layer, bytes);
        }
        const selection = flood(
          bytes,
          job.doc.width,
          job.doc.height,
          job.point,
          job.value,
          job.action === 'select',
        );
        result = job.action === 'select' ? selection : job.doc;
        break;
      }
      case 'polygon': {
        let bytes = layerBytes(job.doc, job.layer);
        if (!bytes) {
          bytes = new Uint8Array(job.doc.width * job.doc.height);
          setLayer(job.doc, job.layer, bytes);
        }
        polygon(bytes, job.doc.width, job.doc.height, job.points, job.value);
        result = job.doc;
        break;
      }
    }
    self.postMessage({ id, result });
  } catch (error) {
    self.postMessage({ id, error: error instanceof Error ? error.message : String(error) });
  }
};
