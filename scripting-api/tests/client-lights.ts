const localLight: Light = Light.spawn({ x: 10, y: 20, z: 2 }, {
  color: { x: 1, y: 0.5, z: 0.2 }, intensity: 2, radius: 8, castShadows: true,
});
const localLightId: number = localLight.id;
const localLightValid: boolean = localLight.valid;
const localLightPosition: Vector3 = localLight.position;
const localLightColor: Vector3 = localLight.color;
localLight.position = new Vector3(10, 20, 3);
localLight.color = new Vector3(0.2, 0.5, 1);
localLight.intensity = 3;
localLight.radius = 6;
localLight.castShadows = false;
// @ts-expect-error Shadows require an actual boolean.
localLight.castShadows = 1;
// @ts-expect-error Shadows require an actual boolean in spawn options too.
Light.spawn({ x: 0, y: 0, z: 1 }, { castShadows: "yes" });
// @ts-expect-error All three numeric position components are required.
localLight.position = { x: 1, y: 2 };
// @ts-expect-error Color components cannot be strings.
localLight.color = { x: "1", y: 0, z: 0 };
// @ts-expect-error Intensity cannot be a numeric string.
localLight.intensity = "2";
// @ts-expect-error Radius cannot be a boolean.
localLight.radius = true;
localLight.destroy();
localLight.destroy();
