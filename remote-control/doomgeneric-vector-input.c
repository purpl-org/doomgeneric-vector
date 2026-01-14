#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <SDL2/SDL.h>

#define SERVER_PORT 666

unsigned char sdl_to_doom(SDL_Keycode key) {
    switch (key) {
        case SDLK_UP: return 'i';
        case SDLK_DOWN: return 'k';
        case SDLK_LEFT: return 'j';
        case SDLK_RIGHT: return 'l';
        case SDLK_w: return 'w';
        case SDLK_s: return 's';
        case SDLK_a: return 'a';
        case SDLK_d: return 'd';
        case SDLK_SPACE: return ' ';
        case SDLK_ESCAPE: return 27;
        case SDLK_RETURN: return '\n';
        case SDLK_LCTRL:
        case SDLK_RCTRL: return 17;
        default:
            if (key >= 32 && key < 127) return (unsigned char)key;
            return 0;
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server_ip>\n", argv[0]);
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow(
        "Doom: Vector Edition (Input Window)",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        600, 550,
        SDL_WINDOW_SHOWN
    );

    if (!win) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    printf("Connecting...\n");

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        fprintf(stderr, "socket() failed: %s\n", strerror(errno));
        goto cleanup;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, argv[1], &addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid IP address: %s\n", argv[1]);
        goto cleanup;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "connect() failed: %s\n", strerror(errno));
        goto cleanup;
    }

    unsigned char probe = 0;
    if (send(sock, &probe, 1, 0) < 0) {
        fprintf(stderr, "send() failed: %s\n", strerror(errno));
        goto cleanup;
    }

    struct timeval tv = {
        .tv_sec = 0,
        .tv_usec = 200000
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    unsigned char tmp;
    ssize_t r = recv(sock, &tmp, 1, 0);
    if (r < 0 &&
        (errno == ECONNREFUSED ||
         errno == EHOSTUNREACH ||
         errno == ENETUNREACH)) {
        fprintf(stderr, "Connection failed: %s\n", strerror(errno));
        goto cleanup;
    }

    printf("Connected!\n");

    SDL_Event e;
    int running = 1;
    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT)
                running = 0;

            if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                unsigned char key = sdl_to_doom(e.key.keysym.sym);
                if (key) {
                    unsigned char packet[2] = {
                        e.type == SDL_KEYDOWN ? 1 : 0,
                        key
                    };
                    send(sock, packet, 2, 0);
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
        SDL_Delay(1);
    }

cleanup:
    if (sock >= 0) close(sock);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

