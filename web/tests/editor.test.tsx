// @vitest-environment jsdom
import { beforeEach, describe, expect, it, vi } from 'vitest';
import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { forwardRef, useImperativeHandle, createRef } from 'react';
import { Editor, type EditorHandle } from '../src/Editor';
import { defaults, type MapDoc, type Point } from '../src/map';
import { translator } from '../src/i18n';
vi.mock('../src/MapCanvas', () => ({
  MapCanvas: forwardRef(function FakeCanvas(
    props: {
      onPoints: (n: number) => void;
      onShape: (p: Point[]) => void;
      onClick: (p: Point) => void;
      onRect: (r: unknown) => void;
    },
    ref,
  ) {
    useImperativeHandle(ref, () => ({
      cancel: () => props.onPoints(0),
      finish: () =>
        props.onShape([
          { x: 0, y: 0 },
          { x: 2, y: 0 },
          { x: 2, y: 2 },
          { x: 0, y: 2 },
        ]),
    }));
    return (
      <div data-testid="canvas">
        <button onClick={() => props.onPoints(4)}>place four points</button>
        <button onClick={() => props.onClick({ x: 1, y: 1 })}>pick region</button>
        <button onClick={() => props.onRect({ x: 0, y: 0, width: 2, height: 2 })}>crop box</button>
      </div>
    );
  }),
}));
vi.mock('../src/jobs', () => ({
  job: vi.fn(async (task: { doc: MapDoc }) => task.doc),
}));
const t = translator('ko');
const make = (): MapDoc => ({
  name: 'Office',
  width: 4,
  height: 3,
  pixels: new Uint8Array(12).fill(205),
  meta: defaults(),
});
function setup(disabled = false) {
  const onChange = vi.fn(),
    ref = createRef<EditorHandle>();
  const result = render(
    <Editor
      ref={ref}
      doc={make()}
      t={t}
      disabled={disabled}
      canUndo={false}
      canRedo={false}
      onUndo={vi.fn()}
      onRedo={vi.fn()}
      onChange={onChange}
      onDraft={vi.fn()}
      run={async (task) => task()}
    />,
  );
  return { ...result, onChange, ref };
}
beforeEach(cleanup);
describe('editor controls', () => {
  it('does not create or dirty a mask by opening its tab', () => {
    const { onChange } = setup();
    fireEvent.click(screen.getByRole('button', { name: '진입 금지', exact: true }));
    expect(onChange).not.toHaveBeenCalled();
  });
  it('places the eyedropper next to the swatches', () => {
    setup();
    const picker = screen.getByRole('button', { name: '스포이드 (I)' });
    expect(picker.parentElement?.querySelector('.swatches')).not.toBeNull();
  });
  it('exposes rotation as both a slider and direct input', () => {
    setup();
    fireEvent.click(screen.getByRole('button', { name: '회전', exact: true }));
    expect(screen.getByRole('slider', { name: '각도' })).toBeTruthy();
    expect(screen.getByRole('spinbutton', { name: '°' })).toBeTruthy();
  });
  it('finishes a four-point polygon with an explicit button', async () => {
    setup();
    fireEvent.click(screen.getByRole('button', { name: '다각형' }));
    fireEvent.click(screen.getByText('place four points'));
    expect((screen.getByRole('button', { name: '완료' }) as HTMLButtonElement).disabled).toBe(
      false,
    );
  });
  it('disables all editing inputs during an asynchronous operation', () => {
    setup(true);
    for (const input of screen.getAllByRole('spinbutton'))
      expect((input.closest('fieldset') as HTMLFieldSetElement).disabled).toBe(true);
    expect(
      (
        screen.getByRole('button', {
          name: '지도',
          exact: true,
        }) as HTMLButtonElement
      ).disabled,
    ).toBe(true);
  });
  it('applies numeric origin changes to the saved metadata', async () => {
    const { ref, onChange } = setup();
    fireEvent.change(screen.getByRole('spinbutton', { name: 'X (m)' }), {
      target: { value: '-12.5' },
    });
    fireEvent.blur(screen.getByRole('spinbutton', { name: 'X (m)' }));
    const doc = await ref.current!.prepare();
    expect(doc.meta.origin[0]).toBe(-12.5);
    expect(onChange).toHaveBeenCalled();
  });
  it('shows the selected language without changing map content', () => {
    const d = make(),
      change = vi.fn();
    const { rerender } = render(
      <Editor
        doc={d}
        t={t}
        disabled={false}
        canUndo={false}
        canRedo={false}
        onUndo={vi.fn()}
        onRedo={vi.fn()}
        onChange={change}
        onDraft={vi.fn()}
        run={async (f) => f()}
      />,
    );
    rerender(
      <Editor
        doc={d}
        t={translator('ja')}
        disabled={false}
        canUndo={false}
        canRedo={false}
        onUndo={vi.fn()}
        onRedo={vi.fn()}
        onChange={change}
        onDraft={vi.fn()}
        run={async (f) => f()}
      />,
    );
    expect(screen.getByRole('button', { name: '進入禁止' })).toBeTruthy();
    expect(change).not.toHaveBeenCalled();
  });
  it('prevents saving a polygon that has not been finished', async () => {
    const { ref } = setup();
    fireEvent.click(screen.getByText('place four points'));
    await waitFor(() => expect(screen.getByRole('button', { name: '완료' })).toBeTruthy());
    await expect(ref.current!.prepare()).rejects.toThrow('점을 찍고');
  });
});

it('switches tutorial tools, cancels unfinished geometry, and opens YAML settings without modifying pixels', async () => {
  const { ref, onChange } = setup();
  fireEvent.click(screen.getByRole('button', { name: '다각형' }));
  fireEvent.click(screen.getByText('place four points'));
  act(() => ref.current!.guide({ layer: 'speed', tool: 'rect' }));
  expect(screen.queryByRole('button', { name: '완료' })).toBeNull();
  expect(screen.getByRole('slider', { name: t('speedValue') })).toBeTruthy();
  act(() => ref.current!.guide({ layer: 'map', tool: 'pen', yaml: true }));
  expect(document.querySelector<HTMLDetailsElement>('[data-tour="yaml"]')!.open).toBe(true);
  expect(onChange).not.toHaveBeenCalled();
});
