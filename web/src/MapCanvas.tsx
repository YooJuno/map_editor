import { forwardRef, useEffect, useImperativeHandle, useRef } from 'react';
import {
  layerBytes,
  setLayer,
  stroke,
  originAt,
  type Layer,
  type MapDoc,
  type Meta,
  type Point,
  type Rect,
} from './map';
export type Tool =
  | 'pen'
  | 'line'
  | 'rect'
  | 'polygon'
  | 'fill'
  | 'select'
  | 'hand'
  | 'picker'
  | 'crop'
  | 'rotate'
  | 'origin';
export type Selection = { mask: Uint8Array; bounds: Rect };
export type CanvasHandle = {
  fit: () => void;
  zoom: (factor: number) => void;
  actual: () => void;
  finish: () => void;
  cancel: () => void;
};
type Props = {
  doc: MapDoc;
  tool: Tool;
  layer: Layer;
  size: number;
  value: number;
  opacity: number;
  visible: boolean;
  maskVisibility: Record<'keepout' | 'speed', boolean>;
  maskOpacity: Record<'keepout' | 'speed', number>;
  angle: number;
  rect: Rect | null;
  selection: Selection | null;
  disabled: boolean;
  onStroke: (doc: MapDoc) => void;
  onShape: (points: Point[]) => void;
  onClick: (point: Point) => void;
  onPick: (value: number) => void;
  onOrigin: (meta: Meta) => void;
  onAngle: (angle: number) => void;
  onRect: (rect: Rect | null) => void;
  onPoints: (count: number) => void;
  onZoom: (zoom: number) => void;
  onPosition: (point: Point | null) => void;
};
type Camera = { x: number; y: number; zoom: number };
type Drag = {
  start: Point;
  last: Point;
  screen: Point;
  mode: string;
  rect?: Rect;
  scratch?: MapDoc;
  angle?: number;
  axisOffset?: number;
};
const rectOf = (a: Point, b: Point): Rect => ({
  x: Math.min(a.x, b.x),
  y: Math.min(a.y, b.y),
  width: Math.abs(a.x - b.x),
  height: Math.abs(a.y - b.y),
});
const clampRect = (r: Rect, d: MapDoc): Rect => {
  const x = Math.max(0, Math.min(d.width - 1, Math.round(r.x))),
    y = Math.max(0, Math.min(d.height - 1, Math.round(r.y)));
  return {
    x,
    y,
    width: Math.max(1, Math.min(d.width - x, Math.round(r.width))),
    height: Math.max(1, Math.min(d.height - y, Math.round(r.height))),
  };
};
function originPixel(doc: MapDoc): Point {
  const [x, y, a] = doc.meta.origin,
    r = doc.meta.resolution;
  return {
    x: (-x * Math.cos(a) - y * Math.sin(a)) / r,
    y: doc.height - (x * Math.sin(a) - y * Math.cos(a)) / r,
  };
}
export const MapCanvas = forwardRef<CanvasHandle, Props>(function MapCanvas(props, ref) {
  const element = useRef<HTMLCanvasElement>(null),
    latest = useRef(props),
    api = useRef<CanvasHandle | null>(null);
  latest.current = props;
  const refresh = useRef<() => void>(() => {});
  useImperativeHandle(
    ref,
    () => ({
      fit: () => api.current?.fit(),
      zoom: (factor) => api.current?.zoom(factor),
      actual: () => api.current?.actual(),
      finish: () => api.current?.finish(),
      cancel: () => api.current?.cancel(),
    }),
    [],
  );
  useEffect(() => {
    refresh.current();
  }, [props]);
  useEffect(() => {
    const canvas = element.current!,
      ctx = canvas.getContext('2d', { alpha: false })!;
    let width = 1,
      height = 1,
      frame = 0,
      fitted = false,
      space = false,
      drag: Drag | null = null,
      hover: Point | null = null,
      points: Point[] = [],
      positionAt = 0;
    const camera: Camera = { x: 0, y: 0, zoom: 1 };
    const caches = new Map<
      Layer,
      { source: Uint8Array; canvas: HTMLCanvasElement; image: ImageData }
    >();
    const request = () => {
      if (!frame) frame = requestAnimationFrame(draw);
    };
    refresh.current = request;
    function cache(doc: MapDoc, layer: Layer, dirty?: Rect) {
      const source = layerBytes(doc, layer);
      if (!source) return null;
      let item = caches.get(layer);
      if (!item || item.canvas.width !== doc.width || item.canvas.height !== doc.height) {
        const imageCanvas = document.createElement('canvas');
        imageCanvas.width = doc.width;
        imageCanvas.height = doc.height;
        item = { source, canvas: imageCanvas, image: new ImageData(doc.width, doc.height) };
        caches.set(layer, item);
        dirty = { x: 0, y: 0, width: doc.width, height: doc.height };
      } else if (item.source !== source && !dirty)
        dirty = { x: 0, y: 0, width: doc.width, height: doc.height };
      if (dirty && dirty.width > 0 && dirty.height > 0) {
        const data = item.image.data;
        for (let y = dirty.y; y < dirty.y + dirty.height; y++)
          for (let x = dirty.x; x < dirty.x + dirty.width; x++) {
            const i = y * doc.width + x,
              v = source[i],
              p = i * 4;
            if (layer === 'map') {
              data[p] = data[p + 1] = data[p + 2] = v;
              data[p + 3] = 255;
            } else {
              data[p] = layer === 'keepout' ? 213 : 114;
              data[p + 1] = layer === 'keepout' ? 121 : 154;
              data[p + 2] = layer === 'keepout' ? 114 : 201;
              data[p + 3] = v === 0 ? 0 : v === 255 ? 65 : 210;
            }
          }
        item.canvas
          .getContext('2d')!
          .putImageData(item.image, 0, 0, dirty.x, dirty.y, dirty.width, dirty.height);
        item.source = source;
      }
      return item.canvas;
    }
    const screenPoint = (e: PointerEvent | WheelEvent) => {
      const rect = canvas.getBoundingClientRect();
      return { x: e.clientX - rect.left, y: e.clientY - rect.top };
    };
    const imagePoint = (e: PointerEvent | WheelEvent): Point => {
      const p = screenPoint(e);
      return { x: (p.x - camera.x) / camera.zoom, y: (p.y - camera.y) / camera.zoom };
    };
    const fit = () => {
      const doc = latest.current.doc;
      camera.zoom = Math.max(
        0.005,
        Math.min((width - 90) / doc.width, (height - 90) / doc.height, 8),
      );
      camera.x = (width - doc.width * camera.zoom) / 2;
      camera.y = (height - doc.height * camera.zoom) / 2;
      latest.current.onZoom(camera.zoom);
      request();
    };
    const zoom = (factor: number, center: Point = { x: width / 2, y: height / 2 }) => {
      const old = camera.zoom;
      camera.zoom = Math.max(0.005, Math.min(80, camera.zoom * factor));
      camera.x = center.x - ((center.x - camera.x) * camera.zoom) / old;
      camera.y = center.y - ((center.y - camera.y) * camera.zoom) / old;
      latest.current.onZoom(camera.zoom);
      request();
    };
    const clear = () => {
      if (drag?.scratch) caches.clear();
      drag = null;
      points = [];
      latest.current.onPoints(0);
      request();
    };
    const finish = () => {
      if (points.length >= 3 && !latest.current.disabled) {
        const finished = points;
        points = [];
        latest.current.onPoints(0);
        latest.current.onShape(finished);
        request();
      }
    };
    api.current = { fit, zoom, actual: () => zoom(1 / camera.zoom), finish, cancel: clear };
    function draw() {
      frame = 0;
      const p = latest.current,
        d = drag?.scratch || p.doc,
        z = camera.zoom;
      const light = document.documentElement.dataset.theme === 'light';
      ctx.setTransform(devicePixelRatio, 0, 0, devicePixelRatio, 0, 0);
      ctx.fillStyle = light ? '#e5e7ea' : '#101215';
      ctx.fillRect(0, 0, width, height);
      ctx.save();
      ctx.translate(camera.x, camera.y);
      ctx.scale(z, z);
      ctx.imageSmoothingEnabled = false;
      if (p.tool === 'rotate' && p.angle) {
        ctx.translate(d.width / 2, d.height / 2);
        ctx.rotate((p.angle * Math.PI) / 180);
        ctx.translate(-d.width / 2, -d.height / 2);
      }
      const base = cache(d, 'map');
      if (base) ctx.drawImage(base, 0, 0);
      for (const layer of ['keepout', 'speed'] as const) {
        if (!p.maskVisibility[layer]) continue;
        const image = cache(d, layer);
        if (image) {
          ctx.globalAlpha = p.maskOpacity[layer];
          ctx.drawImage(image, 0, 0);
        }
      }
      ctx.globalAlpha = 1;
      ctx.strokeStyle = '#8da6cf';
      ctx.lineWidth = 1.5 / z;
      const box = p.tool === 'crop' ? p.rect : p.selection?.bounds;
      if (box) {
        if (p.tool === 'crop') {
          ctx.fillStyle = '#08090c88';
          ctx.beginPath();
          ctx.rect(0, 0, d.width, d.height);
          ctx.rect(box.x, box.y, box.width, box.height);
          ctx.fill('evenodd');
        }
        ctx.strokeRect(box.x, box.y, box.width, box.height);
        if (p.tool === 'crop')
          for (const [x, y] of [
            [box.x, box.y],
            [box.x + box.width / 2, box.y],
            [box.x + box.width, box.y],
            [box.x + box.width, box.y + box.height / 2],
            [box.x + box.width, box.y + box.height],
            [box.x + box.width / 2, box.y + box.height],
            [box.x, box.y + box.height],
            [box.x, box.y + box.height / 2],
          ]) {
            ctx.fillStyle = '#e5edf9';
            ctx.fillRect(x - 3 / z, y - 3 / z, 6 / z, 6 / z);
          }
      }
      const preview = points.length
        ? [...points, ...(hover ? [hover] : [])]
        : drag && (p.tool === 'line' || p.tool === 'rect')
          ? [drag.start, drag.last]
          : [];
      if (preview.length) {
        ctx.beginPath();
        ctx.moveTo(preview[0].x, preview[0].y);
        if (p.tool === 'rect') {
          const r = rectOf(preview[0], preview[1]);
          ctx.rect(r.x, r.y, r.width, r.height);
        } else for (const q of preview.slice(1)) ctx.lineTo(q.x, q.y);
        if (p.tool !== 'line') {
          ctx.fillStyle = '#8da6cf33';
          ctx.fill();
        }
        ctx.stroke();
        for (const q of points) {
          ctx.beginPath();
          ctx.arc(q.x, q.y, 3 / z, 0, Math.PI * 2);
          ctx.fillStyle = '#e5edf9';
          ctx.fill();
        }
      }
      if (hover && p.tool === 'pen' && !p.disabled) {
        ctx.beginPath();
        ctx.arc(hover.x, hover.y, p.size / 2, 0, Math.PI * 2);
        ctx.strokeStyle = '#66758d';
        ctx.lineWidth = 1 / z;
        ctx.stroke();
      }
      const o = originPixel(d),
        a = d.meta.origin[2];
      for (const [offset, color, label] of [
        [0, '#c97672', 'X'],
        [-Math.PI / 2, '#799bc1', 'Y'],
      ] as const) {
        const x = o.x + (Math.cos(a + offset) * 45) / z,
          y = o.y + (Math.sin(a + offset) * 45) / z;
        ctx.strokeStyle = color;
        ctx.fillStyle = color;
        ctx.lineWidth = 1.5 / z;
        ctx.beginPath();
        ctx.moveTo(o.x, o.y);
        ctx.lineTo(x, y);
        ctx.stroke();
        ctx.beginPath();
        ctx.arc(x, y, 3 / z, 0, Math.PI * 2);
        ctx.fill();
        ctx.font = `${11 / z}px sans-serif`;
        ctx.fillText(label, x + 5 / z, y - 5 / z);
      }
      ctx.beginPath();
      ctx.arc(o.x, o.y, 4 / z, 0, Math.PI * 2);
      ctx.fillStyle = '#e9edf2';
      ctx.fill();
      ctx.strokeStyle = '#8094ae';
      ctx.stroke();
      ctx.restore();
    }
    const canPaint = () =>
      latest.current.layer === 'map' || (latest.current.visible && latest.current.opacity > 0);
    const scratch = () => {
      const p = latest.current,
        next = { ...p.doc },
        source = layerBytes(p.doc, p.layer);
      setLayer(next, p.layer, source?.slice() || new Uint8Array(p.doc.width * p.doc.height));
      return next;
    };
    const commitStroke = (doc: MapDoc) => {
      const p = latest.current,
        old = layerBytes(p.doc, p.layer),
        bytes = layerBytes(doc, p.layer)!;
      if (bytes.some((v, i) => v !== (old?.[i] || 0))) p.onStroke(doc);
      else caches.clear();
    };
    const down = (e: PointerEvent) => {
      const p = latest.current;
      if (p.disabled || e.button === 2) return;
      canvas.focus();
      const at = imagePoint(e),
        screen = screenPoint(e);
      canvas.setPointerCapture(e.pointerId);
      if (e.button === 1 || space || p.tool === 'hand')
        drag = { start: at, last: at, screen, mode: 'pan' };
      else if (p.tool === 'rotate')
        drag = {
          start: at,
          last: at,
          screen,
          mode: 'rotate',
          angle:
            p.angle - (Math.atan2(at.y - p.doc.height / 2, at.x - p.doc.width / 2) * 180) / Math.PI,
        };
      else if (p.tool === 'origin') {
        const o = originPixel(p.doc),
          a = p.doc.meta.origin[2];
        let axisOffset: number | undefined;
        for (const offset of [0, -Math.PI / 2])
          if (
            Math.hypot(
              at.x - o.x - (Math.cos(a + offset) * 45) / camera.zoom,
              at.y - o.y - (Math.sin(a + offset) * 45) / camera.zoom,
            ) *
              camera.zoom <
            12
          )
            axisOffset = offset;
        drag = {
          start: o,
          last: at,
          screen,
          mode: axisOffset === undefined ? 'origin' : 'axis',
          axisOffset,
        };
        if (axisOffset === undefined) p.onOrigin(originAt(at, p.doc));
      } else if (p.tool === 'crop') {
        const r = p.rect,
          tolerance = 8 / camera.zoom;
        let mode = 'new';
        if (
          r &&
          at.x >= r.x - tolerance &&
          at.x <= r.x + r.width + tolerance &&
          at.y >= r.y - tolerance &&
          at.y <= r.y + r.height + tolerance
        ) {
          mode = '';
          if (Math.abs(at.x - r.x) < tolerance) mode += 'w';
          else if (Math.abs(at.x - r.x - r.width) < tolerance) mode += 'e';
          if (Math.abs(at.y - r.y) < tolerance) mode += 'n';
          else if (Math.abs(at.y - r.y - r.height) < tolerance) mode += 's';
          mode ||= 'move';
        }
        drag = { start: at, last: at, screen, mode, rect: r || undefined };
      } else if (!canPaint()) return;
      else if (p.tool === 'picker') {
        const x = Math.floor(at.x),
          y = Math.floor(at.y);
        if (x >= 0 && y >= 0 && x < p.doc.width && y < p.doc.height)
          p.onPick(layerBytes(p.doc, p.layer)?.[y * p.doc.width + x] || 0);
      } else if (p.tool === 'fill' || p.tool === 'select') p.onClick(at);
      else if (p.tool === 'polygon') {
        if (
          points.length >= 3 &&
          Math.hypot(points[0].x - at.x, points[0].y - at.y) * camera.zoom < 9
        )
          finish();
        else {
          points.push(at);
          p.onPoints(points.length);
        }
      } else {
        if (at.x < 0 || at.y < 0 || at.x >= p.doc.width || at.y >= p.doc.height) return;
        drag = {
          start: at,
          last: at,
          screen,
          mode: p.tool,
          ...(p.tool === 'pen' ? { scratch: scratch() } : {}),
        };
        if (drag.scratch) {
          const r = stroke(
            layerBytes(drag.scratch, p.layer)!,
            p.doc.width,
            p.doc.height,
            at,
            at,
            p.size,
            p.value,
          );
          cache(drag.scratch, p.layer, r);
        }
      }
      request();
    };
    const move = (e: PointerEvent) => {
      const p = latest.current,
        at = imagePoint(e),
        screen = screenPoint(e);
      hover = at;
      if (performance.now() - positionAt > 80) {
        p.onPosition(at);
        positionAt = performance.now();
      }
      if (drag) {
        if (drag.mode === 'pan') {
          camera.x += screen.x - drag.screen.x;
          camera.y += screen.y - drag.screen.y;
          drag.screen = screen;
        } else if (drag.mode === 'pen' && drag.scratch) {
          const r = stroke(
            layerBytes(drag.scratch, p.layer)!,
            p.doc.width,
            p.doc.height,
            drag.last,
            at,
            p.size,
            p.value,
          );
          cache(drag.scratch, p.layer, r);
        } else if (drag.mode === 'rotate') {
          let angle =
            (Math.atan2(at.y - p.doc.height / 2, at.x - p.doc.width / 2) * 180) / Math.PI +
            drag.angle!;
          angle = ((((angle + 180) % 360) + 360) % 360) - 180;
          p.onAngle(e.shiftKey ? Math.round(angle / 15) * 15 : angle);
        } else if (drag.mode === 'origin') p.onOrigin(originAt(at, p.doc));
        else if (drag.mode === 'axis') {
          let a = Math.atan2(at.y - drag.start.y, at.x - drag.start.x) - drag.axisOffset!;
          if (e.shiftKey) a = (Math.round(a / (Math.PI / 12)) * Math.PI) / 12;
          p.onOrigin(
            originAt(drag.start, {
              ...p.doc,
              meta: { ...p.doc.meta, origin: [p.doc.meta.origin[0], p.doc.meta.origin[1], a] },
            }),
          );
        } else if (p.tool === 'crop') {
          const r = drag.rect,
            dx = at.x - drag.start.x,
            dy = at.y - drag.start.y;
          let box: Rect;
          if (!r || drag.mode === 'new') box = rectOf(drag.start, at);
          else if (drag.mode === 'move')
            box = {
              ...r,
              x: Math.max(0, Math.min(p.doc.width - r.width, r.x + dx)),
              y: Math.max(0, Math.min(p.doc.height - r.height, r.y + dy)),
            };
          else {
            const x1 = drag.mode.includes('w') ? r.x + dx : r.x,
              x2 = drag.mode.includes('e') ? r.x + r.width + dx : r.x + r.width,
              y1 = drag.mode.includes('n') ? r.y + dy : r.y,
              y2 = drag.mode.includes('s') ? r.y + r.height + dy : r.y + r.height;
            box = rectOf({ x: x1, y: y1 }, { x: x2, y: y2 });
          }
          p.onRect(clampRect(box, p.doc));
        }
        drag.last = at;
      }
      canvas.style.cursor =
        drag?.mode === 'pan'
          ? 'grabbing'
          : p.tool === 'hand'
            ? 'grab'
            : p.tool === 'crop'
              ? 'crosshair'
              : p.tool === 'origin'
                ? 'move'
                : 'crosshair';
      request();
    };
    const up = (e: PointerEvent) => {
      if (!drag) return;
      // Include the final pointer coordinate even when a coalesced move was skipped.
      move(e);
      const p = latest.current,
        finished = drag!;
      drag = null;
      if (finished.scratch) commitStroke(finished.scratch);
      else if (finished.mode === 'line') {
        const next = scratch();
        stroke(
          layerBytes(next, p.layer)!,
          next.width,
          next.height,
          finished.start,
          finished.last,
          p.size,
          p.value,
        );
        commitStroke(next);
      } else if (finished.mode === 'rect') {
        const r = rectOf(finished.start, finished.last);
        p.onShape([
          { x: r.x, y: r.y },
          { x: r.x + r.width, y: r.y },
          { x: r.x + r.width, y: r.y + r.height },
          { x: r.x, y: r.y + r.height },
        ]);
      }
      if (canvas.hasPointerCapture(e.pointerId)) canvas.releasePointerCapture(e.pointerId);
      request();
    };
    const wheel = (e: WheelEvent) => {
      e.preventDefault();
      zoom(Math.exp(-e.deltaY * 0.0015), screenPoint(e));
    };
    const keydown = (e: KeyboardEvent) => {
      if (e.key === ' ') {
        space = true;
        e.preventDefault();
      }
      if (e.key === 'Escape') clear();
      if (e.key === 'Enter' && points.length) {
        e.preventDefault();
        e.stopPropagation();
        finish();
      }
      if (
        (e.key === 'Backspace' || ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'z')) &&
        points.length
      ) {
        e.preventDefault();
        e.stopPropagation();
        points.pop();
        latest.current.onPoints(points.length);
        request();
      }
    };
    const keyup = (e: KeyboardEvent) => {
      if (e.key === ' ') space = false;
    };
    const context = (e: MouseEvent) => {
      e.preventDefault();
      finish();
    };
    const leave = () => {
      hover = null;
      latest.current.onPosition(null);
      request();
    };
    const cancel = () => clear();
    const observer = new ResizeObserver((entries) => {
      const box = entries[0].contentRect;
      width = box.width;
      height = box.height;
      canvas.width = Math.round(width * devicePixelRatio);
      canvas.height = Math.round(height * devicePixelRatio);
      if (!fitted) {
        fit();
        fitted = true;
      } else request();
    });
    observer.observe(canvas);
    canvas.addEventListener('pointerdown', down);
    canvas.addEventListener('pointermove', move);
    canvas.addEventListener('pointerup', up);
    canvas.addEventListener('pointercancel', cancel);
    canvas.addEventListener('pointerleave', leave);
    canvas.addEventListener('wheel', wheel, { passive: false });
    canvas.addEventListener('contextmenu', context);
    canvas.addEventListener('keydown', keydown);
    window.addEventListener('keyup', keyup);
    canvas.addEventListener('blur', () => {
      space = false;
    });
    return () => {
      observer.disconnect();
      cancelAnimationFrame(frame);
      canvas.removeEventListener('pointerdown', down);
      canvas.removeEventListener('pointermove', move);
      canvas.removeEventListener('pointerup', up);
      canvas.removeEventListener('pointercancel', cancel);
      canvas.removeEventListener('pointerleave', leave);
      canvas.removeEventListener('wheel', wheel);
      canvas.removeEventListener('contextmenu', context);
      canvas.removeEventListener('keydown', keydown);
      window.removeEventListener('keyup', keyup);
      caches.clear();
    };
  }, []);
  useEffect(() => {
    api.current?.fit();
  }, [props.doc.width, props.doc.height]);
  return (
    <canvas className="map-canvas" ref={element} tabIndex={0} aria-label="Map editing canvas" />
  );
});
