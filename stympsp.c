#include <pspkernel.h>
#include <pspaudiolib.h>
#include <pspiofilemgr_dirent.h> 
#include <pspctrl.h>
#include <pspdebug.h>
#include <stdlib.h>
#include <string.h>

#include "StSoundLibrary/StSoundLibrary.h"

/* Define the module info section */
PSP_MODULE_INFO("STSound", 0, 1, 1);

/* Define the main thread's attribute value (optional) */
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

/* Define printf, just to make typing easier */
#define printf	pspDebugScreenPrintf

int __errno=0;

static int done = 0;

/* Exit callback */
int exit_callback(void)
{
	done = 1;
	return 0;
}

/* Callback thread */
void CallbackThread(void *arg)
{
	int cbid;

	cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
	sceKernelRegisterExitCallback(cbid);

	sceKernelSleepThreadCB();
}

/* Sets up the callback thread and returns its thread id */
int SetupCallbacks(void)
{
	int thid = 0;

	thid = sceKernelCreateThread("update_thread", CallbackThread, 0x11, 0xFA0, 0, 0);
	if(thid >= 0)
	{
		sceKernelStartThread(thid, 0, 0);
	}

	return thid;
}

static char **dir_filelist=NULL;
static int dir_size=0;

static io_dirent_t dir;

static void free_directory(void)
{
	int x;
	for (x = 0; x < dir_size; x++)
		free(dir_filelist[x]);
	free(dir_filelist);
}

int myisalnum(int c)
{
	if (c>='A' && c<='Z') return 1;
	if (c>='a' && c<='z') return 1;
	if (c>='0' && c<='9') return 1;
	return 0;
}

static int load_directory(const char *bootfile)
{
	int dfd;
	int count;
	int pathlen;
	int cursize = 32;
	char *path, *p;

	pathlen = strlen(bootfile)+1;
	path = malloc(pathlen);
	if (!path)
		return 0;

	strcpy(path, bootfile);
	p = strrchr(path, '/');
	if (!p)
	{
		free(path);
		return 0;
	}
	p++;
	*p = '\0';

	dir_filelist = malloc(cursize * sizeof *dir_filelist);
	if (!dir_filelist)
	{
		free(path);
		return 0;
	}
	
	count = 0;
	dfd = sceIoDopen(path);
	if (dfd >= 0)
	{

		for (;;)
		{
			int ok;
			dir.name[0]='\0';
			ok = sceIoDread(dfd, &dir);
			if (ok < 0)
				break;
			if (myisalnum(dir.name[0]) && strcmp(dir.name, "EBOOT.PBP"))
			{
				dir_filelist[count] = malloc(pathlen+strlen(dir.name));
				strcpy(dir_filelist[count], path);
				strcat(dir_filelist[count], dir.name);

				count++;
				if (count >= cursize)
				{
					char **tmp;
					cursize *= 2;
					tmp = realloc(dir_filelist, cursize * sizeof *dir_filelist);
					if (!tmp)
					{
						dir_size = count-1;
						free_directory();
						return 0;
					}
					dir_filelist = tmp;
				}
			}
			if (ok == 0)
				break;
		}
		sceIoDclose(dfd);
		dir_size = count;
	}
	free(path);
	return 1;
}

static unsigned short * const screen=(unsigned short *)0x44000000;
void cls(void)
{
	unsigned short *sc;
	int x;
	for (sc = screen, x = 0; x < 272; x++, sc += 512)
		memset(sc, 0, 1024);
}

int min(int x, int y)
{
	return x<y?x:y;
}

int max(int x, int y)
{
	return x>y?x:y;
}

