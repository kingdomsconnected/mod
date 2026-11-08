import { defineConfig, type Plugin } from "vite";
import preact from "@preact/preset-vite";

const CLOSING_STYLE = "</style";
const CLOSING_SCRIPT = "</script";
const ESCAPED_STYLE = "<\\/style";
const ESCAPED_SCRIPT = "<\\/script";

function decode(source: string | Uint8Array): string {
    return typeof source === "string" ? source : new TextDecoder().decode(source);
}

/** Replaces the whole tag that references `fileName` with `replacement`. */
function replaceTag(html: string, fileName: string, tagName: string, replacement: string): string {
    const reference = html.indexOf(fileName);
    if (reference < 0) {
        return html;
    }

    const start = html.lastIndexOf("<" + tagName, reference);
    const closing = html.indexOf("</" + tagName + ">", reference);
    const end = closing >= 0 ? closing + tagName.length + 3 : html.indexOf(">", reference) + 1;
    if (start < 0 || end <= start) {
        return html;
    }
    return html.slice(0, start) + replacement + html.slice(end);
}

/**
 * Folds the emitted stylesheet and module bundle back into index.html.
 *
 * The menu is served to CEF over the framework's fw:// scheme, where every
 * asset is its own hop onto a CEF file thread and back, and the page has no
 * head start to spend on them: it begins loading only once the D3D overlay is
 * up, a few hundred milliseconds before the backdrop it belongs over. One
 * document is one hop, and nothing here is cached across runs anyway.
 */
function inlinePageAssets(): Plugin {
    return {
        name: "kcdc-inline-page-assets",
        enforce: "post",
        generateBundle(_options, bundle) {
            const page = Object.values(bundle).find(entry => entry.type === "asset" && entry.fileName.endsWith("index.html"));
            if (page === undefined || page.type !== "asset") {
                return;
            }

            let html = decode(page.source);

            for (const [fileName, entry] of Object.entries(bundle)) {
                if (entry.type === "asset" && fileName.endsWith(".css")) {
                    const css = decode(entry.source).split(CLOSING_STYLE).join(ESCAPED_STYLE);
                    html = replaceTag(html, fileName, "link", "<style>" + css + "</style>");
                    delete bundle[fileName];
                }
                else if (entry.type === "chunk" && fileName.endsWith(".js")) {
                    const code = entry.code.split(CLOSING_SCRIPT).join(ESCAPED_SCRIPT);
                    html = replaceTag(html, fileName, "script", '<script type="module">' + code + "</script>");
                    delete bundle[fileName];
                }
            }

            page.source = html;
        },
    };
}

export default defineConfig({
    base: "./",
    plugins: [preact(), inlinePageAssets()],
    build: {
        target: "chrome100",
        outDir: "../files/kcdc/ui",
        emptyOutDir: true,
        rollupOptions: {
            output: {
                entryFileNames: "menu.js",
                chunkFileNames: "chunks/[name]-[hash].js",
                assetFileNames: "menu[extname]",
            },
        },
    },
});
