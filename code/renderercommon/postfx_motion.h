/* Small CPU camera transform for post effects; no tracing/reconstruction state.
 * Column-major inputs, row-vector dot products at the shader boundary. GPL-2.0+. */
#ifndef PFX_MOTION_H
#define PFX_MOTION_H
#include <math.h>
static inline void PFX_Multiply(const float *a,const float *b,float *out) {
    for (int col=0;col<4;++col) for (int row=0;row<4;++row) {
        double value=0;
        for (int k=0;k<4;++k) value+=(double)a[4*k+row]*b[4*col+k];
        out[4*col+row]=(float)value;
    }
}
static inline int PFX_PreviousClipRows(const float *projection,const float *view,
    const float *oldProjection,const float *oldView,float rows[3][4]) {
    float current[16],previous[16],inverse[16],transform[16];
    double augmented[4][8];
    PFX_Multiply(projection,view,current);
    PFX_Multiply(oldProjection,oldView,previous);
    for (int r=0;r<4;++r) for (int c=0;c<8;++c)
        augmented[r][c]=c<4 ? current[4*c+r]:(c-4==r ? 1.0:0.0);
    for (int col=0;col<4;++col) {
        int pivot=col;
        for (int r=col+1;r<4;++r)
            if (fabs(augmented[r][col])>fabs(augmented[pivot][col])) pivot=r;
        if (fabs(augmented[pivot][col])<1e-12) return 0;
        for (int c=0;c<8;++c) {
            double swap=augmented[col][c]; augmented[col][c]=augmented[pivot][c]; augmented[pivot][c]=swap;
        }
        double scale=augmented[col][col];
        for (int c=0;c<8;++c) augmented[col][c]/=scale;
        for (int r=0;r<4;++r) if (r!=col) {
            double factor=augmented[r][col];
            for (int c=0;c<8;++c) augmented[r][c]-=factor*augmented[col][c];
        }
    }
    for (int r=0;r<4;++r) for (int c=0;c<4;++c) inverse[4*c+r]=(float)augmented[r][c+4];
    PFX_Multiply(previous,inverse,transform);
    for (int r=0;r<3;++r) for (int c=0;c<4;++c) rows[r][c]=transform[4*c+(r==2 ? 3:r)];
    return 1;
}
static inline int PFX_MotionContinuous(float seconds,float distanceSquared,float forwardDot) {
    // Pause, long stalls, teleports and camera cuts must not create long streaks.
    return seconds>0 && seconds<=0.25f && distanceSquared<256.0f*256.0f && forwardDot>0.5f;
}
#endif
