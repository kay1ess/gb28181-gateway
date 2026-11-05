#!/usr/bin/env node
const fs = require('node:fs');
const path = require('node:path');

function copyPlayerAssets(destination, source = path.resolve(__dirname, '../dist')) {
    const target = path.resolve(destination);
    const entry = fs.readFileSync(path.join(source, 'smsplayer.core.min.js'), 'utf8');
    const chunks = [...new Set([...entry.matchAll(/\.u\((\d+)\)/g)].map(match => `${match[1]}.smsplayer.min.js`))];
    if (!chunks.length) throw new Error('Build the player before copying assets: npm run build');
    const files = ['smsplayer.core.min.js', 'smsplayer.ui.min.js', 'player.ui.css',
        '265decoder.js', '265decoder.wasm', '265decoder.simd.js', '265decoder.simd.wasm', ...chunks];
    for (const name of files) if (!fs.existsSync(path.join(source, name))) throw new Error(`Missing player asset: ${name}; run the player build first`);
    fs.mkdirSync(target, { recursive: true });
    for (const name of files) fs.copyFileSync(path.join(source, name), path.join(target, name));
    return files;
}

module.exports = { copyPlayerAssets };
if (require.main === module) {
    try {
        const destination = process.argv[2] || 'public/smsplayer';
        const files = copyPlayerAssets(destination);
        console.log(`Copied ${files.length} player assets to ${path.resolve(destination)}`);
    } catch (error) { console.error(error.message); process.exitCode = 1; }
}
