# doomgeneric-vector
## Running

To run it:

`./doom -iwad DOOM.WAD`

Also, run the remote control app to control it for now.</br>
It is found in the remote control folder, run it as so:

`./doomgeneric-vector-input robot-ip-here`

(to compile it, run `gcc doomgeneric-vector-input.c -o doomgeneric-vector-input -lSDL2`)

## Building

On Arch (run this from your home directory):

`wget https://releases.linaro.org/components/toolchain/binaries/latest-7/arm-linux-gnueabi/gcc-linaro-7.5.0-2019.12-x86_64_arm-linux-gnueabi.tar.xz`</br>
`tar xf gcc-linaro-7.5.0-2019.12-x86_64_arm-linux-gnueabi.tar.xz`</br>
`export PATH=~/gcc-linaro-7.5.0-2019.12-x86_64_arm-linux-gnueabi/bin:$PATH`</br>

Then to compile:

`make -f Makefile.vector` (you may need to clean if it doesn't want to compile)

Alternatively, and maybe more correctly, you can use the compiler for vector:

`cd ~`</br>
`mkdir compiler && cd compiler`</br>
`wget https://github.com/os-vector/wire-os-externals/releases/download/5.3.0-r07/vicos-sdk_5.3.0-r07_amd64-linux.tar.gz`</br>
`gunzip vicos-sdk_5.3.0-r07_amd64-linux.tar.gz && tar -xvf vicos-sdk_5.3.0-r07_amd64-linux.tar`</br>
`export PATH=~/compiler/prebuilt/bin:$PATH`</br>

For the vector toolchain, you may need to change the makefile as it is for the one I used above, but both should work.

Get the doom WAD here: https://archive.org/download/theultimatedoom_doom2_doom.wad/DOOM.WAD%20(For%20GZDoom)/

# doomgeneric
The purpose of doomgeneric is to make porting Doom easier.
Of course Doom is already portable but with doomgeneric it is possible with just a few functions.

To try it you will need a WAD file (game data). If you don't own the game, shareware version is freely available (doom1.wad).

# porting
Create a file named doomgeneric_yourplatform.c and just implement these functions to suit your platform.
* DG_Init
* DG_DrawFrame
* DG_SleepMs
* DG_GetTicksMs
* DG_GetKey

|Functions            |Description|
|---------------------|-----------|
|DG_Init              |Initialize your platfrom (create window, framebuffer, etc...).
|DG_DrawFrame         |Frame is ready in DG_ScreenBuffer. Copy it to your platform's screen.
|DG_SleepMs           |Sleep in milliseconds.
|DG_GetTicksMs        |The ticks passed since launch in milliseconds.
|DG_GetKey            |Provide keyboard events.
|DG_SetWindowTitle    |Not required. This is for setting the window title as Doom sets this from WAD file.

### main loop
At start, call doomgeneric_Create().

In a loop, call doomgeneric_Tick().

In simplest form:
```
int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);

    while (1)
    {
        doomgeneric_Tick();
    }
    
    return 0;
}
```

# sound
Sound is much harder to implement! If you need sound, take a look at SDL port. It fully supports sound and music! Where to start? Define FEATURE_SOUND, assign DG_sound_module and DG_music_module.

# platforms
Ported platforms include Windows, X11, SDL, emscripten. Just look at (doomgeneric_win.c, doomgeneric_xlib.c, doomgeneric_sdl.c).
Makefiles provided for each platform.

## emscripten
You can try it directly here:
https://ozkl.github.io/doomgeneric/

emscripten port is based on SDL port, so it supports sound and music! For music, timidity backend is used.

## Windows
![Windows](screenshots/windows.png)

## X11 - Ubuntu
![Ubuntu](screenshots/ubuntu.png)

## X11 - FreeBSD
![FreeBSD](screenshots/freebsd.png)

## SDL
![SDL](screenshots/sdl.png)
