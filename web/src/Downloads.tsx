import { useEffect, useState } from 'react';
import { ArrowLeft, Check, Download, LoaderCircle, Monitor, RotateCcw } from 'lucide-react';
import type { Lang } from './i18n';
import { currentPlatform, loadDownloads, type DownloadRelease, type Platform } from './downloads';
const labels = {
  ko: {
    title: '데스크톱 다운로드',
    back: '지도로 돌아가기',
    subtitle: '로컬 파일로 작업하는 Map Editor.',
    download: '다운로드',
    current: '현재 OS',
    launch: '압축 해제 후 실행',
    unsigned: '서명되지 않은 배포 파일입니다. Qt 포함 · ROS 설치 불필요',
    checksums: 'SHA-256 체크섬',
    failed: '다운로드 목록을 불러오지 못했습니다.',
    retry: '다시 시도',
  },
  en: {
    title: 'Download for desktop',
    back: 'Back to maps',
    subtitle: 'Map Editor for local files.',
    download: 'Download',
    current: 'Your OS',
    launch: 'Extract and launch',
    unsigned: 'Unsigned packages. Qt included · No ROS required',
    checksums: 'SHA-256 checksums',
    failed: 'Could not load downloads.',
    retry: 'Retry',
  },
  ja: {
    title: 'デスクトップ版',
    back: '地図に戻る',
    subtitle: 'ローカルファイル用のMap Editor。',
    download: 'ダウンロード',
    current: '現在のOS',
    launch: '展開して実行',
    unsigned: '未署名のパッケージです。Qt同梱 · ROS不要',
    checksums: 'SHA-256チェックサム',
    failed: '一覧を取得できませんでした。',
    retry: '再試行',
  },
  zh: {
    title: '下载桌面版',
    back: '返回地图',
    subtitle: '用于本地文件的Map Editor。',
    download: '下载',
    current: '当前系统',
    launch: '解压后运行',
    unsigned: '未签名的软件包。包含Qt · 无需ROS',
    checksums: 'SHA-256校验和',
    failed: '无法加载下载列表。',
    retry: '重试',
  },
};
const info: Record<Platform, { name: string; arch: string; launch: string }> = {
  'windows-x86_64': {
    name: 'Windows',
    arch: 'x64',
    launch: 'bin/mapwright.exe',
  },
  'ubuntu-24.04-x86_64': {
    name: 'Ubuntu 24.04',
    arch: 'x64',
    launch: './MapEditor.sh',
  },
  'macos-arm64': {
    name: 'macOS',
    arch: 'Apple Silicon',
    launch: 'mapwright.app',
  },
  'macos-x86_64': { name: 'macOS', arch: 'Intel', launch: 'mapwright.app' },
};
export function Downloads({ lang, onBack }: { lang: Lang; onBack: () => void }) {
  const [release, setRelease] = useState<DownloadRelease | null>(null),
    [error, setError] = useState(false),
    [reload, setReload] = useState(0);
  const t = labels[lang],
    os = currentPlatform();
  useEffect(() => {
    const controller = new AbortController();
    setError(false);
    void loadDownloads(controller.signal, (r) => {
      if (!controller.signal.aborted) setRelease(r);
    })
      .then((r) => {
        if (!controller.signal.aborted) setRelease(r);
      })
      .catch(() => {
        if (!controller.signal.aborted) setError(true);
      });
    return () => controller.abort();
  }, [reload]);
  const assets = release?.assets
    .slice()
    .sort(
      (a, b) => Number(b.platform.startsWith(os || '?')) - Number(a.platform.startsWith(os || '?')),
    );
  return (
    <main className="downloads-page">
      <button className="text-button download-back" onClick={onBack}>
        <ArrowLeft size={17} />
        {t.back}
      </button>
      <div className="download-heading">
        <img src="/icon.svg" alt="" />
        <div>
          <h1>{t.title}</h1>
          <p>{t.subtitle}</p>
        </div>
        {release && <span className="version-badge">v{release.version}</span>}
      </div>
      {!release && !error && (
        <div className="empty-state">
          <LoaderCircle className="spin" />
        </div>
      )}
      {error && (
        <div className="error-panel">
          <p>{t.failed}</p>
          <button onClick={() => setReload((v) => v + 1)}>
            <RotateCcw size={16} />
            {t.retry}
          </button>
        </div>
      )}
      {release && (
        <>
          <div className="download-list">
            {assets!.map((asset) => {
              const platform = info[asset.platform],
                recommended = asset.platform.startsWith(os || '?');
              return (
                <section
                  className={`download-row${recommended ? ' current-platform' : ''}`}
                  key={asset.platform}
                >
                  <div className="download-os">
                    <Monitor size={23} />
                    <div>
                      <h2>{platform.name}</h2>
                      <span>{platform.arch}</span>
                    </div>
                  </div>
                  <div className="download-launch">
                    <span>{t.launch}</span>
                    <code>{platform.launch}</code>
                  </div>
                  <div className="download-action">
                    {recommended && (
                      <small>
                        <Check size={12} />
                        {t.current}
                      </small>
                    )}
                    <a className="primary download-file" href={asset.url} download={asset.name}>
                      <Download size={16} />
                      {t.download}
                      <span>{(asset.bytes / 1048576).toFixed(1)} MB</span>
                    </a>
                  </div>
                </section>
              );
            })}
          </div>
          <div className="download-footnote">
            <span>{t.unsigned}</span>
            {release.checksums && (
              <a href={release.checksums} download="SHA256SUMS">
                {t.checksums}
              </a>
            )}
          </div>
        </>
      )}
    </main>
  );
}
