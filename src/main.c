#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <stdbool.h>
#define VRGCLI
#include "../lib/include/vrg.h"

const int SCREEN_WIDTH = 800;
const int SCREEN_HEIGHT = 800;
const Uint32 OSCI_COLOR = 0x00FF00FF; // green

#define S32_MAX 2147483647.0f
int OSC_AMPLITUDE = 390;
int TARG_FPS = 360;
float FADE_RATE = 0.99f;

typedef struct {
    Uint8* pos;
    Uint32 length;
} AudioData;

uint32_t auPos = 0;
uint32_t auPosOld = 0;
Uint32 wavLength;
Uint32* pixels;

//copy pasted from chatgpt because i dont know jack shit about audio
void audio_callback(void* userdata, Uint8* stream, int len) {
    AudioData* audio = (AudioData*)userdata;

    if (audio->length == 0) return;

    len = (len > audio->length ? audio->length : len);
    SDL_memcpy(stream, audio->pos, len);
    audio->pos += len;
    audio->length -= len;
    auPos = (wavLength - audio->length) / (sizeof(Sint32)); //stereo, will unhardcode this later but its getting late
}


int normalize(Sint32 s, int amplitude, int centerPos) {
    float n = s / S32_MAX;
    return centerPos - (int)(n * amplitude);
}

void upd_phosphor(SDL_Renderer* renderer, SDL_Texture* phosphor, float fade) {
    // Update the phosphor texture with the current audio buffer
    if (fade < 0.0f) fade = 0.0f;
    if (fade > 1.0f) fade = 1.0f;

    SDL_SetRenderTarget(renderer, phosphor);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    Uint8 alpha = (Uint8)((1.0f - fade) * 255.0f);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 3);
    SDL_RenderFillRect(renderer, NULL);

    // target not reset so render_osci can draw on the same texture
}

void fadeToBlack(SDL_Renderer *renderer, SDL_Texture* target, Uint8 alpha) {
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        Uint8* c = (Uint8*)&pixels[i];
        c[0] = (Uint8)(c[0] * (0.95)); // B
        c[1] = (Uint8)(c[1] * (0.95)); // G
        c[2] = (Uint8)(c[2] * (0.95)); // R
    }
}

void render_osci(SDL_Renderer* renderer, Uint8* buf, SDL_Texture* phosphor/*, float fade*/) {
    Sint32* samples = (Sint32*)buf;

    if (auPos - auPosOld < 1) {
        //skip rendering all together because we have rendered all available audio
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(renderer, phosphor);
    for (uint32_t i = auPosOld; i < auPos; i += 2) {
        //upd_phosphor(renderer, phosphor, fade);

        
        Sint32 l = samples[i];
        Sint32 r = samples[i + 1];

        // if (l == NULL) l = 0;
        // if (r == NULL) r = 0;

        int x = normalize(-l, OSC_AMPLITUDE, (int)SCREEN_WIDTH/2);
        int y = normalize(r, OSC_AMPLITUDE, (int)SCREEN_HEIGHT/2);

        if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) continue; // outa bounds

        pixels[y * SCREEN_WIDTH + x] = OSCI_COLOR;
    }

    auPosOld = auPos;
}

