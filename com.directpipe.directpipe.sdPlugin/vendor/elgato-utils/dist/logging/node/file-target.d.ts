import type { LogEntryFormatter } from "../format.js";
import type { LogEntry, LogTarget } from "../target.js";
/**
 * Provides a {@link LogTarget} capable of logging to a local file system.
 */
export declare class FileTarget implements LogTarget {
    #private;
    /**
     * Initializes a new instance of the {@link FileTarget} class.
     * @param options Options that defines how logs should be written to the local file system.
     */
    constructor(options: FileTargetOptions);
    /**
     * @inheritdoc
     */
    write(entry: LogEntry): void;
    /**
     * Gets the file path to an indexed log file.
     * @param index Optional index of the log file to be included as part of the file name.
     * @returns File path that represents the indexed log file.
     */
    private getLogFilePath;
    /**
     * Gets the log files associated with this file target, including past and present.
     * @returns Log file entries.
     */
    private getLogFiles;
    /**
     * Re-indexes the existing log files associated with this file target, removing old log files whose index exceeds the {@link FileTargetOptions.maxFileCount}, and renaming the
     * remaining log files, leaving index "0" free for a new log file.
     */
    private reIndex;
}
/**
 * Options that determine the behavior of a {@link FileTarget}.
 */
export type FileTargetOptions = {
    /**
     * Destination folder where log files will be written.
     */
    dest: string;
    /**
     * Name of the log file. The filename extension will be `.log`, and the file will be indexed, e.g. when {@link FileTargetOptions.fileName} is `com.elgato.test`, the resulting output
     * filename will be `com.elgato.test.1.log`.
     */
    fileName: string;
    /**
     * Formatter responsible for formatting the log entry.
     */
    format: LogEntryFormatter;
    /**
     * Maximum number of files that can be created as part of the target before old logs should be truncated and removed.
     */
    maxFileCount: number;
    /**
     * Maximum size of a log file; when exceeded, file rotation occurs and older log files are removed based on {@link FileTargetOptions.maxFileCount}.
     */
    maxSize: number;
};
