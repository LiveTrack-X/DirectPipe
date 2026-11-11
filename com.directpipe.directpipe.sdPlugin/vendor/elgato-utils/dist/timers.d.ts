/**
 * Creates a wrapper around the specified `fn`, debouncing calls within the `delay`, to prevent multiple calls.
 * @param fn Function to debounce.
 * @param delay Delay before invoking the function.
 * @returns Function that debounces calls.
 */
export declare function debounce<T extends unknown[]>(fn: (...args: T) => Promise<void> | void, delay: number): (...args: T) => Promise<void>;