int win(char* audio) {
    SDL_Window* window = NULL;
    SDL_Renderer* renderer = NULL;

    // init stuff
    if (SDL_Init(SDL_INIT_VIDEO) < 0) { fprintf(stderr, "SDL could not initialize! SDL_Error: %s\n", SDL_GetError()); return 1; }

    SDL_CreateWindowAndRenderer(SCREEN_WIDTH, SCREEN_HEIGHT, SDL_RENDERER_PRESENTVSYNC, &window, &renderer);
    if (window == NULL) { fprintf(stderr, "Window could not be created! SDL_Error: %s\n", SDL_GetError()); return 1; }

    // init bg
    SDL_SetRenderDrawColor(renderer, 0x00, 0x00, 0x00, 255); // black bg
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);

    // load audio
    AudioData sound;
    SDL_AudioSpec wavSpec;
    Uint8* wavBuf;

    if (SDL_LoadWAV(audio, &wavSpec, &wavBuf, &wavLength) == NULL) {
        fprintf(stderr, "could not load wav: %s\n", SDL_GetError());
        exit(1);
    }

    sound.pos = wavBuf;
    sound.length = wavLength;
    wavSpec.callback = audio_callback;
    wavSpec.userdata = &sound;
    // wavSpec.format = AUDIO_S32; // audio file is pcm_s32le
    
    SDL_OpenAudio(&wavSpec, NULL);
    SDL_PauseAudio(0); // start playing

    // main loop

    SDL_Texture* phosphor = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);

    pixels = malloc(sizeof(Uint32) * SCREEN_WIDTH * SCREEN_HEIGHT);
    memset(pixels, 0x0, sizeof(Uint32) * SCREEN_WIDTH * SCREEN_HEIGHT); // start black

    bool quit = false;
    while (sound.length > 0 && quit == false) {
        //Uint32 frameStart = SDL_GetTicks();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = true;
            }
        }
        // stuff goes here
        //upd_phosphor(renderer, phosphor, FADE_RATE);
        static Uint32 lastFadeTime = 0;
        Uint32 currentTime = SDL_GetTicks();
        if (currentTime - lastFadeTime >= (1000 / TARG_FPS)) {
            fadeToBlack(renderer, phosphor, 232);
            lastFadeTime = currentTime;
            render_osci(renderer, wavBuf, phosphor/*, FADE_RATE*/);
            SDL_UpdateTexture(phosphor, NULL, pixels, SCREEN_WIDTH * sizeof(Uint32));
            SDL_RenderCopy(renderer, phosphor, NULL, NULL);
            SDL_RenderPresent(renderer);
        }
        // render_osci(renderer, wavBuf, phosphor/*, FADE_RATE*/);

        //Uint32 delay = (1000 / TARG_FPS) - (SDL_GetTicks() - frameStart);
        //if (delay > 0) SDL_Delay(delay);

        // fade runs at a certain fps to save precision
    }

    //cleanup
    SDL_DestroyWindow(window);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyTexture(phosphor);
    SDL_CloseAudio();
    SDL_FreeWAV(wavBuf);
    free(pixels);
    SDL_Quit();
    return 0;
}

int main(int argc, char* argv[]) {
    vrgcli("osci2 v0.7 - (c) 2025 EnZon3") {
        vrgarg("-h --help\tShow this help") {
            vrgusage();
        }
        vrgarg("-a --amplitude <number>\tset scaling value for the waveform (default: 390)") {
            if (vrgarg == NULL) {
                fprintf(stderr, "enter something\n");
                exit(EXIT_FAILURE);
            }
            int amplitude = atoi(vrgarg);
            if (amplitude <= 0) {
                fprintf(stderr, "invalid amplitude: %s\n", vrgarg);
                exit(EXIT_FAILURE);
            }
            OSC_AMPLITUDE = amplitude;
        }
        vrgarg("-f --fps <number>\tset phosphor FPS (default: 360)") {
            if (vrgarg == NULL) {
                fprintf(stderr, "enter something\n");
                exit(EXIT_FAILURE);
            }
            int fps = atoi(vrgarg);
            if (fps <= 0) {
                fprintf(stderr, "Invalid FPS value: %s\n", vrgarg);
                exit(EXIT_FAILURE);
            }
            TARG_FPS = fps;
        }
        vrgarg("-r --fade-rate <number>\tset phosphor fade rate (default: 0.99)") {
            if (vrgarg == NULL) {
                fprintf(stderr, "enter something\n");
                exit(EXIT_FAILURE);
            }
            float fadeRate = atof(vrgarg);
            if (fadeRate <= 0) {
                fprintf(stderr, "invalid fade rate: %s\n", vrgarg);
                exit(EXIT_FAILURE);
            }
            FADE_RATE = fadeRate;
        }
        vrgarg("-w --wav <file>\tLoad WAV") {
            printf("loading %s\n", vrgarg);

            //assuming in cwd
            FILE* file = fopen(vrgarg, "rb");
            if (file == NULL) {
                fprintf(stderr, "Failed to open file: %s\n", vrgarg);
                exit(EXIT_FAILURE);
            }
            
            if (win(vrgarg) != 0) {
                fprintf(stderr, "an error occurred\n");
                fclose(file);
                exit(EXIT_FAILURE);
            }
            fclose(file);
            printf("done\n");
        }
        vrgarg() {
            vrgusage("try again '%s'\n", vrgarg);
        }
    }
}