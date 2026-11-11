// @vitest-environment jsdom
import { beforeEach, describe, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { LanguageMenu } from '../src/LanguageMenu';
beforeEach(cleanup);
describe('theme-aware language menu', () => {
  it('shows every language and the current selection', () => {
    render(<LanguageMenu value="ko" onChange={vi.fn()} />);
    fireEvent.click(screen.getByRole('button', { name: '한국어' }));
    expect(screen.getAllByRole('menuitemradio')).toHaveLength(4);
    expect(screen.getByRole('menuitemradio', { name: '한국어' }).getAttribute('aria-checked')).toBe(
      'true',
    );
  });
  it('switches language and closes the menu', () => {
    const change = vi.fn();
    render(<LanguageMenu value="ko" onChange={change} />);
    fireEvent.click(screen.getByRole('button', { name: '한국어' }));
    fireEvent.click(screen.getByRole('menuitemradio', { name: 'English' }));
    expect(change).toHaveBeenCalledWith('en');
    expect(screen.queryByRole('menu')).toBeNull();
  });
  it('supports keyboard navigation and Escape without changing the language', () => {
    const change = vi.fn();
    render(<LanguageMenu value="ko" onChange={change} />);
    const trigger = screen.getByRole('button', { name: '한국어' });
    fireEvent.keyDown(trigger, { key: 'ArrowDown' });
    fireEvent.keyDown(screen.getByRole('menu'), { key: 'ArrowDown' });
    expect(document.activeElement).toBe(screen.getByRole('menuitemradio', { name: 'English' }));
    fireEvent.keyDown(screen.getByRole('menu'), { key: 'Escape' });
    expect(screen.queryByRole('menu')).toBeNull();
    expect(document.activeElement).toBe(trigger);
    expect(change).not.toHaveBeenCalled();
  });
  it('closes when clicking outside', () => {
    render(<LanguageMenu value="ko" onChange={vi.fn()} />);
    fireEvent.click(screen.getByRole('button', { name: '한국어' }));
    fireEvent.pointerDown(document.body);
    expect(screen.queryByRole('menu')).toBeNull();
  });
});
