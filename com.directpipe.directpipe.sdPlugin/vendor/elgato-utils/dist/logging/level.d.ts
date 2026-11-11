/**
 * Levels of logging.
 */
export type LogLevel = 
/**
 * Error message used to indicate an error was thrown, or something critically went wrong.
 */
"error"
/**
 * Warning message used to indicate something went wrong, but the application is able to recover.
 */
 | "warn"
/**
 * Information message for general usage.
 */
 | "info"
/**
 * Debug message used to detail information useful for profiling the applications runtime.
 */
 | "debug"
/**
 * Trace message used to monitor low-level information such as method calls, performance tracking, etc.
 */
 | "trace";
/**
 * Gets the priority of the specified log level as a number; low numbers signify a higher priority.
 * @param level Log level.
 * @returns The priority as a number.
 */
export declare function defcon(level: LogLevel): number;
