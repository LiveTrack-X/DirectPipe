/**
 * An object that contains a promise, and its resolve and reject functions.
 */
export type PromiseResolvers<T> = {
    /**
     * The promise.
     */
    promise: Promise<T>;
    /**
     * Resolves the promise.
     * @param value Resolved value.
     */
    resolve(value: PromiseLike<T> | T): void;
    /**
     * Rejects the promise.
     * @param reason Rejection reason.
     */
    reject(reason?: unknown): void;
};
/**
 * Returns an object that contains a promise and two functions to resolve or reject it.
 * @returns The promise, and the resolve and reject functions.
 */
export declare function withResolvers<T = void>(): PromiseResolvers<T>;
