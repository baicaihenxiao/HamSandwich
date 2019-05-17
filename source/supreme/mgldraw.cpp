#include "mgldraw.h"
#include "game.h"
#include "sound.h"
#include <random>

#include <SDL2/SDL_syswm.h>
#include <SDL2/SDL_image.h>

MGLDraw *_globalMGLDraw;

int KEY_MAX;
const Uint8* key = SDL_GetKeyboardState(&KEY_MAX);

MGLDraw::MGLDraw(const char *name, int xRes, int yRes, bool windowed)
	: mouse_x(xRes / 2)
	, mouse_y(yRes / 2)
	, mouse_z(0)
	, mouse_b(0)
	, windowed(windowed)
	, readyToQuit(false)
	, xRes(xRes)
	, yRes(yRes)
	, pitch(xRes)
	, tapTrack(0)
	, lastKeyPressed(0)
	, lastRawCode(0)
{
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK);
	SeedRNG();

	if(JamulSoundInit(512))
		SoundSystemExists();

	Uint32 flags = windowed ? 0 : SDL_WINDOW_FULLSCREEN;
	window = SDL_CreateWindow(name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, xRes, yRes, flags);
	if (!window) {
		printf("SDL_CreateWindow: %s\n", SDL_GetError());
		FatalError("Failed to create window");
		return;
	}

	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
	if (!renderer) {
		printf("SDL_CreateRenderer: %s\n", SDL_GetError());
		FatalError("Failed to create renderer");
		return;
	}

	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, xRes, yRes);
	if (!texture) {
		printf("SDL_CreateTexture: %s\n", SDL_GetError());
		FatalError("Failed to create texture");
		return;
	}

	SDL_SetWindowTitle(window, name);
	SDL_ShowCursor(SDL_DISABLE);

	_globalMGLDraw = this;
	scrn = new byte[xRes * yRes];
	thePal = &pal;
	SeedRNG();
}

MGLDraw::~MGLDraw(void)
{
	JamulSoundExit();
	delete[] scrn;
}

int MGLDraw::GetWidth()
{
	return pitch;
}

int MGLDraw::GetHeight()
{
	return yRes;
}

byte *MGLDraw::GetScreen()
{
	return scrn;
}

void MGLDraw::ClearScreen()
{
	memset(scrn, 0, xRes * yRes);
}

void MGLDraw::GetMouse(int *x,int *y)
{
	*x=mouse_x;
	*y=mouse_y;
}

void MGLDraw::SetMouse(int x,int y)
{
	SDL_WarpMouseInWindow(window, x, y);
}

bool MGLDraw::Process(void)
{
	UpdateMusic();
	return (!readyToQuit);
}

void MGLDraw::Quit()
{
	readyToQuit = true;
}

static int makecol32(int r, int g, int b)
{
	return (r << 24) | (g << 16) | (b << 8);
}

inline void MGLDraw::putpixel(int x, int y, int value)
{
	buffer[y * pitch + x] = value;
}

int MGLDraw::FormatPixel(int x,int y)
{
	byte b = scrn[y*xRes+x];
	return makecol32((*thePal)[b].r, (*thePal)[b].g, (*thePal)[b].b);
}

void MGLDraw::PseudoCopy(int y,int x,byte* data,int len)
{
	for(int i = 0; i < len; ++i, ++x)
		putpixel(x, y, makecol32((*thePal)[data[i]].r, (*thePal)[data[i]].g, (*thePal)[data[i]].b));
}

void MGLDraw::StartFlip(void)
{
	void* p;
	SDL_LockTexture(texture, NULL, &p, &pitch);
	buffer = (int*) p;
	pitch /= sizeof(int);
}

