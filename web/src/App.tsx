import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { onAuthStateChanged, type User } from 'firebase/auth';
import { ArrowLeft, Download, LoaderCircle, LockKeyhole, Save, Upload, X } from 'lucide-react';
import {
  auth,
  login,
  logout,
  finishLogin,
  watchMaps,
  saveMap,
  loadMap,
  renameMap,
  trashMap,
  errorMessage,
  refreshAdminRole,
  type CloudMap,
  type CloudRef,
} from './firebase';
import { Editor, IconButton, type EditorHandle } from './Editor';
import { AccountMenu } from './AccountMenu';
import { Library } from './Library';
import { WorkspaceDialog, JoinWorkspaceDialog } from './WorkspaceDialog';
import {
  acquireLease,
  releaseLease,
  renewLease,
  reserveMapId,
  watchLeases,
  watchWorkspaces,
  canManage,
  leaseActive,
  withLease,
  forceReleaseLease,
  type Workspace,
  type EditContext,
  type EditLease,
} from './workspaces';
import { layerBytes, safeName, type MapDoc, type NamedFile } from './map';
import { translator, type Lang } from './i18n';
import { job } from './jobs';

function thumbnail(doc: MapDoc) {
  const canvas = document.createElement('canvas');
  canvas.width = 320;
  canvas.height = 180;
  const ctx = canvas.getContext('2d')!,
    out = ctx.createImageData(320, 180),
    z = Math.min(300 / doc.width, 160 / doc.height),
    ox = (320 - doc.width * z) / 2,
    oy = (180 - doc.height * z) / 2;
  for (let y = 0; y < 180; y++)
    for (let x = 0; x < 320; x++) {
      const p = (y * 320 + x) * 4,
        sx = Math.floor((x - ox) / z),
        sy = Math.floor((y - oy) / z);
      out.data[p + 3] = 255;
      if (sx < 0 || sy < 0 || sx >= doc.width || sy >= doc.height) {
        out.data[p] = 24;
        out.data[p + 1] = 26;
        out.data[p + 2] = 30;
        continue;
      }
      const index = sy * doc.width + sx,
        gray = doc.pixels[index];
      out.data[p] = out.data[p + 1] = out.data[p + 2] = gray;
      if (doc.keepout?.[index] && doc.keepout[index] !== 255) {
        out.data[p] = gray * 0.45 + 213 * 0.55;
        out.data[p + 1] = gray * 0.45 + 121 * 0.55;
        out.data[p + 2] = gray * 0.45 + 114 * 0.55;
      }
      if (doc.speed?.[index] && doc.speed[index] !== 255) {
        out.data[p] = gray * 0.45 + 114 * 0.55;
        out.data[p + 1] = gray * 0.45 + 154 * 0.55;
        out.data[p + 2] = gray * 0.45 + 201 * 0.55;
      }
    }
  ctx.putImageData(out, 0, 0);
  return canvas.toDataURL('image/webp', 0.75);
}
function downloadBytes(bytes: Uint8Array, name: string) {
  const url = URL.createObjectURL(new Blob([bytes.slice().buffer], { type: 'application/zip' }));
  const a = document.createElement('a');
  a.href = url;
  a.download = `${safeName(name)}.zip`;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 30000);
}
function preference(key: string, fallback: string) {
  try {
    return localStorage.getItem(key) || fallback;
  } catch {
    return fallback;
  }
}

