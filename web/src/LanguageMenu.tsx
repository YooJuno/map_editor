import { useEffect, useRef, useState } from 'react';
import { Check, ChevronDown, Globe2 } from 'lucide-react';
import type { Lang } from './i18n';
const languages: { value: Lang; label: string }[] = [
  { value: 'ko', label: '한국어' },
  { value: 'en', label: 'English' },
  { value: 'ja', label: '日本語' },
  { value: 'zh', label: '简体中文' },
];
export function LanguageMenu({ value, onChange }: { value: Lang; onChange: (lang: Lang) => void }) {
  const [open, setOpen] = useState(false);
  const root = useRef<HTMLDivElement>(null),
    trigger = useRef<HTMLButtonElement>(null);
  useEffect(() => {
    if (!open) return;
    root.current?.querySelector<HTMLElement>('[aria-checked="true"]')?.focus();
    const outside = (event: PointerEvent) => {
      if (!root.current?.contains(event.target as Node)) setOpen(false);
    };
    document.addEventListener('pointerdown', outside);
    return () => document.removeEventListener('pointerdown', outside);
  }, [open]);
  return (
    <div
      className="language-menu"
      ref={root}
      onBlur={(event) => {
        if (!event.currentTarget.contains(event.relatedTarget)) setOpen(false);
      }}
    >
      <button
        className="language-trigger"
        type="button"
        ref={trigger}
        title="Language"
        aria-haspopup="menu"
        aria-expanded={open}
        onClick={() => setOpen(!open)}
        onKeyDown={(event) => {
          if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
            event.preventDefault();
            setOpen(true);
          }
        }}
      >
        <Globe2 size={16} />
        <span>{languages.find((lang) => lang.value === value)!.label}</span>
        <ChevronDown size={12} />
      </button>
      {open && (
        <div
          className="language-options"
          role="menu"
          aria-label="Language"
          onKeyDown={(event) => {
            if (event.key === 'Escape') {
              event.preventDefault();
              event.stopPropagation();
              setOpen(false);
              trigger.current?.focus();
            } else if (['ArrowDown', 'ArrowUp', 'Home', 'End'].includes(event.key)) {
              event.preventDefault();
              event.stopPropagation();
              const options = Array.from(
                event.currentTarget.querySelectorAll<HTMLButtonElement>('button'),
              );
              const current = options.indexOf(document.activeElement as HTMLButtonElement);
              const index =
                event.key === 'Home'
                  ? 0
                  : event.key === 'End'
                    ? options.length - 1
                    : (current + (event.key === 'ArrowDown' ? 1 : -1) + options.length) %
                      options.length;
              options[index].focus();
            }
          }}
        >
          {languages.map((lang) => (
            <button
              type="button"
              key={lang.value}
              role="menuitemradio"
              aria-checked={lang.value === value}
              tabIndex={-1}
              onClick={() => {
                onChange(lang.value);
                setOpen(false);
                trigger.current?.focus();
              }}
            >
              <span>{lang.label}</span>
              {lang.value === value && <Check size={14} />}
            </button>
          ))}
        </div>
      )}
    </div>
  );
}
