import type { IDisposable } from "./explicit-resource-management/disposable.js";
/**
 * An event emitter that enables the listening for, and emitting of, events.
 */
export declare class EventEmitter<TMap extends EventMap<TMap>> {
    /**
     * Underlying collection of events and their listeners.
     */
    private readonly events;
    /**
     * Adds the event {@link listener} for the event named {@link eventName}.
     * @param eventName Name of the event.
     * @param listener Event handler function.
     * @returns This instance with the {@link listener} added.
     */
    addListener<TEventName extends EventsOf<TMap>, TArgs extends EventArgs<TMap, TEventName>>(eventName: TEventName, listener: (...args: TArgs) => void): this;
    /**
     * Adds the event {@link listener} for the event named {@link eventName}, and returns a disposable capable of removing the event listener.
     * @param eventName Name of the event.
     * @param listener Event handler function.
     * @returns A disposable that removes the listener when disposed.
     */
    disposableOn<TEventName extends EventsOf<TMap>, TArgs extends EventArgs<TMap, TEventName>>(eventName: TEventName, listener: (...args: TArgs) => void): IDisposable;
    /**
     * Emits the {@link eventName}, invoking all event listeners with the specified {@link args}.
     * @param eventName Name of the event.
     * @param args Arguments supplied to each event listener.
     * @returns `true` when there was a listener associated with the event; otherwise `false`.
     */
    emit<TEventName extends EventsOf<TMap>, TArgs extends EventArgs<TMap, TEventName>>(eventName: TEventName, ...args: TArgs): boolean;
    /**
     * Gets the event names with event listeners.
     * @returns Event names.
     */
    eventNames(): EventsOf<TMap>[];
    /**
     * Gets the number of event listeners for the event named {@link eventName}. When a {@link listener} is defined, only matching event listeners are counted.
     * @param eventName Name of the event.
     * @param listener Optional event listener to count.
     * @returns Number of event listeners.
     */
    listenerCount<TEventName extends EventsOf<TMap>, TArgs extends EventArgs<TMap, TEventName>>(eventName: TEventName, listener?: (...args: TArgs) => void): number;
    /**
     * Gets the event listeners for the event named {@link eventName}.
     * @param eventName Name of the event.
     * @returns The event listeners.
     */
    listeners<TEventName extends EventsOf<TMap>, TArgs extends EventArgs<TMap, TEventName>>(eventName: TEventName): ((...args: TArgs) => void)[];
    /**
     * Removes the event {@link listener} for the event named {@link eventName}.
     * @param eventName Name of the event.
     * @param listener Event handler function.
     * @returns This instance with the event {@link listener} removed.
     */
    off<TEventName extends EventsOf<TMap>, TArgs extends EventArgs<TMap, TEventName>>(eventName: TEventName, listener: (...args: TArgs) => void): this;
    /**
     * Adds the event {@link listener} for the event named {@link eventName}.
     * @param eventName Name of the event.
     * @param listener Event handler function.
     * @returns This instance with the event {@link listener} added.
     */
    on<TEventName extends EventsOf<TMap>, TArgs extends EventArgs<TMap, TEventName>>(eventName: TEventName, listener: (...args: TArgs) => void): this;
    /**
     * Adds the **one-time** event {@link listener} for the event named {@link eventName}.
     * @param eventName Name of the event.
     * @param listener Event handler function.
     * @returns This instance with the event {@link listener} added.
     */
    once<TEventName extends EventsOf<TMap>, TArgs extends EventArgs<TMap, TEventName>>(eventName: TEventName, listener: (...args: TArgs) => void): this;
    /**
     * Adds the event {@link listener} to the beginning of the listeners for the event named {@link eventName}.
     * @param eventName Name of the event.
     * @param listener Event handler function.
     * @returns This instance with the event {@link listener} prepended.
     */
    prependListener<TEventName extends EventsOf<TMap>, TArgs extends EventArgs<TMap, TEventName>>(eventName: TEventName, listener: (...args: TArgs) => void): this;
    /**
     * Adds the **one-time** event {@link listener} to the beginning of the listeners for the event named {@link eventName}.
     * @param eventName Name of the event.
     * @param listener Event handler function.
     * @returns This instance with the event {@link listener} prepended.
     */
    prependOnceListener<TEventName extends EventsOf<TMap>, TArgs extends EventArgs<TMap, TEventName>>(eventName: TEventName, listener: (...args: TArgs) => void): this;
    /**
     * Removes all event listeners for the event named {@link eventName}.
     * @param eventName Name of the event.
     * @returns This instance with the event listeners removed
     */
    removeAllListeners<TEventName extends EventsOf<TMap>>(eventName: TEventName): this;
    /**
     * Removes the event {@link listener} for the event named {@link eventName}.
     * @param eventName Name of the event.
     * @param listener Event handler function.
     * @returns This instance with the event {@link listener} removed.
     */
    removeListener<TEventName extends EventsOf<TMap>, TArgs extends EventArgs<TMap, TEventName>>(eventName: TEventName, listener: (...args: TArgs) => void): this;
    /**
     * Adds the event {@link listener} for the event named {@link eventName}.
     * @param eventName Name of the event.
     * @param listener Event handler function.
     * @param fn Function responsible for adding the new event handler function.
     * @returns This instance with event {@link listener} added.
     */
    private add;
    /**
     * Removes the listener at the given index.
     * @param eventName Name of the event.
     * @param listeners Listeners registered with the event.
     * @param index Index of the listener to remove.
     */
    private remove;
}
/**
 * Events that occur within all emitters when listeners change.
 */
type EventEmitterListenerEvent = "newListener" | "removeListener";
/**
 * A map of events and their arguments (represented as an array) that are supplied to the event's listener when the event is emitted.
 * @example
 * type UserService = {
 *     created: [id: number, userName: string];
 *     deleted: [id: number];
 * }
 */
type EventMap<T> = {
    [K in keyof T]: K extends string ? (T[K] extends unknown[] ? T[K] : never) : never;
};
/**
 * Parsed {@link EventMap} whereby each property is a `string` that denotes an event name, and the associated value type defines the listener arguments.
 */
export type EventsOf<TMap extends EventMap<TMap>> = EventEmitterListenerEvent | keyof TMap | (string & {});
/**
 * Parses the event arguments for the specified event from the event map.
 */
export type EventArgs<TMap extends EventMap<TMap>, TEvent extends EventsOf<TMap>> = TEvent extends keyof TMap ? TMap[TEvent] extends unknown[] ? TMap[TEvent] : never : TEvent extends EventEmitterListenerEvent ? EventArgUnion<TMap> : unknown[];
/**
 * Converts an event map to a union of event arguments, allowing the `newListener` and `removeListener`
 * event listeners to be typed.
 */
export type EventArgUnion<TMap extends EventMap<TMap>> = {
    [K in keyof TMap]: TMap[K] extends unknown[] ? [K, (...args: TMap[K]) => void] : never;
}[keyof TMap];
export {};
