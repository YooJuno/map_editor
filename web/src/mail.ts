export function gmailInviteUrl(
  to: string,
  workspace: string,
  link: string,
  sender: string | null,
  subject: string,
  body: string,
) {
  const url = new URL('https://mail.google.com/mail/');
  url.search = new URLSearchParams({
    view: 'cm',
    fs: '1',
    to: to.trim(),
    su: `${subject} · ${workspace}`,
    body: body.replace(/\{(name|url)\}/g, (_, key) => (key === 'name' ? workspace : link)),
  }).toString();
  if (sender) url.searchParams.set('authuser', sender);
  return url.toString();
}
