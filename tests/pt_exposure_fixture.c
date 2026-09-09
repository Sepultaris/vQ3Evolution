/* Exercise production menu functions extracted unchanged by the runner,
 * without opening a window or loading game data. */
#include <assert.h>
#include "../code/q3_ui/ui_local.h"
#include "pt_exposure_code.inc"

static int writes;
static float savedExposure;

float Com_Clamp( float min, float max, float value ) {
	return value < min ? min : value > max ? max : value;
}

void trap_Cvar_SetValue( const char *name, float value ) {
	assert( !strcmp( name, "r_pathTracingExposure" ) );
	++writes;
	savedExposure = value;
}

int main( void ) {
	int i;
	float previous = 0;
	for ( i = 0; i <= 32; ++i ) {
		float value = GraphicsOptions_ExposureForStep( i );
		assert( value > previous );
		assert( GraphicsOptions_ExposureStep( value ) == i );
		if ( i < 28 ) assert( fabs( GraphicsOptions_ExposureForStep( i+4 ) / value - 2 ) < 0.000001f );
		previous = value;
	}
	assert( GraphicsOptions_ExposureForStep( -100 ) == 0.0625f );
	assert( GraphicsOptions_ExposureForStep( 16 ) == 1.0f );
	assert( GraphicsOptions_ExposureForStep( 100 ) == 16.0f );
	assert( GraphicsOptions_ExposureStep( 0.01f ) == 0 );
	assert( GraphicsOptions_ExposureStep( 100 ) == 32 );
	puts( "PASS: 33 monotonic quarter-stop values, exact default, bounds and round trips" );

	s_graphicsoptions.ptexposure.generic.id = ID_PTEXPOSURE;
	s_graphicsoptions.ptexposurereset.generic.id = ID_PTEXPOSURERESET;
	s_graphicsoptions.ptexposure.curvalue = 20.2f;
	GraphicsOptions_ExposureEvent( &s_graphicsoptions.ptexposure, QM_GOTFOCUS );
	assert( writes == 0 );
	GraphicsOptions_ExposureEvent( &s_graphicsoptions.ptexposure, QM_ACTIVATED );
	assert( writes == 1 && savedExposure == 2 && s_graphicsoptions.ptexposure.curvalue == 20 );
	GraphicsOptions_ExposureEvent( &s_graphicsoptions.ptexposurereset, QM_ACTIVATED );
	assert( writes == 2 && savedExposure == 1 && s_graphicsoptions.ptexposure.curvalue == 16 );
	puts( "PASS: live callback, click rounding, reset to 1x, focus never writes settings" );

	for ( i = 0; i < 3; ++i ) {
		int unavailable;
		for ( unavailable = 0; unavailable < 2; ++unavailable ) {
			int hidden = QMF_HIDDEN|QMF_INACTIVE;
			s_graphicsoptions.raytracing.curvalue = i;
			s_graphicsoptions.raytracing.generic.flags = unavailable ? QMF_GRAYED : 0;
			GraphicsOptions_UpdateExposureItems();
			assert( (s_graphicsoptions.ptexposure.generic.flags & hidden) == (i == 2 ? 0 : hidden) );
			assert( (s_graphicsoptions.ptexposurereset.generic.flags & hidden) == (i == 2 ? 0 : hidden) );
			assert( (s_graphicsoptions.rtshadowstrength.generic.flags & hidden) == (i == 2 ? hidden : 0) );
			assert( !!(s_graphicsoptions.ptexposure.generic.flags & QMF_GRAYED) == unavailable );
			assert( !!(s_graphicsoptions.ptexposurereset.generic.flags & QMF_GRAYED) == unavailable );
		}
	}
	assert( writes == 2 );
	puts( "PASS: all RTX mode/availability combinations, exclusive row visibility, no cvar mutation" );
	return 0;
}
