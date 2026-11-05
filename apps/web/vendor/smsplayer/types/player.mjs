import { loadPlayerRuntime } from './runtime.mjs';

export { loadPlayerRuntime };

export async function createPlayer({
    container, autoPlay = false, assetsBase, controls = true, nonce, timeout, uiFactory, ...options
}) {
    if (!container || container.nodeType !== 1) throw new TypeError('A player container is required');
    const { core, ui } = await loadPlayerRuntime({ assetsBase, controls, nonce, timeout });
    const player = core.createPlayerInstance({ ...options, container, autoPlay });
    if (ui) {
        try {
            ui.mountUI(player, uiFactory ?? ui.createUI);
        } catch (error) {
            player.destroy();
            throw error;
        }
    }
    return player;
}
