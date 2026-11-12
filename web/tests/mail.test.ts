import { describe, expect, it } from 'vitest';
import { gmailInviteUrl } from '../src/mail';
describe('free Gmail invitations', () => {
  it('opens a compose URL with the recipient, sender and workspace link encoded', () => {
    const url = new URL(
      gmailInviteUrl(
        ' member@example.com ',
        'Office & Lab',
        'https://map-editor-juno.web.app/?workspace=abc',
        'admin@gmail.com',
        'Invitation',
        'Join {name}\n{url}',
      ),
    );
    expect(url.origin).toBe('https://mail.google.com');
    expect(url.searchParams.get('view')).toBe('cm');
    expect(url.searchParams.get('to')).toBe('member@example.com');
    expect(url.searchParams.get('authuser')).toBe('admin@gmail.com');
    expect(url.searchParams.get('body')).toBe(
      'Join Office & Lab\nhttps://map-editor-juno.web.app/?workspace=abc',
    );
  });
  it('keeps template-like workspace names and URL characters as plain content', () => {
    const url = new URL(
      gmailInviteUrl(
        'user@example.com',
        '{url} & bcc=other@example.com',
        'https://map-editor-juno.web.app/?workspace=real',
        null,
        'Invite',
        '{name}\n{url}',
      ),
    );
    expect(url.searchParams.get('bcc')).toBeNull();
    expect(url.searchParams.get('body')).toBe(
      '{url} & bcc=other@example.com\nhttps://map-editor-juno.web.app/?workspace=real',
    );
    expect(url.searchParams.has('authuser')).toBe(false);
  });
});
