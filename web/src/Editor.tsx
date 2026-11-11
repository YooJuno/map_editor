import { forwardRef, useEffect, useImperativeHandle, useMemo, useRef, useState } from 'react';
import {
  Pencil,
  Minus,
  Square,
  Pentagon,
  PaintBucket,
  MousePointer2,
  Hand,
  Pipette,
  Crop,
  RotateCcw,
  LocateFixed,
  Map,
  ShieldOff,
  Gauge,
  Undo2,
  Redo2,
  Maximize,
  Plus,
  Trash2,
  X,
  Check,
  SlidersHorizontal,
  ChevronRight,
} from 'lucide-react';
import { MapCanvas, type CanvasHandle, type Selection, type Tool } from './MapCanvas';
import { job } from './jobs';
import {
  layerBytes,
  setLayer,
  validateMeta,
  worldAt,
  type Layer,
  type MapDoc,
  type Meta,
  type Point,
  type Rect,
} from './map';
import type { Translate } from './i18n';
export function IconButton({
  title,
  active,
  children,
  ...rest
}: React.ButtonHTMLAttributes<HTMLButtonElement> & { title: string; active?: boolean }) {
  return (
    <button
      type="button"
      className={`icon-button${active ? ' active' : ''}`}
      title={title}
      aria-label={title}
      aria-pressed={active}
      {...rest}
    >
      {children}
    </button>
  );
}
function NumberField({
  value,
  onChange,
  label,
  step = 'any',
  min,
}: {
  value: number;
  onChange: (v: number) => void;
  label: string;
  step?: string | number;
  min?: number;
}) {
  const [text, setText] = useState(String(value));
  useEffect(() => setText(String(Number(value.toFixed(6)))), [value]);
  return (
    <label className="number-field">
      <span>{label}</span>
      <input
        aria-label={label}
        type="number"
        value={text}
        step={step}
        min={min}
        onChange={(e) => setText(e.target.value)}
        onBlur={() => {
          const next = Number(text);
          if (text !== '' && Number.isFinite(next) && (min === undefined || next >= min))
            onChange(next);
          else setText(String(value));
        }}
        onKeyDown={(e) => {
          if (e.key === 'Enter') e.currentTarget.blur();
        }}
      />
    </label>
  );
}
export type EditorHandle = { prepare: () => Promise<MapDoc> };
type Props = {
  doc: MapDoc;
  t: Translate;
  disabled: boolean;
  canUndo: boolean;
  canRedo: boolean;
  onUndo: () => void;
  onRedo: () => void;
  onChange: (doc: MapDoc) => void;
  onDraft: (dirty: boolean) => void;
  run: (task: () => Promise<void>) => Promise<void>;
};
const toolIcons = {
  pen: Pencil,
  line: Minus,
  rect: Square,
  polygon: Pentagon,
  fill: PaintBucket,
  select: MousePointer2,
  hand: Hand,
};
const shortcuts: Record<string, Tool> = {
  b: 'pen',
  l: 'line',
  r: 'rect',
  p: 'polygon',
  g: 'fill',
  v: 'select',
  h: 'hand',
  i: 'picker',
  c: 'crop',
  t: 'rotate',
  o: 'origin',
};
export const Editor = forwardRef<EditorHandle, Props>(function Editor(
  { doc, t, disabled, canUndo, canRedo, onUndo, onRedo, onChange, onDraft, run },
  ref,
) {
  const [layer, setActiveLayer] = useState<Layer>('map'),
    [tool, setTool] = useState<Tool>('pen');
  const [size, setSize] = useState(8),
    [color, setColor] = useState(255),
    [gray, setGray] = useState(205),
    [speed, setSpeed] = useState(30),
    [clear, setClear] = useState(false);
  const [opacities, setOpacities] = useState({ keepout: 0.65, speed: 0.65 }),
    [visibility, setVisibility] = useState({ keepout: true, speed: true }),
    [meta, setMeta] = useState<Meta>(doc.meta),
    [angle, setAngle] = useState(0),
    [rect, setRect] = useState<Rect | null>(null);
  const [selection, setSelection] = useState<Selection | null>(null),
    [points, setPoints] = useState(0),
    [zoom, setZoom] = useState(1),
    [position, setPosition] = useState<Point | null>(null),
    [panel, setPanel] = useState(true);
  const canvas = useRef<CanvasHandle>(null),
    previousTool = useRef<Tool>('pen');
  const opacity = layer === 'map' ? 0.65 : opacities[layer],
    visible = layer === 'map' || visibility[layer];
  const geometry = ['crop', 'rotate', 'origin'].includes(tool),
    value = layer === 'map' ? color : clear ? 0 : layer === 'keepout' ? 100 : speed;
  const displayDoc = useMemo(() => ({ ...doc, meta }), [doc, meta]);
  const draft = JSON.stringify(meta) !== JSON.stringify(doc.meta) || angle !== 0 || rect !== null;
  useEffect(() => {
    setMeta(doc.meta);
    setAngle(0);
    setRect(null);
    setSelection(null);
  }, [doc]);
  useEffect(() => {
    onDraft(draft);
  }, [draft, onDraft]);
  useEffect(() => {
    canvas.current?.cancel();
    setSelection(null);
  }, [tool, layer, visible]);
  const activate = (next: Tool) => {
    if (next === 'picker') previousTool.current = tool;
    canvas.current?.cancel();
    setSelection(null);
    if (['crop', 'rotate', 'origin'].includes(next)) setActiveLayer('map');
    if (next !== tool) {
      setAngle(0);
      setRect(null);
      setMeta(doc.meta);
    }
    setTool(next);
  };
  const changeLayer = (next: Layer) => {
    canvas.current?.cancel();
    setMeta(doc.meta);
    setAngle(0);
    setRect(null);
    setSelection(null);
    setActiveLayer(next);
    setTool('pen');
  };
  const prepare = async () => {
    if (points) throw Error(t('polygonHint'));
    validateMeta(meta);
    let next = JSON.stringify(meta) === JSON.stringify(doc.meta) ? doc : displayDoc;
    if (tool === 'rotate' && angle)
      next = await job<MapDoc>({ action: 'rotate', doc: next, angle });
    if (tool === 'crop' && rect) next = await job<MapDoc>({ action: 'crop', doc: next, rect });
    if (next !== doc) {
      onChange(next);
      setMeta(next.meta);
      setAngle(0);
      setRect(null);
    }
    return next;
  };
  useImperativeHandle(ref, () => ({ prepare }));
  const apply = () =>
    run(async () => {
      await prepare();
    });
  const cancel = () => {
    canvas.current?.cancel();
    setMeta(doc.meta);
    setAngle(0);
    setRect(null);
    setSelection(null);
  };
  const removeRegion = () => {
    if (!selection || layer === 'map' || disabled) return;
    const source = layerBytes(doc, layer);
    if (!source) return;
    const next = { ...displayDoc },
      bytes = source.slice();
    for (let i = 0; i < bytes.length; i++) if (selection.mask[i]) bytes[i] = 0;
    setLayer(next, layer, bytes);
    onChange(next);
    setSelection(null);
  };
  const shape = (vertices: Point[]) => {
    if (disabled || (layer !== 'map' && (!visible || opacity === 0))) return;
    if (!layerBytes(doc, layer) && value === 0) return;
    void run(async () =>
      onChange(
        await job<MapDoc>({ action: 'polygon', doc: displayDoc, layer, points: vertices, value }),
      ),
    );
  };
  const click = (point: Point) => {
    if (disabled || (layer !== 'map' && (!visible || opacity === 0))) return;
    const source = layerBytes(doc, layer),
      x = Math.floor(point.x),
      y = Math.floor(point.y);
    if (x < 0 || y < 0 || x >= doc.width || y >= doc.height) {
      setSelection(null);
      return;
    }
    if (tool === 'select') {
      if (layer === 'map') return;
      void run(async () =>
        setSelection(
          await job<Selection | null>({ action: 'select', doc: displayDoc, layer, point, value }),
        ),
      );
    } else if ((source?.[y * doc.width + x] || 0) !== value)
      void run(async () =>
        onChange(await job<MapDoc>({ action: 'fill', doc: displayDoc, layer, point, value })),
      );
  };
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if (
        disabled ||
        (e.target as HTMLElement).closest('input, textarea, select, [contenteditable]') ||
        e.defaultPrevented
      )
        return;
      const key = e.key.toLowerCase();
      if (e.ctrlKey || e.metaKey) {
        if (key === 'z' && !points) {
          e.preventDefault();
          e.shiftKey ? onRedo() : onUndo();
        }
        return;
      }
      if (shortcuts[key] && !(key === 'v' && layer === 'map')) {
        e.preventDefault();
        activate(shortcuts[key]);
      } else if (key === 'f') canvas.current?.fit();
      else if (key === '0') canvas.current?.actual();
      else if (key === '1') setColor(255);
      else if (key === '2') setColor(0);
      else if (key === '3') setColor(gray);
      else if (key === '[') setSize((v) => Math.max(1, v - 1));
      else if (key === ']') setSize((v) => Math.min(512, v + 1));
      else if ((key === 'delete' || key === 'backspace') && selection) {
        e.preventDefault();
        removeRegion();
      } else if (key === 'escape') cancel();
      else if (key === 'enter' && geometry) {
        e.preventDefault();
        void apply();
      }
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  });
  const world = position ? worldAt(position, displayDoc) : null;
  const hint =
    tool === 'polygon'
      ? t('polygonHint')
      : tool === 'crop'
        ? t('cropHint')
        : tool === 'rotate'
          ? t('rotateHint')
          : tool === 'origin'
            ? t('originHint')
            : tool === 'select'
              ? t('selectHint')
              : '';
  return (
    <fieldset className="editor" disabled={disabled}>
      <aside className="workspace-rail" aria-label="Workspace">
        {(
          [
            ['map', Map],
            ['keepout', ShieldOff],
            ['speed', Gauge],
          ] as const
        ).map(([key, Icon]) => (
          <button
            key={key}
            disabled={disabled}
            className={layer === key && !geometry ? 'rail-item active' : 'rail-item'}
            onClick={() => changeLayer(key)}
          >
            <Icon size={21} />
            <span>{t(key)}</span>
          </button>
        ))}
        <div className="rail-divider" />
        {(
          [
            ['rotate', RotateCcw],
            ['crop', Crop],
            ['origin', LocateFixed],
          ] as const
        ).map(([key, Icon]) => (
          <button
            key={key}
            disabled={disabled}
            className={tool === key ? 'rail-item active' : 'rail-item'}
            onClick={() => activate(key)}
          >
            <Icon size={20} />
            <span>{t(key)}</span>
          </button>
        ))}
      </aside>
      <aside className="tool-rail" aria-label="Tools">
        {Object.entries(toolIcons).map(([key, Icon]) => (
          <IconButton
            key={key}
            title={t(key as keyof typeof toolIcons)}
            active={tool === key}
            disabled={disabled || (key === 'select' && layer === 'map')}
            onClick={() => activate(key as Tool)}
          >
            <Icon size={19} />
          </IconButton>
        ))}
        <div className="rail-divider" />
        <IconButton title={t('undo')} disabled={disabled || !canUndo} onClick={onUndo}>
          <Undo2 size={18} />
        </IconButton>
        <IconButton title={t('redo')} disabled={disabled || !canRedo} onClick={onRedo}>
          <Redo2 size={18} />
        </IconButton>
      </aside>
      <main className="editor-main">
        <div className="options-bar">
          <span className={`layer-label ${layer}`}>{t(layer)}</span>
          {!geometry && (
            <>
              {layer === 'map' ? (
                <div className="swatches">
                  {(
                    [
                      ['white', 255],
                      ['black', 0],
                      ['gray', gray],
                    ] as const
                  ).map(([label, v]) => (
                    <button
                      key={label}
                      aria-label={`${t(label)} (${v})`}
                      title={`${t(label)} · ${v}`}
                      className={`swatch${color === v ? ' selected' : ''}`}
                      style={{ '--swatch': `rgb(${v}, ${v}, ${v})` } as React.CSSProperties}
                      onClick={() => setColor(v)}
                    />
                  ))}
                </div>
              ) : (
                <>
                  <button
                    className={`small-button${clear ? '' : ' selected'}`}
                    onClick={() => setClear(false)}
                  >
                    {layer === 'keepout' ? t('keepout') : `${speed}%`}
                  </button>
                  <button
                    className={`text-button${clear ? ' selected' : ''}`}
                    onClick={() => setClear(true)}
                  >
                    {t('clear')}
                  </button>
                </>
              )}
              <IconButton
                title={`${t('picker')} (I)`}
                active={tool === 'picker'}
                onClick={() => {
                  previousTool.current = tool;
                  setTool('picker');
                }}
                disabled={disabled}
              >
                <Pipette size={17} />
              </IconButton>
              <div className="brush-control">
                <span
                  className="brush-preview"
                  role="img"
                  aria-label={`${t('size')} · ${size} px`}
                  title={`${t('size')} · ${size} px`}
                >
                  <svg viewBox="0 0 48 28" aria-hidden="true">
                    <line
                      x1="12"
                      y1="14"
                      x2="36"
                      y2="14"
                      stroke="currentColor"
                      strokeLinecap="round"
                      strokeWidth={size <= 12 ? size : 12 + Math.log2(size / 12) * 2}
                    />
                  </svg>
                </span>
                <input
                  className="brush-slider"
                  type="range"
                  min={1}
                  max={512}
                  step={1}
                  aria-label={t('size')}
                  title={`${t('size')} · ${size} px`}
                  value={size}
                  onChange={(e) => setSize(Number(e.target.value))}
                />
                <input
                  className="brush-input"
                  type="number"
                  min={1}
                  max={512}
                  step={1}
                  aria-label={t('size')}
                  title={t('size')}
                  value={size}
                  onChange={(e) =>
                    setSize(Math.max(1, Math.min(512, Math.round(Number(e.target.value) || 1))))
                  }
                />
                <span className="unit">px</span>
              </div>
            </>
          )}
          {points > 0 && (
            <div className="inline-actions">
              <span>{points}</span>
              <button
                className="small-button"
                disabled={points < 3 || disabled}
                onClick={() => canvas.current?.finish()}
              >
                <Check size={14} />
                {t('finish')}
              </button>
              <IconButton title={t('cancel')} onClick={() => canvas.current?.cancel()}>
                <X size={16} />
              </IconButton>
            </div>
          )}
          {selection && (
            <IconButton title={t('selected')} onClick={removeRegion}>
              <Trash2 size={17} />
            </IconButton>
          )}
          <div className="spacer" />
          <IconButton title={t('properties')} active={panel} onClick={() => setPanel(!panel)}>
            <SlidersHorizontal size={18} />
          </IconButton>
        </div>
        <div className="canvas-wrap">
          <MapCanvas
            ref={canvas}
            doc={displayDoc}
            layer={layer}
            tool={tool}
            size={size}
            value={value}
            opacity={opacity}
            visible={visible}
            maskVisibility={visibility}
            maskOpacity={opacities}
            angle={angle}
            rect={rect}
            selection={selection}
            disabled={disabled}
            onStroke={onChange}
            onShape={shape}
            onClick={click}
            onPick={(v) => {
              if (layer === 'map') {
                if (v !== 0 && v !== 255) setGray(v);
                setColor(v);
              } else {
                setClear(v === 0);
                if (layer === 'speed' && v > 0 && v <= 100) setSpeed(v);
              }
              setTool(previousTool.current === 'picker' ? 'pen' : previousTool.current);
            }}
            onOrigin={setMeta}
            onAngle={setAngle}
            onRect={setRect}
            onPoints={setPoints}
            onZoom={setZoom}
            onPosition={setPosition}
          />
          {hint && <div className="canvas-hint">{hint}</div>}
          <div className="zoom-control">
            <IconButton title="−" onClick={() => canvas.current?.zoom(0.8)}>
              <Minus size={15} />
            </IconButton>
            <button onClick={() => canvas.current?.actual()} title={t('actual')}>
              {Math.round(zoom * 100)}%
            </button>
            <IconButton title="+" onClick={() => canvas.current?.zoom(1.25)}>
              <Plus size={15} />
            </IconButton>
            <IconButton title={t('fit')} onClick={() => canvas.current?.fit()}>
              <Maximize size={16} />
            </IconButton>
          </div>
        </div>
        <footer className="editor-status">
          <span>
            {doc.width.toLocaleString()} × {doc.height.toLocaleString()} px
          </span>
          <span>{doc.meta.resolution} m/px</span>
          <div className="spacer" />
          <span>{world ? `${world.x.toFixed(2)}, ${world.y.toFixed(2)} m` : '—'}</span>
        </footer>
      </main>
      {panel && (
        <aside className="properties" aria-label={t('properties')}>
          <div className="panel-title">
            {geometry
              ? t(tool as 'crop' | 'rotate' | 'origin')
              : layer !== 'map'
                ? t(layer)
                : t('properties')}
          </div>
          {layer !== 'map' && (
            <section className="property-section">
              <label className="check-row">
                <input
                  type="checkbox"
                  checked={visible}
                  onChange={(e) => setVisibility({ ...visibility, [layer]: e.target.checked })}
                />
                {t('masked')}
              </label>
              <label className="slider-field">
                <span>
                  {t('opacity')}
                  <b>{Math.round(opacity * 100)}%</b>
                </span>
                <input
                  aria-label={t('opacity')}
                  type="range"
                  min={0}
                  max={100}
                  value={opacity * 100}
                  onChange={(e) =>
                    setOpacities({ ...opacities, [layer]: Number(e.target.value) / 100 })
                  }
                />
              </label>
              {layer === 'speed' && (
                <label className="slider-field">
                  <span>
                    {t('speedValue')}
                    <b>{speed}%</b>
                  </span>
                  <input
                    aria-label={t('speedValue')}
                    type="range"
                    min={1}
                    max={100}
                    value={speed}
                    onChange={(e) => setSpeed(Number(e.target.value))}
                  />
                </label>
              )}
              <p className="muted">{t('polygonHint')}</p>
              {Math.abs(meta.origin[2]) > 1e-8 && <p className="notice">{t('navHint')}</p>}
            </section>
          )}
          {tool === 'rotate' && (
            <section className="property-section">
              <label className="slider-field">
                <span>
                  {t('angle')}
                  <b>{angle.toFixed(1)}°</b>
                </span>
                <input
                  aria-label={t('angle')}
                  type="range"
                  min={-180}
                  max={180}
                  step={0.1}
                  value={angle}
                  onChange={(e) => setAngle(Number(e.target.value))}
                />
              </label>
              <NumberField label="°" value={angle} onChange={setAngle} />
              <div className="button-row">
                <button onClick={() => setAngle(angle - 90)}>−90°</button>
                <button onClick={() => setAngle(angle + 90)}>+90°</button>
              </div>
            </section>
          )}
          {tool === 'crop' && (
            <section className="property-section">
              <p className="muted">{t('cropHint')}</p>
              {rect && (
                <div className="field-grid">
                  {(['x', 'y', 'width', 'height'] as const).map((key) => (
                    <NumberField
                      key={key}
                      label={
                        key === 'width' ? 'W (px)' : key === 'height' ? 'H (px)' : key.toUpperCase()
                      }
                      value={rect[key]}
                      min={key === 'width' || key === 'height' ? 1 : 0}
                      step={1}
                      onChange={(v) => setRect({ ...rect, [key]: v })}
                    />
                  ))}
                </div>
              )}
            </section>
          )}
          <section className="property-section">
            <NumberField
              label={`${t('resolution')} (m/px)`}
              value={meta.resolution}
              min={0.000001}
              onChange={(v) => setMeta({ ...meta, resolution: v })}
            />
            <div className="field-grid">
              {[0, 1].map((i) => (
                <NumberField
                  key={i}
                  label={`${i === 0 ? 'X' : 'Y'} (m)`}
                  value={meta.origin[i]}
                  onChange={(v) => {
                    const origin = [...meta.origin] as Meta['origin'];
                    origin[i] = v;
                    setMeta({ ...meta, origin });
                  }}
                />
              ))}
            </div>
            <NumberField
              label="Yaw (°)"
              value={(meta.origin[2] * 180) / Math.PI}
              onChange={(v) =>
                setMeta({ ...meta, origin: [meta.origin[0], meta.origin[1], (v * Math.PI) / 180] })
              }
            />
          </section>
          {draft && (
            <div className="panel-actions">
              <button className="primary" disabled={disabled} onClick={() => void apply()}>
                <Check size={16} />
                {t('apply')}
              </button>
              <button disabled={disabled} onClick={cancel}>
                {t('cancel')}
              </button>
            </div>
          )}
          <details className="property-section">
            <summary>
              <ChevronRight size={14} />
              YAML
            </summary>
            <NumberField
              label="free_thresh"
              value={meta.free_thresh}
              onChange={(v) => setMeta({ ...meta, free_thresh: v })}
            />
            <NumberField
              label="occupied_thresh"
              value={meta.occupied_thresh}
              onChange={(v) => setMeta({ ...meta, occupied_thresh: v })}
            />
            <label className="check-row">
              <input
                type="checkbox"
                checked={meta.negate === 1}
                onChange={(e) => setMeta({ ...meta, negate: e.target.checked ? 1 : 0 })}
              />
              negate
            </label>
          </details>
        </aside>
      )}
    </fieldset>
  );
});
