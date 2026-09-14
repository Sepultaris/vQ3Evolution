layout(location=0) in vec2 uv;
layout(location=0) out vec4 resultColor;
layout(set=0,binding=0) uniform sampler2D scene;
layout(push_constant) uniform Effect {
    vec4 extentTimeFrame;
    vec4 values0;
    vec4 values1;
} effect;
void main() {
    if (effect.values0.z<=0.0) { resultColor=texture(scene,uv); return; }
    float sigma=max(effect.values0.z*0.25,0.25);
    float invSigma=0.5/(sigma*sigma);
    int last=min(24,int(ceil(3.0*sigma)));
    vec2 stepUV=BLUR_AXIS/vec2(textureSize(scene,0));
    vec3 sum=texture(scene,uv).rgb;
    float total=1.0;
    // Paired bilinear taps cover every texel in a continuous Gaussian kernel.
    for (int i=1;i<=last;i+=2) {
        float a=exp(-float(i*i)*invSigma);
        float b=i+1<=last ? exp(-float((i+1)*(i+1))*invSigma):0.0;
        float weight=a+b, offset=float(i)+b/weight;
        sum+=(texture(scene,uv+stepUV*offset).rgb+texture(scene,uv-stepUV*offset).rgb)*weight;
        total+=2.0*weight;
    }
    resultColor=vec4(sum/total,1.0);
}
