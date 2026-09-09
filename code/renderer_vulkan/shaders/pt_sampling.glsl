// Spatially blue-noise rank sampling. Each dimension/sample gets independent
// toroidal shifts and a uniform Cranley rotation; there is no shared RNG state
// between pixels and no replacement of stochastic rays by a screen-space effect.
layout(constant_id=1) const bool ptAliasPDF=false;
layout(binding=34, std430) readonly buffer LightGrid {
    vec4 gridOriginCell;
    uvec4 gridDimensions;
    vec4 lightAliases[]; // threshold, alias index, own PDF, alias PDF
};
layout(binding=35, std430) readonly buffer BlueNoise { uint noiseRanks[]; };
uint samplePixel, sampleNumber, sampleDimension, sampleTile;
uvec2 sampleCoordinate;
uint sampleHash(uint x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu;
    return x ^ (x >> 16);
}
float blueNoiseSample() {
    uint key=sampleHash(sampleNumber*0x9e3779b9u+sampleDimension++*0x85ebca6bu);
    uvec2 p=(sampleCoordinate+uvec2(key^sampleTile,(key>>6)^(sampleTile>>6)))&63u;
    float rank=(float(noiseRanks[p.y*64u+p.x])+0.5)*(1.0/4096.0);
    return fract(rank+float(sampleHash(key+0x632be5abu)>>8)*(1.0/16777216.0));
}
uint lightCell(vec3 world) {
    uvec3 p=uvec3(clamp(floor((world-gridOriginCell.xyz)/gridOriginCell.w),vec3(0),vec3(gridDimensions.xyz)-1));
    return (p.z*gridDimensions.y+p.y)*gridDimensions.x+p.x;
}
uint proposedLight(uint cell,float u,out float pdf) {
    uint count=gridDimensions.w;
    float column=u*float(count);
    uint index=min(uint(column),count-1u), offset=cell*count;
    vec4 entry=lightAliases[offset+index];
    bool direct=fract(column)<entry.x;
    uint chosen=direct ? index:uint(entry.y);
    pdf=ptAliasPDF ? (direct ? entry.z:entry.w):lightAliases[offset+chosen].z;
    return chosen;
}