void MGLDraw::FinishFlip(void)
{
	SDL_UnlockTexture(texture);
	SDL_RenderCopy(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
	UpdateMusic();
	buffer = nullptr;

	SDL_Event e;
	while(SDL_PollEvent(&e)) {
		if (e.type == SDL_KEYDOWN) {
			ControlKeyDown(e.key.keysym.scancode);
			lastRawCode = e.key.keysym.scancode;
			if (!(e.key.keysym.sym & ~0xff))
			{
				lastKeyPressed = e.key.keysym.sym;
				if (e.key.keysym.mod & (KMOD_LSHIFT | KMOD_RSHIFT))
				{
					lastKeyPressed = toupper(lastKeyPressed);
				}
			}
		} else if (e.type == SDL_KEYUP) {
			ControlKeyUp(e.key.keysym.scancode);
		} else if (e.type == SDL_MOUSEMOTION) {
			mouse_x = e.motion.x;
			mouse_y = e.motion.y;
		} else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
			int flag = 0;
			if (e.button.button == 1)
				flag = 1;
			else if (e.button.button == 3)
				flag = 2;
			if (e.button.state == SDL_PRESSED)
				mouse_b |= flag;
			else
				mouse_b &= ~flag;
		} else if (e.type == SDL_QUIT) {
			readyToQuit = 1;
		} else if (e.type == SDL_WINDOWEVENT) {
			if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
				PauseGame();
			}
		}
	}
}

void MGLDraw::Flip(void)
{
	int i;

	StartFlip();
	// blit to the screen
	for(i=0;i<yRes;i++)
		PseudoCopy(i,0,&scrn[i*xRes],xRes);
	FinishFlip();
}

void MGLDraw::WaterFlip(int v)
{
	int i;
	char table[24]={ 0, 1, 1, 1, 2, 2, 2, 2,
					 2, 2, 1, 1, 0,-1,-1,-1,
					-1,-2,-2,-2,-2,-1,-1,-1};
	v=v%24;

	StartFlip();
	// blit to the screen
	for(i=0;i<yRes;i++)
	{
		if(table[v]==0)
			PseudoCopy(i,0,&scrn[i*xRes],xRes);
		else if(table[v]<0)
		{
			PseudoCopy(i,0,&scrn[i*xRes-table[v]],xRes+table[v]);
			PseudoCopy(i,xRes+table[v],&scrn[i*xRes],-table[v]);
		}
		else
		{
			PseudoCopy(i,0,&scrn[i*xRes+xRes-table[v]],table[v]);
			PseudoCopy(i,table[v],&scrn[i*xRes],xRes-table[v]);
		}
		if(i&1)
		{
			v++;
			if(v>23)
				v=0;
		}
	}
	FinishFlip();
}

void MGLDraw::TeensyFlip(void)
{
	int i,j,x,y;

	x=640/4;
	y=480/4;
	// blit to the screen
	StartFlip();
	for(i=0;i<yRes/2;i++)
	{
		for(j=0;j<xRes/2;j++)
			putpixel(x+j,y,FormatPixel(j*2,i*2));
		y++;
	}
	FinishFlip();
}

void MGLDraw::TeensyWaterFlip(int v)
{
	int i,j,x,y;
	char table[24]={ 0, 1, 1, 1, 2, 2, 2, 2,
					 2, 2, 1, 1, 0,-1,-1,-1,
					-1,-2,-2,-2,-2,-1,-1,-1};
	v=v%24;

	x=640/4;
	y=480/4;
	// blit to the screen
	StartFlip();
	for(i=0;i<yRes/2;i++)
	{
		putpixel(x-1-table[v],y,0);
		putpixel(x+xRes/2-table[v],y,0);
		for(j=0;j<xRes/2;j++)
		{
			putpixel(x+j-table[v],y,FormatPixel(j*2,i*2));
		}
		if(i&1)
		{
			v++;
			if(v>23)
				v=0;
		}
		y++;
	}
	FinishFlip();
}


void MGLDraw::RasterFlip(void)
{
	int i,j;

	// blit to the screen
	StartFlip();
	for(i=0;i<yRes;i++)
	{
		if(!(i&1))
		{
			PseudoCopy(i,0,&scrn[i*xRes],xRes);
		}
		else
		{
			for(j=0;j<xRes;j++)
				putpixel(j,i,0);
		}
	}
	FinishFlip();
}

