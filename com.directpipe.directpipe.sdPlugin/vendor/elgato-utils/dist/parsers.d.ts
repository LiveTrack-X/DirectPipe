/**
 * Parses the specified value to a truthy boolean (using {@link https://stackoverflow.com/questions/784929/what-does-the-operator-do-in-javascript `!!` notation}).
 * @param value Value to parse.
 * @returns `true` when the value is truthy; otherwise `false`.
 */
export declare function parseBoolean(value: unknown): boolean | undefined;
/**
 * Parses the specified value to a number (using {@link parseFloat}).
 * @param value Value to parse.
 * @returns The parsed value; otherwise `undefined`.
 */
export declare function parseNumber(value: unknown): number | undefined;
