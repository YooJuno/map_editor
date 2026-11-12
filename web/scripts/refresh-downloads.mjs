// Refresh the offline fallback during builds. No credentials are written to output.
import { writeFile } from 'node:fs/promises';
try {
  const headers = { Accept: 'application/vnd.github+json' };
  if (process.env.GH_TOKEN) headers.Authorization = `Bearer ${process.env.GH_TOKEN}`;
  const response = await fetch('https://api.github.com/repos/YooJuno/map_editor/releases/latest', {
    headers,
    signal: AbortSignal.timeout(5000),
  });
  if (!response.ok) throw Error('Release API unavailable');
  const data = await response.json();
  if (!/^v\d+\.\d+\.\d+$/.test(data.tag_name || '')) throw Error('Invalid release tag');
  const version = data.tag_name.slice(1),
    base = `https://github.com/YooJuno/map_editor/releases/download/${data.tag_name}/`;
  for (const platform of ['windows-x86_64', 'ubuntu-24.04-x86_64', 'macos-arm64', 'macos-x86_64']) {
    const name = `MapEditor-${version}-${platform}.${platform.startsWith('windows') ? 'zip' : 'tar.gz'}`;
    if (
      !data.assets?.some(
        (a) => a.name === name && a.browser_download_url === base + name && a.size > 0,
      )
    )
      throw Error('Incomplete release');
  }
  const result = {
    tag_name: data.tag_name,
    published_at: data.published_at,
    assets: data.assets
      .filter((a) => a.name.startsWith(`MapEditor-${version}-`) || a.name === 'SHA256SUMS')
      .map(({ name, size, browser_download_url }) => ({ name, size, browser_download_url })),
  };
  await writeFile(
    new URL('../public/downloads.json', import.meta.url),
    JSON.stringify(result, null, 2) + '\n',
  );
  console.log(`Desktop downloads: ${data.tag_name}`);
} catch {
  console.log('Desktop downloads: using bundled release metadata');
}
