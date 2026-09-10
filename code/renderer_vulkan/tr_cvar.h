#ifndef TR_CVAR_H_
#define TR_CVAR_H_

#include "../qcommon/q_shared.h"


extern cvar_t	*r_railWidth;
extern cvar_t	*r_railCoreWidth;
extern cvar_t	*r_railSegmentLength;

extern cvar_t	*r_verbose;				// used for verbose debug spew

extern cvar_t	*r_znear;				// near Z clip plane


extern cvar_t	*r_depthbits;			// number of desired depth bits




extern cvar_t	*r_inGameVideo;			// controls whether in game video should be draw
extern cvar_t	*r_dynamiclight;		// dynamic lights enabled/disabled

extern cvar_t	*r_norefresh;			// bypasses the ref rendering
extern cvar_t	*r_drawentities;		// disable/enable entity rendering
extern cvar_t	*r_drawworld;			// disable/enable world rendering
extern cvar_t	*r_speeds;				// various levels of information display

extern cvar_t	*r_novis;				// disable/enable usage of PVS
extern cvar_t	*r_nocull;
extern cvar_t	*r_facePlaneCull;		// enables culling of planar surfaces with back side test
extern cvar_t	*r_nocurves;
extern cvar_t	*r_showcluster;

extern cvar_t	*r_mode;				// video mode
extern cvar_t	*r_fullscreen;
extern cvar_t	*r_gamma;


extern cvar_t	*r_singleShader;				// make most world faces use default shader
extern cvar_t	*r_colorMipLevels;				// development aid to see texture mip usage
extern cvar_t	*r_picmip;						// controls picmip values
extern cvar_t	*r_textureMode;
extern cvar_t	*r_ext_texture_filter_anisotropic;
extern cvar_t	*r_ext_max_anisotropy;
extern cvar_t	*r_offsetFactor;
extern cvar_t	*r_offsetUnits;

extern cvar_t	*r_fullbright;					// avoid lightmap pass
extern cvar_t	*r_lightmap;					// render lightmaps only
extern cvar_t	*r_vertexLight;					// vertex lighting mode for better performance
extern cvar_t	*r_uiFullScreen;				// ui is running fullscreen

extern cvar_t	*r_showtris;					// enables wireframe rendering of the world
extern cvar_t	*r_showsky;						// forces sky in front of all surfaces
extern cvar_t	*r_shownormals;					// draws wireframe normals
extern cvar_t	*r_clear;						// force screen clear every frame

extern cvar_t	*r_shadows;						// controls shadows: 0 = none, 1 = blur, 2 = stencil, 3 = black planar projection

extern cvar_t	*r_intensity;

extern cvar_t	*r_lockpvs;
extern cvar_t	*r_noportals;
extern cvar_t	*r_portalOnly;

extern cvar_t	*r_subdivisions;
extern cvar_t	*r_lodCurveError;

//extern	cvar_t	*r_overBrightBits;
extern	cvar_t	*r_mapOverBrightBits;

extern	cvar_t	*r_debugSurface;
extern	cvar_t	*r_simpleMipMaps;

extern	cvar_t	*r_showImages;
extern	cvar_t	*r_debugSort;

extern	cvar_t	*r_printShaders;
extern	cvar_t	*r_saveFontData;


extern cvar_t	*r_maxpolys;
extern cvar_t	*r_maxpolyverts;


extern	cvar_t	*r_ambientScale;
extern	cvar_t	*r_directedScale;
extern	cvar_t	*r_debugLight;

extern cvar_t* r_allowResize; // make window resizable
extern cvar_t* r_mode;
extern cvar_t* r_fullscreen;
extern cvar_t* r_displayRefresh;
extern cvar_t* r_swapInterval;
extern cvar_t* r_loadImgAPI;
extern cvar_t* r_dlss;
extern cvar_t* r_dlssRayReconstruction;
extern cvar_t* r_dlssSharpness;
extern cvar_t* r_dlssNeuralRendering;
extern cvar_t* r_dlssNRIntensity;
extern cvar_t* r_dlssNRLocalToneStrength;
extern cvar_t* r_dlssNRLocalStructureStrength;
extern cvar_t* r_dlssNRSkinStructureStrength;
extern cvar_t* r_dlssFrameGeneration;
extern cvar_t* r_dlssFGIdleHooks;
extern cvar_t* r_reflex;
extern cvar_t* r_dlssAvailable;
extern cvar_t* r_dlssNeuralRenderingAvailable;
extern cvar_t* r_dlssFrameGenerationAvailable;
extern cvar_t* r_reflexAvailable;
extern cvar_t* r_rayTracing;
extern cvar_t* r_pathTracingScale;
extern cvar_t* r_pathTracingSamples;
extern cvar_t* r_pathTracingLightReuse;
extern cvar_t* r_pathTracingAdaptive;
extern cvar_t* r_pathTracingAdaptiveDebug;
extern cvar_t* r_pathTracingBounces;
extern cvar_t* r_pathTracingExposure;
extern cvar_t* r_pathTracingAmbient;
extern cvar_t* r_pathTracingSunAngle;
extern cvar_t* r_pathTracingLightRadius;
extern cvar_t* r_pathTracingReference;
extern cvar_t* r_pathTracingDenoise;
extern cvar_t* r_pathTracingTemporal;
extern cvar_t* r_pathTracingHistory;
extern cvar_t* r_pathTracingTemporalDebug;
extern cvar_t* r_pathTracingDebug;
extern cvar_t* r_pathTracingProfile;
extern cvar_t* r_pathTracingShaderProfile;
extern cvar_t* r_pathTracingSampling;
extern cvar_t* r_pathTracingMaterialFastPath;
extern cvar_t* r_pathTracingEmitterSearch;
extern cvar_t* r_pathTracingEmitterGeometry;
extern cvar_t* r_pathTracingLightLoop;
extern cvar_t* r_pathTracingDlightReservoir;
extern cvar_t* r_pathTracingMaterialCache;
extern cvar_t* r_pathTracingCompactTransport;
extern cvar_t* r_pathTracingStaged;
extern cvar_t* r_pathTracingStagedRows;
extern cvar_t* r_pathTracingRRRows;
extern cvar_t* r_pathTracingDynamicOpaque;
extern cvar_t* r_pathTracingStagedProfile;
extern cvar_t* r_pathTracingPipelineStats;
extern cvar_t* r_pathTracingBRDFReuse;
extern cvar_t* r_pathTracingMapLightCull;
extern cvar_t* r_pathTracingAliasPDF;
extern cvar_t* r_pathTracingTestScene;
extern cvar_t* r_pathTracingTestMotion;
extern cvar_t* r_rayTracingShadowStrength;
extern cvar_t* r_rayTracingShadowBias;
extern cvar_t* r_rayTracingAvailable;

void R_Register( void );



#endif
