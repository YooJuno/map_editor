// @vitest-environment jsdom
import { beforeEach, afterEach, describe, it, expect, vi } from 'vitest';
import { render, screen, cleanup, fireEvent } from '@testing-library/react';
import { parseRelease, loadDownloads } from '../src/downloads';
import { Downloads } from '../src/Downloads';
import release from '../public/downloads.json';
beforeEach(cleanup);
afterEach(() => vi.unstubAllGlobals());
describe('desktop downloads', () => {
  it('rejects releases with missing platform packages', () => {
    expect(() => parseRelease({ ...release, assets: release.assets.slice(0, 2) })).toThrow(
      'incomplete',
    );
  });
  it('rejects download URLs outside this repository', () => {
    expect(() =>
      parseRelease({
        ...release,
        assets: release.assets.map((a) => ({
          ...a,
          browser_download_url: a.browser_download_url.replace('github.com', 'example.com'),
        })),
      }),
    ).toThrow('incomplete');
  });
  it('uses bundled packages when the latest-release API is unavailable', async () => {
    vi.stubGlobal(
      'fetch',
      vi
        .fn()
        .mockResolvedValueOnce({ ok: true, json: async () => release })
        .mockRejectedValueOnce(Error('offline')),
    );
    expect((await loadDownloads()).assets).toHaveLength(4);
  });
  it('offers direct file links without navigating to a GitHub release page', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue({ ok: true, json: async () => release }));
    const back = vi.fn();
    render(<Downloads lang="ko" onBack={back} />);
    const links = await screen.findAllByRole('link', { name: /다운로드/ });
    expect(links).toHaveLength(4);
    for (const link of links) {
      expect(link.getAttribute('href')).toMatch(/\/releases\/download\/v[\d.]+\/MapEditor-/);
      expect(link.hasAttribute('download')).toBe(true);
      expect(link.getAttribute('target')).toBeNull();
    }
    fireEvent.click(screen.getByRole('button', { name: '지도로 돌아가기' }));
    expect(back).toHaveBeenCalled();
  });
});
