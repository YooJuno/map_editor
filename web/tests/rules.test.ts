import { beforeAll, afterAll, beforeEach, describe, it } from 'vitest';
import {
  initializeTestEnvironment,
  assertFails,
  assertSucceeds,
  type RulesTestEnvironment,
} from '@firebase/rules-unit-testing';
import {
  doc,
  getDoc,
  getDocs,
  collection,
  query,
  orderBy,
  setDoc,
  updateDoc,
  deleteDoc,
  Bytes,
  serverTimestamp,
  writeBatch,
  runTransaction,
} from 'firebase/firestore';
import { readFileSync } from 'node:fs';
let env: RulesTestEnvironment;
const path = 'users/alice/maps/office';
const valid = () => ({
  name: 'Office',
  width: 20,
  height: 10,
  resolution: 0.05,
  revision: 'first',
  previousRevision: '',
  chunks: 1,
  bytes: 3,
  preview: 'data:image/webp;base64,a',
  createdAt: serverTimestamp(),
  updatedAt: serverTimestamp(),
  trashed: false,
});
beforeAll(async () => {
  env = await initializeTestEnvironment({
    projectId: 'demo-map-editor',
    firestore: { rules: readFileSync(new URL('../firestore.rules', import.meta.url), 'utf8') },
  });
});
beforeEach(async () => env.clearFirestore());
afterAll(async () => env?.cleanup());
describe('account isolation and recoverable map lifecycle', () => {
  it('rejects signed-out reads and writes', async () => {
    const db = env.unauthenticatedContext().firestore();
    await assertFails(setDoc(doc(db, path), valid()));
    await assertFails(getDoc(doc(db, path)));
  });
  it('lets an owner create, list, rename, trash and restore a map', async () => {
    const db = env.authenticatedContext('alice').firestore(),
      ref = doc(db, path);
    await assertSucceeds(setDoc(ref, valid()));
    await assertSucceeds(
      getDocs(query(collection(db, 'users/alice/maps'), orderBy('updatedAt', 'desc'))),
    );
    await assertSucceeds(updateDoc(ref, { name: 'New name', updatedAt: serverTimestamp() }));
    await assertSucceeds(updateDoc(ref, { trashed: true, updatedAt: serverTimestamp() }));
    await assertSucceeds(updateDoc(ref, { trashed: false, updatedAt: serverTimestamp() }));
  });
  it('denies another account access to metadata and binary content', async () => {
    const db = env.authenticatedContext('bob').firestore();
    await assertFails(getDoc(doc(db, path)));
    await assertFails(setDoc(doc(db, path), valid()));
    await assertFails(getDocs(collection(db, `${path}/payloads`)));
    await assertFails(
      setDoc(doc(db, `${path}/payloads/first_0`), {
        revision: 'first',
        index: 0,
        data: Bytes.fromUint8Array(Uint8Array.of(1, 2, 3)),
      }),
    );
  });
  it('blocks permanent map deletion', async () => {
    const db = env.authenticatedContext('alice').firestore(),
      ref = doc(db, path);
    await setDoc(ref, valid());
    await assertFails(deleteDoc(ref));
  });
  it('rejects malformed metadata and excessive image sizes', async () => {
    const db = env.authenticatedContext('alice').firestore();
    for (const bad of [
      { width: 0 },
      { width: 33554433 },
      { name: '' },
      { chunks: 41 },
      { bytes: 20971521 },
      { resolution: -1 },
      { extra: 'unexpected' },
    ])
      await assertFails(setDoc(doc(db, path), { ...valid(), ...bad }));
  });
  it('preserves creation timestamps', async () => {
    const db = env.authenticatedContext('alice').firestore(),
      ref = doc(db, path);
    await setDoc(ref, valid());
    await assertFails(updateDoc(ref, { createdAt: new Date(0), updatedAt: serverTimestamp() }));
  });
  it('permits immutable staged chunks before publishing metadata', async () => {
    const db = env.authenticatedContext('alice').firestore(),
      chunk = doc(db, `${path}/payloads/first_0`),
      data = { revision: 'first', index: 0, data: Bytes.fromUint8Array(Uint8Array.of(1, 2, 3)) };
    await assertSucceeds(setDoc(chunk, data));
    await assertSucceeds(setDoc(doc(db, path), valid()));
    await assertSucceeds(getDoc(chunk));
    await assertFails(setDoc(chunk, data));
    await assertFails(deleteDoc(chunk));
  });
  it('rejects incorrectly named, malformed and oversized chunks', async () => {
    const db = env.authenticatedContext('alice').firestore();
    await assertFails(
      setDoc(doc(db, `${path}/payloads/wrong`), {
        revision: 'first',
        index: 0,
        data: Bytes.fromUint8Array(Uint8Array.of(1)),
      }),
    );
    await assertFails(
      setDoc(doc(db, `${path}/payloads/first_0`), {
        revision: 'first',
        index: 0,
        data: Bytes.fromUint8Array(new Uint8Array(600001)),
      }),
    );
  });
  it('protects current and preceding revisions while allowing obsolete cleanup', async () => {
    const db = env.authenticatedContext('alice').firestore(),
      ref = doc(db, path);
    for (const revision of ['first', 'second', 'third'])
      await setDoc(doc(ref, 'payloads', `${revision}_0`), {
        revision,
        index: 0,
        data: Bytes.fromUint8Array(Uint8Array.of(1)),
      });
    await setDoc(ref, { ...valid(), revision: 'third', previousRevision: 'second' });
    await assertFails(deleteDoc(doc(ref, 'payloads', 'third_0')));
    await assertFails(deleteDoc(doc(ref, 'payloads', 'second_0')));
    await assertSucceeds(deleteDoc(doc(ref, 'payloads', 'first_0')));
  });
  it('supports complete multi-chunk publication in a transaction', async () => {
    const db = env.authenticatedContext('alice').firestore(),
      ref = doc(db, path),
      batch = writeBatch(db);
    for (let i = 0; i < 3; i++)
      batch.set(doc(ref, 'payloads', `first_${i}`), {
        revision: 'first',
        index: i,
        data: Bytes.fromUint8Array(new Uint8Array(600000)),
      });
    await assertSucceeds(batch.commit());
    await assertSucceeds(
      runTransaction(db, async (tx) => {
        await tx.get(ref);
        tx.set(ref, { ...valid(), chunks: 3, bytes: 1800000 });
      }),
    );
  });
});
