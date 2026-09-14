#ifndef IMAGE_LOADER_H_
#define IMAGE_LOADER_H_


/*
=============================================================

IMAGE LOADERS

=============================================================
*/

void R_LoadBMP( const char *name, byte **pic, int *width, int *height );
void R_LoadJPG( const char *name, byte **pic, int *width, int *height );
void R_LoadPCX( const char *name, byte **pic, int *width, int *height );
void R_LoadPNG( const char *name, byte **pic, int *width, int *height );
// PostFX variant reads local files, limits dimensions to 4096, returns malloc-owned pixels.
void R_LoadPostFXPNG( const char *name, byte **pic, int *width, int *height );
void R_LoadTGA( const char *name, byte **pic, int *width, int *height );


#endif
