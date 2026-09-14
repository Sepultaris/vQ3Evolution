/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
//
/*
=======================================================================

DISPLAY OPTIONS MENU

=======================================================================
*/

#include "ui_local.h"


#define ART_FRAMEL			"menu/art/frame2_l"
#define ART_FRAMER			"menu/art/frame1_r"
#define ART_BACK0			"menu/art/back_0"
#define ART_BACK1			"menu/art/back_1"

#define ID_GRAPHICS			10
#define ID_DISPLAY			11
#define ID_SOUND			12
#define ID_NETWORK			13
#define ID_BRIGHTNESS		14
#define ID_SCREENSIZE		15
#define ID_BACK				16
#define ID_VSYNC             17
#define ID_FOV               18
#define ID_APPLY             19


typedef struct {
	menuframework_s	menu;

	menutext_s		banner;
	menubitmap_s	framel;
	menubitmap_s	framer;

	menutext_s		graphics;
	menutext_s		display;
	menutext_s		sound;
	menutext_s		network;

	menuslider_s	brightness;
	menuslider_s	screensize;
	menulist_s      vsync;
	menuslider_s    fov;
	menutext_s      apply;
	int             initialVsync;

	menubitmap_s	back;
} displayOptionsInfo_t;

static displayOptionsInfo_t	displayOptionsInfo;


/*
=================
UI_DisplayOptionsMenu_Event
=================
*/
static void UI_DisplayOptionsMenu_Event( void* ptr, int event ) {
	if( event != QM_ACTIVATED ) {
		return;
	}

	switch( ((menucommon_s*)ptr)->id ) {
	case ID_GRAPHICS:
		UI_PopMenu();
		UI_GraphicsOptionsMenu();
		break;

	case ID_DISPLAY:
		break;

	case ID_SOUND:
		UI_PopMenu();
		UI_SoundOptionsMenu();
		break;

	case ID_NETWORK:
		UI_PopMenu();
		UI_NetworkOptionsMenu();
		break;

	case ID_BRIGHTNESS:
		trap_Cvar_SetValue( "r_gamma", displayOptionsInfo.brightness.curvalue / 10.0f );
		break;
	
	case ID_SCREENSIZE:
		trap_Cvar_SetValue( "cg_viewsize", displayOptionsInfo.screensize.curvalue * 10 );
		break;

	case ID_FOV:
		// Half-degree steps, retaining cg_fov's existing 4:3 / Hor+ convention.
		displayOptionsInfo.fov.curvalue = Com_Clamp( 2, 320,
			(int)(displayOptionsInfo.fov.curvalue + 0.5f) );
		trap_Cvar_SetValue( "cg_fov", displayOptionsInfo.fov.curvalue * 0.5f );
		break;

	case ID_VSYNC:
		// Stage the toggle; restart only after the explicit Apply action.
		break;

	case ID_APPLY:
		if ( displayOptionsInfo.vsync.curvalue != displayOptionsInfo.initialVsync ) {
			trap_Cvar_SetValue( "r_swapInterval", displayOptionsInfo.vsync.curvalue );
			displayOptionsInfo.initialVsync = displayOptionsInfo.vsync.curvalue;
			trap_Cmd_ExecuteText( EXEC_APPEND, "vid_restart\n" );
		}
		break;

	case ID_BACK:
		UI_PopMenu();
		break;
	}
}

static void UI_DisplayOptionsMenu_Status( void *ptr ) {
	if ( ((menucommon_s *)ptr)->id == ID_FOV ) {
		UI_DrawString( 320, 442, "4:3 base degrees; widescreen expands the view.",
			UI_CENTER|UI_SMALLFONT, text_color_normal );
		UI_DrawString( 320, 458, "FOV changes immediately; mods/servers may limit it.",
			UI_CENTER|UI_SMALLFONT, text_color_normal );
	} else {
		UI_DrawString( 320, 442, "Apply VSync restarts the renderer. Back cancels.",
			UI_CENTER|UI_SMALLFONT, text_color_normal );
		UI_DrawString( 320, 458, "Vulkan Frame Generation overrides VSync while active.",
			UI_CENTER|UI_SMALLFONT, text_color_normal );
	}
}

