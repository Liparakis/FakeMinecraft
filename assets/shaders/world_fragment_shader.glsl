#version 420 core

in vec3 TexCoord;
in vec3 Normal;
in vec3 FragPos; // Added for manual clipping
in float vSkyLight;
in float vBlockLight;
// Shadow calculation variables
in vec4 FragPosLightSpace;
out vec4 FragColor;
layout(binding = 0) uniform sampler2DArray tex;
layout(binding = 1) uniform sampler2D shadowMap;

uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform float ambientStrength;
uniform vec4 clipPlane;
uniform int simpleLighting;

float calculateShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir)
{
    // Make objects smaller as they get further away.
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    // Map the coordinates so they fit between 0 and 1.
    projCoords = projCoords * 0.5 + 0.5;
    
    // Check if fragment is outside shadow map bounds - return 0.0 (no shadow)
    if(projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0)
        return 0.0;
        
    // Add a tiny offset to keep shadows from looking glitchy.
    float bias = max(0.0005 * (1.0 - dot(normal, lightDir)), 0.0001);
    
    // Use a set of random points to smooth out the edges of shadows.
    // Learned this from: https://learnopengl.com/Advanced-Lighting/Shadows/Point-Shadows
    vec2 poissonDisk[16] = vec2[](
        vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725),
        vec2(-0.094184101, -0.92938870), vec2(0.34495938, 0.29387760),
        vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
        vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379),
        vec2(0.44323325, -0.97511554), vec2(0.53742981, -0.47373420),
        vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
        vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590),
        vec2(0.19984126, 0.78641367), vec2(0.14383161, -0.14100790)
    );
    
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    
    // Check the shadow map multiple times to make the shadow look soft.
    for(int i = 0; i < 16; ++i)
    {
        vec2 offset = poissonDisk[i] * texelSize * 2.5;
        float pcfDepth = texture(shadowMap, projCoords.xy + offset).r;
        shadow += projCoords.z - bias > pcfDepth ? 1.0 : 0.0;
    }
    shadow /= 16.0;

    // Fade out towards the edge of the shadow map instead of cutting off in a hard line.
    vec2 edge = abs(projCoords.xy * 2.0 - 1.0);
    return shadow * (1.0 - smoothstep(0.8, 1.0, max(edge.x, edge.y)));
}

void main()
{
    // Don't draw pixels that are below the water line in reflections.
    if (dot(vec4(FragPos, 1.0), clipPlane) < 0.0)
        discard;

	// Calculate the direction of the sunlight.
	vec3 lightDir = normalize(-sunDirection); 
	float diff = max(dot(Normal, lightDir), 0.0);
	
	float shadow = 0.0;
	vec3 diffuse = vec3(0.0);

	if (simpleLighting == 1) {
	    // Simple Lighting: Skip shadow map, use vertex light level (vSkyLight)
	    // Mimic classic voxel lighting: Sunlight intensity * Light Level * Face Shade
	    diffuse = diff * sunColor * vSkyLight;
	} else {
    	// Calculate shadow
    	shadow = calculateShadow(FragPosLightSpace, Normal, lightDir);
    	// Apply shadow to diffuse light
    	diffuse = diff * sunColor * (1.0 - shadow);
	}

	// Ambient (sky contribution)
	vec3 ambient = vec3(ambientStrength);

	// Block light (lava etc) - warm orange glow
	vec3 blockLight = vBlockLight * vec3(1.0, 0.85, 0.6);

	// Find the final light value by choosing the brightest one plus the sky light.
	vec3 totalLight = ambient + max(diffuse, blockLight);
	totalLight = clamp(totalLight, 0.0, 1.4);

	vec4 texResult = texture(tex, TexCoord);
	if (texResult.a == 0)
	    discard;
	FragColor = vec4(texResult.rgb * totalLight, 1.0);
}