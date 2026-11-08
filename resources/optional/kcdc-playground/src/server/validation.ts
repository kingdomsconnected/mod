export function object(value: unknown): Record<string, unknown> {
    if (typeof value === "string") {
        if (value.length > 8192) throw new Error("Request is too large.");
        value = JSON.parse(value) as unknown;
    }
    if (!value || typeof value !== "object" || Array.isArray(value)) throw new Error("Invalid request.");
    return value as Record<string, unknown>;
}

export function integer(value: unknown, min: number, max: number): number {
    if (typeof value !== "number" || !Number.isInteger(value) || value < min || value > max) throw new Error(`Choose a whole number from ${min} to ${max}.`);
    return value;
}

export function text(value: unknown, max = 100): string {
    if (typeof value !== "string" || value.length > max) throw new Error("Invalid text.");
    return value;
}

export function vector(value: unknown): Vector3 {
    const p = object(value);
    if (![p.x, p.y, p.z].every((n) => typeof n === "number" && Number.isFinite(n))) throw new Error("Invalid position.");
    return new Vector3(p.x as number, p.y as number, p.z as number);
}

export function distance(a: { x: number; y: number; z: number }, b: { x: number; y: number; z: number }): number {
    return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}
