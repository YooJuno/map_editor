import { useEffect, useState } from 'react';

export function useCompact() {
  const query = '(max-width: 900px)';
  const [compact, setCompact] = useState(() => window.matchMedia?.(query).matches ?? false);
  useEffect(() => {
    const media = window.matchMedia?.(query);
    if (!media) return;
    const changed = () => setCompact(media.matches);
    changed();
    media.addEventListener('change', changed);
    return () => media.removeEventListener('change', changed);
  }, []);
  return compact;
}
