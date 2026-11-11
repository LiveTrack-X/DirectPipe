import type { IDisposable } from "./disposable.js";
/**
 * Stack-based container of disposable resources.
 */
export declare class DisposableStack implements IDisposable {
    #private;
    /**
     * Gets a value indicating whether the stack has been disposed.
     * @returns value indicating whether the stack has been disposed.
     */
    get disposed(): boolean;
    /**
     * @inheritdoc
     */
    [Symbol.dispose](): void;
    /**
     * Adds a non-disposable resource and a disposal callback to the top of the stack.
     * @param value Resource to be disposed.
     * @param dispose Callback invoked to dispose the provided value.
     * @returns This instance for chaining.
     */
    adopt<T>(value: T, dispose: (value: T) => void): this;
    /**
     * Adds a disposal callback to the top of the stack.
     * @param dispose Function to invoke when this object is disposed.
     * @returns This instance for chaining.
     */
    defer(dispose: () => void): this;
    /**
     * @inheritdoc
     */
    dispose(): void;
    /**
     * Adds a resource to the top of the stack.
     * @param value The `Disposable` to add.
     * @returns This instance for chaining.
     */
    use(value: IDisposable): this;
}
