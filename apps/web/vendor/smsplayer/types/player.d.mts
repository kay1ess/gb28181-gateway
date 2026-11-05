export type PlayerStatus = 'idle' | 'loading' | 'ready' | 'playing' | 'paused' | 'waiting' | 'blocked' | 'error' | 'ended' | 'destroyed';
export interface PlayerState {
    readonly status: PlayerStatus;
    readonly source: string | null;
    readonly generation: number;
    readonly currentTime: number;
    readonly duration: number;
    readonly bufferedDuration: number;
    readonly volume: number;
    readonly playbackRate: number;
    readonly mediaInfo: Readonly<Record<string, unknown>> | null;
    readonly error: Readonly<{ message: string }> | null;
    readonly version: string;
    readonly capabilities: Readonly<{ audio: boolean; seek: boolean; snapshot: boolean }>;
}

export interface PlayerOptions {
    cacheduration?: number;
    downloadBuffer?: number;
    loglevel?: 'debug' | 'info' | 'warn' | 'error';
    maxQueuedBytes?: number;
    maxDecodedBytes?: number;
    wasmSIMD?: boolean;
    hevcRenderer?: 'canvas2d' | 'webgl' | 'offscreen';
    discardLateFrames?: boolean;
}

export interface PlayerEventMap {
    statechange: CustomEvent<PlayerState>;
    mediainfo: CustomEvent<Readonly<Record<string, unknown>>>;
    timeupdate: CustomEvent<number>;
    durationchange: CustomEvent<number>;
    cacheduration: CustomEvent<number>;
    volumechange: CustomEvent<number>;
    ratechange: CustomEvent<number>;
    error: ErrorEvent;
    loadstart: CustomEvent<null>;
    ready: CustomEvent<null>;
    play: CustomEvent<null>;
    pause: CustomEvent<null>;
    playing: CustomEvent<null>;
    waiting: CustomEvent<null>;
    autoplayblocked: CustomEvent<null>;
    ended: CustomEvent<null>;
    stop: CustomEvent<null>;
    destroy: CustomEvent<null>;
}

export interface PlayerInstance extends EventTarget {
    addEventListener<K extends keyof PlayerEventMap>(type: K, listener: (this: PlayerInstance, event: PlayerEventMap[K]) => void, options?: boolean | AddEventListenerOptions): void;
    addEventListener(type: string, listener: EventListenerOrEventListenerObject | null, options?: boolean | AddEventListenerOptions): void;
    removeEventListener<K extends keyof PlayerEventMap>(type: K, listener: (this: PlayerInstance, event: PlayerEventMap[K]) => void, options?: boolean | EventListenerOptions): void;
    removeEventListener(type: string, listener: EventListenerOrEventListenerObject | null, options?: boolean | EventListenerOptions): void;

    readonly container: HTMLElement;
    readonly state: PlayerState;
    readonly status: PlayerStatus;
    readonly currentTime: number;
    readonly duration: number;
    readonly version: string;
    volume: number;
    playbackRate: number;
    load(source: string): void;
    play(): void;
    pause(): void;
    seek(seconds: number): void;
    reload(): void;
    stop(): void;
    destroy(): void;
    snapshot(): void;
    debug(): void;
}

export interface PlayerUI extends EventTarget {
    render(state: PlayerState): void;
    destroy(): void;
}
export type UIFactory = (context: { container: HTMLElement; state: PlayerState }) => PlayerUI;
export interface PlayerRuntime {
    core: { createPlayerInstance(options: PlayerOptions & { container: HTMLElement; autoPlay: boolean }): PlayerInstance };
    ui?: {
        createUI: UIFactory;
        mountUI(player: PlayerInstance, factory: UIFactory): { ui: PlayerUI; destroy(): void };
    } | null;
}
export interface RuntimeOptions {
    assetsBase?: string;
    controls?: boolean;
    nonce?: string;
    timeout?: number;
}
export interface CreatePlayerOptions extends PlayerOptions, RuntimeOptions {
    container: HTMLElement;
    autoPlay?: boolean;
    uiFactory?: UIFactory;
}

export declare function createPlayer(options: CreatePlayerOptions): Promise<PlayerInstance>;
export declare function loadPlayerRuntime(options?: RuntimeOptions): Promise<PlayerRuntime>;
