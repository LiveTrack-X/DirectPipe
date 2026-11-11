import { deferredDisposable } from "./deferred.js";
/**
 * Stack-based container of disposable resources.
 */
export class DisposableStack {
    /**
     * Determines whether the stack is disposed.
     */
    #disposed = false;
    /**
     * Resources managed by this stack.
     */
    #disposables = [];
    /**
     * Gets a value indicating whether the stack has been disposed.
     * @returns value indicating whether the stack has been disposed.
     */
    get disposed() {
        return this.#disposed;
    }
    /**
     * @inheritdoc
     */
    [Symbol.dispose]() {
        this.dispose();
    }
    /**
     * Adds a non-disposable resource and a disposal callback to the top of the stack.
     * @param value Resource to be disposed.
     * @param dispose Callback invoked to dispose the provided value.
     * @returns This instance for chaining.
     */
    adopt(value, dispose) {
        if (this.disposed) {
            throw new Error("Cannot add disposable resource to stack as the stack is already disposed.");
        }
        return this.use(deferredDisposable(() => dispose(value)));
    }
    /**
     * Adds a disposal callback to the top of the stack.
     * @param dispose Function to invoke when this object is disposed.
     * @returns This instance for chaining.
     */
    defer(dispose) {
        if (this.disposed) {
            throw new Error("Cannot add disposable resource to stack as the stack is already disposed.");
        }
        return this.use(deferredDisposable(dispose));
    }
    /**
     * @inheritdoc
     */
    dispose() {
        if (this.disposed) {
            return;
        }
        let disposable;
        while ((disposable = this.#disposables.pop())) {
            disposable.dispose();
        }
        this.#disposed = true;
    }
    /**
     * Adds a resource to the top of the stack.
     * @param value The `Disposable` to add.
     * @returns This instance for chaining.
     */
    use(value) {
        if (this.disposed) {
            throw new Error("Cannot add disposable resource to stack as the stack is already disposed.");
        }
        this.#disposables.push(value);
        return this;
    }
}
