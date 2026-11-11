import type { IDisposable } from "./disposable.js";
/**
 * Creates a {@link IDisposable} that defers the disposing to the {@link dispose} function; disposing is guarded so that it may only occur once.
 * @param dispose Function responsible for disposing.
 * @returns Disposable whereby the disposing is delegated to the {@link dispose}  function.
 */
export declare function deferredDisposable(dispose: () => void): IDisposable;
