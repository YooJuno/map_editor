import { useEffect, useState } from 'react';
import { Check, Copy, Link2, Plus, Shield, Trash2, X } from 'lucide-react';
import { IconButton } from './Editor';
import type { Translate } from './i18n';
import {
  canManage,
  hasMembership,
  changeMember,
  createWorkspace,
  getShareLink,
  joinWorkspace,
  resolveRequest,
  shareUrl,
  updateWorkspace,
  watchMembers,
  watchMyRequest,
  watchRequests,
  type JoinRequest,
  type Member,
  type ShareLink,
  type Workspace,
} from './workspaces';
import { auth } from './firebase';
import { doc, onSnapshot } from 'firebase/firestore';
import { db } from './firebase';
type Runner = (task: () => Promise<void>) => Promise<void>;
type Props = {
  mode: 'create' | 'manage' | 'trash';
  workspace?: Workspace;
  workspaces: Workspace[];
  globalAdmin: boolean;
  t: Translate;
  busy: boolean;
  run: Runner;
  onClose: () => void;
  onCreated: (id: string) => void;
};
export function WorkspaceDialog({
  mode,
  workspace,
  workspaces,
  globalAdmin,
  t,
  busy,
  run,
  onClose,
  onCreated,
}: Props) {
  const [name, setName] = useState(workspace?.name || ''),
    [approval, setApproval] = useState(workspace?.requireApproval ?? true),
    [link, setLink] = useState<ShareLink | null>(null),
    [enabled, setEnabled] = useState(true),
    [members, setMembers] = useState<Member[]>([]),
    [requests, setRequests] = useState<JoinRequest[]>([]),
    [copied, setCopied] = useState(false),
    [error, setError] = useState('');
  const manage = canManage(workspace, globalAdmin);
  useEffect(() => {
    setName(workspace?.name || '');
    setApproval(workspace?.requireApproval ?? true);
  }, [workspace?.id, workspace?.name, workspace?.requireApproval]);
  useEffect(() => {
    if (!workspace) return;
    const fail = (e: Error) => setError(e.message);
    const stopMembers = watchMembers(workspace.id, setMembers, fail);
    const stopLink = onSnapshot(
      doc(db, 'workspaceLinks', workspace.linkId),
      (snap) => {
        const data = snap.exists() ? ({ ...snap.data(), id: snap.id } as ShareLink) : null;
        setLink(data);
        setEnabled(data?.enabled ?? false);
      },
      () => {
        setLink(null);
        setEnabled(false);
      },
    );
    const stopRequests = manage ? watchRequests(workspace.id, setRequests, fail) : () => {};
    return () => {
      stopMembers();
      stopLink();
      stopRequests();
    };
  }, [workspace?.id, workspace?.linkId, manage]);
  return (
    <div
      className="modal-backdrop"
      onClick={() => {
        if (!busy) onClose();
      }}
    >
      <section
        className="modal workspace-modal"
        role="dialog"
        aria-modal="true"
        aria-labelledby="workspace-title"
        onClick={(e) => e.stopPropagation()}
      >
        <div className="modal-heading">
          <h2 id="workspace-title">
            {mode === 'create'
              ? t('createWorkspace')
              : mode === 'trash'
                ? t('workspaceTrash')
                : workspace?.name}
          </h2>
          <IconButton title={t('close')} disabled={busy} onClick={onClose}>
            <X size={19} />
          </IconButton>
        </div>
        {error && <p className="notice">{error}</p>}
        {mode === 'create' && (
          <form
            onSubmit={(e) => {
              e.preventDefault();
              void run(async () => onCreated(await createWorkspace(name)));
            }}
          >
            <label className="form-field">
              {t('workspaceName')}
              <input
                autoFocus
                required
                maxLength={120}
                value={name}
                disabled={busy}
                onChange={(e) => setName(e.target.value)}
              />
            </label>
            <div className="modal-actions">
              <button type="button" onClick={onClose}>
                {t('cancel')}
              </button>
              <button className="primary" disabled={busy || !name.trim()}>
                <Plus size={16} />
                {t('createWorkspace')}
              </button>
            </div>
          </form>
        )}
        {mode === 'trash' && (
          <div className="workspace-rows">
            {workspaces.filter((w) => w.deleted).length === 0 ? (
              <p className="muted">{t('emptyTrash')}</p>
            ) : (
              workspaces
                .filter((w) => w.deleted)
                .map((w) => (
                  <div className="workspace-row" key={w.id}>
                    <span>{w.name}</span>
                    <button
                      disabled={busy}
                      onClick={() =>
                        void run(async () => {
                          await updateWorkspace(w, {
                            deleted: false,
                            enabled: true,
                          });
                        })
                      }
                    >
                      {t('restore')}
                    </button>
                  </div>
                ))
            )}
          </div>
        )}
        {mode === 'manage' && workspace && (
          <div className="workspace-dialog-body">
            {manage && (
              <section className="workspace-section">
                <label className="form-field">
                  {t('workspaceName')}
                  <input
                    maxLength={120}
                    value={name}
                    disabled={busy}
                    onChange={(e) => setName(e.target.value)}
                  />
                </label>
                <label className="check-row">
                  <input
                    type="checkbox"
                    checked={approval}
                    disabled={busy}
                    onChange={(e) => setApproval(e.target.checked)}
                  />
                  {t('approvalRequired')}
                </label>
                <button
                  className="small-button"
                  disabled={busy || !name.trim()}
                  onClick={() =>
                    void run(async () => {
                      await updateWorkspace(workspace, {
                        name,
                        requireApproval: approval,
                        enabled,
                      });
                    })
                  }
                >
                  {t('save')}
                </button>
              </section>
            )}
            <section className="workspace-section">
              <div className="workspace-section-title">
                <Link2 size={16} />
                <h3>{t('shareLink')}</h3>
                {manage && (
                  <label className="check-row">
                    <input
                      type="checkbox"
                      checked={enabled}
                      disabled={busy}
                      onChange={(e) => {
                        const next = e.target.checked;
                        setEnabled(next);
                        void run(async () => {
                          await updateWorkspace(workspace, { enabled: next });
                        });
                      }}
                    />
                    {t('linkEnabled')}
                  </label>
                )}
              </div>
              {link?.enabled ? (
                <div className="copy-link">
                  <input aria-label={t('shareLink')} readOnly value={shareUrl(link.id)} />
                  <IconButton
                    title={t(copied ? 'linkCopied' : 'copyLink')}
                    onClick={() =>
                      void run(async () => {
                        await navigator.clipboard.writeText(shareUrl(link.id));
                        setCopied(true);
                      })
                    }
                  >
                    {copied ? <Check size={16} /> : <Copy size={16} />}
                  </IconButton>
                </div>
              ) : (
                <p className="muted">{t('linkUnavailable')}</p>
              )}
              {manage && (
                <button
                  className="text-button small-button"
                  disabled={busy}
                  onClick={() =>
                    void run(async () => {
                      await updateWorkspace(workspace, {
                        rotateLink: true,
                        enabled: true,
                      });
                      setCopied(false);
                    })
                  }
                >
                  {t('renewLink')}
                </button>
              )}
            </section>
            {manage && (
              <section className="workspace-section">
                <div className="workspace-section-title">
                  <h3>{t('joinRequests')}</h3>
                  <span>{requests.length}</span>
                </div>
                {requests.length === 0 ? (
                  <p className="muted">{t('noRequests')}</p>
                ) : (
                  requests.map((request) => (
                    <div className="workspace-row" key={request.uid}>
                      <div>
                        <strong>{request.name}</strong>
                        <small>{request.email}</small>
                      </div>
                      <button
                        className="small-button"
                        disabled={busy}
                        onClick={() =>
                          void run(async () => resolveRequest(workspace.id, request.uid, true))
                        }
                      >
                        {t('approve')}
                      </button>
                      <button
                        className="text-button small-button"
                        disabled={busy}
                        onClick={() =>
                          void run(async () => resolveRequest(workspace.id, request.uid, false))
                        }
                      >
                        {t('reject')}
                      </button>
                    </div>
                  ))
                )}
              </section>
            )}
            <section className="workspace-section">
              <div className="workspace-section-title">
                <h3>{t('members')}</h3>
                <span>{members.length}</span>
              </div>
              {members.map((member) => (
                <div className="workspace-row" key={member.uid}>
                  <div className="member-identity">
                    <strong>{member.name}</strong>
                    <small>{member.email}</small>
                  </div>
                  {manage ? (
                    <>
                      <button
                        className="role-button"
                        disabled={busy}
                        title={t(member.role === 'admin' ? 'makeMember' : 'makeAdmin')}
                        onClick={() =>
                          void run(async () =>
                            changeMember(
                              workspace.id,
                              member.uid,
                              member.role === 'admin' ? 'editor' : 'admin',
                            ),
                          )
                        }
                      >
                        {member.role === 'admin' && <Shield size={12} />}{' '}
                        {t(member.role === 'admin' ? 'admin' : 'member')}
                      </button>
                      <IconButton
                        title={t('removeMember')}
                        disabled={busy}
                        onClick={() => {
                          if (confirm(`${member.name} · ${t('removeMember')}?`))
                            void run(async () => changeMember(workspace.id, member.uid, 'remove'));
                        }}
                      >
                        <X size={15} />
                      </IconButton>
                    </>
                  ) : (
                    <span className="member-role">
                      {t(member.role === 'admin' ? 'admin' : 'member')}
                    </span>
                  )}
                </div>
              ))}
            </section>
            {globalAdmin && (
              <div className="workspace-danger">
                <button
                  disabled={busy}
                  onClick={() => {
                    if (confirm(`${workspace.name} · ${t('deleteWorkspace')}?`))
                      void run(async () => {
                        await updateWorkspace(workspace, { deleted: true });
                        onClose();
                      });
                  }}
                >
                  <Trash2 size={15} />
                  {t('deleteWorkspace')}
                </button>
              </div>
            )}
          </div>
        )}
      </section>
    </div>
  );
}
export function JoinWorkspaceDialog({
  linkId,
  t,
  userReady,
  globalAdmin,
  busy,
  run,
  onLogin,
  onClose,
  onJoined,
}: {
  linkId: string;
  t: Translate;
  userReady: boolean;
  globalAdmin: boolean;
  busy: boolean;
  run: Runner;
  onLogin: () => void;
  onClose: () => void;
  onJoined: (id: string) => void;
}) {
  const [link, setLink] = useState<ShareLink | null>(null),
    [error, setError] = useState(''),
    [status, setStatus] = useState('');
  useEffect(() => {
    let active = true;
    void getShareLink(linkId)
      .then((data) => {
        if (active) setLink(data);
      })
      .catch(() => {
        if (active) setError(t('linkUnavailable'));
      });
    return () => {
      active = false;
    };
  }, [linkId, t]);
  useEffect(() => {
    if (!link || !userReady || !auth.currentUser || globalAdmin) return;
    return watchMyRequest(
      link.workspaceId,
      (request) => {
        setStatus(request?.status || '');
        if (request) {
          if (request.status === 'approved')
            void hasMembership(link.workspaceId)
              .then((exists) => {
                if (exists) onJoined(link.workspaceId);
              })
              .catch(() => {});
        }
      },
      () => {},
    );
  }, [link?.workspaceId, userReady, globalAdmin]);
  return (
    <div className="modal-backdrop">
      <section
        className="modal compact"
        role="dialog"
        aria-modal="true"
        aria-labelledby="join-title"
      >
        <div className="modal-heading">
          <h2 id="join-title">{link?.name || t('joinWorkspace')}</h2>
          <IconButton title={t('close')} onClick={onClose}>
            <X size={18} />
          </IconButton>
        </div>
        {error ? (
          <p className="notice">{error}</p>
        ) : !link ? (
          <p>{t('busy')}</p>
        ) : !auth.currentUser ? (
          <>
            <p className="muted">{t('loginToJoin')}</p>
            <button className="primary join-button" disabled={busy || !userReady} onClick={onLogin}>
              {t('login')}
            </button>
          </>
        ) : globalAdmin ? (
          <button className="primary join-button" onClick={() => onJoined(link.workspaceId)}>
            {t('joinWorkspace')}
          </button>
        ) : status === 'pending' ? (
          <p className="notice">{t('joinPending')}</p>
        ) : status === 'rejected' ? (
          <p className="notice">{t('joinRejected')}</p>
        ) : (
          <button
            className="primary join-button"
            disabled={busy}
            onClick={() =>
              void run(async () => {
                const result = await joinWorkspace(linkId);
                setStatus(result.status);
                if (result.status === 'approved') onJoined(result.workspaceId);
              })
            }
          >
            {t(link.requireApproval ? 'requestJoin' : 'joinWorkspace')}
          </button>
        )}
      </section>
    </div>
  );
}
