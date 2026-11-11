/**
 * Provides a read-only iterable collection of items that also acts as a partial polyfill for iterator helpers.
 */
export declare class Enumerable<T> implements IterableIterator<T> {
    #private;
    /**
     * Initializes a new instance of the {@link Enumerable} class.
     * @param source Source that contains the items.
     * @returns The enumerable.
     */
    constructor(source: Enumerable<T> | Map<unknown, T> | Set<T> | T[] | (() => IterableIterator<T>));
    /**
     * Gets the number of items in the enumerable.
     * @returns The number of items.
     */
    get length(): number;
    /**
     * Gets the iterator for the enumerable.
     * @yields The items.
     */
    [Symbol.iterator](): IterableIterator<T>;
    /**
     * Transforms each item within this iterator to an indexed pair, with each pair represented as an array.
     * @returns An iterator of indexed pairs.
     */
    asIndexedPairs(): Enumerable<[number, T]>;
    /**
     * Returns an iterator with the first items dropped, up to the specified limit.
     * @param limit The number of elements to drop from the start of the iteration.
     * @returns An iterator of items after the limit.
     */
    drop(limit: number): Enumerable<T>;
    /**
     * Determines whether all items satisfy the specified predicate.
     * @param predicate Function that determines whether each item fulfils the predicate.
     * @returns `true` when all items satisfy the predicate; otherwise `false`.
     */
    every(predicate: (value: T) => boolean): boolean;
    /**
     * Returns an iterator of items that meet the specified predicate..
     * @param predicate Function that determines which items to filter.
     * @returns An iterator of filtered items.
     */
    filter(predicate: (value: T) => boolean): Enumerable<T>;
    /**
     * Finds the first item that satisfies the specified predicate.
     * @param predicate Predicate to match items against.
     * @returns The first item that satisfied the predicate; otherwise `undefined`.
     */
    find(predicate: (value: T) => boolean): T | undefined;
    /**
     * Finds the last item that satisfies the specified predicate.
     * @param predicate Predicate to match items against.
     * @returns The first item that satisfied the predicate; otherwise `undefined`.
     */
    findLast(predicate: (value: T) => boolean): T | undefined;
    /**
     * Returns an iterator containing items transformed using the specified mapper function.
     * @param mapper Function responsible for transforming each item.
     * @returns An iterator of transformed items.
     */
    flatMap<U>(mapper: (item: T) => IterableIterator<U>): Enumerable<U>;
    /**
     * Iterates over each item, and invokes the specified function.
     * @param fn Function to invoke against each item.
     */
    forEach(fn: (item: T) => void): void;
    /**
     * Determines whether the search item exists in the collection exists.
     * @param search Item to search for.
     * @returns `true` when the item was found; otherwise `false`.
     */
    includes(search: T): boolean;
    /**
     * Returns an iterator of mapped items using the mapper function.
     * @param mapper Function responsible for mapping the items.
     * @returns An iterator of mapped items.
     */
    map<U>(mapper: (value: T) => U): Enumerable<U>;
    /**
     * Captures the underlying iterable, if it is not already captured, and gets the next item in the iterator.
     * @param args Optional values to send to the generator.
     * @returns An iterator result of the current iteration; when `done` is `false`, the current `value` is provided.
     */
    next(...args: [] | [undefined]): IteratorResult<T, T>;
    /**
     * Applies the accumulator function to each item, and returns the result.
     * @param accumulator Function responsible for accumulating all items within the collection.
     * @returns Result of accumulating each value.
     */
    reduce(accumulator: (previous: T, current: T) => T): T;
    /**
     * Applies the accumulator function to each item, and returns the result.
     * @param accumulator Function responsible for accumulating all items within the collection.
     * @param initial Initial value supplied to the accumulator.
     * @returns Result of accumulating each value.
     */
    reduce<R>(accumulator: (previous: R, current: T) => R, initial: R): R;
    /**
     * Acts as if a `return` statement is inserted in the generator's body at the current suspended position.
     *
     * Please note, in the context of an {@link Enumerable}, calling {@link Enumerable.return} will clear the captured iterator,
     * if there is one. Subsequent calls to {@link Enumerable.next} will result in re-capturing the underlying iterable, and
     * yielding items from the beginning.
     * @param value Value to return.
     * @returns The value as an iterator result.
     */
    return?<TReturn>(value?: TReturn): IteratorResult<T, TReturn | undefined>;
    /**
     * Determines whether an item in the collection exists that satisfies the specified predicate.
     * @param predicate Function used to search for an item.
     * @returns `true` when the item was found; otherwise `false`.
     */
    some(predicate: (value: T) => boolean): boolean;
    /**
     * Returns an iterator with the items, from 0, up to the specified limit.
     * @param limit Limit of items to take.
     * @returns An iterator of items from 0 to the limit.
     */
    take(limit: number): Enumerable<T>;
    /**
     * Acts as if a `throw` statement is inserted in the generator's body at the current suspended position.
     * @param e Error to throw.
     */
    throw?<TReturn>(e?: TReturn): IteratorResult<T, TReturn | undefined>;
    /**
     * Converts this iterator to an array.
     * @returns The array of items from this iterator.
     */
    toArray(): T[];
    /**
     * Converts this iterator to serializable collection.
     * @returns The serializable collection of items.
     */
    toJSON(): T[];
    /**
     * Converts this iterator to a string.
     * @returns The string.
     */
    toString(): string;
}
