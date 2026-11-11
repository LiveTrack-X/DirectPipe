import { EventEmitter } from "../event-emitter.js";
import {} from "../explicit-resource-management/disposable.js";
import { JsonRpcErrorCode } from "./json-rpc/error.js";
import { JsonRpcRequest } from "./json-rpc/request.js";
import { RpcRequestResponder } from "./response.js";
/**
 * Server capable of receiving requests from, and sending responses to, a client.
 */
export class RpcServer {
    /**
     * Registered methods, and their respective handlers.
     */
    #methods = new EventEmitter();
    /**
     * Function responsible for sending responses.
     */
    #send;
    /**
     * Initializes a new instance of the {@link RpcServer} class.
     * @param send Function responsible for sending responses.
     */
    constructor(send) {
        this.#send = send;
    }
    /**
     * Adds a method handler to this instance, allowing it to receive requests and notifications.
     * @param name The name of the method.
     * @param handler The function to be invoked when the method is called.
     * @returns A disposable object that can be used to remove the method handler.
     */
    addMethod(name, handler) {
        return this.#methods.disposableOn(name, handler);
    }
    /**
     * Attempts to process the specified request received from a client.
     * @param value Value to process.
     * @returns `true` when the server was able to process the request; otherwise `false`.
     */
    async receive(value) {
        const { success, data: request } = JsonRpcRequest.safeParse(value);
        if (!success) {
            return false;
        }
        const responder = new RpcRequestResponder(this.#send, request.id);
        const methods = this.#methods.listeners(request.method);
        if (methods.length === 0) {
            await responder.error({
                code: JsonRpcErrorCode.MethodNotFound,
                message: "The method does not exist or is not available.",
            });
            return false;
        }
        await this.#invoke(methods, request.params, responder);
        return true;
    }
    /**
     * Invokes the specified methods with the request information and attempts to return a response.
     * @param methods Chain of methods.
     * @param parameters Parameters provided with the request.
     * @param responder Request responder responsible for sending a response.
     */
    async #invoke(methods, parameters, responder) {
        // `next` function used to chain methods.
        const next = (methods) => {
            return () => {
                const [curr, ...rest] = methods;
                if (curr === undefined) {
                    return null;
                }
                return curr(parameters, responder, next(rest));
            };
        };
        try {
            // Execute the first method.
            const result = await next(methods)();
            await responder.success(result ?? null);
        }
        catch (err) {
            // Respond with an error before throwing.
            await responder.error({
                code: JsonRpcErrorCode.InternalError,
                message: err instanceof Error ? err.message : err instanceof Object ? err.toString() : "Unknown error",
            });
            throw err;
        }
    }
}
