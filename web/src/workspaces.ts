import {
  collection,
  doc,
  query,
  where,
  orderBy,
  onSnapshot,
  getDocFromServer,
  writeBatch,
  runTransaction,
  serverTimestamp,
  arrayUnion,
  arrayRemove,
  updateDoc,
  type Timestamp,
} from 'firebase/firestore';
import { auth, db, network, mapCollection, type MapScope } from './firebase';
export const LEASE_MS = 90000;
export type Workspace = {
  id: string;
  name: string;
  createdBy: string;
  memberIds: string[];
  adminIds: string[];
  requireApproval: boolean;
  linkId: string;
  deleted: boolean;
  createdAt: Timestamp;
  updatedAt: Timestamp;
};
export type Member = {
  uid: string;
  name: string;
  email: string;
  role: 'admin' | 'editor';
  joinedAt: Timestamp;
};
export type JoinRequest = {
  uid: string;
  name: string;
  email: string;
  linkId: string;
  status: 'pending' | 'approved' | 'rejected';
  requestedAt: Timestamp;
};
export type ShareLink = {
  id: string;
  workspaceId: string;
  name: string;
  enabled: boolean;
  requireApproval: boolean;
};
export type EditLease = {
  id: string;
  uid: string;
  sessionId: string;
  name: string;
  active: boolean;
  updatedAt: Timestamp | null;
};
export type EditContext = MapScope & { mapId: string };
const current = () => {
  if (!auth.currentUser) throw Error('로그인하세요.');
  return auth.currentUser;
};
const wsRef = (id: string) => doc(db, 'workspaces', id);
const identity = () => {
  const u = current();
  return {
    uid: u.uid,
    name: (u.displayName || u.email || 'Member').slice(0, 120),
    email: (u.email || '').toLowerCase(),
  };
};
export function canManage(workspace: Workspace | undefined, globalAdmin: boolean) {
  return globalAdmin || !!workspace?.adminIds.includes(auth.currentUser?.uid || '');
}
export function watchWorkspaces(
  globalAdmin: boolean,
  update: (items: Workspace[]) => void,
  error: (e: Error) => void,
) {
  const q = globalAdmin
    ? query(collection(db, 'workspaces'), orderBy('updatedAt', 'desc'))
    : query(
        collection(db, 'workspaces'),
        where('memberIds', 'array-contains', current().uid),
        where('deleted', '==', false),
        orderBy('updatedAt', 'desc'),
      );
  return onSnapshot(
    q,
    (snap) => update(snap.docs.map((d) => ({ ...d.data(), id: d.id }) as Workspace)),
    error,
  );
}
export async function createWorkspace(name: string) {
  name = name.trim();
  if (!name || name.length > 120) throw Error('이름은 1~120자로 입력하세요.');
  const user = identity(),
    ref = doc(collection(db, 'workspaces')),
    linkId = crypto.randomUUID(),
    batch = writeBatch(db);
  batch.set(ref, {
    name,
    createdBy: user.uid,
    memberIds: [user.uid],
    adminIds: [user.uid],
    requireApproval: true,
    linkId,
    deleted: false,
    createdAt: serverTimestamp(),
    updatedAt: serverTimestamp(),
  });
  batch.set(doc(ref, 'members', user.uid), { ...user, role: 'admin', joinedAt: serverTimestamp() });
  batch.set(doc(db, 'workspaceLinks', linkId), {
    workspaceId: ref.id,
    name,
    enabled: true,
    requireApproval: true,
    createdBy: user.uid,
    createdAt: serverTimestamp(),
  });
  await network(batch.commit());
  return ref.id;
}
export async function updateWorkspace(
  workspace: Workspace,
  patch: {
    name?: string;
    requireApproval?: boolean;
    enabled?: boolean;
    rotateLink?: boolean;
    deleted?: boolean;
  },
) {
  const ref = wsRef(workspace.id);
  await network(
    runTransaction(db, async (tx) => {
      const snapshot = await tx.get(ref);
      if (!snapshot.exists()) throw Error('워크스페이스가 없습니다.');
      const old = snapshot.data() as Workspace,
        linkRef = doc(db, 'workspaceLinks', old.linkId),
        link = await tx.get(linkRef);
      const name = (patch.name ?? old.name).trim(),
        requireApproval = patch.requireApproval ?? old.requireApproval;
      if (!name || name.length > 120) throw Error('이름은 1~120자로 입력하세요.');
      const linkId = patch.rotateLink ? crypto.randomUUID() : old.linkId,
        deleted = patch.deleted ?? old.deleted;
      tx.update(ref, { name, requireApproval, linkId, deleted, updatedAt: serverTimestamp() });
      if (patch.rotateLink) tx.update(linkRef, { enabled: false });
      const target = doc(db, 'workspaceLinks', linkId),
        data = {
          workspaceId: ref.id,
          name,
          enabled: deleted ? false : (patch.enabled ?? link.data()?.enabled ?? true),
          requireApproval,
          createdBy: link.data()?.createdBy || current().uid,
          createdAt: patch.rotateLink
            ? serverTimestamp()
            : link.data()?.createdAt || serverTimestamp(),
        };
      tx.set(target, data);
    }),
  );
}
export function watchMembers(
  id: string,
  update: (members: Member[]) => void,
  error: (e: Error) => void,
) {
  return onSnapshot(
    collection(wsRef(id), 'members'),
    (s) => update(s.docs.map((d) => d.data() as Member)),
    error,
  );
}
export function watchRequests(
  id: string,
  update: (requests: JoinRequest[]) => void,
  error: (e: Error) => void,
) {
  return onSnapshot(
    query(collection(wsRef(id), 'joinRequests'), where('status', '==', 'pending')),
    (s) => update(s.docs.map((d) => d.data() as JoinRequest)),
    error,
  );
}
export function watchMyRequest(
  id: string,
  update: (request: JoinRequest | null) => void,
  error: (e: Error) => void,
) {
  return onSnapshot(
    doc(wsRef(id), 'joinRequests', current().uid),
    (s) => update(s.exists() ? (s.data() as JoinRequest) : null),
    error,
  );
}
export async function getShareLink(id: string) {
  const snap = await network(getDocFromServer(doc(db, 'workspaceLinks', id)));
  if (!snap.exists() || !snap.data().enabled) throw Error('사용할 수 없는 공유 링크입니다.');
  return { ...snap.data(), id } as ShareLink;
}
export async function joinWorkspace(linkId: string) {
  const link = await getShareLink(linkId),
    user = identity(),
    ref = wsRef(link.workspaceId);
  const membership = await network(getDocFromServer(doc(ref, 'members', user.uid)));
  if (membership.exists()) return { workspaceId: ref.id, status: 'approved' as const };
  const batch = writeBatch(db),
    status = link.requireApproval ? 'pending' : 'approved';
  batch.set(doc(ref, 'joinRequests', user.uid), {
    ...user,
    linkId,
    status,
    requestedAt: serverTimestamp(),
  });
  if (!link.requireApproval) {
    batch.update(ref, { memberIds: arrayUnion(user.uid), updatedAt: serverTimestamp() });
    batch.set(doc(ref, 'members', user.uid), {
      ...user,
      role: 'editor',
      joinedAt: serverTimestamp(),
    });
  }
  await network(batch.commit());
  return { workspaceId: ref.id, status };
}
export async function resolveRequest(id: string, uid: string, approve: boolean) {
  const ref = wsRef(id);
  await network(
    runTransaction(db, async (tx) => {
      const ws = await tx.get(ref),
        requestRef = doc(ref, 'joinRequests', uid),
        snap = await tx.get(requestRef);
      if (!ws.exists() || !snap.exists() || snap.data().status !== 'pending')
        throw Error('이미 처리된 요청입니다.');
      const request = snap.data() as JoinRequest;
      if (approve) {
        tx.update(ref, { memberIds: arrayUnion(uid), updatedAt: serverTimestamp() });
        tx.set(doc(ref, 'members', uid), {
          uid,
          name: request.name,
          email: request.email,
          role: 'editor',
          joinedAt: serverTimestamp(),
        });
      }
      tx.update(requestRef, { status: approve ? 'approved' : 'rejected' });
    }),
  );
}
export async function changeMember(id: string, uid: string, role: 'admin' | 'editor' | 'remove') {
  const ref = wsRef(id);
  await network(
    runTransaction(db, async (tx) => {
      const ws = await tx.get(ref);
      if (!ws.exists()) throw Error('워크스페이스가 없습니다.');
      const d = ws.data() as Workspace;
      if (
        role !== 'admin' &&
        d.adminIds.includes(uid) &&
        d.adminIds.length <= 1 &&
        !(await current().getIdTokenResult()).claims.appAdmin
      )
        throw Error('마지막 관리자는 다른 관리자를 지정한 뒤 변경하세요.');
      tx.update(ref, {
        ...(role === 'remove' ? { memberIds: arrayRemove(uid) } : {}),
        adminIds: role === 'admin' ? arrayUnion(uid) : arrayRemove(uid),
        updatedAt: serverTimestamp(),
      });
      if (role === 'remove') tx.delete(doc(ref, 'members', uid));
      else tx.update(doc(ref, 'members', uid), { role });
    }),
  );
}
export function watchLeases(
  id: string,
  update: (leases: EditLease[]) => void,
  error: (e: Error) => void,
) {
  return onSnapshot(
    collection(wsRef(id), 'locks'),
    (s) => update(s.docs.map((d) => ({ ...d.data(), id: d.id }) as EditLease)),
    error,
  );
}
export function leaseActive(lease: EditLease | undefined, now = Date.now()) {
  return !!lease?.active && !!lease.updatedAt && lease.updatedAt.toMillis() + LEASE_MS > now;
}
export async function acquireLease(
  workspaceId: string,
  mapId: string,
  sessionId = crypto.randomUUID(),
) {
  const ref = doc(wsRef(workspaceId), 'locks', mapId),
    u = identity();
  const acquired = await network(
    runTransaction(db, async (tx) => {
      const snap = await tx.get(ref),
        old = snap.data() as EditLease | undefined;
      if (leaseActive(old) && (old!.uid !== u.uid || old!.sessionId !== sessionId)) return false;
      tx.set(ref, {
        uid: u.uid,
        name: u.name,
        sessionId,
        active: true,
        updatedAt: serverTimestamp(),
      });
      return true;
    }),
  );
  return { acquired, context: { workspaceId, mapId, sessionId } as EditContext };
}
export async function renewLease(context: EditContext) {
  const ref = doc(wsRef(context.workspaceId), 'locks', context.mapId);
  await network(
    runTransaction(db, async (tx) => {
      const snap = await tx.get(ref),
        data = snap.data();
      if (!data?.active || data.uid !== current().uid || data.sessionId !== context.sessionId)
        throw Error('편집 잠금을 잃었습니다.');
      tx.update(ref, { updatedAt: serverTimestamp() });
    }),
  );
}
export async function releaseLease(context: EditContext) {
  const ref = doc(wsRef(context.workspaceId), 'locks', context.mapId);
  await network(
    runTransaction(db, async (tx) => {
      const snap = await tx.get(ref),
        data = snap.data();
      if (data?.uid === current().uid && data.sessionId === context.sessionId && data.active)
        tx.update(ref, { active: false, updatedAt: serverTimestamp() });
    }),
  );
}
export const forceReleaseLease = (workspaceId: string, mapId: string) =>
  network(
    updateDoc(doc(wsRef(workspaceId), 'locks', mapId), {
      active: false,
      updatedAt: serverTimestamp(),
    }),
  );
export async function withLease<T>(
  workspaceId: string,
  mapId: string,
  action: (scope: MapScope) => Promise<T>,
) {
  const result = await acquireLease(workspaceId, mapId);
  if (!result.acquired) throw Error('다른 사람이 편집 중입니다.');
  try {
    return await action(result.context);
  } finally {
    await releaseLease(result.context).catch(() => {});
  }
}
export const reserveMapId = (workspaceId: string) => doc(mapCollection(workspaceId)).id;
export const shareUrl = (linkId: string) =>
  `${location.origin}/?workspace=${encodeURIComponent(linkId)}`;

export async function hasMembership(id: string) {
  return (await network(getDocFromServer(doc(wsRef(id), 'members', current().uid)))).exists();
}