static void UI_DisplayOptionsMenu_Draw( void ) {
	if ( displayOptionsInfo.vsync.curvalue == displayOptionsInfo.initialVsync )
		displayOptionsInfo.apply.generic.flags |= QMF_GRAYED;
	else
		displayOptionsInfo.apply.generic.flags &= ~QMF_GRAYED;
	Menu_Draw( &displayOptionsInfo.menu );
	UI_DrawString( 520, displayOptionsInfo.fov.generic.y,
		va( "%.1f deg", displayOptionsInfo.fov.curvalue * 0.5f ),
		UI_LEFT|UI_SMALLFONT, text_color_normal );
}

/*
===============
UI_DisplayOptionsMenu_Init
===============
*/
static void UI_DisplayOptionsMenu_Init( void ) {
	int		y;
	static const char *enabled_names[] = { "Off", "On", NULL };

	memset( &displayOptionsInfo, 0, sizeof(displayOptionsInfo) );

	UI_DisplayOptionsMenu_Cache();
	displayOptionsInfo.menu.wrapAround = qtrue;
	displayOptionsInfo.menu.fullscreen = qtrue;
	displayOptionsInfo.menu.draw = UI_DisplayOptionsMenu_Draw;

	displayOptionsInfo.banner.generic.type		= MTYPE_BTEXT;
	displayOptionsInfo.banner.generic.flags		= QMF_CENTER_JUSTIFY;
	displayOptionsInfo.banner.generic.x			= 320;
	displayOptionsInfo.banner.generic.y			= 16;
	displayOptionsInfo.banner.string			= "SYSTEM SETUP";
	displayOptionsInfo.banner.color				= color_white;
	displayOptionsInfo.banner.style				= UI_CENTER;

	displayOptionsInfo.framel.generic.type		= MTYPE_BITMAP;
	displayOptionsInfo.framel.generic.name		= ART_FRAMEL;
	displayOptionsInfo.framel.generic.flags		= QMF_INACTIVE;
	displayOptionsInfo.framel.generic.x			= 0;  
	displayOptionsInfo.framel.generic.y			= 78;
	displayOptionsInfo.framel.width				= 256;
	displayOptionsInfo.framel.height			= 329;

	displayOptionsInfo.framer.generic.type		= MTYPE_BITMAP;
	displayOptionsInfo.framer.generic.name		= ART_FRAMER;
	displayOptionsInfo.framer.generic.flags		= QMF_INACTIVE;
	displayOptionsInfo.framer.generic.x			= 376;
	displayOptionsInfo.framer.generic.y			= 76;
	displayOptionsInfo.framer.width				= 256;
	displayOptionsInfo.framer.height			= 334;

	displayOptionsInfo.graphics.generic.type		= MTYPE_PTEXT;
	displayOptionsInfo.graphics.generic.flags		= QMF_RIGHT_JUSTIFY|QMF_PULSEIFFOCUS;
	displayOptionsInfo.graphics.generic.id			= ID_GRAPHICS;
	displayOptionsInfo.graphics.generic.callback	= UI_DisplayOptionsMenu_Event;
	displayOptionsInfo.graphics.generic.x			= 216;
	displayOptionsInfo.graphics.generic.y			= 240 - 2 * PROP_HEIGHT;
	displayOptionsInfo.graphics.string				= "GRAPHICS";
	displayOptionsInfo.graphics.style				= UI_RIGHT;
	displayOptionsInfo.graphics.color				= color_red;

	displayOptionsInfo.display.generic.type			= MTYPE_PTEXT;
	displayOptionsInfo.display.generic.flags		= QMF_RIGHT_JUSTIFY;
	displayOptionsInfo.display.generic.id			= ID_DISPLAY;
	displayOptionsInfo.display.generic.callback		= UI_DisplayOptionsMenu_Event;
	displayOptionsInfo.display.generic.x			= 216;
	displayOptionsInfo.display.generic.y			= 240 - PROP_HEIGHT;
	displayOptionsInfo.display.string				= "DISPLAY";
	displayOptionsInfo.display.style				= UI_RIGHT;
	displayOptionsInfo.display.color				= color_red;

	displayOptionsInfo.sound.generic.type			= MTYPE_PTEXT;
	displayOptionsInfo.sound.generic.flags			= QMF_RIGHT_JUSTIFY|QMF_PULSEIFFOCUS;
	displayOptionsInfo.sound.generic.id				= ID_SOUND;
	displayOptionsInfo.sound.generic.callback		= UI_DisplayOptionsMenu_Event;
	displayOptionsInfo.sound.generic.x				= 216;
	displayOptionsInfo.sound.generic.y				= 240;
	displayOptionsInfo.sound.string					= "SOUND";
	displayOptionsInfo.sound.style					= UI_RIGHT;
	displayOptionsInfo.sound.color					= color_red;

	displayOptionsInfo.network.generic.type			= MTYPE_PTEXT;
	displayOptionsInfo.network.generic.flags		= QMF_RIGHT_JUSTIFY|QMF_PULSEIFFOCUS;
	displayOptionsInfo.network.generic.id			= ID_NETWORK;
	displayOptionsInfo.network.generic.callback		= UI_DisplayOptionsMenu_Event;
	displayOptionsInfo.network.generic.x			= 216;
	displayOptionsInfo.network.generic.y			= 240 + PROP_HEIGHT;
	displayOptionsInfo.network.string				= "NETWORK";
	displayOptionsInfo.network.style				= UI_RIGHT;
	displayOptionsInfo.network.color				= color_red;

	y = 240 - 3 * (BIGCHAR_HEIGHT+2);
	displayOptionsInfo.brightness.generic.type		= MTYPE_SLIDER;
	displayOptionsInfo.brightness.generic.name		= "Brightness:";
	displayOptionsInfo.brightness.generic.flags		= QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	displayOptionsInfo.brightness.generic.callback	= UI_DisplayOptionsMenu_Event;
	displayOptionsInfo.brightness.generic.id		= ID_BRIGHTNESS;
	displayOptionsInfo.brightness.generic.x			= 400;
	displayOptionsInfo.brightness.generic.y			= y;
	displayOptionsInfo.brightness.minvalue			= 5;
	displayOptionsInfo.brightness.maxvalue			= 20;
	if( !uis.glconfig.deviceSupportsGamma ) {
		displayOptionsInfo.brightness.generic.flags |= QMF_GRAYED;
	}

	y += BIGCHAR_HEIGHT+2;
	displayOptionsInfo.screensize.generic.type		= MTYPE_SLIDER;
	displayOptionsInfo.screensize.generic.name		= "Screen Size:";
	displayOptionsInfo.screensize.generic.flags		= QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	displayOptionsInfo.screensize.generic.callback	= UI_DisplayOptionsMenu_Event;
	displayOptionsInfo.screensize.generic.id		= ID_SCREENSIZE;
	displayOptionsInfo.screensize.generic.x			= 400;
	displayOptionsInfo.screensize.generic.y			= y;
	displayOptionsInfo.screensize.minvalue			= 3;
    displayOptionsInfo.screensize.maxvalue			= 10;

	y += BIGCHAR_HEIGHT+2;
	displayOptionsInfo.vsync.generic.type = MTYPE_SPINCONTROL;
	displayOptionsInfo.vsync.generic.name = "VSync:";
	displayOptionsInfo.vsync.generic.flags = QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	displayOptionsInfo.vsync.generic.callback = UI_DisplayOptionsMenu_Event;
	displayOptionsInfo.vsync.generic.statusbar = UI_DisplayOptionsMenu_Status;
	displayOptionsInfo.vsync.generic.id = ID_VSYNC;
	displayOptionsInfo.vsync.generic.x = 400;
	displayOptionsInfo.vsync.generic.y = y;
	displayOptionsInfo.vsync.itemnames = enabled_names;

	y += BIGCHAR_HEIGHT+2;
	displayOptionsInfo.fov.generic.type = MTYPE_SLIDER;
	displayOptionsInfo.fov.generic.name = "Horizontal FOV:";
	displayOptionsInfo.fov.generic.flags = QMF_PULSEIFFOCUS|QMF_SMALLFONT;
	displayOptionsInfo.fov.generic.callback = UI_DisplayOptionsMenu_Event;
	displayOptionsInfo.fov.generic.statusbar = UI_DisplayOptionsMenu_Status;
	displayOptionsInfo.fov.generic.id = ID_FOV;
	displayOptionsInfo.fov.generic.x = 400;
	displayOptionsInfo.fov.generic.y = y;
	displayOptionsInfo.fov.minvalue = 2;
	displayOptionsInfo.fov.maxvalue = 320;

	y += 2 * (BIGCHAR_HEIGHT+2);
	displayOptionsInfo.apply.generic.type = MTYPE_PTEXT;
	displayOptionsInfo.apply.generic.flags = QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS|QMF_GRAYED;
	displayOptionsInfo.apply.generic.callback = UI_DisplayOptionsMenu_Event;
	displayOptionsInfo.apply.generic.statusbar = UI_DisplayOptionsMenu_Status;
	displayOptionsInfo.apply.generic.id = ID_APPLY;
	displayOptionsInfo.apply.generic.x = 440;
	displayOptionsInfo.apply.generic.y = y;
	displayOptionsInfo.apply.string = "APPLY VSYNC";
	displayOptionsInfo.apply.style = UI_CENTER|UI_SMALLFONT;
	displayOptionsInfo.apply.color = color_red;

	displayOptionsInfo.back.generic.type		= MTYPE_BITMAP;
	displayOptionsInfo.back.generic.name		= ART_BACK0;
	displayOptionsInfo.back.generic.flags		= QMF_LEFT_JUSTIFY|QMF_PULSEIFFOCUS;
	displayOptionsInfo.back.generic.callback	= UI_DisplayOptionsMenu_Event;
	displayOptionsInfo.back.generic.id			= ID_BACK;
	displayOptionsInfo.back.generic.x			= 0;
	displayOptionsInfo.back.generic.y			= 480-64;
	displayOptionsInfo.back.width				= 128;
	displayOptionsInfo.back.height				= 64;
	displayOptionsInfo.back.focuspic			= ART_BACK1;

	Menu_AddItem( &displayOptionsInfo.menu, ( void * ) &displayOptionsInfo.banner );
	Menu_AddItem( &displayOptionsInfo.menu, ( void * ) &displayOptionsInfo.framel );
	Menu_AddItem( &displayOptionsInfo.menu, ( void * ) &displayOptionsInfo.framer );
	Menu_AddItem( &displayOptionsInfo.menu, ( void * ) &displayOptionsInfo.graphics );
	Menu_AddItem( &displayOptionsInfo.menu, ( void * ) &displayOptionsInfo.display );
	Menu_AddItem( &displayOptionsInfo.menu, ( void * ) &displayOptionsInfo.sound );
	Menu_AddItem( &displayOptionsInfo.menu, ( void * ) &displayOptionsInfo.network );
	Menu_AddItem( &displayOptionsInfo.menu, ( void * ) &displayOptionsInfo.brightness );
	Menu_AddItem( &displayOptionsInfo.menu, ( void * ) &displayOptionsInfo.screensize );
	Menu_AddItem( &displayOptionsInfo.menu, ( void * ) &displayOptionsInfo.vsync );
	Menu_AddItem( &displayOptionsInfo.menu, ( void * ) &displayOptionsInfo.fov );
	Menu_AddItem( &displayOptionsInfo.menu, ( void * ) &displayOptionsInfo.apply );
	Menu_AddItem( &displayOptionsInfo.menu, ( void * ) &displayOptionsInfo.back );

	displayOptionsInfo.brightness.curvalue  = trap_Cvar_VariableValue("r_gamma") * 10;
	displayOptionsInfo.screensize.curvalue  = trap_Cvar_VariableValue( "cg_viewsize")/10;
	displayOptionsInfo.initialVsync = trap_Cvar_VariableValue("r_swapInterval") != 0;
	displayOptionsInfo.vsync.curvalue = displayOptionsInfo.initialVsync;
	displayOptionsInfo.fov.curvalue = Com_Clamp( 1, 160, trap_Cvar_VariableValue("cg_fov") ) * 2;
}


/*
===============
UI_DisplayOptionsMenu_Cache
===============
*/
void UI_DisplayOptionsMenu_Cache( void ) {
	trap_R_RegisterShaderNoMip( ART_FRAMEL );
	trap_R_RegisterShaderNoMip( ART_FRAMER );
	trap_R_RegisterShaderNoMip( ART_BACK0 );
	trap_R_RegisterShaderNoMip( ART_BACK1 );
}


/*
===============
UI_DisplayOptionsMenu
===============
*/
void UI_DisplayOptionsMenu( void ) {
	UI_DisplayOptionsMenu_Init();
	UI_PushMenu( &displayOptionsInfo.menu );
	Menu_SetCursorToItem( &displayOptionsInfo.menu, &displayOptionsInfo.display );
}
