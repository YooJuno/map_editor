import { initializeApp } from 'firebase/app';
import {
  getAuth,
  getIdTokenResult,
  GoogleAuthProvider,
  signInWithPopup,
  signInWithRedirect,
  getRedirectResult,
  signOut,
  connectAuthEmulator,
} from 'firebase/auth';
import {
  getFirestore,
  connectFirestoreEmulator,
  collection,
  doc,
  onSnapshot,
  query,
  orderBy,
  getDocFromServer,
  getDocsFromServer,
  where,
  writeBatch,
  runTransaction,
  serverTimestamp,
  Bytes,
  updateDoc,
  type Timestamp,
} from 'firebase/firestore';
import { MAX_CLOUD_BYTES, type MapDoc } from './map';
import { job } from './jobs';
import { authHost, googleLogin, mobileLogin } from './login-flow';

const emulators = import.meta.env.DEV && import.meta.env.VITE_USE_EMULATORS === 'true';
const app = initializeApp({
  apiKey: 'AIzaSyDh9mA7VvLKDLmzEj7uMLR9MsD0c6j6yP4',
  authDomain: authHost(typeof location === 'undefined' ? '' : location.hostname),
  projectId: emulators ? 'demo-map-editor' : 'map-editor-juno',
  storageBucket: 'map-editor-juno.firebasestorage.app',
  messagingSenderId: '687727395742',
  appId: '1:687727395742:web:d0516e8944d7654b338c8f',
  measurementId: 'G-EL327B6THJ',
});
export const auth = getAuth(app),
  db = getFirestore(app);
if (emulators) {
  connectAuthEmulator(auth, 'http://127.0.0.1:9099', { disableWarnings: true });
  connectFirestoreEmulator(db, '127.0.0.1', 8080);
}
const provider = new GoogleAuthProvider();
provider.setCustomParameters({ prompt: 'select_account' });
export const login = (beforeRedirect: () => Promise<void> = async () => {}) =>
  googleLogin(
    !emulators && mobileLogin(navigator),
    () => signInWithPopup(auth, provider),
    () => signInWithRedirect(auth, provider),
    beforeRedirect,
  );
