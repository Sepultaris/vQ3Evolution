// These views alias native-denoiser scratch/history only while RR is active.
// No native shader accesses them in that frame. Mode changes invalidate history.
struct RrSurface { vec4 position; vec4 normal; }; // identity bits; roughness
layout(set=1,binding=7,std430) buffer RrSurfaces { RrSurface rrSurfaces[]; };
layout(set=1,binding=8,std430) readonly buffer RrOldSurfaces { RrSurface rrOldSurfaces[]; };
// reservoir: selected map-light index, normalized weight, effective M, age
layout(set=1,binding=9,std430) buffer RrTemporal { vec4 rrTemporal[]; };
layout(set=1,binding=10,std430) readonly buffer RrOldTemporal { vec4 rrOldTemporal[]; };
layout(set=1,binding=11,std430) buffer RrSpatial { vec4 rrSpatial[]; };
// moments of noisy luminance, observation age, current per-pixel sample budget
layout(set=1,binding=12,std430) buffer RrAdaptive { vec4 rrAdaptive[]; };
layout(set=1,binding=13,std430) readonly buffer RrOldAdaptive { vec4 rrOldAdaptive[]; };
