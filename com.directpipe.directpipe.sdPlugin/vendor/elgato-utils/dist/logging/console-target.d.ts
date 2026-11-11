import type { LogEntry, LogTarget } from "./target.js";
/**
 * Provides a {@link LogTarget} that logs to the console.
 */
export declare class ConsoleTarget implements LogTarget {
    /**
     * @inheritdoc
     */
    write(entry: LogEntry): void;
}
