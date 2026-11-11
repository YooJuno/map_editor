import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { onAuthStateChanged, type User } from 'firebase/auth';
import {
  ArrowLeft,
  ArrowUpRight,
  Download,
  Globe2,
  LogOut,
  Map,
  Moon,
  MoreHorizontal,
  Pencil,
  Plus,
  Search,
  Sun,
  Trash2,
  Upload,
  X,
  RotateCcw,
  LoaderCircle,
  Save,
  HardDrive,
  ShieldOff,
  Gauge,
} from 'lucide-react';
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
  type CloudMap,
} from './firebase';
import { Editor, IconButton, type EditorHandle } from './Editor';
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
  const [theme, setTheme] = useState(() => preference('map-editor-theme', 'dark'));
  const t = useMemo(() => translator(lang), [lang]);
  const [user, setUser] = useState<User | null>(null),
    [authReady, setAuthReady] = useState(false),
    [records, setRecords] = useState<CloudMap[]>([]),
    [listReady, setListReady] = useState(false),
    [listError, setListError] = useState('');
  const [current, setCurrent] = useState<MapDoc | null>(null),
    [cloud, setCloud] = useState<{ id: string; revision: string }>(),
    [dirty, setDirty] = useState(false),
    [draft, setDraft] = useState(false);
  const [busy, setBusy] = useState(false),
    [toast, setToast] = useState(''),
    [upload, setUpload] = useState(false),
    [trash, setTrash] = useState(false),
    [search, setSearch] = useState(''),
    [rename, setRename] = useState<{ id?: string; name: string } | null>(null);
  const [online, setOnline] = useState(navigator.onLine),
    [, setHistoryVersion] = useState(0),
    [reload, setReload] = useState(0),
    [session, setSession] = useState(0);
  const currentRef = useRef<MapDoc | null>(null);
  currentRef.current = current;
  const editor = useRef<EditorHandle>(null),
    input = useRef<HTMLInputElement>(null),
    busyRef = useRef(false),
    history = useRef<{ past: MapDoc[]; future: MapDoc[] }>({ past: [], future: [] }),
    owner = useRef<string | null>(null);
  useEffect(() => {
    document.documentElement.dataset.theme = theme;
    document.documentElement.lang = lang;
    try {
      localStorage.setItem('map-editor-theme', theme);
      localStorage.setItem('map-editor-language', lang);
    } catch {
      /* Preferences are optional in private browsing. */
    }
  }, [theme, lang]);
  useEffect(() => {
    const timer = setTimeout(() => setToast(''), 9000);
    return () => clearTimeout(timer);
  }, [toast]);
  const run = useCallback(async (task: () => Promise<void>) => {
    if (busyRef.current) return;
    busyRef.current = true;
    setBusy(true);
    try {
      await task();
    } catch (error) {
      setToast(errorMessage(error));
    } finally {
      busyRef.current = false;
      setBusy(false);
    }
  }, []);
  useEffect(() => {
    void finishLogin().catch((e) => setToast(errorMessage(e)));
    return onAuthStateChanged(auth, (next) => {
      if (owner.current && owner.current !== next?.uid) {
        setCloud(undefined);
        setRecords([]);
        setDirty(true);
      }
      owner.current = next?.uid || null;
      setUser(next);
      setAuthReady(true);
    });
  }, []);
  useEffect(() => {
    setRecords([]);
    setListError('');
    setListReady(!user);
    if (!user) return;
    return watchMaps(
      (maps) => {
        setRecords(maps);
        setListReady(true);
      },
      (e) => {
        setListError(errorMessage(e));
        setListReady(true);
      },
    );
  }, [user, reload]);
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
    const before = (e: BeforeUnloadEvent) => {
      if (current && (dirty || draft || busy)) {
        e.preventDefault();
        e.returnValue = '';
      }
    };
    window.addEventListener('beforeunload', before);
    return () => window.removeEventListener('beforeunload', before);
  }, [dirty, draft, busy, current]);
  const commit = useCallback((next: MapDoc) => {
    const old = currentRef.current;
    if (old && old !== next) {
      history.current.past.push(old);
      history.current.future = [];
      const memory = () => {
        const seen = new Set<ArrayBufferLike>();
        for (const d of [...history.current.past, next])
          for (const layer of ['map', 'keepout', 'speed'] as const) {
            const bytes = layerBytes(d, layer);
            if (bytes) seen.add(bytes.buffer);
          }
        return [...seen].reduce((sum, bytes) => sum + bytes.byteLength, 0);
      };
      while (
        history.current.past.length > 20 ||
        (history.current.past.length && memory() > 96 * 1024 * 1024)
      )
        history.current.past.shift();
    }
    currentRef.current = next;
    setCurrent(next);
    setDirty(true);
    setHistoryVersion((v) => v + 1);
  }, []);
  const reset = (doc: MapDoc, saved?: { id: string; revision: string }) => {
    currentRef.current = doc;
    setCurrent(doc);
    setSession((v) => v + 1);
    setCloud(saved);
    setDirty(!saved);
    setDraft(false);
    history.current = { past: [], future: [] };
    setHistoryVersion((v) => v + 1);
  };
  const undo = () => {
    if (!current || busyRef.current || !history.current.past.length) return;
    history.current.future.push(current);
    setCurrent(history.current.past.pop()!);
    setDirty(true);
    setHistoryVersion((v) => v + 1);
  };
  const redo = () => {
    if (!current || busyRef.current || !history.current.future.length) return;
    history.current.past.push(current);
    setCurrent(history.current.future.pop()!);
    setDirty(true);
    setHistoryVersion((v) => v + 1);
  };
  const leave = () => {
    if (current && (dirty || draft) && !confirm(t('leave'))) return false;
    return true;
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
      reset(await job<MapDoc>({ action: 'import', files: data }));
      setUpload(false);
    });
  };
  const save = () => {
    if (!current || !(dirty || draft)) return;
    void run(async () => {
      const map = await editor.current!.prepare();
      if (!auth.currentUser) await login();
      const result = await saveMap(map, thumbnail(map), cloud);
      setCloud(result);
      setDirty(false);
      setDraft(false);
      setToast(t('saved'));
    });
  };
  const download = (record?: CloudMap) =>
    void run(async () => {
      const map = record ? (await loadMap(record.id)).map : await editor.current!.prepare();
      downloadBytes(await job<Uint8Array>({ action: 'pack', doc: map }), map.name);
    });
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's' && current) {
        e.preventDefault();
        if (!busyRef.current) save();
      }
      if (e.key === 'Escape' && !busyRef.current) {
        setUpload(false);
        setRename(null);
      }
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  });
  const filtered = records.filter(
    (r) => r.trashed === trash && r.name.toLocaleLowerCase().includes(search.toLocaleLowerCase()),
  );
  const signout = () => {
    if (!leave()) return;
    void run(async () => {
      await logout();
      setCurrent(null);
      setCloud(undefined);
      setDirty(false);
    });
  };
  const drop = (e: React.DragEvent) => {
    if (!e.dataTransfer.types.includes('Files')) return;
    e.preventDefault();
    openFiles(e.dataTransfer.files);
  };
  return (
    <div
      className="app"
      onDragOver={(e) => {
        if (e.dataTransfer.types.includes('Files')) e.preventDefault();
      }}
      onDrop={drop}
    >
      <header className="app-header">
        <a
          className="brand"
          href="#"
          onClick={(e) => {
            e.preventDefault();
            if (!busy && leave()) {
              setCurrent(null);
              setDirty(false);
              setDraft(false);
            }
          }}
        >
          <img src="/icon.svg" alt="" />
          <span>Map Editor</span>
          <small>WEB</small>
        </a>
        {current && (
          <>
            <span className="header-divider" />
            <IconButton
              title={t('back')}
              disabled={busy}
              onClick={() => {
                if (leave()) {
                  setCurrent(null);
                  setDirty(false);
                  setDraft(false);
                }
              }}
            >
              <ArrowLeft size={17} />
            </IconButton>
            <span className="document-name">{current.name}</span>
            <IconButton
              title={t('rename')}
              disabled={busy}
              onClick={() => setRename({ name: current.name })}
            >
              <Pencil size={14} />
            </IconButton>
            <span className={`save-state${dirty || draft ? ' edited' : ''}`}>
              {!cloud ? t('local') : dirty || draft ? t('unsaved') : t('saved')}
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
              title={!user ? t('loginSave') : t('save')}
              disabled={busy || !(dirty || draft)}
              onClick={save}
            >
              <Save size={16} />
              <span>{t('save')}</span>
            </button>
            <span className="header-divider" />
          </>
        )}
        <label className="language-switch" title="Language">
          <Globe2 size={16} />
          <select
            aria-label="Language"
            value={lang}
            onChange={(e) => setLang(e.target.value as Lang)}
          >
            <option value="ko">한국어</option>
            <option value="en">English</option>
            <option value="ja">日本語</option>
            <option value="zh">简体中文</option>
          </select>
        </label>
        <IconButton
          title={theme === 'dark' ? 'Light theme' : 'Dark theme'}
          onClick={() => setTheme(theme === 'dark' ? 'light' : 'dark')}
        >
          {theme === 'dark' ? <Sun size={18} /> : <Moon size={18} />}
        </IconButton>
        {user ? (
          <div className="account">
            <span className="avatar" title={user.email || ''}>
              {user.photoURL ? (
                <img
                  src={user.photoURL}
                  referrerPolicy="no-referrer"
                  alt={user.displayName || ''}
                />
              ) : (
                user.displayName?.[0] || 'G'
              )}
            </span>
            <IconButton title={t('logout')} disabled={busy} onClick={signout}>
              <LogOut size={16} />
            </IconButton>
          </div>
        ) : (
          <button
            className="login-small"
            disabled={busy || !authReady}
            onClick={() => void run(login)}
          >
            {t('login')}
          </button>
        )}
      </header>
      {!online && <div className="offline-banner">{t('offline')}</div>}
      {current ? (
        <Editor
          key={session}
          ref={editor}
          doc={current}
          t={t}
          disabled={busy}
          canUndo={!!history.current.past.length}
          canRedo={!!history.current.future.length}
          onUndo={undo}
          onRedo={redo}
          onChange={commit}
          onDraft={setDraft}
          run={run}
        />
      ) : (
        <div className="library">
          <aside className="library-sidebar">
            <div className="sidebar-heading">WORKSPACE</div>
            <button
              className={!trash ? 'nav-item active' : 'nav-item'}
              onClick={() => setTrash(false)}
            >
              <Map size={18} />
              {t('maps')}
              <span>{records.filter((r) => !r.trashed).length}</span>
            </button>
            <button
              className={trash ? 'nav-item active' : 'nav-item'}
              onClick={() => setTrash(true)}
            >
              <Trash2 size={18} />
              {t('trash')}
            </button>
            <div className="spacer" />
            <a
              className="desktop-link"
              href="https://github.com/YooJuno/map_editor/releases/latest"
              target="_blank"
              rel="noreferrer"
            >
              <HardDrive size={16} />
              Desktop
              <ArrowUpRight size={14} />
            </a>
          </aside>
          <main className="library-main">
            <div className="library-heading">
              <div>
                <div className="eyebrow">MAP LIBRARY</div>
                <h1>{t(trash ? 'trash' : 'maps')}</h1>
                <p>{t(trash ? 'trashHint' : 'intro')}</p>
              </div>
              <button className="primary" disabled={busy} onClick={() => setUpload(true)}>
                <Plus size={17} />
                {t('upload')}
              </button>
            </div>
            {!user && authReady && (
              <div className="signin-panel">
                <div className="signin-copy">
                  <div className="section-icon">
                    <Map size={26} />
                  </div>
                  <h2>Map Editor</h2>
                  <p>{t('intro')}</p>
                  <button className="google-button" disabled={busy} onClick={() => void run(login)}>
                    <span className="google-mark">G</span>
                    {t('login')}
                  </button>
                  <small>{t('loginHint')}</small>
                  <div className="local-links">
                    <button onClick={() => setUpload(true)}>
                      <Upload size={15} />
                      {t('upload')}
                    </button>
                  </div>
                </div>
                <div className="signin-preview">
                  <img src="/preview.png" alt="PGM map preview" />
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
            {user && (
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
                  <span className="muted">{filtered.length}</span>
                </div>
                {listError ? (
                  <div className="error-panel">
                    <p>{listError}</p>
                    <button onClick={() => setReload((v) => v + 1)}>
                      <RotateCcw size={16} />
                      {t('redo')}
                    </button>
                  </div>
                ) : !listReady ? (
                  <div className="empty-state">
                    <LoaderCircle className="spin" />
                    {t('busy')}
                  </div>
                ) : !filtered.length ? (
                  <div className="empty-state">
                    <div className="empty-icon">
                      {trash ? <Trash2 size={28} /> : <Map size={30} />}
                    </div>
                    <h2>{t(search ? 'noResults' : trash ? 'emptyTrash' : 'empty')}</h2>
                    {!trash && !search && (
                      <>
                        <p>{t('emptyHint')}</p>
                        <button className="primary" onClick={() => setUpload(true)}>
                          <Upload size={16} />
                          {t('upload')}
                        </button>
                      </>
                    )}
                  </div>
                ) : (
                  <div className="map-grid">
                    {filtered.map((record) => (
                      <article className="map-card" key={record.id}>
                        <button
                          className="map-preview"
                          disabled={busy || trash}
                          title={record.name}
                          onClick={() =>
                            void run(async () => {
                              const result = await loadMap(record.id);
                              reset(result.map, result.cloud);
                            })
                          }
                        >
                          <img src={record.preview} alt={record.name} />
                          <span className="map-open-icon">
                            <ArrowUpRight size={18} />
                          </span>
                        </button>
                        <div className="map-card-body">
                          <div className="map-card-title">
                            <h2 title={record.name}>{record.name}</h2>
                            <details className="card-menu">
                              <summary aria-label="More">
                                <MoreHorizontal size={19} />
                              </summary>
                              <div>
                                {trash ? (
                                  <button
                                    disabled={busy}
                                    onClick={() =>
                                      void run(async () => {
                                        await trashMap(record.id, false);
                                        setToast(t('restore'));
                                      })
                                    }
                                  >
                                    <RotateCcw size={15} />
                                    {t('restore')}
                                  </button>
                                ) : (
                                  <>
                                    <button
                                      disabled={busy}
                                      onClick={() =>
                                        setRename({ id: record.id, name: record.name })
                                      }
                                    >
                                      <Pencil size={15} />
                                      {t('rename')}
                                    </button>
                                    <button disabled={busy} onClick={() => download(record)}>
                                      <Download size={15} />
                                      {t('download')}
                                    </button>
                                    <button
                                      disabled={busy}
                                      onClick={() =>
                                        void run(async () => {
                                          await trashMap(record.id, true);
                                          setToast(t('remove'));
                                        })
                                      }
                                    >
                                      <Trash2 size={15} />
                                      {t('remove')}
                                    </button>
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
                          <time>{record.updatedAt?.toDate().toLocaleDateString(lang)}</time>
                        </div>
                      </article>
                    ))}
                  </div>
                )}
              </>
            )}
          </main>
        </div>
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
            onClick={(e) => e.stopPropagation()}
            role="dialog"
            aria-modal="true"
            aria-label={t('rename')}
            onSubmit={(e) => {
              e.preventDefault();
              const name = rename.name.trim();
              if (!name) return;
              void run(async () => {
                if (rename.id) await renameMap(rename.id, name);
                else if (current) commit({ ...current, name });
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
      {toast && (
        <div role="status" className="toast">
          <span>{toast}</span>
          <IconButton title={t('close')} onClick={() => setToast('')}>
            <X size={16} />
          </IconButton>
        </div>
      )}
      {busy && (
        <div className="busy-indicator" role="status">
          <LoaderCircle size={16} className="spin" />
          {t('busy')}
        </div>
      )}
    </div>
  );
}
