/**
 * Responder responsible for responding to a request.
 */
export class RpcRequestResponder {
    /**
     * Identifier of the request.
     */
    #id;
    /**
     * Indicates whether a response has already been sent in relation to the response.
     */
    #responded = false;
    /**
     * Function responsible for sending responses.
     */
    #send;
    /**
     * Initializes a new instance of the {@link RpcRequestResponder} class.
     * @param send Function responsible for sending responses.
     * @param id Identifier of the request.
     */
    constructor(send, id) {
        this.#send = send;
        this.#id = id;
    }
    /**
     * Indicates whether a response can be sent.
     * @returns `true` when a response has not yet been set and the request accepts responses.
     */
    get canRespond() {
        return !this.#responded && this.#id !== undefined;
    }
    /**
     * Sends an error response to the client.
     * @param error The error.
     * @returns Promise fulfilled once the response has been sent.
     */
    error(error) {
        return this.#trySend({
            jsonrpc: "2.0",
            id: this.#id,
            error,
        });
    }
    /**
     * Sends the result to the client.
     * @param result The result.
     * @returns Promise fulfilled once the response has been sent.
     */
    async success(result) {
        if (this.#id) {
            await this.#trySend({
                jsonrpc: "2.0",
                id: this.#id,
                result,
            });
        }
    }
    /**
     * Sends the response to the client if one can be sent.
     * @param res The response.
     */
    async #trySend(res) {
        if (!this.#responded) {
            await this.#send(res);
            this.#responded = true;
        }
    }
}
