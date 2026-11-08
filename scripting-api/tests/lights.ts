const torchLight: Light = Light.spawn(new Vector3(1, 2, 3), {
    color: { x: 1, y: 0.65, z: 0.3 },
    intensity: 2,
    radius: 6,
    castShadows: true,
});
torchLight.position = new Vector3(2, 3, 4);
torchLight.color = new Vector3(0.2, 0.4, 1);
torchLight.intensity = 3;
torchLight.radius = 8;
torchLight.castShadows = false;
const shadows: boolean = torchLight.castShadows;
void shadows;
const exportedLights: Light[] = WorldResource.find("market")?.lights ?? [];
void exportedLights;
torchLight.destroy();

// @ts-expect-error Position is required.
Light.spawn();
// @ts-expect-error Intensity is numeric.
Light.spawn(new Vector3(), { intensity: "bright" });
// @ts-expect-error Color requires all three channels.
Light.spawn(new Vector3(), { color: { x: 1 } });

// @ts-expect-error Shadow casting is a boolean.
Light.spawn(new Vector3(), { castShadows: 1 });
// @ts-expect-error Shadow casting is a boolean.
torchLight.castShadows = "true";
