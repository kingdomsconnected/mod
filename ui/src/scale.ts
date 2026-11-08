import { useEffect, useState } from "preact/hooks";

/**
 * How the page is sized against the game's resolution.
 *
 * CEF renders this view offscreen at the framebuffer's own size with a device
 * ratio of 1, so a 4K client is a 3840 CSS pixel viewport rather than a 1920
 * one drawn twice as sharply. Every length in `menu.css` is authored against
 * 1920x1080, which is why the menu came out half size there.
 *
 * Root zoom is the one lever that fixes that without restating those lengths:
 * Chromium resolves it before layout, so percentages, `inset: 0` and hit
 * testing all follow the zoomed box -- a mouse position CEF hands over in
 * framebuffer pixels still lands on what the player sees -- and glyphs are
 * rasterised at the real resolution instead of being scaled up afterwards.
 *
 * Viewport units are the exception: `vh` and `vw` keep resolving against the
 * unzoomed viewport. The design survives that because every one of them sits
 * inside a `min()` or a `clamp()` whose other bound already wins at 1920x1080,
 * so a larger client cannot push one further.
 */

const DESIGN_WIDTH = 1920;
const DESIGN_HEIGHT = 1080;

/**
 * Never below 1: a client smaller than the design is what the media queries in
 * `menu.css` are for, and shrinking it further only makes the text unreadable.
 */
function fitScale(): number {
    const fit = Math.min(window.innerWidth / DESIGN_WIDTH, window.innerHeight / DESIGN_HEIGHT);
    return Math.max(1, Math.round(fit * 20) / 20);
}

/** Owned by the root component: the zoom stays applied while the menu is down. */
export function useInterfaceScale(): void {
    const [fit, setFit] = useState(fitScale);

    useEffect(() => {
        const onResize = () => setFit(fitScale());
        window.addEventListener("resize", onResize);
        return () => window.removeEventListener("resize", onResize);
    }, []);

    useEffect(() => {
        document.documentElement.style.setProperty("zoom", String(fit));
    }, [fit]);
}
