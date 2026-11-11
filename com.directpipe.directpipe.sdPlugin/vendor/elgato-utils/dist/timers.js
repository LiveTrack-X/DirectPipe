import { withResolvers } from "./promises.js";
/**
 * Creates a wrapper around the specified `fn`, debouncing calls within the `delay`, to prevent multiple calls.
 * @param fn Function to debounce.
 * @param delay Delay before invoking the function.
 * @returns Function that debounces calls.
 */
export function debounce(fn, delay) {
    let handle;
    let resolvers;
    return (...args) => {
        clearTimeout(handle);
        resolvers ??= withResolvers();
        handle = setTimeout(async (args) => {
            const capturedResolvers = resolvers;
            resolvers = undefined;
            await fn(...args);
            capturedResolvers?.resolve();
        }, delay, args);
        return resolvers.promise;
    };
}
