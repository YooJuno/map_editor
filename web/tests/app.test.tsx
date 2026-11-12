// @vitest-environment jsdom
import { beforeEach, describe, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { forwardRef, useImperativeHandle } from 'react';
const f = vi.hoisted(() => ({
  user: {
    uid: 'alice',
    email: 'alice@example.com',
    displayName: 'Alice',
    photoURL: null,
  },
  map: {
    name: 'Office',
    width: 2,
    height: 2,
    pixels: Uint8Array.of(0, 255, 205, 0),
    keepout: Uint8Array.of(0, 100, 0, 0),
    speed: Uint8Array.of(0, 0, 30, 0),
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
  lease: vi.fn(),
  reserve: vi.fn(() => 'new-map'),
  extraNames: [] as string[],
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
      ...f.extraNames.map((name, i) => ({
        id: `extra-${i}`,
        name,
        trashed: false,
        width: 2,
        height: 2,
        resolution: 0.05,
        bytes: 100,
        preview: 'data:',
        updatedAt: { toDate: () => new Date() },
      })),
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
  reserveMapId: (...args: any[]) => f.reserve(...args),
  withLease: (...args: any[]) => f.lease(...args),
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
vi.mock('../src/jobs', () => ({
  job: async (task: { action: string }) =>
    task.action === 'import' ? { ...f.map } : new Uint8Array(4),
}));
import App from '../src/App';
beforeEach(() => {
  cleanup();
  localStorage.clear();
  window.history.replaceState(null, '', '/');
  f.global = false;
  f.workspaces = [];
  f.leases = [];
  f.extraNames = [];
  f.reserve.mockClear();
  f.lease
    .mockReset()
    .mockImplementation(async (workspaceId, mapId, action) =>
      action({ workspaceId, sessionId: 'copy-session' }),
    );
  f.save.mockReset().mockResolvedValue({ id: 'map1', revision: 'r2' });
  vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockImplementation(
    () =>
      ({
        createImageData: (w: number, h: number) => ({
          data: new Uint8ClampedArray(w * h * 4),
        }),
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
    const name = screen.getByRole('textbox', {
      name: '지도 이름',
    }) as HTMLInputElement;
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
      {
        id: 'team',
        name: 'Team',
        memberIds: ['alice', 'bob'],
        adminIds: ['bob'],
        deleted: false,
      },
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

describe('tutorial practice isolation', () => {
  beforeEach(() => {
    vi.stubGlobal(
      'ResizeObserver',
      class {
        observe() {}
        disconnect() {}
      },
    );
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue({
        ok: true,
        arrayBuffer: async () => new ArrayBuffer(4),
      }),
    );
  });
  it('opens a practice map and never calls cloud save', async () => {
    render(<App />);
    fireEvent.click(screen.getByRole('button', { name: '튜토리얼' }));
    await screen.findByRole('region', { name: '튜토리얼' });
    expect(screen.getByText('예제 지도')).toBeTruthy();
    const name = screen.getByRole('textbox', { name: '지도 이름' });
    fireEvent.change(name, { target: { value: 'Practice only' } });
    fireEvent.keyDown(name, { key: 's', ctrlKey: true });
    await screen.findByText(/저장 동작을 연습했습니다/);
    expect(f.save).not.toHaveBeenCalled();
    fireEvent.click(screen.getByRole('button', { name: '튜토리얼 종료' }));
    expect(screen.queryByTestId('editor')).toBeNull();
    expect(screen.queryByTestId('tutorial')).toBeNull();
  });
  it('advances instructions and restores the previous step', async () => {
    render(<App />);
    fireEvent.click(screen.getByRole('button', { name: '튜토리얼' }));
    await screen.findByRole('region', { name: '튜토리얼' });
    fireEvent.click(screen.getByRole('button', { name: '다음' }));
    expect(screen.getByText('세 가지 색')).toBeTruthy();
    fireEvent.click(screen.getByRole('button', { name: '이전' }));
    expect(screen.getByText('예제 지도')).toBeTruthy();
  });
});

describe('map duplication', () => {
  async function copy() {
    await screen.findByRole('button', { name: 'Office', exact: true });
    const menu = screen.getAllByLabelText('More')[0];
    fireEvent.click(menu);
    fireEvent.click(within(menu.parentElement!).getByRole('button', { name: '복제', exact: true }));
    await waitFor(() => expect(f.save).toHaveBeenCalled());
    expect((menu.parentElement as HTMLDetailsElement).open).toBe(false);
    return f.save.mock.calls[0];
  }
  it('creates a separate map containing both masks and metadata without overwriting the original', async () => {
    render(<App />);
    const args = await copy();
    expect(args[0]).toEqual({ ...f.map, name: 'Office (복사본)' });
    expect(args[2]).toBeUndefined();
    expect(f.map.name).toBe('Office');
    expect(f.lease).not.toHaveBeenCalled();
  });
  it('numbers copies when a name is already used', async () => {
    f.extraNames = ['Office (복사본)', 'Office (복사본 2)'];
    render(<App />);
    expect((await copy())[0].name).toBe('Office (복사본 3)');
  });
  it('lets workspace members create a copy under a new map lease', async () => {
    f.workspaces = [
      { id: 'team', name: 'Team', memberIds: ['alice', 'bob'], adminIds: ['bob'], deleted: false },
    ];
    render(<App />);
    fireEvent.click(await screen.findByRole('button', { name: 'Team', exact: true }));
    const args = await copy();
    expect(f.lease.mock.calls[0].slice(0, 2)).toEqual(['team', 'new-map']);
    expect(args[2]).toEqual({ id: 'new-map', revision: '', workspaceId: 'team' });
    expect(args[3]).toEqual({ workspaceId: 'team', sessionId: 'copy-session' });
  });
});


it('cancels duplication if the signed-in account changes while loading the source', async () => {
  const { auth } = await import('../src/firebase');
  const original = auth.currentUser;
  try {
    render(<App />);
    await screen.findByRole('button', { name: 'Office', exact: true });
    fireEvent.click(screen.getByLabelText('More'));
    fireEvent.click(screen.getByRole('button', {name: '복제', exact: true}));
    Object.assign(auth, { currentUser: { uid: 'different-account' } });
    await screen.findByText('로그인 계정이 바뀌었습니다. 다시 시도하세요.');
    expect(f.save).not.toHaveBeenCalled();
  } finally {
    Object.assign(auth, { currentUser: original });
  }
});
