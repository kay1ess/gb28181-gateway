// Cache shared assets per document, so multiple players share one download.
// No DOM is accessed during module evaluation (SSR-safe).
const documents = new WeakMap();

function registry(document) {
    if (!documents.has(document)) documents.set(document, new Map());
    return documents.get(document);
}

function resource(document, url, kind, name, { nonce, timeout = 15000 } = {}) {
    const cache = registry(document), key = `${kind}:${url}`;
    if (cache.has(key)) return cache.get(key);
    const promise = new Promise((resolve, reject) => {
        const element = document.createElement(kind === 'script' ? 'script' : 'link');
        let timer, settled = false;
        const finish = error => {
            if (settled) return;
            settled = true;
            clearTimeout(timer);
            element.onload = element.onerror = null;
            if (error) { element.remove(); reject(error); }
            else resolve(kind === 'script' ? document.defaultView[name] : undefined);
        };
        if (nonce) element.setAttribute('nonce', nonce);
        if (kind === 'script') { element.async = true; element.src = url; }
        else { element.rel = 'stylesheet'; element.href = url; }
        element.onload = () => {
            const api = document.defaultView[name];
            if (kind === 'script' && !(name === 'playerjsCore' ? api?.createPlayerInstance : api?.mountUI && api?.createUI)) {
                finish(new Error(`Invalid smsplayer asset: ${url}`));
            } else finish();
        };
        element.onerror = () => finish(new Error(`Unable to load smsplayer asset: ${url}`));
        timer = setTimeout(() => finish(new Error(`Timed out loading smsplayer asset: ${url}`)), timeout);
        try { document.head.appendChild(element); }
        catch (error) { finish(error); }
    });
    cache.set(key, promise);
    promise.catch(() => { if (cache.get(key) === promise) cache.delete(key); });
    return promise;
}

export async function loadPlayerRuntime({ assetsBase = '/smsplayer/', controls = true, nonce, timeout = 15000 } = {}) {
    if (typeof document === 'undefined') throw new Error('Load the player runtime in a browser after mounting');
    const base = new URL(assetsBase || '/smsplayer/', document.baseURI);
    if (!['http:', 'https:'].includes(base.protocol)) throw new Error('Player assets must be served over HTTP(S)');
    if (!base.pathname.endsWith('/')) base.pathname += '/';
    const options = { nonce, timeout };
    const corePromise = resource(document, new URL('smsplayer.core.min.js', base).href, 'script', 'playerjsCore', options);
    const uiPromise = controls ? Promise.all([
        resource(document, new URL('smsplayer.ui.min.js', base).href, 'script', 'playerjsUI', options),
        resource(document, new URL('player.ui.css', base).href, 'style', '', options)
    ]).then(([ui]) => ui) : Promise.resolve(null);
    const [core, ui] = await Promise.all([corePromise, uiPromise]);
    return { core, ui };
}
