import type * as Shared from './player.mjs' with { 'resolution-mode': 'import' };
import type core from './core.cjs';

declare namespace smsplayer {
    export type PlayerConfig = core.CoreConfig & { controller?: boolean; ui?: UIFactory };
    export type PlayerOptions = Shared.PlayerOptions;
    export type PlayerInstance = Shared.PlayerInstance;
    export type PlayerState = Shared.PlayerState;
    export type PlayerStatus = Shared.PlayerStatus;
    export type PlayerEventMap = Shared.PlayerEventMap;
    export type PlayerUI = Shared.PlayerUI;
    export type UIFactory = Shared.UIFactory;

    export function createPlayerInstance(config: PlayerConfig): PlayerInstance;
    export function createDefaultUI(context: { container: HTMLElement; state: PlayerState }): PlayerUI;
    export function mountUI(player: PlayerInstance, factory: UIFactory, options?: { progressInterval?: number }): { ui: PlayerUI; destroy(): void };
}

export = smsplayer;
