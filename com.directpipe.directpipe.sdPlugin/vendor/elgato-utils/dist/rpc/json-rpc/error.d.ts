import { z } from "zod/v4-mini";
import type { JsonValue } from "../../json.js";
/**
 * Contains information about an error that occurred.
 */
export type JsonRpcError = {
    /**
     * Indicates the error type that occurred.
     */
    readonly code: JsonRpcErrorCode | number;
    /**
     * Short description of the error.
     */
    readonly message: string;
    /**
     * Contains additional information about the error.
     */
    readonly data?: JsonValue;
};
/**
 * Contains information about an error that occurred on the server.
 */
export declare const JsonRpcError: z.ZodMiniType<JsonRpcError>;
/**
 * Reserved error codes for JSON-RPC 2.0.
 */
export declare const JsonRpcErrorCode: {
    /**
     * Invalid JSON was received by the server. An error occurred on the server while parsing the JSON text.
     */
    readonly ParseError: -32700;
    /**
     * 	The JSON sent is not a valid Request object.
     */
    readonly InvalidRequest: -32600;
    /**
     * 	The method does not exist / is not available.
     */
    readonly MethodNotFound: -32601;
    /**
     * Invalid method parameter(s).
     */
    readonly InvalidParams: -32602;
    /**
     * Internal JSON-RPC error.
     */
    readonly InternalError: -32603;
};
/**
 * Reserved error codes for JSON-RPC 2.0.
 */
export type JsonRpcErrorCode = (typeof JsonRpcErrorCode)[keyof typeof JsonRpcErrorCode];
