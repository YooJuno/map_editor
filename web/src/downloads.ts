export const platforms = [
  'windows-x86_64',
  'ubuntu-24.04-x86_64',
  'macos-arm64',
  'macos-x86_64',
] as const;
export type Platform = (typeof platforms)[number];
export type DownloadRelease = {
  version: string;
  publishedAt: string;
  assets: { platform: Platform; name: string; url: string; bytes: number }[];
  checksums?: string;
};
const base = 'https://github.com/YooJuno/map_editor/releases/download/';
export function parseRelease(data: {
  tag_name?: string;
  published_at?: string;
  assets?: { name: string; browser_download_url: string; size: number }[];
}): DownloadRelease {
  if (!/^v\d+\.\d+\.\d+$/.test(data.tag_name || '')) throw Error('Invalid release');
  const version = data.tag_name!.slice(1),
    assets: DownloadRelease['assets'] = [];
  for (const platform of platforms) {
    const name = `MapEditor-${version}-${platform}.${platform.startsWith('windows') ? 'zip' : 'tar.gz'}`;
    const expected = `${base}v${version}/${name}`;
    const asset = data.assets?.find(
      (a) => a.name === name && a.browser_download_url === expected && a.size > 0,
    );
    if (asset) assets.push({ platform, name, url: expected, bytes: asset.size });
  }
  if (assets.length !== 4) throw Error('Release packages are incomplete');
  const sums = `${base}v${version}/SHA256SUMS`;
  return {
    version,
    assets,
    publishedAt: data.published_at || '',
    checksums: data.assets?.some((a) => a.name === 'SHA256SUMS' && a.browser_download_url === sums)
      ? sums
      : undefined,
  };
}
export async function loadDownloads(
  signal?: AbortSignal,
  onCached?: (release: DownloadRelease) => void,
) {
  const cached = await fetch('/downloads.json', { signal });
  if (!cached.ok) throw Error('Download list unavailable');
  const fallback = parseRelease(await cached.json());
  onCached?.(fallback);
  try {
    const response = await fetch(
      'https://api.github.com/repos/YooJuno/map_editor/releases/latest',
      {
        signal: AbortSignal.any([
          signal || new AbortController().signal,
          AbortSignal.timeout(5000),
        ]),
        headers: { Accept: 'application/vnd.github+json' },
      },
    );
    if (response.ok) return parseRelease(await response.json());
  } catch {
    /* The bundled release stays usable when the API is unavailable. */
  }
  return fallback;
}
export function currentPlatform() {
  const ua = navigator.userAgent.toLowerCase();
  if (ua.includes('win')) return 'windows';
  if (ua.includes('mac') && !ua.includes('iphone') && !ua.includes('ipad')) return 'macos';
  if (ua.includes('linux') && !ua.includes('android')) return 'ubuntu';
  return '';
}
