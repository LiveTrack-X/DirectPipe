import { z } from "zod/v4-mini";
/**
 * Contains information about an error that occurred on the server.
 */
export const JsonRpcError = z.object({
    code: z.number(),
    message: z.string(),
    data: z.optional(z.any()),
});
/**
 * Reserved error codes for JSON-RPC 2.0.
 */
export const JsonRpcErrorCode = {
    /**
     * Invalid JSON was received by the server. An error occurred on the server while parsing the JSON text.
     */
    ParseError: -32700,
    /**
     * 	The JSON sent is not a valid Request object.
     */
    InvalidRequest: -32600,
    /**
     * 	The method does not exist / is not available.
     */
    MethodNotFound: -32601,
    /**
     * Invalid method parameter(s).
     */
    InvalidParams: -32602,
    /**
     * Internal JSON-RPC error.
     */
    InternalError: -32603,
};