char *select_file(void)
{
	ctrl_data_t pad;
	static int cursel = 0;
	int x = 0;
	int debounce=0;

	pspDebugScreenClear();	
	while (!done)
	{
		pspDebugScreenSetXY(0, 0);
		pspDebugScreenSetBackColor(0x0000);
		pspDebugScreenSetTextColor(0x07C1);
		printf("ST-Sound PSP V1.1 by Jim Shaw\n");
		pspDebugScreenSetTextColor(0xF801);
		printf("X - Select, /\\ - Quit\n");

		pspDebugScreenSetTextColor(0xFFFF);		
		for (x = cursel-7; x < min(dir_size, cursel+7); x++)
		{
			if (x < 0)
			{
				printf("\n");
				continue;
			}

			if (x == cursel)
				pspDebugScreenSetTextColor(0x003F);
			printf("%s\n", strrchr(dir_filelist[x], '/')+1);
			if (x == cursel)
				pspDebugScreenSetTextColor(0xFFFF);
		}
		sceDisplayWaitVblankStart();

    		sceCtrlReadBufferPositive(&pad, 1); 
		
		if (pad.buttons && !debounce)
		{
			if (pad.buttons & CTRL_CROSS)
				return dir_filelist[cursel];

			if (pad.buttons & CTRL_TRIANGLE)
				return NULL;		

			if (pad.buttons & CTRL_UP)
			{
				cursel--;
				if (cursel < 0) cursel = 0;
				debounce = 10;
			} 
			if (pad.buttons & CTRL_DOWN)
			{
				cursel++;
				if (cursel >= dir_size) cursel = dir_size-1;
				debounce = 10;
			} 
		}
		if (debounce) debounce--;
	}
	return NULL;
}

#define VRAM_ADDR	0x04000000
#define SCREEN_WIDTH	480
#define SCREEN_HEIGHT	272

static int playing = 0;
static YMMUSIC *pMusic;

static void sound_callback(void *buf, unsigned int reqn)
{
	if (playing)
	{
		short *s = (short *)buf;
		short *d = (short *)buf;

		ymMusicCompute((void *)pMusic, (ymsample *)buf, reqn);

		s += reqn-1;
		d += reqn*2-1;
		while (reqn--)
		{
			*d = *s;
			*(d-1) = *s;
			s--;
			d -= 2;
		}
	}
	else
	{
		memset(buf, 0, reqn*4);
	}
}

int main(void)
{
	void *mf;
	char *choice;

	pspDebugScreenInit();

	sceDisplaySetMode(0, SCREEN_WIDTH, SCREEN_HEIGHT);
	sceDisplaySetFrameBuf((char*)VRAM_ADDR, 512, 0, 1);

	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(1);

	SetupCallbacks();

	pspAudioInit();
	pspAudioSetChannelCallback(0, (void *)sound_callback);

	if (load_directory(g_elf_name) == 0)
		return 0;

	do
	{
		choice = select_file();
		if (choice)
		{
			pspDebugScreenClear();
			pspDebugScreenSetXY(0, 0);
			printf("Loading Module\n%s\n", strrchr(choice, '/')+1);

			pMusic = ymMusicCreate();

			mf = ymMusicLoad(pMusic, choice);

			if (mf)
			{
				ymMusicInfo_t info;
				ctrl_data_t pad;
				unsigned short mask = 0;

				ymMusicGetInfo(pMusic,&info);

				ymMusicSetLoopMode(pMusic,YMTRUE);
				ymMusicPlay(pMusic);
				playing = 1;

				do
				{
					int x, y;
					unsigned short *scl, *sc;

					cls();
					pspDebugScreenSetXY(0, 0);
					pspDebugScreenSetTextColor(0x07C1);
					printf("Now Playing\n");
					printf("Name.....: %s\n",info.pSongName);
					printf("Author...: %s\n",info.pSongAuthor);
					printf("Comment..: %s\n",info.pSongComment);
					printf("Duration.: %d:%02d\n",(int)info.musicTimeInSec/60,(int)info.musicTimeInSec%60);
					pspDebugScreenSetTextColor(0xF801);
					printf("%s\n", strrchr(choice, '/')+1);
					pspDebugScreenSetTextColor(0x003F);
					printf("Press O to choose another\n");

					sc = screen+110*512;
					for (y = 110; y < 272; y++)
					{
						scl = sc;
						for (x = 0; x < 480; x++)
						{
							*scl++ = (((x+mask)^(mask+y))|(((x+mask)^(mask+y))<<8));
						}
						sc += 512;
					}
					mask++;

					sceDisplayWaitVblankStart();
			    		sceCtrlReadBufferPositive(&pad, 1); 
				} while (!done && !(pad.buttons & CTRL_CIRCLE));

				playing = 0;
				ymMusicStop(pMusic);
			}
			else
			{
				printf("Error loading module!\n");
				sceKernelDelayThread(3000000);

			}
			ymMusicDestroy(pMusic);
		}
	} while (!done && choice);

	free_directory();

	pspAudioEndPre();
	pspAudioEnd();

	sceKernelExitGame();
	return 0;
}
