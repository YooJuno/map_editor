import { useEffect, useRef, useState, type ReactNode } from 'react';

export function Popover({
  label,
  trigger,
  children,
  className = '',
  disabled,
  closeOnAction = true,
  tour,
}: {
  label: string;
  trigger: ReactNode;
  children: ReactNode;
  className?: string;
  disabled?: boolean;
  closeOnAction?: boolean;
  tour?: string;
}) {
  const [open, setOpen] = useState(false),
    root = useRef<HTMLDivElement>(null),
    button = useRef<HTMLButtonElement>(null);
  useEffect(() => {
    if (!open) return;
    const outside = (e: PointerEvent) => {
      if (!root.current?.contains(e.target as Node)) setOpen(false);
    };
    const escape = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        e.preventDefault();
        e.stopPropagation();
        setOpen(false);
        button.current?.focus();
      }
    };
    document.addEventListener('pointerdown', outside);
    document.addEventListener('keydown', escape);
    return () => {
      document.removeEventListener('pointerdown', outside);
      document.removeEventListener('keydown', escape);
    };
  }, [open]);
  return (
    <div
      className={`popover ${className}`}
      ref={root}
      onBlur={(e) => {
        if (!e.currentTarget.contains(e.relatedTarget)) setOpen(false);
      }}
    >
      <button
        ref={button}
        type="button"
        className="popover-trigger"
        aria-label={label}
        title={label}
        aria-expanded={open}
        aria-haspopup="dialog"
        data-tour={tour}
        disabled={disabled}
        onClick={() => setOpen(!open)}
      >
        {trigger}
      </button>
      {open && (
        <div
          className="popover-content"
          role="dialog"
          aria-label={label}
          onClick={(e) => {
            if (closeOnAction && (e.target as Element).closest('button')) setOpen(false);
          }}
        >
          {children}
        </div>
      )}
    </div>
  );
}
