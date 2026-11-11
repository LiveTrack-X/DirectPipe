var _a;
import { JsonRpcErrorCode } from "./json-rpc/error.js";
import { JsonRpcResponse } from "./json-rpc/response.js";
import {} from "./request.js";
import {} from "./response.js";
/**
 * Client capable of sending requests to, and receiving responses from, a server.
 */
export class RpcClient {
    /**
     * The timeout, in milliseconds, used when the request does not have a timeout specified; default 30 seconds.
     */
    static #DEFAULT_TIMEOUT = 30000;
    /**
     * The client's options.
     */
    #options;
    /**
     * Function responsible for sending requests.
     */
    #send;
    /**
     * Requests with pending responses.
     */
    #requests = new Map();
    /**
     * Initializes a new instance of the {@link RpcClient} class.
     * @param send Function responsible for sending requests.
     * @param options Client options.
     */
    constructor(send, options) {
        this.#send = send;
        this.#options = {
            ...{ error: () => { } },
            ...options,
        };
    }
    /**
     * Sends a notification to the RPC server.
     * @param methodOrRequest The method name, or the request.
     * @param params Parameters to be used during the invocation of the method.
     */
    async notify(methodOrRequest, params) {
        if (typeof methodOrRequest === "string") {
            await this.#send({
                jsonrpc: "2.0",
                method: methodOrRequest,
                params,
            });
        }
        else {
            await this.#send({
                jsonrpc: "2.0",
                method: methodOrRequest.method,
                params: methodOrRequest.params,
            });
        }
    }
    /**
     * Attempts to process the specified value as a response from a server.
     * @param value Value to process.
     * @returns `true` when the client was able to process the value as a response; otherwise `false`.
     */
    async receive(value) {
        const { success, data: response } = JsonRpcResponse.safeParse(value);
        if (!success) {
            return false;
        }
        if ("result" in response) {
            this.#resolve(response.id, {
                ok: true,
                result: response.result,
            });
        }
        else {
            const err = {
                ok: false,
                error: response.error,
            };
            if (response.id === undefined) {
                this.#options.error(err);
            }
            else {
                this.#resolve(response.id, err);
            }
        }
        return true;
    }
    /**
     * Sends the request to the RPC server.
     * @param methodOrRequest The method name, or the request.
     * @param params Parameters to be used during the invocation of the method.
     * @returns The response.
     */
    async request(methodOrRequest, params) {
        if (typeof methodOrRequest === "string") {
            return this.#sendRequest({
                method: methodOrRequest,
                params,
            });
        }
        else {
            return this.#sendRequest(methodOrRequest);
        }
    }
    /**
     * Resolves pending requests.
     * @param id The request identifier.
     * @param response The response.
     */
    #resolve(id, response) {
        // Get the handler
        const handler = this.#requests.get(id);
        this.#requests.delete(id);
        // Provide the result or error to the handler.
        if (handler) {
            handler(response);
        }
    }
    /**
     * Sends the request to the RPC server.
     * @param request The request.
     * @returns The response.
     */
    async #sendRequest(request) {
        const id = crypto.randomUUID();
        const { method, params, timeout = _a.#DEFAULT_TIMEOUT } = request;
        // Initialize the response handler.
        const response = new Promise((resolve) => {
            this.#requests.set(id, (res) => {
                clearTimeout(timeoutMonitor);
                resolve(res);
            });
        });
        // Start the timeout, and send the request.
        const timeoutMonitor = setTimeout(() => {
            this.#resolve(id, {
                ok: false,
                error: {
                    code: JsonRpcErrorCode.InternalError,
                    message: "The request timed out.",
                },
            });
        }, timeout);
        await this.#send({ jsonrpc: "2.0", method, params, id });
        return response;
    }
}
_a = RpcClient;
