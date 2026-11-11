import type { Job } from './worker';
let worker: Worker | undefined,
  sequence = 0;
const pending = new Map<
  number,
  { resolve: (value: unknown) => void; reject: (error: Error) => void }
>();
export function job<T>(task: Job): Promise<T> {
  if (!worker) {
    worker = new Worker(new URL('./worker.ts', import.meta.url), { type: 'module' });
    worker.onmessage = ({ data }) => {
      const item = pending.get(data.id);
      if (!item) return;
      pending.delete(data.id);
      if (data.error) item.reject(new Error(data.error));
      else item.resolve(data.result);
    };
    worker.onerror = () => {
      pending.forEach((p) => p.reject(Error('지도 처리 중 오류가 발생했습니다. 다시 시도하세요.')));
      pending.clear();
      worker?.terminate();
      worker = undefined;
    };
  }
  const id = ++sequence;
  return new Promise<T>((resolve, reject) => {
    pending.set(id, { resolve: resolve as (value: unknown) => void, reject });
    worker!.postMessage({ id, job: task });
  });
}
