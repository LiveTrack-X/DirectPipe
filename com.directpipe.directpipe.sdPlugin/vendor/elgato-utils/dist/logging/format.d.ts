import type { LogEntry } from "./target.js";
/**
 * Formats the specified entry.
 * @param entry Log entry to format.
 * @returns The formatted log entry as a `string`.
 */
export type LogEntryFormatter = (entry: LogEntry) => string;
/**
 * Creates a new string log entry formatter.
 * @param opts Options that defines the type for the formatter.
 * @returns The string {@link LogEntryFormatter}.
 */
export declare function stringFormatter(opts?: StringFormatOptions): LogEntryFormatter;
/**
 * Options associated with {@link stringFormatter}.
 */
type StringFormatOptions = {
    /**
     * Indicates whether the entry should only log the {@link LogEntry.data}.
     */
    dataOnly?: boolean;
};
export {};