export const finishLogin = () => getRedirectResult(auth);
export const logout = () => signOut(auth);
export type MapScope = { workspaceId: string; sessionId: string };
export type CloudRef = { id: string; revision: string; workspaceId?: string };
export async function refreshAdminRole() {
  return (
    !!auth.currentUser && (await getIdTokenResult(auth.currentUser, true)).claims.appAdmin === true
  );
}
export type CloudMap = {
  id: string;
  name: string;
  width: number;
  height: number;
  resolution: number;
  revision: string;
  previousRevision: string;
  chunks: number;
  bytes: number;
  preview: string;
  createdAt: Timestamp;
  updatedAt: Timestamp;
  trashed: boolean;
};
export function mapCollection(workspaceId?: string) {
  if (!auth.currentUser) throw Error('Google 계정으로 로그인하세요.');
  return workspaceId
    ? collection(db, 'workspaces', workspaceId, 'maps')
    : collection(db, 'users', auth.currentUser.uid, 'maps');
}
export function watchMaps(
  update: (maps: CloudMap[]) => void,
  error: (e: Error) => void,
  workspaceId?: string,
) {
  return onSnapshot(
    query(mapCollection(workspaceId), orderBy('updatedAt', 'desc')),
    (snapshot) => update(snapshot.docs.map((d) => ({ ...d.data(), id: d.id }) as CloudMap)),
    error,
  );
}
export async function network<T>(operation: Promise<T>): Promise<T> {
  let timer: ReturnType<typeof setTimeout>;
  try {
    return await Promise.race([
      operation,
      new Promise<never>((_, reject) => {
        timer = setTimeout(
          () =>
            reject(
              Error(
                '서버 응답이 지연되고 있습니다. 편집 내용은 현재 창에 남아 있습니다. 저장 상태는 지도 목록에서 확인하세요.',
              ),
            ),
          30000,
        );
      }),
    ]);
  } finally {
    clearTimeout(timer!);
  }
}
async function cleanRevision(mapId: string, revision: string, scope?: MapScope) {
  if (!revision) return;
  const records = await getDocsFromServer(
    query(
      collection(doc(mapCollection(scope?.workspaceId), mapId), 'payloads'),
      where('revision', '==', revision),
    ),
  );
  const batch = writeBatch(db);
  records.docs.forEach((d) => batch.delete(d.ref));
  await network(batch.commit());
}
export async function saveMap(
  map: MapDoc,
  preview: string,
  existing?: CloudRef,
  scope?: MapScope,
): Promise<CloudRef> {
  const root = mapCollection(scope?.workspaceId),
    uid = auth.currentUser!.uid;
  const payload = await job<Uint8Array>({ action: 'pack', doc: map });
  if (payload.length > MAX_CLOUD_BYTES)
    throw Error('압축 후 20 MiB 이하인 지도를 저장할 수 있습니다. ZIP으로 다운로드해 보관하세요.');
  if (auth.currentUser?.uid !== uid) throw Error('로그인 계정이 바뀌었습니다. 다시 저장하세요.');
  if (!navigator.onLine)
    throw Error('인터넷에 연결한 뒤 저장하세요. 편집 내용은 현재 창에 남아 있습니다.');
  const ref = existing ? doc(root, existing.id) : doc(root),
    revision = crypto.randomUUID();
  const chunkSize = 600000,
    chunks = Math.ceil(payload.length / chunkSize);
  let obsolete = '';
  try {
    for (let base = 0; base < chunks; base += 6) {
      const batch = writeBatch(db);
      for (let i = base; i < Math.min(chunks, base + 6); i++)
        batch.set(doc(ref, 'payloads', `${revision}_${i}`), {
          revision,
          index: i,
          data: Bytes.fromUint8Array(payload.slice(i * chunkSize, (i + 1) * chunkSize)),
          ...(scope ? { editorSession: scope.sessionId } : {}),
        });
      await network(batch.commit());
    }
    await network(
      runTransaction(
        db,
        async (transaction) => {
          const old = await transaction.get(ref),
            data = old.data();
          if (
            existing?.revision
              ? !old.exists() || data?.revision !== existing.revision || data?.trashed
              : old.exists()
          )
            throw Error(
              '다른 창에서 지도가 변경되었거나 삭제되었습니다. ZIP으로 내려받은 뒤 지도를 다시 열어 주세요.',
            );
          obsolete = data?.previousRevision || '';
          transaction.set(ref, {
            name: map.name.trim().slice(0, 120),
            width: map.width,
            height: map.height,
            resolution: map.meta.resolution,
            revision,
            previousRevision: data?.revision || '',
            chunks,
            bytes: payload.length,
            preview,
            createdAt: data?.createdAt || serverTimestamp(),
            updatedAt: serverTimestamp(),
            trashed: false,
            ...(scope ? { editorSession: scope.sessionId } : {}),
          });
        },
        { maxAttempts: 3 },
      ),
    );
  } catch (error) {
    // A revision is visible only after the metadata transaction succeeds.
    void cleanRevision(ref.id, revision, scope).catch(() => {});
    throw error;
  }
  // Keep one preceding revision. Cleanup failure never converts a successful save into an error.
  if (obsolete) void cleanRevision(ref.id, obsolete, scope).catch(() => {});
  return { id: ref.id, revision, ...(scope ? { workspaceId: scope.workspaceId } : {}) };
}
export async function loadMap(
  id: string,
  workspaceId?: string,
): Promise<{ map: MapDoc; cloud: CloudRef }> {
  const ref = doc(mapCollection(workspaceId), id);
  for (let attempt = 0; attempt < 2; attempt++) {
    const entry = await network(getDocFromServer(ref)),
      meta = entry.data() as CloudMap | undefined;
    if (!meta || meta.trashed) throw Error('지도가 없거나 휴지통에 있습니다.');
    const records = await network(
      getDocsFromServer(query(collection(ref, 'payloads'), where('revision', '==', meta.revision))),
    );
    if (records.size !== meta.chunks) {
      if (attempt === 0) continue;
      throw Error('지도 파일이 완전하지 않습니다. 다시 열어 주세요.');
    }
    if (meta.bytes > MAX_CLOUD_BYTES || meta.chunks > 40) throw Error('지도 파일이 너무 큽니다.');
    const payload = new Uint8Array(meta.bytes);
    let offset = 0;
    const parts = records.docs.map((d) => d.data()).sort((a, b) => a.index - b.index);
    parts.forEach((part, index) => {
      const bytes = (part.data as Bytes).toUint8Array();
      if (part.index !== index || offset + bytes.length > payload.length)
        throw Error('지도 파일이 손상되었습니다.');
      payload.set(bytes, offset);
      offset += bytes.length;
    });
    if (offset !== payload.length) throw Error('지도 파일이 손상되었습니다.');
    const map = await job<MapDoc>({ action: 'unpack', bytes: payload });
    map.name = meta.name;
    return { map, cloud: { id, revision: meta.revision, ...(workspaceId ? { workspaceId } : {}) } };
  }
  throw Error('지도를 읽지 못했습니다.');
}
export async function renameMap(id: string, name: string, scope?: MapScope) {
  const trimmed = name.trim();
  if (!trimmed || trimmed.length > 120) throw Error('지도 이름은 1~120자로 입력하세요.');
  await network(
    updateDoc(doc(mapCollection(scope?.workspaceId), id), {
      name: trimmed,
      updatedAt: serverTimestamp(),
      ...(scope ? { editorSession: scope.sessionId } : {}),
    }),
  );
}
export const trashMap = (id: string, trashed: boolean, scope?: MapScope) =>
  network(
    updateDoc(doc(mapCollection(scope?.workspaceId), id), {
      trashed,
      updatedAt: serverTimestamp(),
      ...(scope ? { editorSession: scope.sessionId } : {}),
    }),
  );
export function errorMessage(error: unknown) {
  const code = (error as { code?: string })?.code;
  const messages: Record<string, string> = {
    'auth/popup-closed-by-user': '로그인 창을 닫았습니다.',
    'auth/cancelled-popup-request': '다른 로그인 창을 확인하세요.',
    'auth/unauthorized-domain': '이 주소가 Firebase 로그인 허용 도메인에 등록되지 않았습니다.',
    'auth/operation-not-allowed': 'Firebase에서 Google 로그인을 활성화해 주세요.',
    'auth/network-request-failed': '인터넷 연결을 확인하세요.',
    'permission-denied': '저장 권한이 없습니다. Firebase 보안 규칙과 로그인 상태를 확인하세요.',
    unavailable: '저장 서버에 연결할 수 없습니다. 인터넷 연결을 확인하세요.',
    'resource-exhausted': 'Firebase 사용량 한도에 도달했습니다. 잠시 뒤 다시 시도하세요.',
    'failed-precondition': 'Firebase Firestore 데이터베이스 설정을 확인하세요.',
  };
  return code && messages[code]
    ? messages[code]
    : error instanceof Error
      ? error.message
      : String(error);
}
