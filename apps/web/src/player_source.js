// Only the configured media proxy is contacted; endpoint hosts are never used
// as browser fetch targets. This also works when the UI is served over HTTPS.
export function playerSource(playback, base = '/') {
  const endpoint = playback?.endpoints?.find((item) => item.protocol === 'http-flv');
  if (!endpoint) return '';
  try {
    const url = new URL(endpoint.url);
    if (!['http:', 'https:'].includes(url.protocol)) return '';
    if (!/^\/rtp\/[A-Za-z0-9_-]+\.live\.flv$/.test(url.pathname)) return '';
    return `${base.replace(/\/?$/, '/')}media${url.pathname}`;
  } catch {
    return '';
  }
}
