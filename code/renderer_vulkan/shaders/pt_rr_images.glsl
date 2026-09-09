// Separate set: ordinary tracing pipelines do not carry RR image descriptors.
layout(set=1,binding=0,rgba16f) uniform image2D rrNoisy;
layout(set=1,binding=1,rgba16f) uniform image2D rrDiffuse;
layout(set=1,binding=2,rgba16f) uniform image2D rrSpecular;
layout(set=1,binding=3,rgba16f) uniform image2D rrNormalRoughness;
layout(set=1,binding=4,r32f) uniform image2D rrHitDistance;
layout(set=1,binding=5,rgba16f) uniform image2D rrOutput;
layout(set=1,binding=6,rgba8) uniform image2D rrDisplay;
