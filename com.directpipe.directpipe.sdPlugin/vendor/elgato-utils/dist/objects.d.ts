/**
 * Prevents the modification of existing property attributes and values on the value, and all of its child properties, and prevents the addition of new properties.
 * @param value Value to freeze.
 */
export declare function freeze<T>(value: T): void;
/**
 * Gets the value at the specified {@link path}.
 * @param source Source object that is being read from.
 * @param path Path to the property to get.
 * @returns Value of the property.
 */
export declare function get(source: unknown, path: string): unknown;
/**
 * Sets the specified `value` on the `target` object at the desired property `path`.
 * @param target The target object that is being written to.
 * @param path The path to the property to set.
 * @param value The value to write.
 */
export declare function set(target: any, path: string, value: unknown): void;