void MGLDraw::RasterWaterFlip(int v)
{
	int i,j;
	char table[24]={ 0, 1, 1, 1, 2, 2, 2, 2,
					 2, 2, 1, 1, 0,-1,-1,-1,
					-1,-2,-2,-2,-2,-1,-1,-1};
	v=v%24;

	// blit to the screen
	StartFlip();
	for(i=0;i<yRes;i++)
	{
		if(!(i&1))
		{
			if(table[v]==0)
				PseudoCopy(i,0,&scrn[i*xRes],xRes);
			else if(table[v]<0)
			{
				PseudoCopy(i,0,&scrn[i*xRes-table[v]],xRes+table[v]);
				PseudoCopy(i,xRes+table[v],&scrn[i*xRes],-table[v]);
			}
			else
			{
				PseudoCopy(i,0,&scrn[i*xRes+xRes-table[v]],table[v]);
				PseudoCopy(i,table[v],&scrn[i*xRes],xRes-table[v]);
			}
		}
		else
		{
			for(j=0;j<xRes;j++)
				putpixel(j,i,0);
		}
		if(i&1)
		{
			v++;
			if(v>23)
				v=0;
		}
	}
	FinishFlip();
}

void MGLDraw::SetPalette(PALETTE newpal)
{
	memcpy(pal, newpal, sizeof(PALETTE));
}

const RGB *MGLDraw::GetPalette(void)
{
	return pal;
}

void MGLDraw::RealizePalette(void)
{
	thePal = &pal;
}

void MGLDraw::SetSecondaryPalette(PALETTE newpal)
{
	memcpy(pal2, newpal, sizeof(PALETTE));
	thePal = &pal2;
}

// 8-bit graphics only
void MGLDraw::Box(int x,int y,int x2,int y2,byte c)
{
	int i;
	byte noleft=0,noright=0;
	byte notop=0,nobottom=0;

	if(x>x2)
	{
		i=x;
		x=x2;
		x2=i;
	}
	if(y>y2)
	{
		i=y;
		y=y2;
		y2=i;
	}
	if(x<0)
	{
		noleft=1;
		x=0;
	}
	if(x>=xRes)
		return;

	if(y<0)
	{
		notop=1;
		y=0;
	}
	if(y>=yRes)
		return;
	if(x2<0)
		return;
	if(x2>=xRes)
	{
		noright=1;
		x2=xRes-1;
	}
	if(y2<0)
		return;
	if(y2>=yRes)
	{
		nobottom=1;
		y2=yRes-1;
	}

	if(!notop)
		memset(&scrn[x+y*pitch],c,x2-x+1);
	if(!nobottom)
		memset(&scrn[x+y2*pitch],c,x2-x+1);
	for(i=y;i<=y2;i++)
	{
		if(!noleft)
			scrn[x+i*pitch]=c;
		if(!noright)
			scrn[x2+i*pitch]=c;
	}
}

void MGLDraw::FillBox(int x,int y,int x2,int y2,byte c)
{
	int i;

	if(y>=yRes)
		return;

	if(x<0)
		x=0;
	if(x>=xRes)
		return;

	if(y<0)
		y=0;
	if(y>=yRes)
		y=yRes-1;
	if(x2<0)
		return;
	if(x2>=xRes)
		x2=xRes-1;
	if(y2<0)
		return;
	if(y2>=yRes)
		y2=yRes-1;

	for(i=y;i<=y2;i++)
	{
		memset(&scrn[x+i*pitch],c,x2-x+1);
	}
}

void MGLDraw::SelectLineH(int x,int x2,int y,byte ofs)
{
	int i;
	byte col=ofs;
	byte *scr;

	if(x>x2)
	{
		i=x;
		x=x2;
		x2=i;
	}

	if(x<0)
		x=0;
	if(x>=xRes)
		x=xRes-1;
	if(y<0)
		return;
	if(y>=yRes)
		return;
	if(x2<0)
		return;
	if(x2>=xRes)
		x2=xRes-1;

	scr=&scrn[x+y*pitch];
	for(i=x;i<x2;i++)
	{
		if(col<4)
			*scr=31;	// white
		else
			*scr=0;		// black
		col++;
		if(col>7)
			col=0;
		scr++;
	}
}

