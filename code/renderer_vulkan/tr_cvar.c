
#include "tr_cvar.h"
#include "ref_import.h"

cvar_t	*r_railWidth;
cvar_t	*r_railCoreWidth;
cvar_t	*r_railSegmentLength;

cvar_t	*r_verbose;

cvar_t	*r_znear;


cvar_t	*r_inGameVideo;
cvar_t	*r_dynamiclight;

cvar_t	*r_norefresh;
cvar_t	*r_drawentities;
cvar_t	*r_drawworld;
cvar_t	*r_speeds;
cvar_t	*r_fullbright;
cvar_t	*r_novis;
cvar_t	*r_nocull;
cvar_t	*r_facePlaneCull;
cvar_t	*r_showcluster;
cvar_t	*r_nocurves;



cvar_t* r_fullscreen;
// display refresh rate
cvar_t* r_displayRefresh;

cvar_t	*r_lightmap;
cvar_t	*r_vertexLight;
cvar_t	*r_uiFullScreen;
cvar_t	*r_shadows;
cvar_t	*r_flares;
cvar_t	*r_singleShader;
cvar_t	*r_colorMipLevels;
cvar_t	*r_picmip;
cvar_t	*r_textureMode;
cvar_t	*r_ext_texture_filter_anisotropic;
cvar_t	*r_ext_max_anisotropy;
cvar_t	*r_showtris;
cvar_t	*r_showsky;
cvar_t	*r_shownormals;
cvar_t	*r_offsetFactor;
cvar_t	*r_offsetUnits;
cvar_t	*r_gamma;
cvar_t	*r_intensity;
cvar_t	*r_lockpvs;
cvar_t	*r_noportals;
cvar_t	*r_portalOnly;

cvar_t	*r_subdivisions;
cvar_t	*r_lodCurveError;

// r_overbrightBits->integer, but set to 0 if no hw gamma
// cvar_t	*r_overBrightBits;
cvar_t	*r_mapOverBrightBits;

cvar_t	*r_debugSurface;
cvar_t	*r_simpleMipMaps;

cvar_t	*r_showImages;

cvar_t	*r_ambientScale;
cvar_t	*r_directedScale;
cvar_t	*r_debugLight;
cvar_t	*r_debugSort;
cvar_t	*r_printShaders;
cvar_t	*r_saveFontData;

cvar_t	*r_maxpolys;
cvar_t	*r_maxpolyverts;

cvar_t* r_allowResize; // make window resizable
cvar_t* r_mode;

cvar_t* r_loadImgAPI;
cvar_t* r_dlss;
cvar_t* r_dlssSharpness;
cvar_t* r_dlssNeuralRendering;
cvar_t* r_dlssNRIntensity;
cvar_t* r_dlssNRLocalToneStrength;
cvar_t* r_dlssNRLocalStructureStrength;
cvar_t* r_dlssNRSkinStructureStrength;
cvar_t* r_dlssFrameGeneration;
cvar_t* r_reflex;
cvar_t* r_dlssAvailable;
cvar_t* r_dlssNeuralRenderingAvailable;
cvar_t* r_dlssFrameGenerationAvailable;
cvar_t* r_reflexAvailable;
cvar_t* r_rayTracing;
cvar_t* r_pathTracingSamples;
cvar_t* r_pathTracingBounces;
cvar_t* r_pathTracingExposure;
cvar_t* r_pathTracingReference;
cvar_t* r_pathTracingDenoise;
cvar_t* r_pathTracingTemporal;
cvar_t* r_pathTracingHistory;
cvar_t* r_pathTracingTemporalDebug;
cvar_t* r_pathTracingDebug;
cvar_t* r_pathTracingProfile;
cvar_t* r_pathTracingTestScene;
cvar_t* r_rayTracingShadowStrength;
cvar_t* r_rayTracingShadowBias;
cvar_t* r_rayTracingAvailable;

