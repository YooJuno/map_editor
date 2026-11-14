// Local-only checkpoint. An opaque URL key survives the OAuth round trip without
// relying on sessionStorage or putting map data in the URL.
const database = 'map-editor-login',
  store = 'drafts',
  ttl = 24 * 60 * 60 * 1000;
type Draft = { id: string; created: number; bytes: Uint8Array; name: string };
async function open() {
  return new Promise<IDBDatabase>((resolve, reject) => {
    const request = indexedDB.open(database, 1);
    request.onupgradeneeded = () => {
      request.result.createObjectStore(store, { keyPath: 'id' }).createIndex('created', 'created');
    };
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}
async function transaction<T>(
  mode: IDBTransactionMode,
  operation: (store: IDBObjectStore) => IDBRequest<T>,
) {
  const db = await open();
  try {
    return await new Promise<T>((resolve, reject) => {
      const tx = db.transaction(store, mode),
        request = operation(tx.objectStore(store));
      tx.oncomplete = () => resolve(request.result);
      tx.onabort = tx.onerror = () =>
        reject(tx.error || request.error || Error('Local storage failed'));
    });
  } finally {
    db.close();
  }
}
export async function writeLoginDraft(bytes: Uint8Array, name: string) {
  const id = crypto.randomUUID();
  await transaction('readwrite', (s) =>
    s.put({ id, created: Date.now(), bytes, name } satisfies Draft),
  );
  // Prune expired keys through the index; do not load old map payloads into memory.
  void (async () => {
    const db = await open();
    const tx = db.transaction(store, 'readwrite');
    const request = tx
      .objectStore(store)
      .index('created')
      .openKeyCursor(IDBKeyRange.upperBound(Date.now() - ttl));
    request.onsuccess = () => {
      const cursor = request.result;
      if (cursor) {
        tx.objectStore(store).delete(cursor.primaryKey);
        cursor.continue();
      }
    };
    tx.oncomplete = tx.onabort = tx.onerror = () => db.close();
  })().catch(() => {});
  return id;
}
export async function readLoginDraft(id: string) {
  const draft = await transaction<Draft | undefined>('readonly', (s) => s.get(id));
  return draft && draft.created > Date.now() - ttl
    ? { bytes: draft.bytes, name: draft.name }
    : undefined;
}
export async function deleteLoginDraft(id: string) {
  await transaction('readwrite', (s) => s.delete(id));
}
