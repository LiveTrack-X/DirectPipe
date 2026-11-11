import { type LogLevel } from "./level.js";
import type { LogEntry, LogEntryData, LogTarget } from "./target.js";
/**
 * Logger capable of forwarding messages to a {@link LogTarget}.
 */
export declare class Logger {
    #private;
    /**
     * Initializes a new instance of the {@link Logger} class.
     * @param opts Options that define the loggers behavior.
     */
    constructor(opts: LoggerOptions);
    /**
     * Gets the {@link LogLevel}.
     * @returns The {@link LogLevel}.
     */
    get level(): LogLevel;
    /**
     * Creates a scoped logger with the given {@link scope}; logs created by scoped-loggers include their scope to enable their source to be easily identified.
     * @param scope Value that represents the scope of the new logger.
     * @returns The scoped logger, or this instance when {@link scope} is not defined.
     */
    createScope(scope: string): Logger | this;
    /**
     * Writes the arguments as a debug log entry.
     * @param data Message or data to log.
     * @returns This instance for chaining.
     */
    debug(...data: LogEntryData): this;
    /**
     * Writes the arguments as error log entry.
     * @param data Message or data to log.
     * @returns This instance for chaining.
     */
    error(...data: LogEntryData): this;
    /**
     * Writes the arguments as an info log entry.
     * @param data Message or data to log.
     * @returns This instance for chaining.
     */
    info(...data: LogEntryData): this;
    /**
     * Sets the log-level that determines which logs should be written. The specified level will be inherited by all scoped loggers unless they have log-level explicitly defined.
     * @param level The log-level that determines which logs should be written; when `undefined`, the level will be inherited from the parent logger, or default to the environment level.
     * @returns This instance for chaining.
     */
    setLevel(level?: LogLevel): this;
    /**
     * Writes the arguments as a trace log entry.
     * @param data Message or data to log.
     * @returns This instance for chaining.
     */
    trace(...data: LogEntryData): this;
    /**
     * Writes the arguments as a warning log entry.
     * @param data Message or data to log.
     * @returns This instance for chaining.
     */
    warn(...data: LogEntryData): this;
    /**
     * Writes the log entry.
     * @param entry Log entry to write.
     * @returns This instance for chaining.
     */
    write(entry: LogEntry): this;
}
/**
 * Options that define the logger's behavior.
 */
export type LoggerOptions = {
    /**
     * Determines the minimum level of logs that can be written.
     */
    level: LogLevel | (() => LogLevel);
    /**
     * Minimum level the logger can be set to.
     */
    minimumLevel?: LogLevel;
    /**
     * Optional value that defines the scope of the logger.
     */
    scope?: string;
    /**
     * Log targets where logs will be written to.
     */
    targets: LogTarget[];
};