void MGLDraw::SelectLineV(int x,int y,int y2,byte ofs)
{
	int i;
	byte col=ofs;
	byte *scr;

	if(y>y2)
	{
		i=y;
		y=y2;
		y2=i;
	}

	if(x<0)
		return;
	if(x>=xRes)
		return;
	if(y<0)
		y=0;
	if(y>=yRes)
		y=yRes-1;
	if(y2<0)
		return;
	if(y2>=yRes)
		y2=yRes-1;

	scr=&scrn[x+y*pitch];
	for(i=y;i<y2;i++)
	{
		if(col<4)
			*scr=31;	// white
		else
			*scr=0;		// black
		col++;
		if(col>7)
			col=0;
		scr+=pitch;
	}
}

char MGLDraw::LastKeyPressed(void)
{
	char i = lastKeyPressed;
	lastKeyPressed = 0;
	return i;
}

char MGLDraw::LastKeyPeek(void)
{
	return lastKeyPressed;
}

void MGLDraw::ClearKeys(void)
{
	lastKeyPressed = 0;
}

bool MGLDraw::MouseTap(void)
{
	byte b,mb;

	mb=mouse_b&1;

	if((mb&1) && !(tapTrack&1))
		b=1;
	else
		b=0;

	tapTrack&=2;
	tapTrack|=mb;
	return b;
}

bool MGLDraw::RMouseTap(void)
{
	byte b,mb;

	mb=mouse_b&2;

	if((mb&2) && !(tapTrack&2))
		b=1;
	else
		b=0;

	tapTrack&=1;
	tapTrack|=mb;
	return b;
}

bool MGLDraw::MouseDown(void)
{
	return ((mouse_b&1)!=0);
}

bool MGLDraw::RMouseDown(void)
{
	return ((mouse_b&2)!=0);
}

bool MGLDraw::MouseDown3(void)
{
	return ((mouse_b&4)!=0);
}

bool MGLDraw::LoadBMP(char *name)
{
	return LoadBMP(name, pal);
}

bool MGLDraw::LoadBMP(char *name, PALETTE pal)
{
	int i,w;

	SDL_Surface* b = IMG_Load(name);
	if (!b) {
		printf("%s: %s\n", name, SDL_GetError());
		return false;
	}

	if(pal && b->format->palette)
		for(i=0; i<256 && i < b->format->palette->ncolors; i++)
		{
			pal[i].r = b->format->palette->colors[i].r;
			pal[i].g = b->format->palette->colors[i].g;
			pal[i].b = b->format->palette->colors[i].b;
		}
	RealizePalette();

	w=b->w;
	if(w>SCRWID)
		w=SCRWID;

	SDL_LockSurface(b);
	for(i=0;i<b->h;i++)
	{
		if(i<SCRHEI)
			memcpy(&scrn[i*pitch], &((byte*) b->pixels)[b->pitch * i], w);
	}

	SDL_UnlockSurface(b);
	SDL_FreeSurface(b);
	return true;
}

bool MGLDraw::SaveBMP(char *name)
{
	SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, xRes, yRes, 8, SDL_PIXELFORMAT_INDEX8);
	SDL_LockSurface(surface);
	memcpy(surface->pixels, scrn, xRes * yRes);
	SDL_UnlockSurface(surface);
	memcpy(surface->format->palette->colors, *thePal, sizeof(PALETTE));
	SDL_SaveBMP(surface, name);
	SDL_FreeSurface(surface);
	return true;
}

#ifdef _WIN32
HWND MGLDraw::GetHWnd(void)
{
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	SDL_GetWindowWMInfo(window, &info);
	return info.info.win.window;
}
#endif

//--------------------------------------------------------------------------
// Fatal error

void FatalError(char *msg)
{
	fprintf(stderr, "FATAL: %s\n", msg);
	if (_globalMGLDraw)
		_globalMGLDraw->Quit();
}

//--------------------------------------------------------------------------
// Global RNG

std::mt19937_64 mersenne;

void SeedRNG(void)
{
	mersenne.seed(timeGetTime());
}

dword Random(dword range)
{
	return std::uniform_int_distribution<dword>(0, range - 1)(mersenne);
}
