import { z } from "zod/v4-mini";
import { JsonRpcError } from "./error.js";
/**
 * Successful response object sent to a client.
 */
export const JsonRpcSuccessResponse = z.strictObject({
    jsonrpc: z.literal("2.0"),
    result: z.optional(z.any()),
    id: z.string(),
});
/**
 * Error response object sent to a client.
 */
export const JsonRpcErrorResponse = z.strictObject({
    jsonrpc: z.literal("2.0"),
    error: JsonRpcError,
    id: z.optional(z.string()),
});
/**
 * Response object sent to a client.
 */
export const JsonRpcResponse = z.union([JsonRpcSuccessResponse, JsonRpcErrorResponse]);
