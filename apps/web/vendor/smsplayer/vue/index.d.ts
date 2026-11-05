import type { App, ComponentOptionsMixin, DefineComponent } from 'vue';

import type { PlayerStatus, PlayerState, PlayerOptions, PlayerInstance, PlayerUI, UIFactory, PlayerRuntime, RuntimeOptions } from '../types/player.mjs';
export type { PlayerStatus, PlayerState, PlayerOptions, PlayerInstance, PlayerUI, UIFactory, PlayerRuntime, RuntimeOptions } from '../types/player.mjs';

export interface SmsPlayerError {
    message: string;
    error: unknown;
    phase: 'runtime' | 'load' | 'ui' | 'playback' | 'command';
}
export interface SmsPlayerProps {
    src?: string;
    autoplay?: boolean;
    controls?: boolean;
    loop?: boolean;
    muted?: boolean;
    volume?: number;
    playbackRate?: number;
    width?: number | string;
    height?: number | string;
    assetsBase?: string;
    nonce?: string;
    options?: PlayerOptions;
    runtime?: PlayerRuntime | null;
    uiFactory?: UIFactory;
    progressInterval?: number;
}
export interface SmsPlayerExpose {
    play(): void;
    pause(): void;
    seek(seconds: number): void;
    load(source: string): void;
    reload(): void;
    stop(): void;
    setVolume(volume: number): void;
    setPlaybackRate(rate: number): void;
    snapshot(): void;
    debug(): void;
    getPlayer(): PlayerInstance | null;
    getState(): PlayerState | null;
    readonly state: PlayerState | null;
    readonly error: SmsPlayerError | null;
}
export interface SmsPlayerSlotScope extends Omit<SmsPlayerExpose, 'state' | 'error'> {
    state: PlayerState | null;
    error: SmsPlayerError | null;
    player: PlayerInstance | null;
}
export type SmsPlayerEmits = {
    created: (player: PlayerInstance) => void;
    destroy: () => void;
    error: (error: SmsPlayerError) => void;
    statechange: (state: PlayerState) => void;
    timeupdate: (seconds: number) => void;
    loadstart: () => void;
    ready: () => void;
    play: () => void;
    pause: () => void;
    waiting: () => void;
    playing: () => void;
    autoplayblocked: () => void;
    ended: () => void;
    stop: () => void;
    mediainfo: (info: Readonly<Record<string, unknown>>) => void;
    durationchange: (seconds: number) => void;
    cacheduration: (seconds: number) => void;
    volumechange: (volume: number) => void;
    ratechange: (rate: number) => void;
    'update:src': (source: string) => void;
    'update:volume': (volume: number) => void;
    'update:muted': (muted: boolean) => void;
    'update:playbackRate': (rate: number) => void;
};

export declare const SmsPlayer: DefineComponent<SmsPlayerProps, SmsPlayerExpose, {}, {}, {}, ComponentOptionsMixin, ComponentOptionsMixin, SmsPlayerEmits> & {
    install(app: App): void;
};
export declare function loadPlayerRuntime(options?: RuntimeOptions): Promise<PlayerRuntime>;
export default SmsPlayer;