export default function App() {
  const [lang, setLang] = useState<Lang>(() => {
    const value = preference('map-editor-language', 'ko');
    return ['ko', 'en', 'ja', 'zh'].includes(value) ? (value as Lang) : 'ko';
  });
  const [theme, setTheme] = useState(() => preference('map-editor-theme', 'dark')),
    t = useMemo(() => translator(lang), [lang]);
  const [user, setUser] = useState<User | null>(null),
    [authReady, setAuthReady] = useState(false),
    [globalAdmin, setGlobalAdmin] = useState(false),
    [roleReady, setRoleReady] = useState(false);
  const [records, setRecords] = useState<CloudMap[]>([]),
    [listReady, setListReady] = useState(false),
    [listError, setListError] = useState(''),
    [reload, setReload] = useState(0);
  const [workspaces, setWorkspaces] = useState<Workspace[]>([]),
    [workspaceId, setWorkspaceId] = useState<string>(),
    [workspaceDialog, setWorkspaceDialog] = useState<'create' | 'manage' | 'trash' | null>(null);
  const [current, setCurrent] = useState<MapDoc | null>(null),
    [cloud, setCloud] = useState<CloudRef>(),
    [dirty, setDirty] = useState(false),
    [draft, setDraft] = useState(false),
    [nameDraft, setNameDraft] = useState<string | null>(null),
    [session, setSession] = useState(0);
  const [editingWorkspaceId, setEditingWorkspaceId] = useState<string>(),
    [editContext, setEditContext] = useState<EditContext>(),
    [leaseHeld, setLeaseHeld] = useState(false),
    [leases, setLeases] = useState<EditLease[]>([]),
    [clock, setClock] = useState(Date.now());
  const [busy, setBusy] = useState(false),
    [toast, setToast] = useState(''),
    [upload, setUpload] = useState(false),
    [trash, setTrash] = useState(false),
    [rename, setRename] = useState<{ id: string; name: string } | null>(null),
    [online, setOnline] = useState(navigator.onLine),
    [joinLink, setJoinLink] = useState<string | null>(() =>
      new URLSearchParams(location.search).get('workspace'),
    );
  const [, renderHistory] = useState(0),
    currentRef = useRef<MapDoc | null>(null),
    historyRef = useRef<{ past: MapDoc[]; future: MapDoc[] }>({ past: [], future: [] }),
    previousWorkspaces = useRef<Workspace[]>([]),
    owner = useRef<string | null>(null),
    busyRef = useRef(false),
    editor = useRef<EditorHandle>(null),
    input = useRef<HTMLInputElement>(null);
  currentRef.current = current;
  const selectedWorkspace = workspaces.find((w) => w.id === workspaceId && !w.deleted),
    workspaceManager = canManage(selectedWorkspace, globalAdmin),
    nameChanged = !!nameDraft?.trim() && nameDraft.trim() !== current?.name,
    unsaved = dirty || draft || nameChanged,
    readOnly = !!editingWorkspaceId && (!editContext || !leaseHeld || !online),
    holder = leases.find((l) => l.id === cloud?.id && leaseActive(l, clock));
  const run = useCallback(async (task: () => Promise<void>) => {
    if (busyRef.current) return;
    busyRef.current = true;
    setBusy(true);
    try {
      await task();
    } catch (e) {
      setToast(errorMessage(e));
    } finally {
      busyRef.current = false;
      setBusy(false);
    }
  }, []);
  useEffect(() => {
    document.documentElement.dataset.theme = theme;
    document.documentElement.lang = lang;
    try {
      localStorage.setItem('map-editor-theme', theme);
      localStorage.setItem('map-editor-language', lang);
    } catch {}
  }, [theme, lang]);
  useEffect(() => {
    const timer = setTimeout(() => setToast(''), 9000);
    return () => clearTimeout(timer);
  }, [toast]);
  useEffect(() => {
    void finishLogin().catch((e) => setToast(errorMessage(e)));
    return onAuthStateChanged(auth, (next) => {
      if (owner.current && owner.current !== next?.uid) {
        setCloud(undefined);
        setRecords([]);
        setDirty(true);
        setEditContext(undefined);
      }
      owner.current = next?.uid || null;
      setUser(next);
      setAuthReady(true);
    });
  }, []);
  useEffect(() => {
    let active = true;
    setRoleReady(false);
    setGlobalAdmin(false);
    if (!user) {
      setRoleReady(true);
      setWorkspaceId(undefined);
      setEditContext(undefined);
      return;
    }
    void refreshAdminRole()
      .then((value) => {
        if (active) {
          setGlobalAdmin(value);
          setRoleReady(true);
        }
      })
      .catch(() => {
        if (active) setRoleReady(true);
      });
    return () => {
      active = false;
    };
  }, [user?.uid]);
  useEffect(() => {
    setWorkspaces([]);
    previousWorkspaces.current = [];
    if (!user || !roleReady) return;
    return watchWorkspaces(
      globalAdmin,
      (next) => {
        const previous = previousWorkspaces.current;
        setWorkspaceId((old) =>
          old && previous.some((w) => w.id === old) && !next.some((w) => w.id === old && !w.deleted)
            ? undefined
            : old,
        );
        previousWorkspaces.current = next;
        setWorkspaces(next);
      },
      (e) => setToast(errorMessage(e)),
    );
  }, [user?.uid, roleReady, globalAdmin]);
  useEffect(() => {
    setRecords([]);
    setListError('');
    setListReady(!user);
    if (!user) return;
    return watchMaps(
      (items) => {
        setRecords(items);
        setListReady(true);
      },
      (e) => {
        setListError(errorMessage(e));
        setListReady(true);
        if (workspaceId) setLeaseHeld(false);
      },
      workspaceId,
    );
  }, [user?.uid, workspaceId, reload]);
  useEffect(() => {
    setLeases([]);
    if (!workspaceId || !user) return;
    return watchLeases(workspaceId, setLeases, () => setLeaseHeld(false));
  }, [workspaceId, user?.uid]);
  useEffect(() => {
    if (!workspaceId && !editContext) return;
    const timer = setInterval(() => setClock(Date.now()), 5000);
    return () => clearInterval(timer);
  }, [workspaceId, editContext]);
  useEffect(() => {
    const lease = leases.find((l) => l.id === editContext?.mapId);
    setLeaseHeld(
      !!editContext &&
        online &&
        leaseActive(lease, clock) &&
        lease?.uid === user?.uid &&
        lease?.sessionId === editContext.sessionId,
    );
  }, [leases, editContext, clock, online, user?.uid]);
  useEffect(() => {
    if (!editContext) return;
    const context = editContext,
      timer = setInterval(() => {
        void renewLease(context).catch(() => setLeaseHeld(false));
      }, 25000);
    return () => {
      clearInterval(timer);
      void releaseLease(context).catch(() => {});
    };
  }, [editContext]);
  useEffect(() => {
    const handler = () => setOnline(navigator.onLine);
    window.addEventListener('online', handler);
    window.addEventListener('offline', handler);
    return () => {
      window.removeEventListener('online', handler);
      window.removeEventListener('offline', handler);
    };
  }, []);
  useEffect(() => {
    const handler = (e: BeforeUnloadEvent) => {
      if (current && (unsaved || busy)) {
        e.preventDefault();
        e.returnValue = '';
      }
    };
    window.addEventListener('beforeunload', handler);
    return () => window.removeEventListener('beforeunload', handler);
  }, [current, unsaved, busy]);
  const commit = useCallback((next: MapDoc) => {
    const old = currentRef.current;
    if (old && old !== next) {
      historyRef.current.past.push(old);
      historyRef.current.future = [];
      const memory = () => {
        const seen = new Set<ArrayBufferLike>();
        for (const d of [...historyRef.current.past, next])
          for (const layer of ['map', 'keepout', 'speed'] as const) {
            const b = layerBytes(d, layer);
            if (b) seen.add(b.buffer);
          }
        return [...seen].reduce((sum, b) => sum + b.byteLength, 0);
      };
      while (
        historyRef.current.past.length > 20 ||
        (historyRef.current.past.length && memory() > 96 * 1024 * 1024)
      )
        historyRef.current.past.shift();
    }
    currentRef.current = next;
    setCurrent(next);
    setDirty(true);
    renderHistory((v) => v + 1);
  }, []);
  const reset = (map: MapDoc, saved?: CloudRef, context?: EditContext) => {
    currentRef.current = map;
    setCurrent(map);
    setCloud(saved);
    setDirty(!saved?.revision);
    setDraft(false);
    setNameDraft(null);
    setSession((v) => v + 1);
    setEditingWorkspaceId(saved?.workspaceId ?? workspaceId);
    setEditContext(context);
    setLeaseHeld(!!context);
    historyRef.current = { past: [], future: [] };
    renderHistory((v) => v + 1);
  };
  const undo = () => {
    if (!current || busyRef.current || readOnly || !historyRef.current.past.length) return;
    historyRef.current.future.push(current);
    setCurrent(historyRef.current.past.pop()!);
    setDirty(true);
    renderHistory((v) => v + 1);
  };
  const redo = () => {
    if (!current || busyRef.current || readOnly || !historyRef.current.future.length) return;
    historyRef.current.past.push(current);
    setCurrent(historyRef.current.future.pop()!);
    setDirty(true);
    renderHistory((v) => v + 1);
  };
  const leave = () => !current || !unsaved || confirm(t('leave'));
  const closeMap = () => {
    if (busy || !leave()) return;
    setCurrent(null);
    setDirty(false);
    setDraft(false);
    setNameDraft(null);
    setEditContext(undefined);
    setEditingWorkspaceId(undefined);
  };
  const openFiles = (files: FileList | File[]) => {
    if (!files.length || busyRef.current || !leave()) return;
    void run(async () => {
      const selected = Array.from(files);
      if (selected.length > 12 || selected.reduce((sum, f) => sum + f.size, 0) > 128 * 1024 * 1024)
        throw Error('최대 128 MiB의 지도 파일을 선택하세요.');
      const data: NamedFile[] = await Promise.all(
        selected.map(async (f) => ({ name: f.name, bytes: new Uint8Array(await f.arrayBuffer()) })),
      );
      const map = await job<MapDoc>({ action: 'import', files: data });
      if (workspaceId) {
        const id = reserveMapId(workspaceId),
          lease = await acquireLease(workspaceId, id);
        if (!lease.acquired) throw Error(t('editLocked'));
        reset(map, { id, revision: '', workspaceId }, lease.context);
      } else reset(map);
      setUpload(false);
    });
  };
  const persist = async () => {
    if (!current || readOnly) throw Error(t('viewOnly'));
    const desiredName = (nameDraft ?? current.name).trim() || current.name,
      prepared = await editor.current!.prepare(),
      map = prepared.name === desiredName ? prepared : { ...prepared, name: desiredName };
    if (map !== prepared) commit(map);
    setNameDraft(null);
    if (!auth.currentUser) await login();
    const result = await saveMap(map, thumbnail(map), cloud, editContext);
    setCloud(result);
    setDirty(false);
    setDraft(false);
    setToast(t('saved'));
  };
  const save = () => {
    if (current && unsaved && !readOnly) void run(persist);
  };
  const openRecord = (record: CloudMap) =>
    void run(async () => {
      const lease = workspaceId ? await acquireLease(workspaceId, record.id) : undefined;
      try {
        const result = await loadMap(record.id, workspaceId);
        reset(result.map, result.cloud, lease?.acquired ? lease.context : undefined);
      } catch (error) {
        if (lease?.acquired) await releaseLease(lease.context).catch(() => {});
        throw error;
      }
    });
  const download = (record?: CloudMap) =>
    void run(async () => {
      const map = record
          ? (await loadMap(record.id, workspaceId)).map
          : await editor.current!.prepare(),
        output = !record && nameDraft?.trim() ? { ...map, name: nameDraft.trim() } : map;
      downloadBytes(await job<Uint8Array>({ action: 'pack', doc: output }), output.name);
    });
  const editAgain = () =>
    void run(async () => {
      if (!editingWorkspaceId) return;
      const id = cloud?.id || reserveMapId(editingWorkspaceId),
        lease = await acquireLease(editingWorkspaceId, id);
      if (!lease.acquired) throw Error(t('editLocked'));
      if (cloud?.revision && !unsaved) {
        const result = await loadMap(id, editingWorkspaceId);
        reset(result.map, result.cloud, lease.context);
      } else {
        setCloud(cloud || { id, revision: '', workspaceId: editingWorkspaceId });
        setEditContext(lease.context);
        setLeaseHeld(true);
      }
    });
  const finishEditing = () =>
    void run(async () => {
      if (unsaved) await persist();
      if (editContext) await releaseLease(editContext);
      setEditContext(undefined);
      setLeaseHeld(false);
    });
  const remove = (record: CloudMap, trashed: boolean) =>
    void run(async () => {
      if (workspaceId)
        await withLease(workspaceId, record.id, (scope) => trashMap(record.id, trashed, scope));
      else await trashMap(record.id, trashed);
      setToast(t(trashed ? 'remove' : 'restore'));
    });
  const signout = () => {
    if (!leave()) return;
    void run(async () => {
      if (editContext) await releaseLease(editContext).catch(() => {});
      setEditContext(undefined);
      await logout();
      setCurrent(null);
      setCloud(undefined);
      setDirty(false);
    });
  };
  const clearJoin = () => {
    setJoinLink(null);
    const url = new URL(location.href);
    url.searchParams.delete('workspace');
    window.history.replaceState(null, '', url);
  };
  const joined = (id: string) => {
    setWorkspaceId(id);
    setTrash(false);
    clearJoin();
  };
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's' && current) {
        e.preventDefault();
        if (!busyRef.current && !upload && !rename && !workspaceDialog) save();
      }
      if (e.key === 'Escape' && !busyRef.current) {
        setUpload(false);
        setRename(null);
        setWorkspaceDialog(null);
      }
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  });
  return (
    <div
      className="app"
      onDragOver={(e) => {
        if (e.dataTransfer.types.includes('Files')) e.preventDefault();
      }}
      onDrop={(e) => {
        if (!e.dataTransfer.types.includes('Files')) return;
        e.preventDefault();
        openFiles(e.dataTransfer.files);
      }}
    >
      <header className="app-header">
        <a
          className="brand"
          href="#"
          onClick={(e) => {
            e.preventDefault();
            closeMap();
          }}
        >
          <img src="/icon.svg" alt="" />
          <span>Map Editor</span>
        </a>
        {current && (
          <>
            <span className="header-divider" />
            <IconButton title={t('back')} disabled={busy} onClick={closeMap}>
              <ArrowLeft size={17} />
            </IconButton>
            <input
              className="document-name-input"
              aria-label={t('name')}
              title={t('name')}
              maxLength={120}
              disabled={busy || readOnly}
              size={Math.max(8, Math.min(26, (nameDraft ?? current.name).length + 2))}
              value={nameDraft ?? current.name}
              onChange={(e) => setNameDraft(e.target.value)}
              onFocus={(e) => e.currentTarget.select()}
              onBlur={(e) => {
                const name = e.currentTarget.value.trim();
                setNameDraft(null);
                if (name && name !== current.name) commit({ ...current, name });
              }}
              onKeyDown={(e) => {
                if (e.key === 'Enter') {
                  e.preventDefault();
                  e.stopPropagation();
                  e.currentTarget.blur();
                } else if (e.key === 'Escape') {
                  e.preventDefault();
                  e.stopPropagation();
                  e.currentTarget.value = current.name;
                  setNameDraft(null);
                  e.currentTarget.blur();
                }
              }}
            />
            <span className={`save-state${unsaved ? ' edited' : ''}`}>
              {!cloud?.revision ? t('local') : unsaved ? t('unsaved') : t('saved')}
            </span>
          </>
        )}
        <div className="spacer" />
        {current && (
          <>
            <IconButton title={t('upload')} disabled={busy} onClick={() => setUpload(true)}>
              <Upload size={18} />
            </IconButton>
            <IconButton title={`${t('download')} · ZIP`} disabled={busy} onClick={() => download()}>
              <Download size={18} />
            </IconButton>
            <button
              className="primary save-button"
              aria-label={t('save')}
              title={!user ? t('loginSave') : t('save')}
              disabled={busy || readOnly || !unsaved}
              onClick={save}
            >
              <Save size={16} />
            </button>
            <span className="header-divider" />
          </>
        )}
        <AccountMenu
          user={user}
          serviceAdmin={globalAdmin}
          lang={lang}
          theme={theme}
          t={t}
          busy={busy || !authReady}
          onLang={setLang}
          onTheme={setTheme}
          onLogin={() => void run(login)}
          onLogout={signout}
        />
      </header>
      {!online && <div className="offline-banner">{t('offline')}</div>}
      {current ? (
        <>
          {editingWorkspaceId && (
            <div className="edit-lease-banner">
              <LockKeyhole size={14} />
              <span>
                {leaseHeld
                  ? t('editing')
                  : holder
                    ? `${holder.name} · ${t('editing')}`
                    : t('viewOnly')}
              </span>
              <div className="spacer" />
              <button
                className="small-button"
                disabled={busy || !online || (!leaseHeld && !!holder)}
                onClick={leaseHeld ? finishEditing : editAgain}
              >
                {t(leaseHeld ? 'endEditing' : 'beginEditing')}
              </button>
            </div>
          )}
          <Editor
            key={session}
            ref={editor}
            doc={current}
            t={t}
            disabled={busy || readOnly}
            canUndo={!!historyRef.current.past.length}
            canRedo={!!historyRef.current.future.length}
            onUndo={undo}
            onRedo={redo}
            onChange={commit}
            onDraft={setDraft}
            run={run}
          />
        </>
      ) : (
        <Library
          user={user}
          authReady={authReady}
          globalAdmin={globalAdmin}
          workspaceManager={workspaceManager}
          workspaces={workspaces}
          workspaceId={workspaceId}
          records={records}
          leases={leases}
          clock={clock}
          trash={trash}
          loading={!listReady}
          error={listError}
          busy={busy}
          t={t}
          lang={lang}
          onWorkspace={setWorkspaceId}
          onTrash={setTrash}
          onWorkspaceDialog={setWorkspaceDialog}
          onRetry={() => setReload((v) => v + 1)}
          onUpload={() => setUpload(true)}
          onLogin={() => void run(login)}
          onOpen={openRecord}
          onRename={(record) => setRename({ id: record.id, name: record.name })}
          onDownload={(record) => download(record)}
          onRemove={remove}
          onForceUnlock={(record) =>
            void run(async () => {
              if (workspaceId) await forceReleaseLease(workspaceId, record.id);
            })
          }
        />
      )}
      <input
        ref={input}
        className="sr-only"
        type="file"
        multiple
        accept=".pgm,.yaml,.yml,.zip"
        aria-label={t('chooseFiles')}
        onChange={(e) => {
          if (e.target.files) openFiles(e.target.files);
          e.target.value = '';
        }}
      />
      {upload && (
        <div
          className="modal-backdrop"
          onClick={() => {
            if (!busy) setUpload(false);
          }}
        >
          <section
            className="modal"
            role="dialog"
            aria-modal="true"
            aria-labelledby="upload-title"
            onClick={(e) => e.stopPropagation()}
          >
            <div className="modal-heading">
              <h2 id="upload-title">{t('upload')}</h2>
              <IconButton title={t('close')} disabled={busy} onClick={() => setUpload(false)}>
                <X size={19} />
              </IconButton>
            </div>
            <p>{t('importHint')}</p>
            <button className="drop-area" disabled={busy} onClick={() => input.current?.click()}>
              <Upload size={30} />
              <strong>{t('drop')}</strong>
              <span>PGM + YAML / ZIP</span>
              <span className="small-button">{t('chooseFiles')}</span>
            </button>
            <p className="muted">{t('yamlHint')}</p>
          </section>
        </div>
      )}
      {rename && (
        <div
          className="modal-backdrop"
          onClick={() => {
            if (!busy) setRename(null);
          }}
        >
          <form
            className="modal compact"
            role="dialog"
            aria-modal="true"
            aria-label={t('rename')}
            onClick={(e) => e.stopPropagation()}
            onSubmit={(e) => {
              e.preventDefault();
              const name = rename.name.trim();
              if (!name) return;
              void run(async () => {
                if (workspaceId)
                  await withLease(workspaceId, rename.id, (scope) =>
                    renameMap(rename.id, name, scope),
                  );
                else await renameMap(rename.id, name);
                setRename(null);
              });
            }}
          >
            <div className="modal-heading">
              <h2>{t('rename')}</h2>
              <IconButton title={t('close')} disabled={busy} onClick={() => setRename(null)}>
                <X size={18} />
              </IconButton>
            </div>
            <label className="form-field">
              {t('name')}
              <input
                autoFocus
                required
                disabled={busy}
                maxLength={120}
                value={rename.name}
                onChange={(e) => setRename({ ...rename, name: e.target.value })}
              />
            </label>
            <div className="modal-actions">
              <button type="button" disabled={busy} onClick={() => setRename(null)}>
                {t('cancel')}
              </button>
              <button className="primary" disabled={busy || !rename.name.trim()}>
                {t('apply')}
              </button>
            </div>
          </form>
        </div>
      )}
      {workspaceDialog && (
        <WorkspaceDialog
          mode={workspaceDialog}
          workspace={workspaceDialog === 'manage' ? selectedWorkspace : undefined}
          workspaces={workspaces}
          globalAdmin={globalAdmin}
          t={t}
          busy={busy}
          run={run}
          onClose={() => setWorkspaceDialog(null)}
          onCreated={(id) => {
            setWorkspaceId(id);
            setTrash(false);
            setWorkspaceDialog(null);
          }}
        />
      )}
      {joinLink && (
        <JoinWorkspaceDialog
          linkId={joinLink}
          t={t}
          userReady={authReady && roleReady}
          globalAdmin={globalAdmin}
          busy={busy}
          run={run}
          onLogin={() => void run(login)}
          onClose={clearJoin}
          onJoined={joined}
        />
      )}
      {toast && (
        <div className="toast" role="status">
          <span>{toast}</span>
          <IconButton title={t('close')} onClick={() => setToast('')}>
            <X size={16} />
          </IconButton>
        </div>
      )}
      {busy && (
        <div className="busy-indicator" role="status">
          <LoaderCircle className="spin" size={16} />
          {t('busy')}
        </div>
      )}
    </div>
  );
}