void R_Register( void ) 
{
	//
	// latched and archived variables
	//
	r_picmip = ri.Cvar_Get ("r_picmip", "1", CVAR_ARCHIVE | CVAR_LATCH );
    ri.Cvar_CheckRange( r_picmip, 0, 8, qtrue );

	r_simpleMipMaps = ri.Cvar_Get( "r_simpleMipMaps", "1", CVAR_ARCHIVE | CVAR_LATCH );
	r_textureMode = ri.Cvar_Get( "r_textureMode", "GL_LINEAR_MIPMAP_NEAREST", CVAR_ARCHIVE );
	r_ext_texture_filter_anisotropic = ri.Cvar_Get( "r_ext_texture_filter_anisotropic", "0", CVAR_ARCHIVE | CVAR_LATCH );
	ri.Cvar_CheckRange( r_ext_texture_filter_anisotropic, 0, 1, qtrue );
	r_ext_max_anisotropy = ri.Cvar_Get( "r_ext_max_anisotropy", "2", CVAR_ARCHIVE | CVAR_LATCH );
	ri.Cvar_CheckRange( r_ext_max_anisotropy, 1, 16, qtrue );
	r_dlss = ri.Cvar_Get( "r_dlss", "0", CVAR_ARCHIVE | CVAR_LATCH );
	ri.Cvar_CheckRange( r_dlss, 0, 5, qtrue );
	r_dlssSharpness = ri.Cvar_Get( "r_dlssSharpness", "0.0", CVAR_ARCHIVE );
	ri.Cvar_CheckRange( r_dlssSharpness, 0.0f, 1.0f, qfalse );
	r_dlssNeuralRendering = ri.Cvar_Get( "r_dlssNeuralRendering", "0", CVAR_ARCHIVE | CVAR_LATCH );
	ri.Cvar_CheckRange( r_dlssNeuralRendering, 0, 3, qtrue );
	r_dlssNRIntensity = ri.Cvar_Get( "r_dlssNRIntensity", "1.0", CVAR_ARCHIVE );
	ri.Cvar_CheckRange( r_dlssNRIntensity, 0.0f, 2.0f, qfalse );
	r_dlssNRLocalToneStrength = ri.Cvar_Get( "r_dlssNRLocalToneStrength", "1.0", CVAR_ARCHIVE );
	ri.Cvar_CheckRange( r_dlssNRLocalToneStrength, 0.0f, 2.0f, qfalse );
	r_dlssNRLocalStructureStrength = ri.Cvar_Get( "r_dlssNRLocalStructureStrength", "1.0", CVAR_ARCHIVE );
	ri.Cvar_CheckRange( r_dlssNRLocalStructureStrength, 0.0f, 2.0f, qfalse );
	r_dlssNRSkinStructureStrength = ri.Cvar_Get( "r_dlssNRSkinStructureStrength", "1.0", CVAR_ARCHIVE );
	ri.Cvar_CheckRange( r_dlssNRSkinStructureStrength, 0.0f, 2.0f, qfalse );
	r_dlssFrameGeneration = ri.Cvar_Get( "r_dlssFrameGeneration", "0", CVAR_ARCHIVE | CVAR_LATCH );
	ri.Cvar_CheckRange( r_dlssFrameGeneration, 0, 1, qtrue );
	r_reflex = ri.Cvar_Get( "r_reflex", "1", CVAR_ARCHIVE | CVAR_LATCH );
	ri.Cvar_CheckRange( r_reflex, 0, 2, qtrue );
	r_dlssAvailable = ri.Cvar_Get( "r_dlssAvailable", "0", CVAR_TEMP );
	r_dlssNeuralRenderingAvailable = ri.Cvar_Get( "r_dlssNeuralRenderingAvailable", "0", CVAR_TEMP );
	r_dlssFrameGenerationAvailable = ri.Cvar_Get( "r_dlssFrameGenerationAvailable", "0", CVAR_TEMP );
	r_reflexAvailable = ri.Cvar_Get( "r_reflexAvailable", "0", CVAR_TEMP );
	r_rayTracing = ri.Cvar_Get( "r_rayTracing", "0", CVAR_ARCHIVE | CVAR_LATCH );
	ri.Cvar_CheckRange( r_rayTracing, 0, 2, qtrue );
	r_pathTracingSamples = ri.Cvar_Get("r_pathTracingSamples", "4", CVAR_ARCHIVE);
	ri.Cvar_CheckRange(r_pathTracingSamples, 1, 64, qtrue);
	r_pathTracingBounces = ri.Cvar_Get("r_pathTracingBounces", "4", CVAR_ARCHIVE);
	ri.Cvar_CheckRange(r_pathTracingBounces, 1, 12, qtrue);
	r_pathTracingExposure = ri.Cvar_Get("r_pathTracingExposure", "1", CVAR_ARCHIVE);
	ri.Cvar_CheckRange(r_pathTracingExposure, 0.01f, 16.0f, qfalse);
	r_pathTracingReference = ri.Cvar_Get("r_pathTracingReference", "0", CVAR_CHEAT);
	ri.Cvar_CheckRange(r_pathTracingReference, 0, 1, qtrue);
	r_pathTracingDenoise = ri.Cvar_Get("r_pathTracingDenoise", "1", CVAR_ARCHIVE);
	ri.Cvar_CheckRange(r_pathTracingDenoise, 0, 1, qtrue);
	r_pathTracingTemporal = ri.Cvar_Get("r_pathTracingTemporal", "1", CVAR_ARCHIVE);
	ri.Cvar_CheckRange(r_pathTracingTemporal, 0, 1, qtrue);
	r_pathTracingHistory = ri.Cvar_Get("r_pathTracingHistory", "8", CVAR_ARCHIVE);
	ri.Cvar_CheckRange(r_pathTracingHistory, 1, 32, qtrue);
	r_pathTracingTemporalDebug = ri.Cvar_Get("r_pathTracingTemporalDebug", "0", CVAR_CHEAT);
	ri.Cvar_CheckRange(r_pathTracingTemporalDebug, 0, 2, qtrue);
	r_pathTracingDebug = ri.Cvar_Get("r_pathTracingDebug", "0", CVAR_CHEAT);
	r_pathTracingProfile = ri.Cvar_Get("r_pathTracingProfile", "0", CVAR_CHEAT);
	ri.Cvar_CheckRange(r_pathTracingProfile, 0, 1, qtrue);
	ri.Cvar_CheckRange(r_pathTracingDebug, 0, 6, qtrue);
	r_pathTracingTestScene = ri.Cvar_Get("r_pathTracingTestScene", "0", CVAR_CHEAT | CVAR_LATCH);
	ri.Cvar_CheckRange(r_pathTracingTestScene, 0, 1, qtrue);
	r_rayTracingShadowStrength = ri.Cvar_Get( "r_rayTracingShadowStrength", "0.55", CVAR_ARCHIVE );
	ri.Cvar_CheckRange( r_rayTracingShadowStrength, 0.0f, 1.0f, qfalse );
	r_rayTracingShadowBias = ri.Cvar_Get( "r_rayTracingShadowBias", "1.5", CVAR_ARCHIVE );
	ri.Cvar_CheckRange( r_rayTracingShadowBias, 0.05f, 16.0f, qfalse );
	r_rayTracingAvailable = ri.Cvar_Get( "r_rayTracingAvailable", "0", CVAR_TEMP );
	r_colorMipLevels = ri.Cvar_Get ("r_colorMipLevels", "0", CVAR_LATCH );

	// r_overBrightBits = ri.Cvar_Get ("r_overBrightBits", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_vertexLight = ri.Cvar_Get( "r_vertexLight", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_uiFullScreen = ri.Cvar_Get( "r_uifullscreen", "0", 0);
	r_subdivisions = ri.Cvar_Get ("r_subdivisions", "4", CVAR_ARCHIVE | CVAR_LATCH);

	//
	// temporary latched variables that can only change over a restart
	//
	r_fullbright = ri.Cvar_Get ("r_fullbright", "0", CVAR_LATCH|CVAR_CHEAT );
	r_mapOverBrightBits = ri.Cvar_Get ("r_mapOverBrightBits", "1", CVAR_LATCH );
	r_intensity = ri.Cvar_Get ("r_intensity", "1.5", CVAR_LATCH | CVAR_ARCHIVE );
	r_singleShader = ri.Cvar_Get ("r_singleShader", "0", CVAR_CHEAT | CVAR_LATCH );

	//
	// archived variables that can change at any time
	//
	r_lodCurveError = ri.Cvar_Get( "r_lodCurveError", "250", CVAR_ARCHIVE|CVAR_CHEAT );
    r_flares = ri.Cvar_Get ("r_flares", "0", CVAR_ARCHIVE );
	r_znear = ri.Cvar_Get( "r_znear", "4", CVAR_CHEAT );
    ri.Cvar_CheckRange( r_znear, 0.001f, 200, qtrue );

	r_inGameVideo = ri.Cvar_Get( "r_inGameVideo", "1", CVAR_ARCHIVE );
	r_dynamiclight = ri.Cvar_Get( "r_dynamiclight", "1", CVAR_ARCHIVE );
	r_gamma = ri.Cvar_Get( "r_gamma", "1", CVAR_ARCHIVE | CVAR_LATCH );
	r_facePlaneCull = ri.Cvar_Get ("r_facePlaneCull", "1", CVAR_ARCHIVE );

	r_railWidth = ri.Cvar_Get( "r_railWidth", "16", CVAR_ARCHIVE );
	r_railCoreWidth = ri.Cvar_Get( "r_railCoreWidth", "6", CVAR_ARCHIVE );
	r_railSegmentLength = ri.Cvar_Get( "r_railSegmentLength", "32", CVAR_ARCHIVE );

	r_ambientScale = ri.Cvar_Get( "r_ambientScale", "0.6", CVAR_CHEAT );
	r_directedScale = ri.Cvar_Get( "r_directedScale", "1", CVAR_CHEAT );

	//
	// temporary variables that can change at any time
	//
	r_showImages = ri.Cvar_Get( "r_showImages", "0", CVAR_TEMP );

	r_debugLight = ri.Cvar_Get( "r_debuglight", "0", CVAR_TEMP );
	r_debugSort = ri.Cvar_Get( "r_debugSort", "0", CVAR_CHEAT );
	r_printShaders = ri.Cvar_Get( "r_printShaders", "0", 0 );
	r_saveFontData = ri.Cvar_Get( "r_saveFontData", "0", 0 );

	r_nocurves = ri.Cvar_Get ("r_nocurves", "0", CVAR_CHEAT );
	r_drawworld = ri.Cvar_Get ("r_drawworld", "1", CVAR_CHEAT );
	r_lightmap = ri.Cvar_Get ("r_lightmap", "0", 0 );
	r_portalOnly = ri.Cvar_Get ("r_portalOnly", "0", CVAR_TEMP );


	r_norefresh = ri.Cvar_Get ("r_norefresh", "0", CVAR_CHEAT);
	r_drawentities = ri.Cvar_Get ("r_drawentities", "1", CVAR_CHEAT );
	r_nocull = ri.Cvar_Get ("r_nocull", "0", CVAR_CHEAT);
	r_novis = ri.Cvar_Get ("r_novis", "0", CVAR_CHEAT);
	r_showcluster = ri.Cvar_Get ("r_showcluster", "0", CVAR_CHEAT);
	r_speeds = ri.Cvar_Get ("r_speeds", "0", CVAR_CHEAT);
	r_verbose = ri.Cvar_Get( "r_verbose", "0", CVAR_CHEAT );
	r_debugSurface = ri.Cvar_Get ("r_debugSurface", "0", CVAR_TEMP);
	r_showtris = ri.Cvar_Get ("r_showtris", "0", CVAR_TEMP);
	r_showsky = ri.Cvar_Get ("r_showsky", "0", CVAR_TEMP);
	r_shownormals = ri.Cvar_Get ("r_shownormals", "0", CVAR_TEMP);
	r_offsetFactor = ri.Cvar_Get( "r_offsetfactor", "-1", CVAR_CHEAT );
	r_offsetUnits = ri.Cvar_Get( "r_offsetunits", "-2", CVAR_CHEAT );
	r_lockpvs = ri.Cvar_Get ("r_lockpvs", "0", CVAR_CHEAT);
	r_noportals = ri.Cvar_Get ("r_noportals", "0", CVAR_CHEAT);
	r_shadows = ri.Cvar_Get( "cg_shadows", "1", 0 );

	r_maxpolys = ri.Cvar_Get( "r_maxpolys", va("%d", 600), 0);
	r_maxpolyverts = ri.Cvar_Get( "r_maxpolyverts", va("%d", 3000), 0);

    r_fullscreen = ri.Cvar_Get( "r_fullscreen", "1", CVAR_ARCHIVE | CVAR_LATCH );
    r_displayRefresh = ri.Cvar_Get( "r_displayRefresh", "60", CVAR_LATCH );
    ri.Cvar_CheckRange( r_displayRefresh, 0, 200, qtrue );

    r_allowResize = ri.Cvar_Get( "r_allowResize", "0", CVAR_ARCHIVE | CVAR_LATCH );

    r_mode = ri.Cvar_Get( "r_mode", "-2", CVAR_ARCHIVE | CVAR_LATCH );

    r_loadImgAPI = ri.Cvar_Get( "r_loadImgAPI", "0", CVAR_ARCHIVE | CVAR_LATCH );
}
