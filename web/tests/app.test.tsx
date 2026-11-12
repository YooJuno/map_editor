// @vitest-environment jsdom
import { beforeEach, describe, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { forwardRef, useImperativeHandle } from 'react';
const f = vi.hoisted(() => ({
  user: { uid: 'alice', email: 'alice@example.com', displayName: 'Alice', photoURL: null },
  map: {
    name: 'Office',
    width: 2,
    height: 2,
    pixels: Uint8Array.of(0, 255, 205, 0),
    meta: {
      resolution: 0.05,
      origin: [0, 0, 0],
      negate: 0,
      free_thresh: 0.196,
      occupied_thresh: 0.65,
      mode: 'trinary',
    },
  },
  save: vi.fn(),
  global: false,
  workspaces: [] as any[],
  leases: [] as any[],
}));
vi.mock('firebase/auth', () => ({
  onAuthStateChanged: (_auth: unknown, callback: (u: unknown) => void) => {
    callback(f.user);
    return () => {};
  },
}));
vi.mock('../src/firebase', () => ({
  auth: { currentUser: f.user },
  db: {},
  finishLogin: async () => null,
  refreshAdminRole: async () => f.global,
  login: async () => {},
  logout: async () => {},
  watchMaps: (update: (rows: any[]) => void) => {
    update([
      {
        id: 'map1',
        name: 'Office',
        width: 2,
        height: 2,
        resolution: 0.05,
        bytes: 1024,
        preview: 'data:',
        revision: 'r1',
        previousRevision: '',
        chunks: 1,
        trashed: false,
        updatedAt: { toDate: () => new Date() },
      },
    ]);
    return () => {};
  },
  loadMap: async (id: string, workspaceId?: string) => ({
    map: f.map,
    cloud: { id, revision: 'r1', ...(workspaceId ? { workspaceId } : {}) },
  }),
  saveMap: (...args: any[]) => f.save(...args),
  renameMap: vi.fn(),
  trashMap: vi.fn(),
  errorMessage: (e: Error) => e.message,
  network: async (p: Promise<any>) => p,
}));
vi.mock('../src/workspaces', async (importOriginal) => ({
  ...(await importOriginal<any>()),
  watchWorkspaces: (_admin: boolean, update: (rows: any[]) => void) => {
    update(f.workspaces);
    return () => {};
  },
  watchLeases: (_id: string, update: (rows: any[]) => void) => {
    update(f.leases);
    return () => {};
  },
  acquireLease: async () => ({
    acquired: false,
    context: { workspaceId: 'team', mapId: 'map1', sessionId: 'new' },
  }),
}));
vi.mock('../src/Editor', async (importOriginal) => {
  const original = await importOriginal<any>();
  return {
    ...original,
    Editor: forwardRef(function FakeEditor(props: any, ref) {
      useImperativeHandle(ref, () => ({ prepare: async () => props.doc }));
      return <div data-testid="editor" data-disabled={props.disabled} />;
    }),
  };
});
vi.mock('../src/jobs', () => ({ job: async () => new Uint8Array(4) }));
import App from '../src/App';
beforeEach(() => {
  cleanup();
  localStorage.clear();
  f.global = false;
  f.workspaces = [];
  f.leases = [];
  f.save.mockReset().mockResolvedValue({ id: 'map1', revision: 'r2' });
  vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockImplementation(
    () =>
      ({
        createImageData: (w: number, h: number) => ({ data: new Uint8ClampedArray(w * h * 4) }),
        putImageData: () => {},
      }) as any,
  );
  vi.spyOn(HTMLCanvasElement.prototype, 'toDataURL').mockReturnValue('data:image/webp;base64,a');
});
async function open() {
  fireEvent.click(await screen.findByRole('button', { name: /^Office(?:\s|$)/ }));
  await screen.findByRole('textbox', { name: '지도 이름' });
}
describe('library and editor integration', () => {
  it('saves an inline name edit with Ctrl+S before the input loses focus', async () => {
    render(<App />);
    await open();
    const name = screen.getByRole('textbox', { name: '지도 이름' });
    fireEvent.change(name, { target: { value: 'Updated' } });
    fireEvent.keyDown(name, { key: 's', ctrlKey: true });
    await waitFor(() => expect(f.save).toHaveBeenCalled());
    expect(f.save.mock.calls[0][0].name).toBe('Updated');
  });
  it('cancels inline name changes with Escape', async () => {
    render(<App />);
    await open();
    const name = screen.getByRole('textbox', { name: '지도 이름' }) as HTMLInputElement;
    fireEvent.change(name, { target: { value: 'Unwanted' } });
    fireEvent.keyDown(name, { key: 'Escape' });
    expect(name.value).toBe('Office');
    expect(f.save).not.toHaveBeenCalled();
  });
  it('closes card menus with an outside click or Escape', async () => {
    render(<App />);
    await screen.findByRole('button', { name: /^Office(?:\s|$)/ });
    const summary = screen.getByLabelText('More'),
      details = summary.parentElement as HTMLDetailsElement;
    fireEvent.click(summary);
    expect(details.open).toBe(true);
    fireEvent.pointerDown(document.body);
    expect(details.open).toBe(false);
    fireEvent.click(summary);
    fireEvent.keyDown(document, { key: 'Escape' });
    expect(details.open).toBe(false);
  });
  it('closes a card menu when selecting Rename', async () => {
    render(<App />);
    await screen.findByRole('button', { name: /^Office(?:\s|$)/ });
    const summary = screen.getByLabelText('More'),
      details = summary.parentElement as HTMLDetailsElement;
    fireEvent.click(summary);
    fireEvent.click(screen.getByRole('button', { name: '이름 변경' }));
    expect(details.open).toBe(false);
    expect(screen.getByRole('dialog', { name: '이름 변경' })).toBeTruthy();
  });
  it('shows ZIP size without the removed marketing headings', async () => {
    render(<App />);
    await screen.findByRole('button', { name: /^Office(?:\s|$)/ });
    expect(screen.getByTitle('ZIP 파일 크기').textContent).toBe('1.0 KiB');
    expect(screen.queryByText('MAP LIBRARY')).toBeNull();
    expect(screen.queryByText('WEB')).toBeNull();
  });
  it('keeps map deletion hidden for workspace members and opens occupied maps read-only', async () => {
    f.workspaces = [
      { id: 'team', name: 'Team', memberIds: ['alice', 'bob'], adminIds: ['bob'], deleted: false },
    ];
    f.leases = [
      {
        id: 'map1',
        uid: 'bob',
        sessionId: 'occupied',
        name: 'Bob',
        active: true,
        updatedAt: { toMillis: () => Date.now() },
      },
    ];
    render(<App />);
    fireEvent.click(await screen.findByRole('button', { name: 'Team' }));
    fireEvent.click(screen.getByLabelText('More'));
    expect(screen.queryByRole('button', { name: '휴지통으로 이동' })).toBeNull();
    await open();
    expect(screen.getByTestId('editor').getAttribute('data-disabled')).toBe('true');
    expect((screen.getByRole('textbox', { name: '지도 이름' }) as HTMLInputElement).disabled).toBe(
      true,
    );
  });
});
