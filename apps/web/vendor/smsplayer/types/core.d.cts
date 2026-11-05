import type * as Shared from './player.mjs' with { 'resolution-mode': 'import' };

declare namespace core {
    export type CoreConfig = Shared.PlayerOptions & { autoPlay?: boolean } & (
        { container: HTMLElement; divID?: string } | { container?: HTMLElement; divID: string }
    );
    export type PlayerOptions = Shared.PlayerOptions;
    export type PlayerInstance = Shared.PlayerInstance;
    export type PlayerState = Shared.PlayerState;
    export type PlayerStatus = Shared.PlayerStatus;
    export type PlayerEventMap = Shared.PlayerEventMap;
    export const Player: new (config: CoreConfig) => PlayerInstance;
    export function createPlayerInstance(config: CoreConfig): PlayerInstance;
}

export = core;
