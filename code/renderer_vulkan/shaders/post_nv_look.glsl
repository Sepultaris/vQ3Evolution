// Display-space image-intensifier approximation, not a thermal/IR sensor.
float nvLuminance(vec3 c) {
    // Negative red/blue weights erased lit surfaces in the old "IR" remap.
    return dot(c, vec3(0.24, 0.64, 0.12));
}
float nvSignal(float luminance, float gain) {
    // Dim-detail lift and a smooth shoulder, without clipping before gain.
    return pow(1.0 - exp(-2.0 * max(luminance, 0.0) * max(gain, 0.0)), 0.85);
}
vec3 nvPhosphor(float signal, uint tint) {
    float highlight = smoothstep(0.35, 1.0, signal);
    vec3 colour = tint == 1u
        ? mix(vec3(0.70, 0.86, 0.79), vec3(0.94, 1.0, 0.96), highlight)
        : mix(vec3(0.18, 0.90, 0.30), vec3(0.72, 1.0, 0.74), highlight);
    return colour * signal;
}
uint nvHash(uint n) {
    n ^= n >> 16; n *= 0x7feb352du;
    n ^= n >> 15; n *= 0x846ca68bu;
    return n ^ (n >> 16);
}
float nvNoise(vec2 cell, float time) {
    uint seed = uint(cell.x) * 1973u ^ uint(cell.y) * 9277u ^ uint(floor(time * 60.0)) * 26699u;
    uint a = nvHash(seed), b = nvHash(seed ^ 0x68bc21ebu);
    // Zero-mean triangular grain: no brightness bias when raising grain.
    return (float(a & 65535u) + float(b & 65535u)) / 65535.0 - 1.0;
}
float nvLensMask(vec2 q, float aspect) {
    // Screen-height units keep circles circular. Limit spread on ultrawide
    // displays so the lenses remain connected rather than developing gaps.
    float span = min(aspect, 2.0);
    float radius = min(0.74, 0.41 * span);
    float coverage = 0.0;
    for (int i = 0; i < 4; ++i) {
        float centre = (i == 0 ? -0.66 : i == 1 ? -0.18 : i == 2 ? 0.18 : 0.66) * span;
        float r = length(q - vec2(centre, 0.0)) / radius;
        float edge = 1.0 - smoothstep(0.80, 1.0, r);
        float falloff = 1.0 - 0.25 * smoothstep(0.25, 0.95, r);
        coverage = max(coverage, edge * falloff);
    }
    // Soft outer-tube seams; the strongly overlapping inner pair leaves a
    // broad central view. Never duplicate or distort the actual scene.
    float seam = (abs(q.x) - 0.42 * span) / (0.035 * span);
    return coverage * (1.0 - 0.28 * exp(-seam * seam));
}
