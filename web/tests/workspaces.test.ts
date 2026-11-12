import { afterAll, beforeAll, beforeEach, describe, expect, it, vi } from 'vitest';
import {
  initializeTestEnvironment,
  assertFails,
  assertSucceeds,
  type RulesTestEnvironment,
} from '@firebase/rules-unit-testing';
import { GoogleAuthProvider, signInWithCredential, signOut } from 'firebase/auth';
import {
  collection,
  doc,
  getDocFromServer,
  getDocsFromServer,
  query,
  where,
  serverTimestamp,
  setDoc,
  updateDoc,
  writeBatch,
  Timestamp,
  Bytes,
} from 'firebase/firestore';
import { readFileSync } from 'node:fs';
import { defaults, pack, unpack, type MapDoc } from '../src/map';
vi.mock('../src/jobs', () => ({
  job: async (task: { action: string; doc: MapDoc; bytes: Uint8Array }) =>
    task.action === 'pack' ? pack(task.doc) : unpack(task.bytes),
}));
vi.stubEnv('VITE_USE_EMULATORS', 'true');
vi.stubGlobal('navigator', { onLine: true });
const backend = await import('../src/firebase'),
  ws = await import('../src/workspaces');
let env: RulesTestEnvironment;
async function signin(name = 'alice') {
  await signInWithCredential(
    backend.auth,
    GoogleAuthProvider.credential(
      JSON.stringify({
        sub: name,
        email: `${name}@example.com`,
        email_verified: true,
        iss: 'https://accounts.google.com',
        aud: 'demo-map-editor',
      }),
    ),
  );
  return backend.auth.currentUser!.uid;
}
const map = (): MapDoc => ({
  name: 'Office',
  width: 4,
  height: 2,
  pixels: Uint8Array.of(0, 0, 205, 255, 255, 205, 0, 0),
  meta: defaults(),
});
async function root(id: string) {
  return {
    ...(await getDocFromServer(doc(backend.db, 'workspaces', id))).data(),
    id,
  } as ws.Workspace;
}
async function addBob(id: string) {
  const alice = backend.auth.currentUser!.uid,
    workspace = await root(id);
  const bob = await signin('bob');
  await ws.joinWorkspace(workspace.linkId);
  await signin();
  await ws.resolveRequest(id, bob, true);
  return { alice, bob };
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
  await env.cleanup();
});
describe('shared workspace lifecycle and admission', () => {
  it('creates a workspace with its creator as administrator', async () => {
    const id = await ws.createWorkspace('Team'),
      d = await root(id);
    expect(d.adminIds).toEqual([backend.auth.currentUser!.uid]);
    expect(d.requireApproval).toBe(true);
    expect((await ws.getShareLink(d.linkId)).workspaceId).toBe(id);
  });
  it('lets a signed-out visitor inspect only the share link', async () => {
    const id = await ws.createWorkspace('Team'),
      d = await root(id);
    await signOut(backend.auth);
    expect((await ws.getShareLink(d.linkId)).name).toBe('Team');
    await expect(getDocFromServer(doc(backend.db, 'workspaces', id))).rejects.toThrow();
  });
  it('keeps applicants outside until an administrator approves them', async () => {
    const id = await ws.createWorkspace('Team'),
      d = await root(id);
    const bob = await signin('bob');
    expect((await ws.joinWorkspace(d.linkId)).status).toBe('pending');
    await expect(getDocFromServer(doc(backend.db, 'workspaces', id))).rejects.toThrow();
    await signin();
    await ws.resolveRequest(id, bob, true);
    await signin('bob');
    expect((await root(id)).memberIds).toContain(bob);
  });
  it('supports joining without approval when explicitly enabled', async () => {
    const id = await ws.createWorkspace('Team');
    await ws.updateWorkspace(await root(id), { requireApproval: false });
    const d = await root(id),
      bob = await signin('bob');
    expect((await ws.joinWorkspace(d.linkId)).status).toBe('approved');
    expect((await root(id)).memberIds).toContain(bob);
  });
  it('invalidates an old share link after rotation', async () => {
    const id = await ws.createWorkspace('Team'),
      old = await root(id);
    await ws.updateWorkspace(old, { rotateLink: true });
    await signOut(backend.auth);
    await expect(ws.getShareLink(old.linkId)).rejects.toThrow();
  });
  it('rejects a forged self-admission while approval is required', async () => {
    const id = await ws.createWorkspace('Team'),
      d = await root(id),
      bob = await signin('bob'),
      ref = doc(backend.db, 'workspaces', id),
      batch = writeBatch(backend.db);
    batch.set(doc(ref, 'joinRequests', bob), {
      uid: bob,
      name: 'Bob',
      email: 'bob@example.com',
      linkId: d.linkId,
      status: 'approved',
      requestedAt: serverTimestamp(),
    });
    batch.update(ref, { memberIds: [...d.memberIds, bob], updatedAt: serverTimestamp() });
    await assertFails(batch.commit());
  });
  it('lets workspace administrators remove members and immediately revokes access', async () => {
    const id = await ws.createWorkspace('Team'),
      { bob } = await addBob(id);
    await ws.changeMember(id, bob, 'remove');
    await signin('bob');
    await expect(getDocFromServer(doc(backend.db, 'workspaces', id))).rejects.toThrow();
  });
  it('lets administrators grant and remove workspace administrator roles', async () => {
    const id = await ws.createWorkspace('Team'),
      { bob } = await addBob(id);
    await ws.changeMember(id, bob, 'admin');
    expect((await root(id)).adminIds).toContain(bob);
    await ws.changeMember(id, bob, 'editor');
    expect((await root(id)).adminIds).not.toContain(bob);
  });
  it('prevents a member from promoting themselves', async () => {
    const id = await ws.createWorkspace('Team'),
      { bob } = await addBob(id);
    await signin('bob');
    await assertFails(
      updateDoc(doc(backend.db, 'workspaces', id), {
        adminIds: [bob],
        updatedAt: serverTimestamp(),
      }),
    );
  });
});
describe('editing leases and map permissions', () => {
  it('lets a member upload, edit, rename and download a workspace map', async () => {
    const id = await ws.createWorkspace('Team');
    await addBob(id);
    await signin('bob');
    const mapId = ws.reserveMapId(id),
      lease = await ws.acquireLease(id, mapId);
    expect(lease.acquired).toBe(true);
    const saved = await backend.saveMap(
      map(),
      'data:',
      { id: mapId, revision: '', workspaceId: id },
      lease.context,
    );
    await backend.renameMap(mapId, 'Renamed', lease.context);
    expect((await backend.loadMap(saved.id, id)).map.name).toBe('Renamed');
    await ws.renewLease(lease.context);
    await ws.releaseLease(lease.context);
  });
  it('blocks a second editor, including another tab of the same account', async () => {
    const id = await ws.createWorkspace('Team'),
      mapId = ws.reserveMapId(id);
    expect((await ws.acquireLease(id, mapId, 'first')).acquired).toBe(true);
    expect((await ws.acquireLease(id, mapId, 'second')).acquired).toBe(false);
  });
  it('allows another member to acquire a released lease', async () => {
    const id = await ws.createWorkspace('Team');
    await addBob(id);
    const mapId = ws.reserveMapId(id),
      first = await ws.acquireLease(id, mapId);
    await signin('bob');
    expect((await ws.acquireLease(id, mapId)).acquired).toBe(false);
    await signin();
    await ws.releaseLease(first.context);
    await signin('bob');
    expect((await ws.acquireLease(id, mapId)).acquired).toBe(true);
  });
  it('recovers an expired lease and rejects a stale writer', async () => {
    const id = await ws.createWorkspace('Team');
    await addBob(id);
    const mapId = ws.reserveMapId(id),
      first = await ws.acquireLease(id, mapId);
    await env.withSecurityRulesDisabled(async (context) =>
      updateDoc(doc(context.firestore(), 'workspaces', id, 'locks', mapId), {
        updatedAt: Timestamp.fromMillis(Date.now() - 120000),
      }),
    );
    await signin('bob');
    expect((await ws.acquireLease(id, mapId)).acquired).toBe(true);
    await signin();
    await expect(
      backend.saveMap(map(), 'data:', { id: mapId, revision: '', workspaceId: id }, first.context),
    ).rejects.toThrow();
  });
  it('denies saving a workspace map without its current lease', async () => {
    const id = await ws.createWorkspace('Team'),
      mapId = ws.reserveMapId(id);
    await expect(
      backend.saveMap(
        map(),
        'data:',
        { id: mapId, revision: '', workspaceId: id },
        { workspaceId: id, sessionId: 'missing' },
      ),
    ).rejects.toThrow();
  });
  it('lets members rename but reserves trash and restore for administrators', async () => {
    const id = await ws.createWorkspace('Team');
    await addBob(id);
    const mapId = ws.reserveMapId(id),
      first = await ws.acquireLease(id, mapId);
    await backend.saveMap(
      map(),
      'data:',
      { id: mapId, revision: '', workspaceId: id },
      first.context,
    );
    await ws.releaseLease(first.context);
    await signin('bob');
    const second = await ws.acquireLease(id, mapId);
    await backend.renameMap(mapId, 'Member rename', second.context);
    await expect(backend.trashMap(mapId, true, second.context)).rejects.toThrow();
    await ws.releaseLease(second.context);
    await signin();
    const admin = await ws.acquireLease(id, mapId);
    await backend.trashMap(mapId, true, admin.context);
    await backend.trashMap(mapId, false, admin.context);
  });
  it('denies private map access to a global shared-workspace administrator', async () => {
    const alice = backend.auth.currentUser!.uid;
    const saved = await backend.saveMap(map(), 'data:');
    const admin = env.authenticatedContext('global', { appAdmin: true }).firestore();
    await assertFails(getDocFromServer(doc(admin, 'users', alice, 'maps', saved.id)));
  });
  it('allows global administrators to inspect, manage and delete all shared workspaces', async () => {
    const id = await ws.createWorkspace('Team'),
      admin = env.authenticatedContext('global', { appAdmin: true }).firestore();
    await assertSucceeds(getDocFromServer(doc(admin, 'workspaces', id)));
    await assertSucceeds(getDocsFromServer(collection(admin, 'workspaces')));
    await assertSucceeds(
      updateDoc(doc(admin, 'workspaces', id), { name: 'Managed', updatedAt: serverTimestamp() }),
    );
    await assertSucceeds(
      updateDoc(doc(admin, 'workspaces', id), { deleted: true, updatedAt: serverTimestamp() }),
    );
    await expect(getDocFromServer(doc(backend.db, 'workspaces', id))).rejects.toThrow();
    await assertSucceeds(
      updateDoc(doc(admin, 'workspaces', id), { deleted: false, updatedAt: serverTimestamp() }),
    );
  });
  it('denies workspace deletion to an ordinary workspace administrator', async () => {
    const id = await ws.createWorkspace('Team');
    await assertFails(
      updateDoc(doc(backend.db, 'workspaces', id), { deleted: true, updatedAt: serverTimestamp() }),
    );
  });
  it('denies lock theft and forged chunk sessions', async () => {
    const id = await ws.createWorkspace('Team');
    await addBob(id);
    const mapId = ws.reserveMapId(id),
      lease = await ws.acquireLease(id, mapId);
    await signin('bob');
    await assertFails(
      setDoc(doc(backend.db, 'workspaces', id, 'locks', mapId), {
        uid: backend.auth.currentUser!.uid,
        name: 'Bob',
        sessionId: 'stolen',
        active: true,
        updatedAt: serverTimestamp(),
      }),
    );
    await assertFails(
      setDoc(doc(backend.db, 'workspaces', id, 'maps', mapId, 'payloads', 'x_0'), {
        revision: 'x',
        index: 0,
        data: Bytes.fromUint8Array(Uint8Array.of(1)),
        editorSession: lease.context.sessionId,
      }),
    );
  });
  it('lists only joined, active workspaces for ordinary accounts', async () => {
    const id = await ws.createWorkspace('Team'),
      alice = backend.auth.currentUser!.uid;
    const snap = await getDocsFromServer(
      query(
        collection(backend.db, 'workspaces'),
        where('memberIds', 'array-contains', alice),
        where('deleted', '==', false),
      ),
    );
    expect(snap.docs.map((d) => d.id)).toContain(id);
    await signin('bob');
    await assertFails(getDocsFromServer(collection(backend.db, 'workspaces')));
  });
});

it('grants exactly one lease when sessions race to open a map', async () => {
  const id = await ws.createWorkspace('Team'),
    mapId = ws.reserveMapId(id);
  const results = await Promise.all([
    ws.acquireLease(id, mapId, 'first'),
    ws.acquireLease(id, mapId, 'second'),
  ]);
  expect(results.filter((r) => r.acquired)).toHaveLength(1);
});
