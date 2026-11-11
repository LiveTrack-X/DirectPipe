import type { IDisposable } from "../explicit-resource-management/disposable.js";
import type { JsonObject } from "../json.js";
import { type Language } from "./language.js";
/**
 * Internalization provider, responsible for managing localizations and translating resources.
 */
export declare class I18nProvider<T extends string> {
    #private;
    /**
     * Initializes a new instance of the {@link I18nProvider} class.
     * @param language The default language to be used when retrieving translations for a given key.
     * @param readTranslations Function responsible for providing localized resources for a given language.
     */
    constructor(language: Language<T>, readTranslations: TranslationsReader<T>);
    /**
     * The default language of the provider.
     * @returns The language.
     */
    get language(): Language<T>;
    /**
     * The default language of the provider.
     * @param value The language.
     */
    set language(value: Language<T>);
    /**
     * Adds an event listener that is called when the language within the provider changes.
     * @param listener Listener function to be called.
     * @returns Resource manager that, when disposed, removes the event listener.
     */
    onLanguageChange(listener: (language: Language<T>) => void): IDisposable;
    /**
     * Translates the specified {@link key}, as defined within the resources for the {@link language}.
     * When the key is not found, the default language is checked. Alias of {@link I18nProvider.translate}.
     * @param key Key of the translation.
     * @param language Optional language to get the translation for; otherwise the default language.
     * @returns The translation; otherwise the key.
     */
    t(key: string, language?: Language<T>): string;
    /**
     * Translates the specified {@link key}, as defined within the resources for the {@link language}.
     * When the key is not found, the default language is checked.
     * @param key Key of the translation.
     * @param language Optional language to get the translation for; otherwise the default language.
     * @returns The translation; otherwise the key.
     */
    translate(key: string, language?: Language<T>): string;
    /**
     * Gets the translations for the specified language.
     * @param language Language whose translations are being retrieved.
     * @returns The translations; otherwise `null`.
     */
    private getTranslations;
}
/**
 * Function responsible for providing localized resources for a given language.
 * @param language The language whose resources should be retrieved.
 * @returns Localized resources represented as a JSON object.
 */
export type TranslationsReader<T extends string> = (language: Language<T>) => JsonObject | null;
