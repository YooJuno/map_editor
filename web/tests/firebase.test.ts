import { beforeAll, beforeEach, afterAll, describe, expect, it, vi } from 'vitest';
import { initializeTestEnvironment, type RulesTestEnvironment } from '@firebase/rules-unit-testing';
import { GoogleAuthProvider, signInWithCredential, signOut } from 'firebase/auth';
import { doc, getDocFromServer, getDocsFromServer, collection } from 'firebase/firestore';
import { readFileSync } from 'node:fs';
import { defaults, pack, unpack, type MapDoc } from '../src/map';
vi.mock('../src/jobs', () => ({
  job: async (task: { action: string; doc: MapDoc; bytes: Uint8Array }) =>
    task.action === 'pack' ? pack(task.doc) : unpack(task.bytes),
}));
vi.stubEnv('VITE_USE_EMULATORS', 'true');
vi.stubGlobal('navigator', { onLine: true });
const backend = await import('../src/firebase');
let env: RulesTestEnvironment;
const map = (): MapDoc => ({
  name: 'Office',
  width: 4,
  height: 2,
  pixels: Uint8Array.of(0, 0, 205, 255, 255, 205, 0, 0),
  meta: defaults(),
  speed: Uint8Array.of(0, 30, 30, 0, 0, 0, 60, 0),
});
async function signin(uid = 'alice') {
  await signInWithCredential(
    backend.auth,
    GoogleAuthProvider.credential(
      JSON.stringify({
        sub: uid,
        email: `${uid}@example.com`,
        email_verified: true,
        iss: 'https://accounts.google.com',
        aud: 'demo-map-editor',
      }),
    ),
  );
}
beforeAll(async () => {
  env = await initializeTestEnvironment({
    projectId: 'demo-map-editor',
    firestore: { rules: readFileSync(new URL('../firestore.rules', import.meta.url), 'utf8') },
  });
});
beforeEach(async () => {
  await env.clearFirestore();
  await signin();
});
afterAll(async () => {
  await signOut(backend.auth);
  await env?.cleanup();
});
describe('actual cloud persistence client', () => {
  it('saves compressed map and masks, then reloads the exact pixels', async () => {
    const d = map(),
      cloud = await backend.saveMap(d, 'data:image/webp;base64,a'),
      result = await backend.loadMap(cloud.id);
    expect(result.map.pixels).toEqual(d.pixels);
    expect(result.map.speed).toEqual(d.speed);
    expect(result.cloud).toEqual(cloud);
  });
  it('updates an existing map and retains its preceding revision', async () => {
    const d = map(),
      first = await backend.saveMap(d, 'data:'),
      edited = { ...d, pixels: Uint8Array.of(100, ...d.pixels.slice(1)) };
    const second = await backend.saveMap(edited, 'data:', first);
    expect(second.id).toBe(first.id);
    expect(second.revision).not.toBe(first.revision);
    const meta = await getDocFromServer(
      doc(backend.db, `users/${backend.auth.currentUser!.uid}/maps`, first.id),
    );
    expect(meta.data()!.previousRevision).toBe(first.revision);
    expect((await backend.loadMap(first.id)).map.pixels[0]).toBe(100);
  });
  it('rejects a stale concurrent save without overwriting the current map', async () => {
    const d = map(),
      first = await backend.saveMap(d, 'data:');
    await backend.saveMap({ ...d, name: 'Latest' }, 'data:', first);
    await expect(backend.saveMap({ ...d, name: 'Stale' }, 'data:', first)).rejects.toThrow(
      '다른 창',
    );
    expect((await backend.loadMap(first.id)).map.name).toBe('Latest');
  });
  it('renames, trashes, and restores without losing binary files', async () => {
    const first = await backend.saveMap(map(), 'data:');
    await backend.renameMap(first.id, 'Renamed');
    expect((await backend.loadMap(first.id)).map.name).toBe('Renamed');
    await backend.trashMap(first.id, true);
    await expect(backend.loadMap(first.id)).rejects.toThrow('휴지통');
    await backend.trashMap(first.id, false);
    expect((await backend.loadMap(first.id)).map.name).toBe('Renamed');
  });
  it('does not let a second Google identity open another user’s map', async () => {
    const first = await backend.saveMap(map(), 'data:');
    const owner = backend.auth.currentUser!.uid;
    await signin('bob');
    await expect(backend.loadMap(first.id)).rejects.toThrow();
    await expect(
      getDocFromServer(doc(backend.db, `users/${owner}/maps`, first.id)),
    ).rejects.toThrow();
  });
  it('keeps binary chunks out of the map list', async () => {
    await backend.saveMap(map(), 'data:');
    const list = await getDocsFromServer(
      collection(backend.db, `users/${backend.auth.currentUser!.uid}/maps`),
    );
    expect(list.size).toBe(1);
    expect(list.docs[0].data()).not.toHaveProperty('pixels');
    expect(list.docs[0].data()).not.toHaveProperty('data');
  });
  it('rejects offline saves before queuing publication', async () => {
    vi.stubGlobal('navigator', { onLine: false });
    await expect(backend.saveMap(map(), 'data:')).rejects.toThrow('인터넷');
    vi.stubGlobal('navigator', { onLine: true });
    const list = await getDocsFromServer(
      collection(backend.db, `users/${backend.auth.currentUser!.uid}/maps`),
    );
    expect(list.empty).toBe(true);
  });
});
