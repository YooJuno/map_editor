import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import { ChevronLeft, ChevronRight, X } from 'lucide-react';
import { useCompact } from './mobile';
import type { Lang } from './i18n';
import { tutorialLabels, tutorialSteps } from './tutorial';
type Box = { left: number; top: number; width: number; height: number };
export function Tutorial({
  lang,
  index,
  busy,
  onStep,
  onClose,
}: {
  lang: Lang;
  index: number;
  busy: boolean;
  onStep: (index: number) => void;
  onClose: () => void;
}) {
  const compact = useCompact();
  const panelStep = /metadata|yaml|mask-display|speed-value/.test(
    tutorialSteps(lang)[index].target,
  );
  const steps = tutorialSteps(lang, compact),
    step = steps[index],
    labels = tutorialLabels[lang];
  const [box, setBox] = useState<Box | null>(null),
    [position, setPosition] = useState({ left: 0, top: 0 });
  const card = useRef<HTMLElement>(null),
    close = useRef<HTMLButtonElement>(null);
  useEffect(() => {
    close.current?.focus();
  }, []);
  useLayoutEffect(() => {
    let observer: ResizeObserver | undefined;
    const measure = () => {
      const target = document.querySelector<HTMLElement>(step.target),
        r = target?.getBoundingClientRect();
      setBox(
        r && r.width && r.height
          ? {
              left: r.left - 5,
              top: r.top - 5,
              width: r.width + 10,
              height: r.height + 10,
            }
          : null,
      );
      const w = card.current?.offsetWidth || 340,
        h = card.current?.offsetHeight || 230;
      const vw = window.innerWidth,
        vh = window.innerHeight;
      // Keep the canvas usable. Prefer the right of left-side controls and below top controls.
      let left = r && r.right < vw * 0.5 ? r.right + 20 : r ? r.left - w - 20 : vw - w - 24;
      if (step.target === '.account-trigger') left = 24;
      let top = r && r.height < vh * 0.6 ? r.bottom + 18 : vh - h - 50;
      if (r && r.top > 120 && r.height < vh * 0.6) top = r.top;
      setPosition({
        left: Math.max(14, Math.min(vw - w - 14, left)),
        top: Math.max(76, Math.min(vh - h - 14, top)),
      });
    };
    const frame = requestAnimationFrame(() => {
      document
        .querySelector<HTMLElement>(step.target)
        ?.scrollIntoView?.({ block: 'nearest', inline: 'nearest' });
      measure();
      const target = document.querySelector<HTMLElement>(step.target);
      observer = new ResizeObserver(measure);
      if (target) observer.observe(target);
      if (card.current) observer.observe(card.current);
    });
    window.addEventListener('resize', measure);
    window.addEventListener('scroll', measure, true);
    return () => {
      cancelAnimationFrame(frame);
      observer?.disconnect();
      window.removeEventListener('resize', measure);
      window.removeEventListener('scroll', measure, true);
    };
  }, [index, lang, step.target]);
  return (
    <div className="tutorial" data-testid="tutorial">
      {box && <div className="tutorial-highlight" style={box} aria-hidden="true" />}
      <section
        ref={card}
        className="tutorial-card"
        data-position={compact && panelStep ? 'top' : undefined}
        data-shortcuts="off"
        role="region"
        aria-label={labels.name}
        style={position}
        onKeyDown={(e) => {
          if (e.key === 'Escape') {
            e.preventDefault();
            e.stopPropagation();
            if (!busy) onClose();
          }
          if (e.key === 'ArrowRight' && !busy && index < steps.length - 1) {
            e.preventDefault();
            onStep(index + 1);
          }
          if (e.key === 'ArrowLeft' && !busy && index > 0) {
            e.preventDefault();
            onStep(index - 1);
          }
        }}
      >
        <div className="tutorial-heading">
          <span>
            {labels.name}{' '}
            <b>
              {index + 1} / {steps.length}
            </b>
          </span>
          <button
            ref={close}
            className="icon-button"
            title={labels.close}
            aria-label={labels.close}
            disabled={busy}
            onClick={onClose}
          >
            <X size={17} />
          </button>
        </div>
        <div className="tutorial-progress" aria-hidden="true">
          <span style={{ width: `${((index + 1) / steps.length) * 100}%` }} />
        </div>
        <div aria-live="polite" aria-atomic="true">
          <h2>{step.title}</h2>
          <p>{step.body}</p>
        </div>
        <div className="tutorial-actions">
          <button disabled={busy || index === 0} onClick={() => onStep(index - 1)}>
            <ChevronLeft size={15} />
            {labels.previous}
          </button>
          <button
            className="primary"
            disabled={busy}
            onClick={() => (index === steps.length - 1 ? onClose() : onStep(index + 1))}
          >
            {index === steps.length - 1 ? labels.finish : labels.next}
            <ChevronRight size={15} />
          </button>
        </div>
      </section>
    </div>
  );
}
