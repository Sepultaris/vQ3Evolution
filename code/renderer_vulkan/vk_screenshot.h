#ifndef VK_SCREENSHOT_H_
#define VK_SCREENSHOT_H_

void R_ScreenShotJPEG_f(void);
void R_ScreenShot_f( void );


typedef struct {
	int commandId;
	int x;
	int y;
	int width;
	int height;
	char *fileName;
	qboolean jpeg;
} screenshotCommand_t;

typedef struct {
	int				commandId;
	int				width;
	int				height;
	unsigned char*  captureBuffer;
	unsigned char*  encodeBuffer;
	VkBool32        motionJpeg;
} videoFrameCommand_t;

void RB_TakeVideoFrameCmd( const videoFrameCommand_t * const cmd );
void RB_TakeScreenshot( int width, int height, char *fileName, VkBool32 isJpeg);
void vk_queue_screenshot(const screenshotCommand_t *cmd);
void vk_queue_video_frame(const videoFrameCommand_t *cmd);
void vk_flush_captures(void);
void vk_reset_captures(void);

#endif
