/**
 * Formats the specified string, replacing placeholders with their associated arguments.
 * @example Formatting
 * format("Hello {0}", "world") // "Hello world"
 * @example Escaping braces
 * format("Escaped {{0}}") // "Escaped {0}"
 * @param format Source format string.
 * @param args Arguments used to replace placeholders.
 * @returns The formatted string.
 */
export function format(format, ...args) {
    return (format
        // Transform escaped braces, e.g. {{0}}, {{1}}, etc.
        .replace(/{{(\d+)}}/g, (match, index) => {
        return `\uE000${index}\uE001`;
    })
        // Replace placeholders, e.g. {0}, {1}, etc.
        .replace(/{(\d+)}/g, (match, index) => {
        const value = args.at(Number(index));
        return value === undefined ? match : `${value}`;
    })
        // Restore escaped braces
        .replace(/\uE000(\d+)\uE001/g, (match, index) => {
        return `{${index}}`;
    }));
}
