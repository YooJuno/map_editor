import { useEffect, useRef, useState } from 'react';
import { LogOut, Moon, Sun, UserRound } from 'lucide-react';
import type { User } from 'firebase/auth';
import type { Lang, Translate } from './i18n';
import { LanguageMenu } from './LanguageMenu';
type Props = {
  user: User | null;
  lang: Lang;
  theme: string;
  t: Translate;
  busy: boolean;
  onLang: (lang: Lang) => void;
  onTheme: (theme: 'dark' | 'light') => void;
  onLogin: () => void;
  onLogout: () => void;
};
export function AccountMenu({
  user,
  lang,
  theme,
  t,
  busy,
  onLang,
  onTheme,
  onLogin,
  onLogout,
}: Props) {
  const [open, setOpen] = useState(false),
    root = useRef<HTMLDivElement>(null),
    trigger = useRef<HTMLButtonElement>(null);
  useEffect(() => {
    if (!open) return;
    const outside = (event: PointerEvent) => {
      if (!root.current?.contains(event.target as Node)) setOpen(false);
    };
    document.addEventListener('pointerdown', outside);
    return () => document.removeEventListener('pointerdown', outside);
  }, [open]);
  return (
    <div
      className="account-menu"
      ref={root}
      onBlur={(event) => {
        if (!event.currentTarget.contains(event.relatedTarget)) setOpen(false);
      }}
      onKeyDown={(event) => {
        if (event.key === 'Escape' && open) {
          event.preventDefault();
          event.stopPropagation();
          setOpen(false);
          trigger.current?.focus();
        }
      }}
    >
      <button
        className="account-trigger"
        type="button"
        ref={trigger}
        title={t('account')}
        aria-label={t('account')}
        aria-haspopup="dialog"
        aria-expanded={open}
        onClick={() => setOpen(!open)}
      >
        <span className="account-avatar">
          {user?.photoURL ? (
            <img src={user.photoURL} referrerPolicy="no-referrer" alt="" />
          ) : (
            <UserRound size={19} />
          )}
        </span>
      </button>
      {open && (
        <div className="account-popover" role="dialog" aria-label={t('account')}>
          {user && (
            <div className="account-profile">
              <strong>{user.displayName || user.email}</strong>
              <span>{user.email}</span>
            </div>
          )}
          <LanguageMenu value={lang} onChange={onLang} />
          <div className="account-theme">
            <span>{t('theme')}</span>
            <div role="group" aria-label={t('theme')}>
              <button
                type="button"
                aria-label={t('lightTheme')}
                title={t('lightTheme')}
                aria-pressed={theme === 'light'}
                onClick={() => onTheme('light')}
              >
                <Sun size={16} />
              </button>
              <button
                type="button"
                aria-label={t('darkTheme')}
                title={t('darkTheme')}
                aria-pressed={theme === 'dark'}
                onClick={() => onTheme('dark')}
              >
                <Moon size={16} />
              </button>
            </div>
          </div>
          <div className="account-separator" />
          <button
            className="account-action"
            type="button"
            disabled={busy}
            onClick={() => {
              setOpen(false);
              user ? onLogout() : onLogin();
            }}
          >
            {user ? <LogOut size={16} /> : <UserRound size={16} />}
            <span>{t(user ? 'logout' : 'login')}</span>
          </button>
        </div>
      )}
    </div>
  );
}
