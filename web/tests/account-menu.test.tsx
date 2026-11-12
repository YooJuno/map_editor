// @vitest-environment jsdom
import { beforeEach, describe, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import type { User } from 'firebase/auth';
import { AccountMenu } from '../src/AccountMenu';
import { translator } from '../src/i18n';
const user = { displayName: 'Juno', email: 'test@example.com', photoURL: null } as User;
function setup() {
  const onTheme = vi.fn(),
    onLogout = vi.fn();
  render(
    <AccountMenu
      user={user}
      lang="ko"
      theme="dark"
      t={translator('ko')}
      busy={false}
      onLang={vi.fn()}
      onTheme={onTheme}
      onLogin={vi.fn()}
      onLogout={onLogout}
    />,
  );
  return { onTheme, onLogout };
}
beforeEach(cleanup);
describe('account settings popover', () => {
  it('keeps language, theme and logout inside the avatar menu', () => {
    setup();
    expect(screen.queryByRole('button', { name: '로그아웃' })).toBeNull();
    fireEvent.click(screen.getByRole('button', { name: '계정' }));
    expect(screen.getByRole('button', { name: '한국어' })).toBeTruthy();
    expect(screen.getByRole('button', { name: '다크' })).toBeTruthy();
    expect(screen.getByRole('button', { name: '로그아웃' })).toBeTruthy();
  });
  it('changes theme without signing out', () => {
    const { onTheme, onLogout } = setup();
    fireEvent.click(screen.getByRole('button', { name: '계정' }));
    fireEvent.click(screen.getByRole('button', { name: '라이트' }));
    expect(onTheme).toHaveBeenCalledWith('light');
    expect(onLogout).not.toHaveBeenCalled();
  });
  it('closes on Escape and outside click', () => {
    setup();
    const trigger = screen.getByRole('button', { name: '계정' });
    fireEvent.click(trigger);
    fireEvent.keyDown(screen.getByRole('dialog'), { key: 'Escape' });
    expect(screen.queryByRole('dialog')).toBeNull();
    fireEvent.click(trigger);
    fireEvent.pointerDown(document.body);
    expect(screen.queryByRole('dialog')).toBeNull();
  });
});
