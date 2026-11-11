/**
 * Provides a wrapper around a value that is lazily instantiated.
 */
export declare class Lazy<T> {
    #private;
    /**
     * Initializes a new instance of the {@link Lazy} class.
     * @param valueFactory The factory responsible for instantiating the value.
     */
    constructor(valueFactory: () => T);
    /**
     * Gets the value.
     * @returns The value.
     */
    get value(): T;
}
