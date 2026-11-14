const hosts = ['map-editor-juno.web.app', 'map-editor-juno.firebaseapp.com'];

export function authHost(hostname: string) {
  return hosts.includes(hostname) ? hostname : hosts[0];
}

export function mobileLogin(device: Pick<Navigator, 'userAgent' | 'platform' | 'maxTouchPoints'>) {
  return (
    /Android|iPhone|iPad|iPod/i.test(device.userAgent) ||
    (device.platform === 'MacIntel' && device.maxTouchPoints > 1)
  );
}

export async function googleLogin(
  redirect: boolean,
  popup: () => Promise<unknown>,
  navigate: () => Promise<unknown>,
  beforeRedirect: () => Promise<void>,
) {
  if (!redirect) {
    try {
      await popup();
      return true;
    } catch (error) {
      if ((error as { code?: string }).code !== 'auth/popup-blocked') throw error;
    }
  }
  await beforeRedirect();
  await navigate();
  return false;
}
