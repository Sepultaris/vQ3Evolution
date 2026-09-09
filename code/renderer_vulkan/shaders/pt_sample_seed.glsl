// Independent per-path white-noise streams permit concurrent samples. The
// blue-noise dimensions still use their original pixel/frame/sample indices.
// Sample zero retains its legacy seed. Never permit the xorshift zero state.
uint independentPathSeed(uint pixel,uint frame,uint sampleIndex) {
    uint seed=(pixel+1u)*747796405u+(frame+1u)*2891336453u;
    if(sampleIndex!=0u) seed=sampleHash(seed^sampleHash(sampleIndex*0x9e3779b9u));
    return seed==0u ? 1u:seed;
}
