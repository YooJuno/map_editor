// @vitest-environment jsdom
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { createRef, type ComponentProps } from 'react';
import { MapCanvas, type CanvasHandle } from '../src/MapCanvas';
import { defaults, layerBytes, type Layer, type MapDoc } from '../src/map';
let frames: Map<number, FrameRequestCallback>, arcs: ReturnType<typeof vi.fn>;
beforeEach(() => {
  frames = new Map();
  let next = 0;
  vi.stubGlobal('requestAnimationFrame', (fn: FrameRequestCallback) => {
    frames.set(++next, fn);
    return next;
  });
  vi.stubGlobal('cancelAnimationFrame', (id: number) => frames.delete(id));
  vi.stubGlobal('PointerEvent', MouseEvent);
  vi.stubGlobal('devicePixelRatio', 1);
  vi.stubGlobal('ImageData', class {
    data: Uint8ClampedArray;
    constructor(public width: number, public height: number) { this.data = new Uint8ClampedArray(width * height * 4); }
  });
  vi.stubGlobal(
    'ResizeObserver',
    class {
      constructor(private callback: ResizeObserverCallback) {}
      observe(target: Element) {
        this.callback(
          [{ target, contentRect: { width: 800, height: 600 } } as ResizeObserverEntry],
          this as unknown as ResizeObserver,
        );
      }
      disconnect() {}
    },
  );
  arcs = vi.fn();
  const context = new Proxy(
    {
      arc: arcs,
      createImageData: (w: number, h: number) => ({
        data: new Uint8ClampedArray(w * h * 4),
        width: w,
        height: h,
      }),
    },
    { get: (target, key) => (key in target ? target[key as keyof typeof target] : () => {}) },
  );
  vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(
    context as unknown as CanvasRenderingContext2D,
  );
  Object.assign(HTMLCanvasElement.prototype, {
    setPointerCapture: () => {},
    hasPointerCapture: () => false,
    releasePointerCapture: () => {},
  });
});
afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
});
function flush() {
  act(() => {
    const callbacks = [...frames.values()];
    frames.clear();
    for (const fn of callbacks) fn(0);
  });
}
function setup(layer: Layer = 'map') {
  const doc: MapDoc = {
    name: 'Office',
    width: 64,
    height: 48,
    pixels: new Uint8Array(64 * 48).fill(255),
    meta: defaults(),
  };
  const props: ComponentProps<typeof MapCanvas> = {
    doc,
    tool: 'line',
    layer,
    size: 3,
    value: layer === 'map' ? 0 : layer === 'keepout' ? 100 : 30,
    opacity: 0.65,
    visible: true,
    maskVisibility: { keepout: true, speed: true },
    maskOpacity: { keepout: 0.65, speed: 0.65 },
    angle: 0,
    rect: null,
    selection: null,
    disabled: false,
    onStroke: vi.fn(),
    onShape: vi.fn(),
    onClick: vi.fn(),
    onPick: vi.fn(),
    onOrigin: vi.fn(),
    onAngle: vi.fn(),
    onRect: vi.fn(),
    onPoints: vi.fn(),
    onZoom: vi.fn(),
    onPosition: vi.fn(),
  };
  const ref = createRef<CanvasHandle>(),
    result = render(<MapCanvas {...props} ref={ref} />),
    canvas = screen.getByLabelText('Map editing canvas');
  const pointer = (
    type: 'pointerDown' | 'pointerMove' | 'pointerUp',
    x: number,
    y: number,
    button = 0,
  ) => fireEvent[type](canvas, { clientX: x, clientY: y, button });
  const click = (x: number, y: number) => {
    pointer('pointerDown', x, y);
    pointer('pointerUp', x, y);
  };
  // Fit is capped at 8x: image origin is (144,108).
  const pixel = (x: number, y: number) => click(144 + x * 8, 108 + y * 8);
  return { ...result, props, doc, ref, canvas, pointer, click, pixel };
}
describe('two-click line drawing', () => {
  it.each<Layer>(['map', 'keepout', 'speed'])('commits only after B on the %s layer', (layer) => {
    const { props, doc, pixel, pointer } = setup(layer);
    pixel(5.5, 7.5);
    pointer('pointerMove', 350, 300);
    flush();
    expect(props.onStroke).not.toHaveBeenCalled();
    expect(doc.pixels.every((v) => v === 255)).toBe(true);
    expect(arcs.mock.calls.some(([x, y]) => x === 5.5 && y === 7.5)).toBe(true);
    pixel(30.5, 25.5);
    expect(props.onStroke).toHaveBeenCalledTimes(1);
    const next = vi.mocked(props.onStroke).mock.calls[0][0],
      bytes = layerBytes(next, layer)!;
    expect(bytes[7 * 64 + 5]).toBe(props.value);
    expect(bytes[25 * 64 + 30]).toBe(props.value);
    expect(bytes[0]).toBe(layer === 'map' ? 255 : 0);
    if (layer !== 'map') expect(next.pixels).toEqual(doc.pixels);
    pixel(40.5, 30.5);
    expect(props.onStroke).toHaveBeenCalledTimes(1);
  });
  it('keeps A through a drag release, then uses the next click as B', () => {
    const { pointer, pixel, props } = setup();
    pointer('pointerDown', 188, 168);
    pointer('pointerMove', 460, 340);
    pointer('pointerUp', 460, 340);
    expect(props.onStroke).not.toHaveBeenCalled();
    pixel(30.5, 25.5);
    const next = vi.mocked(props.onStroke).mock.calls[0][0];
    expect(next.pixels[7 * 64 + 5]).toBe(0);
    expect(next.pixels[25 * 64 + 30]).toBe(0);
  });
  it('preserves A while zooming and panning with Space', () => {
    const { pixel, canvas, pointer, click, props } = setup();
    pixel(5.5, 7.5);
    fireEvent.wheel(canvas, { clientX: 400, clientY: 300, deltaY: -Math.log(2) / 0.0015 });
    fireEvent.keyDown(canvas, { key: ' ' });
    pointer('pointerDown', 400, 300);
    pointer('pointerMove', 420, 310);
    pointer('pointerUp', 420, 310);
    fireEvent.keyUp(window, { key: ' ' });
    expect(props.onStroke).not.toHaveBeenCalled();
    click(-92 + 30.5 * 16, -74 + 25.5 * 16);
    const next = vi.mocked(props.onStroke).mock.calls[0][0];
    expect(next.pixels[7 * 64 + 5]).toBe(0);
    expect(next.pixels[25 * 64 + 30]).toBe(0);
  });
  it.each(['Escape', 'Backspace', 'Undo', 'Right click'])(
    'cancels A with %s without committing a line',
    (method) => {
      const { pixel, canvas, props } = setup();
      pixel(5.5, 7.5);
      if (method === 'Right click') fireEvent.contextMenu(canvas);
      else
        fireEvent.keyDown(canvas, {
          key: method === 'Undo' ? 'z' : method,
          ctrlKey: method === 'Undo',
        });
      pixel(30.5, 25.5);
      expect(props.onStroke).not.toHaveBeenCalled();
      pixel(40.5, 30.5);
      expect(props.onStroke).toHaveBeenCalledTimes(1);
      const next = vi.mocked(props.onStroke).mock.calls[0][0];
      expect(next.pixels[7 * 64 + 5]).toBe(255);
    },
  );
  it('ignores clicks outside the image while awaiting B', () => {
    const { pixel, click, props } = setup();
    pixel(5.5, 7.5);
    click(5, 5);
    expect(props.onStroke).not.toHaveBeenCalled();
    pixel(30.5, 25.5);
    expect(props.onStroke).toHaveBeenCalledTimes(1);
  });
  it('cancels A when the tool, document, or editing permission changes', () => {
    const { pixel, props, rerender, ref } = setup();
    for (const patch of [
      { tool: 'hand' as const },
      { doc: { ...props.doc, pixels: props.doc.pixels.slice() } },
      { disabled: true },
    ]) {
      pixel(5.5, 7.5);
      rerender(<MapCanvas {...props} {...patch} ref={ref} />);
      rerender(<MapCanvas {...props} ref={ref} />);
      pixel(30.5, 25.5);
      expect(props.onStroke).not.toHaveBeenCalled();
      act(() => ref.current!.cancel());
    }
  });
});
