// Independent free-flight processes can be sampled before finding a surface.
// A closer surface still wins, but geometry beyond the candidate fog event
// cannot affect this segment. Do not resample on a surface hit: conditioning
// the second sample on the first search result would bias transport.
bool fogPathHit(vec3 origin,vec3 direction,float segmentDistance,int bounce,
    out Hit hit,out float fogDistance,out uint fogVolume) {
    bool event=fogSample(origin,direction,segmentDistance,pc.parameters.y,fogDistance,fogVolume);
    if(event) { PT_COUNT(7u); }
    float minimum=segmentDistance>0 ? continuationT(segmentDistance):0.001;
    float maximum=event ? fogDistance:pc.parameters.y;
    return minimum<maximum && (bounce==0 ? primaryHit(origin,direction,minimum,maximum,hit):
        trace(origin,direction,minimum,maximum,0u,hit));
}
