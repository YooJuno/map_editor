import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { IDBFactory, IDBKeyRange, IDBObjectStore } from 'fake-indexeddb';
import { authHost, googleLogin, mobileLogin } from '../src/login-flow';
import { writeLoginDraft, readLoginDraft, deleteLoginDraft } from '../src/login-draft';

afterEach(() => {
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
});
describe('Google login navigation', () => {
  it('keeps the auth handler on the app origin on both supported Hosting domains', () => {
    for (const host of ['map-editor-juno.web.app', 'map-editor-juno.firebaseapp.com'])
      expect(authHost(host)).toBe(host);
    expect(authHost('localhost')).toBe('map-editor-juno.web.app');
    expect(authHost('untrusted.example')).toBe('map-editor-juno.web.app');
  });
  it('uses redirects on phones and iPads with a desktop user agent', () => {
    expect(mobileLogin({ userAgent: 'iPhone CriOS', platform: 'iPhone', maxTouchPoints: 5 })).toBe(
      true,
    );
    expect(mobileLogin({ userAgent: 'Android Chrome', platform: 'Linux', maxTouchPoints: 5 })).toBe(
      true,
    );
    expect(
      mobileLogin({ userAgent: 'Macintosh Safari', platform: 'MacIntel', maxTouchPoints: 5 }),
    ).toBe(true);
    expect(
      mobileLogin({ userAgent: 'Macintosh Safari', platform: 'MacIntel', maxTouchPoints: 0 }),
    ).toBe(false);
  });
  it('finishes the checkpoint before starting a mobile redirect', async () => {
    const calls: string[] = [],
      popup = vi.fn();
    expect(
      await googleLogin(
        true,
        popup,
        async () => {
          calls.push('redirect');
        },
        async () => {
          await Promise.resolve();
          calls.push('stored');
        },
      ),
    ).toBe(false);
    expect(calls).toEqual(['stored', 'redirect']);
    expect(popup).not.toHaveBeenCalled();
  });
  it('keeps desktop popups within the original user gesture and checkpoints only on fallback', async () => {
    const checkpoint = vi.fn(),
      redirect = vi.fn();
    expect(await googleLogin(false, async () => {}, redirect, checkpoint)).toBe(true);
    expect(checkpoint).not.toHaveBeenCalled();
    expect(redirect).not.toHaveBeenCalled();
    await googleLogin(
      false,
      async () => {
        throw { code: 'auth/popup-blocked' };
      },
      redirect,
      checkpoint,
    );
    expect(checkpoint).toHaveBeenCalledOnce();
    expect(redirect).toHaveBeenCalledOnce();
  });
  it('never navigates away if the local checkpoint failed or the popup was dismissed', async () => {
    const redirect = vi.fn();
    await expect(
      googleLogin(true, vi.fn(), redirect, async () => {
        throw Error('storage full');
      }),
    ).rejects.toThrow('storage full');
    await expect(
      googleLogin(
        false,
        async () => {
          throw { code: 'auth/popup-closed-by-user' };
        },
        redirect,
        vi.fn(),
      ),
    ).rejects.toEqual({ code: 'auth/popup-closed-by-user' });
    expect(redirect).not.toHaveBeenCalled();
  });
});
describe('local login checkpoints', () => {
  beforeEach(() => {
    vi.stubGlobal('indexedDB', new IDBFactory());
    vi.stubGlobal('IDBKeyRange', IDBKeyRange);
  });
  it('keeps independent binary drafts across database connections, without sessionStorage', async () => {
    const first = await writeLoginDraft(Uint8Array.of(0, 205, 255), '지도: 1/2'),
      second = await writeLoginDraft(Uint8Array.of(100), 'Second');
    expect(first).not.toBe(second);
    expect(await readLoginDraft(first)).toEqual({
      bytes: Uint8Array.of(0, 205, 255),
      name: '지도: 1/2',
    });
    expect(await readLoginDraft(second)).toEqual({ bytes: Uint8Array.of(100), name: 'Second' });
    await deleteLoginDraft(first);
    expect(await readLoginDraft(first)).toBeUndefined();
    expect(await readLoginDraft(second)).toEqual({ bytes: Uint8Array.of(100), name: 'Second' });
  });
  it('does not restore expired or missing checkpoints', async () => {
    const key = await writeLoginDraft(Uint8Array.of(1), 'Office'),
      now = Date.now();
    vi.spyOn(Date, 'now').mockReturnValue(now + 25 * 60 * 60 * 1000);
    expect(await readLoginDraft(key)).toBeUndefined();
    expect(await readLoginDraft('missing')).toBeUndefined();
  });
  it('reports a failed storage write instead of allowing navigation', async () => {
    vi.spyOn(IDBObjectStore.prototype, 'put').mockImplementation(() => {
      throw new DOMException('Full', 'QuotaExceededError');
    });
    await expect(writeLoginDraft(Uint8Array.of(1), 'Office')).rejects.toThrow('Full');
  });
});
