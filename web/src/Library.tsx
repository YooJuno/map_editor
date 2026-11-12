import { useEffect, useState } from 'react';
import {
  ArrowUpRight,
  Download,
  HardDrive,
  LoaderCircle,
  LockKeyhole,
  Map,
  MoreHorizontal,
  Pencil,
  Plus,
  RotateCcw,
  Search,
  Settings,
  ShieldOff,
  Gauge,
  Trash2,
  Upload,
  Users,
} from 'lucide-react';
import type { User } from 'firebase/auth';
import type { CloudMap } from './firebase';
import type { Lang, Translate } from './i18n';
import { leaseActive, type EditLease, type Workspace } from './workspaces';
import { IconButton } from './Editor';
type Props = {
  user: User | null;
  authReady: boolean;
  globalAdmin: boolean;
  workspaceManager: boolean;
  workspaces: Workspace[];
  workspaceId?: string;
  records: CloudMap[];
  leases: EditLease[];
  clock: number;
  trash: boolean;
  loading: boolean;
  error: string;
  busy: boolean;
  t: Translate;
  lang: Lang;
  onWorkspace: (id?: string) => void;
  onTrash: (value: boolean) => void;
  onWorkspaceDialog: (mode: 'create' | 'manage' | 'trash') => void;
  onRetry: () => void;
  onUpload: () => void;
  onLogin: () => void;
  onDesktop: () => void;
  onOpen: (map: CloudMap) => void;
  onRename: (map: CloudMap) => void;
  onDownload: (map: CloudMap) => void;
  onRemove: (map: CloudMap, trashed: boolean) => void;
  onForceUnlock: (map: CloudMap) => void;
};
function formatBytes(bytes: number) {
  return bytes < 1024
    ? `${bytes} B`
    : bytes < 1024 * 1024
      ? `${(bytes / 1024).toFixed(bytes < 10240 ? 1 : 0)} KiB`
      : `${(bytes / 1048576).toFixed(1)} MiB`;
}
export function Library(p: Props) {
  const { t } = p,
    [search, setSearch] = useState('');
  const workspace = p.workspaces.find((w) => w.id === p.workspaceId),
    maps = p.records.filter(
      (r) =>
        r.trashed === p.trash && r.name.toLocaleLowerCase().includes(search.toLocaleLowerCase()),
    );
  useEffect(() => {
    const outside = (e: PointerEvent) =>
      document.querySelectorAll<HTMLDetailsElement>('details.card-menu[open]').forEach((menu) => {
        if (!menu.contains(e.target as Node)) menu.open = false;
      });
    const escape = (e: KeyboardEvent) => {
      if (e.key === 'Escape')
        document.querySelectorAll<HTMLDetailsElement>('details.card-menu[open]').forEach((menu) => {
          menu.open = false;
          menu.querySelector<HTMLElement>('summary')?.focus();
        });
    };
    document.addEventListener('pointerdown', outside);
    document.addEventListener('keydown', escape);
    return () => {
      document.removeEventListener('pointerdown', outside);
      document.removeEventListener('keydown', escape);
    };
  }, []);
  return (
    <div className="library">
      <aside className="library-sidebar">
        <div className="sidebar-heading">{t('maps')}</div>
        <button
          className={!p.workspaceId && !p.trash ? 'nav-item active' : 'nav-item'}
          onClick={() => {
            p.onWorkspace();
            p.onTrash(false);
          }}
        >
          <Map size={18} />
          {t('maps')}
        </button>
        <button
          className={!p.workspaceId && p.trash ? 'nav-item active' : 'nav-item'}
          onClick={() => {
            p.onWorkspace();
            p.onTrash(true);
          }}
        >
          <Trash2 size={18} />
          {t('trash')}
        </button>
        {p.user && (
          <>
            <div className="workspace-nav-heading">
              <span>{t('workspaces')}</span>
              <IconButton
                title={t('createWorkspace')}
                onClick={() => p.onWorkspaceDialog('create')}
              >
                <Plus size={15} />
              </IconButton>
            </div>
            <div className="workspace-nav-list">
              {p.workspaces
                .filter((w) => !w.deleted)
                .map((w) => (
                  <button
                    className={p.workspaceId === w.id ? 'nav-item active' : 'nav-item'}
                    title={w.name}
                    key={w.id}
                    onClick={() => {
                      p.onWorkspace(w.id);
                      p.onTrash(false);
                    }}
                  >
                    <Users size={16} />
                    <span className="workspace-nav-name">{w.name}</span>
                  </button>
                ))}
            </div>
            {p.globalAdmin && (
              <button
                className="nav-item workspace-trash-nav"
                onClick={() => p.onWorkspaceDialog('trash')}
              >
                <Trash2 size={15} />
                {t('workspaceTrash')}
              </button>
            )}
          </>
        )}
        <div className="spacer" />
        <button className="desktop-link" onClick={p.onDesktop}>
          <HardDrive size={16} />
          Desktop
        </button>
      </aside>
      <main className="library-main">
        <div className="library-heading">
          <div>
            <h1>{workspace?.name || t(p.trash ? 'trash' : 'maps')}</h1>
            {p.trash && <p>{t('trashHint')}</p>}
          </div>
          <div className="library-heading-actions">
            {workspace && (
              <>
                <IconButton
                  title={t('workspaceSettings')}
                  onClick={() => p.onWorkspaceDialog('manage')}
                >
                  <Settings size={18} />
                </IconButton>
                {p.workspaceManager && (
                  <IconButton
                    title={t('trash')}
                    active={p.trash}
                    onClick={() => p.onTrash(!p.trash)}
                  >
                    <Trash2 size={17} />
                  </IconButton>
                )}
              </>
            )}
            <button className="primary" disabled={p.busy} onClick={p.onUpload}>
              <Upload size={17} />
              {t('upload')}
            </button>
          </div>
        </div>
        {!p.user && p.authReady && (
          <div className="signin-panel">
            <div className="signin-copy">
              <div className="section-icon">
                <Map size={26} />
              </div>
              <h2>Map Editor</h2>
              <button className="google-button" disabled={p.busy} onClick={p.onLogin}>
                <span className="google-mark">G</span>
                {t('login')}
              </button>
              <small>{t('loginHint')}</small>
              <div className="local-links">
                <button onClick={p.onUpload}>
                  <Upload size={15} />
                  {t('upload')}
                </button>
              </div>
            </div>
            <div className="signin-preview">
              <img src="/preview.png" alt="Map Editor" />
              <div className="preview-caption">
                <span>
                  <ShieldOff size={14} />
                  {t('keepout')}
                </span>
                <span>
                  <Gauge size={14} />
                  {t('speed')}
                </span>
              </div>
            </div>
          </div>
        )}
        {p.user && (
          <>
            <div className="library-tools">
              <label className="search">
                <Search size={17} />
                <input
                  aria-label={t('search')}
                  placeholder={t('search')}
                  value={search}
                  onChange={(e) => setSearch(e.target.value)}
                />
              </label>
            </div>
            {p.error ? (
              <div className="error-panel">
                <p>{p.error}</p>
                <button onClick={p.onRetry}>
                  <RotateCcw size={16} />
                  {t('redo')}
                </button>
              </div>
            ) : p.loading ? (
              <div className="empty-state">
                <LoaderCircle className="spin" />
                {t('busy')}
              </div>
            ) : !maps.length ? (
              <div className="empty-state">
                <div className="empty-icon">
                  {p.trash ? <Trash2 size={28} /> : <Map size={30} />}
                </div>
                <h2>{t(search ? 'noResults' : p.trash ? 'emptyTrash' : 'empty')}</h2>
                {!p.trash && !search && (
                  <>
                    <p>{t('emptyHint')}</p>
                    <button className="primary" onClick={p.onUpload}>
                      <Upload size={16} />
                      {t('upload')}
                    </button>
                  </>
                )}
              </div>
            ) : (
              <div className="map-grid">
                {maps.map((record) => {
                  const lease = p.leases.find((l) => l.id === record.id),
                    locked = !!p.workspaceId && leaseActive(lease, p.clock);
                  return (
                    <article className="map-card" key={record.id}>
                      <button
                        className="map-preview"
                        aria-label={record.name}
                        disabled={p.busy || p.trash}
                        title={record.name}
                        onClick={() => p.onOpen(record)}
                      >
                        <img src={record.preview} alt="" />
                        {locked && (
                          <span className="map-lease-label">
                            <LockKeyhole size={12} />
                            {lease?.name}
                          </span>
                        )}
                        <span className="map-open-icon">
                          <ArrowUpRight size={18} />
                        </span>
                      </button>
                      <div className="map-card-body">
                        <div className="map-card-title">
                          <h2 title={record.name}>{record.name}</h2>
                          <details
                            className="card-menu"
                            onToggle={(e) => {
                              if (e.currentTarget.open)
                                document
                                  .querySelectorAll<HTMLDetailsElement>('details.card-menu[open]')
                                  .forEach((menu) => {
                                    if (menu !== e.currentTarget) menu.open = false;
                                  });
                            }}
                            onClick={(e) => {
                              if ((e.target as Element).closest('button'))
                                e.currentTarget.open = false;
                            }}
                          >
                            <summary aria-label="More">
                              <MoreHorizontal size={19} />
                            </summary>
                            <div>
                              {p.trash ? (
                                <button disabled={p.busy} onClick={() => p.onRemove(record, false)}>
                                  <RotateCcw size={15} />
                                  {t('restore')}
                                </button>
                              ) : (
                                <>
                                  <button disabled={p.busy} onClick={() => p.onRename(record)}>
                                    <Pencil size={15} />
                                    {t('rename')}
                                  </button>
                                  <button disabled={p.busy} onClick={() => p.onDownload(record)}>
                                    <Download size={15} />
                                    {t('download')}
                                  </button>
                                  {(!p.workspaceId || p.workspaceManager) && (
                                    <button
                                      disabled={p.busy}
                                      onClick={() => p.onRemove(record, true)}
                                    >
                                      <Trash2 size={15} />
                                      {t('remove')}
                                    </button>
                                  )}
                                  {p.workspaceId && p.globalAdmin && locked && (
                                    <button
                                      disabled={p.busy}
                                      onClick={() => p.onForceUnlock(record)}
                                    >
                                      <LockKeyhole size={15} />
                                      {t('forceUnlock')}
                                    </button>
                                  )}
                                </>
                              )}
                            </div>
                          </details>
                        </div>
                        <p>
                          {record.width.toLocaleString()} × {record.height.toLocaleString()}
                          <span>·</span>
                          {record.resolution} m/px
                        </p>
                        <div className="map-card-footer">
                          <time>{record.updatedAt?.toDate().toLocaleDateString(p.lang)}</time>
                          <span title={t('fileSize')}>{formatBytes(record.bytes)}</span>
                        </div>
                      </div>
                    </article>
                  );
                })}
              </div>
            )}
          </>
        )}
      </main>
    </div>
  );
}
