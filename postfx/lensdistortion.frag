#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 resultColor;
layout(set=0,binding=0) uniform sampler2D scene;
layout(push_constant) uniform Effect {
    vec4 extentTimeFrame;
    vec4 values0; // radial strength, zoom, border softness
    vec4 values1;
} effect;
void main() {
    vec2 extent=max(effect.extentTimeFrame.xy,vec2(1.0));
    vec2 aspect=extent/min(extent.x,extent.y);
    vec2 position=(uv*2.0-1.0)*aspect;
    float radius2=dot(position,position)/dot(aspect,aspect);
    // Destination-to-source mapping. Positive strength produces barrel
    // distortion; negative strength produces pincushion. Normalize at corners
    // so even ultrawide targets stay inside the monotonic polynomial range.
    vec2 sampleUV=0.5+0.5*position/aspect*
        (1.0+effect.values0.x*radius2)/max(effect.values0.y,0.01);
    vec4 color=texture(scene,sampleUV);
    // Outside the warped image is black, never a stretched clamp-to-edge smear.
    float edge=min(min(sampleUV.x,sampleUV.y),min(1.0-sampleUV.x,1.0-sampleUV.y));
    float coverage=effect.values0.z>0.0 ? smoothstep(0.0,effect.values0.z,edge) : step(0.0,edge);
    resultColor=vec4(color.rgb*coverage,color.a);
}
