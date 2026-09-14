#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 resultColor;
layout(set=0,binding=0) uniform sampler2D scene;
layout(push_constant) uniform Effect {
    vec4 extentTimeFrame;
    vec4 values0; // exposure, contrast, shadows, highlights
    vec4 values1; // gamma, saturation, black point, shoulder
} effect;
void main() {
    vec4 source=texture(scene,uv);
    // V1 effects receive display color, not HDR radiance. Decode with an
    // approximate 2.2 display gamma for exposure, then reshape display luma.
    vec3 color=pow(max(source.rgb,vec3(0.0)),vec3(2.2))*exp2(effect.values0.x);
    float luma=dot(color,vec3(0.2126,0.7152,0.0722));
    // A luminance-only Reinhard shoulder avoids independent RGB clipping hues.
    color/=1.0+effect.values1.w*luma;
    color=pow(max(color,vec3(0.0)),vec3(1.0/2.2));
    luma=dot(color,vec3(0.2126,0.7152,0.0722));
    float shadows=1.0-smoothstep(0.0,0.6,luma);
    float highlights=smoothstep(0.4,1.0,luma);
    float curve=(luma-0.5)*effect.values0.y+0.5+
        effect.values0.z*shadows+effect.values0.w*highlights;
    curve=(curve-effect.values1.z)/max(1.0-effect.values1.z,0.01);
    // Retain chroma while shifting luminance; saturation zero is neutral gray.
    color=vec3(curve)+(color-vec3(luma))*effect.values1.y;
    color=pow(clamp(color,0.0,1.0),vec3(1.0/max(effect.values1.x,0.01)));
    resultColor=vec4(color,source.a);
}
