#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <SDL.h>

#define ASSERT(_e, ...) if (!(_e)) { fprintf(stderr, __VA_ARGS__); exit(1); }

typedef float    f32;
typedef double   f64;
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
typedef size_t   usize;
typedef ssize_t  isize;

#define PI 3.14159265359f
#define TAU (2.0f * PI)
#define PI_2 (PI / 2.0f)
#define PI_4 (PI / 4.0f)

#define DEG2RAD(_d) ((_d) * (PI / 180.0f))
#define RAD2DEG(_d) ((_d) * (180.0f / PI))

#define SCREEN_WIDTH 384
#define SCREEN_HEIGHT 216

#define EYE_Z 1.65f
#define HFOV DEG2RAD(90.0f)
#define VFOV 0.5f

#define ZNEAR 0.0001f
#define ZFAR  128.0f

typedef struct v2_s { f32 x, y; } v2;
typedef struct v2i_s { i32 x, y; } v2i;

#define v2_to_v2i(_v) ({ __typeof__(_v) __v = (_v); (v2i) { __v.x, __v.y }; })
#define v2i_to_v2(_v) ({ __typeof__(_v) __v = (_v); (v2) { __v.x, __v.y }; })

#define dot(_v0, _v1) ({ __typeof__(_v0) __v0 = (_v0), __v1 = (_v1); (__v0.x * __v1.x) + (__v0.y * __v1.y); })
#define length(_vl) ({ __typeof__(_vl) __vl = (_vl); sqrtf(dot(__vl, __vl)); })
#define normalize(_vn) ({ __typeof__(_vn) __vn = (_vn); const f32 l = length(__vn); (__typeof__(_vn)) { __vn.x / l, __vn.y / l }; })
#define min(_a, _b) ({ __typeof__(_a) __a = (_a), __b = (_b); __a < __b ? __a : __b; })
#define max(_a, _b) ({ __typeof__(_a) __a = (_a), __b = (_b); __a > __b ? __a : __b; })
#define clamp(_x, _mi, _ma) (min(max(_x, _mi), _ma))
#define ifnan(_x, _alt) ({ __typeof__(_x) __x = (_x); isnan(__x) ? (_alt) : __x; })

// -1 right, 0 on, 1 left
#define point_side(_p, _a, _b) ({                                              \
        __typeof__(_p) __p = (_p), __a = (_a), __b = (_b);                         \
        -(((__p.x - __a.x) * (__b.y - __a.y))                                  \
            - ((__p.y - __a.y) * (__b.x - __a.x)));                            \
    })

// rotate vector v by angle a
static inline v2 rotate(v2 v, f32 a) {
    return (v2) {
        (v.x * cos(a)) - (v.y * sin(a)),
        (v.x * sin(a)) + (v.y * cos(a)),
    };
}

// see: https://en.wikipedia.org/wiki/Line–line_intersection
// compute intersection of two line segments, returns (NAN, NAN) if there is
// no intersection.
static inline v2 intersect_segs(v2 a0, v2 a1, v2 b0, v2 b1) {
    const f32 d =
        ((a0.x - a1.x) * (b0.y - b1.y))
            - ((a0.y - a1.y) * (b0.x - b1.x));

    if (fabsf(d) < 0.000001f) { return (v2) { NAN, NAN }; }

    const f32
        t = (((a0.x - b0.x) * (b0.y - b1.y))
                - ((a0.y - b0.y) * (b0.x - b1.x))) / d,
        u = (((a0.x - b0.x) * (a0.y - a1.y))
                - ((a0.y - b0.y) * (a0.x - a1.x))) / d;
    return (t >= 0 && t <= 1 && u >= 0 && u <= 1) ?
        ((v2) {
            a0.x + (t * (a1.x - a0.x)),
            a0.y + (t * (a1.y - a0.y)) })
        : ((v2) { NAN, NAN });
}

static inline u32 abgr_mul(u32 col, u32 a) {
    const u32
        br = ((col & 0xFF00FF) * a) >> 8,
        g  = ((col & 0x00FF00) * a) >> 8;

    return 0xFF000000 | (br & 0xFF00FF) | (g & 0x00FF00);
}

struct wall {
    v2i a, b;
    int portal;
};

// sector id for "no sector"
#define SECTOR_NONE 0
#define SECTOR_MAX 128

struct sector {
    int id;
    usize firstwall, nwalls;
    f32 zfloor, zceil;
};

static struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture, *debug;
    u32 *pixels;
    bool quit;

    struct { struct sector arr[32]; usize n; } sectors;
    struct { struct wall arr[128]; usize n; } walls;

    u16 y_lo[SCREEN_WIDTH], y_hi[SCREEN_WIDTH];

    struct {
        v2 pos;
        f32 angle, anglecos, anglesin;
        int sector;
    } camera;

    bool sleepy;
} state;

// convert angle in [-(HFOV / 2)..+(HFOV / 2)] to X coordinate
static inline int screen_angle_to_x(f32 angle) {
    return
        ((int) (SCREEN_WIDTH / 2))
            * (1.0f - tan(((angle + (HFOV / 2.0)) / HFOV) * PI_2 - PI_4));
}

// noramlize angle to +/-PI
static inline f32 normalize_angle(f32 a) {
    return a - (TAU * floor((a + PI) / TAU));
}

// world space -> camera space (translate and rotate)
static inline v2 world_pos_to_camera(v2 p) {
    const v2 u = { p.x - state.camera.pos.x, p.y - state.camera.pos.y };
    return (v2) {
        u.x * state.camera.anglesin - u.y * state.camera.anglecos,
        u.x * state.camera.anglecos + u.y * state.camera.anglesin,
    };
}

static void present();

// load sectors from file -> state
static int load_sectors(const char *path) {
    // sector 0 does not exist
    state.sectors.n = 1;

    FILE *f = fopen(path, "r");
    if (!f) { return -1; }

    int retval = 0;
    enum { SCAN_SECTOR, SCAN_WALL, SCAN_NONE } ss = SCAN_NONE;

    char line[1024], buf[64];
    while (fgets(line, sizeof(line), f)) {
        const char *p = line;
        while (isspace(*p)) {
            p++;
        }

        // skip line, empty or comment
        if (!*p || *p == '#') {
            continue;
        } else if (*p == '[') {
            strncpy(buf, p + 1, sizeof(buf));
            const char *section = strtok(buf, "]");
            if (!section) { retval = -2; goto done; }

            if (!strcmp(section, "SECTOR")) { ss = SCAN_SECTOR; }
            else if (!strcmp(section, "WALL")) { ss = SCAN_WALL; }
            else { retval = -3; goto done; }
        } else {
            switch (ss) {
            case SCAN_WALL: {
                struct wall *wall = &state.walls.arr[state.walls.n++];
                if (sscanf(
                        p,
                        "%d %d %d %d %d",
                        &wall->a.x,
                        &wall->a.y,
                        &wall->b.x,
                        &wall->b.y,
                        &wall->portal)
                        != 5) {
                    retval = -4; goto done;
                }
            }; break;
            case SCAN_SECTOR: {
                struct sector *sector = &state.sectors.arr[state.sectors.n++];
                if (sscanf(
                        p,
                        "%d %zu %zu %f %f",
                        &sector->id,
                        &sector->firstwall,
                        &sector->nwalls,
                        &sector->zfloor,
                        &sector->zceil)
                        != 5) {
                    retval = -5; goto done;
                }
            }; break;
            default: retval = -6; goto done;
            }
        }
    }

    if (ferror(f)) { retval = -128; goto done; }
done:
    fclose(f);
    return retval;
}

static void verline(int x, int y0, int y1, u32 color) {
    for (int y = y0; y <= y1; y++) {
        state.pixels[y * SCREEN_WIDTH + x] = color;
    }
}

// point is in sector if it is on the left side of all walls
static bool point_in_sector(const struct sector *sector, v2 p) {
    for (usize i = 0; i < sector->nwalls; i++) {
        const struct wall *wall = &state.walls.arr[sector->firstwall + i];

        if (point_side(p, v2i_to_v2(wall->a), v2i_to_v2(wall->b)) > 0) {
            return false;
        }
    }

    return true;
}

static void render() {
    for (int i = 0; i < SCREEN_WIDTH; i++) {
        state.y_hi[i] = SCREEN_HEIGHT - 1;
        state.y_lo[i] = 0;
    }

    // track if sector has already been drawn
    bool sectdraw[SECTOR_MAX];
    memset(sectdraw, 0, sizeof(sectdraw));

    // calculate edges of near/far planes (looking down +Y axis)
    const v2
        zdl = rotate(((v2) { 0.0f, 1.0f }), +(HFOV / 2.0f)),
        zdr = rotate(((v2) { 0.0f, 1.0f }), -(HFOV / 2.0f)),
        znl = (v2) { zdl.x * ZNEAR, zdl.y * ZNEAR },
        znr = (v2) { zdr.x * ZNEAR, zdr.y * ZNEAR },
        zfl = (v2) { zdl.x * ZFAR, zdl.y * ZFAR },
        zfr = (v2) { zdr.x * ZFAR, zdr.y * ZFAR };

    enum { QUEUE_MAX = 64 };
    struct queue_entry { int id, x0, x1; };

    struct { struct queue_entry arr[QUEUE_MAX]; usize n; } queue = {
        {{ state.camera.sector, 0, SCREEN_WIDTH - 1 }},
        1
    };

    while (queue.n != 0) {
        // grab tail of queue
        struct queue_entry entry = queue.arr[--queue.n];

        if (sectdraw[entry.id]) {
            continue;
        }

        sectdraw[entry.id] = true;

        const struct sector *sector = &state.sectors.arr[entry.id];

        for (usize i = 0; i < sector->nwalls; i++) {
            const struct wall *wall =
                &state.walls.arr[sector->firstwall + i];

            // translate relative to player and rotate points around player's view
            const v2
                op0 = world_pos_to_camera(v2i_to_v2(wall->a)),
                op1 = world_pos_to_camera(v2i_to_v2(wall->b));

            // wall clipped pos
            v2 cp0 = op0, cp1 = op1;

            // both are negative -> wall is entirely behind player
            if (cp0.y <= 0 && cp1.y <= 0) {
                continue;
            }

            // angle-clip against view frustum
            f32
                ap0 = normalize_angle(atan2(cp0.y, cp0.x) - PI_2),
                ap1 = normalize_angle(atan2(cp1.y, cp1.x) - PI_2);

            // clip against view frustum if both angles are not clearly within
            // HFOV
            if (cp0.y < ZNEAR
                || cp1.y < ZNEAR
                || ap0 > +(HFOV / 2)
                || ap1 < -(HFOV / 2)) {
                const v2
                    il = intersect_segs(cp0, cp1, znl, zfl),
                    ir = intersect_segs(cp0, cp1, znr, zfr);

                // recompute angles if points change
                if (!isnan(il.x)) {
                    cp0 = il;
                    ap0 = normalize_angle(atan2(cp0.y, cp0.x) - PI_2);
                }

                if (!isnan(ir.x)) {
                    cp1 = ir;
                    ap1 = normalize_angle(atan2(cp1.y, cp1.x) - PI_2);
                }
            }

            if (ap0 < ap1) {
                continue;
            }

            if ((ap0 < -(HFOV / 2) && ap1 < -(HFOV / 2))
                || (ap0 > +(HFOV / 2) && ap1 > +(HFOV / 2))) {
                continue;
            }

            // "true" xs before portal clamping
            const int
                tx0 = screen_angle_to_x(ap0),
                tx1 = screen_angle_to_x(ap1);

            // bounds check against portal window
            if (tx0 > entry.x1) { continue; }
            if (tx1 < entry.x0) { continue; }

            const int wallshade =
                16 * (sin(atan2f(
                    wall->b.x - wall->a.x,
                    wall->b.y - wall->b.y)) + 1.0f);

            const int
                x0 = clamp(tx0, entry.x0, entry.x1),
                x1 = clamp(tx1, entry.x0, entry.x1);

            const f32
                z_floor = sector->zfloor,
                z_ceil = sector->zceil,
                nz_floor =
                    wall->portal ? state.sectors.arr[wall->portal].zfloor : 0,
                nz_ceil =
                    wall->portal ? state.sectors.arr[wall->portal].zceil : 0;

            const f32
                sy0 = ifnan((VFOV * SCREEN_HEIGHT) / cp0.y, 1e10),
                sy1 = ifnan((VFOV * SCREEN_HEIGHT) / cp1.y, 1e10);

            const int
                yf0  = (SCREEN_HEIGHT / 2) + (int) (( z_floor - EYE_Z) * sy0),
                yc0  = (SCREEN_HEIGHT / 2) + (int) (( z_ceil  - EYE_Z) * sy0),
                yf1  = (SCREEN_HEIGHT / 2) + (int) (( z_floor - EYE_Z) * sy1),
                yc1  = (SCREEN_HEIGHT / 2) + (int) (( z_ceil  - EYE_Z) * sy1),
                nyf0 = (SCREEN_HEIGHT / 2) + (int) ((nz_floor - EYE_Z) * sy0),
                nyc0 = (SCREEN_HEIGHT / 2) + (int) ((nz_ceil  - EYE_Z) * sy0),
                nyf1 = (SCREEN_HEIGHT / 2) + (int) ((nz_floor - EYE_Z) * sy1),
                nyc1 = (SCREEN_HEIGHT / 2) + (int) ((nz_ceil  - EYE_Z) * sy1),
                txd = tx1 - tx0,
                yfd = yf1 - yf0,
                ycd = yc1 - yc0,
                nyfd = nyf1 - nyf0,
                nycd = nyc1 - nyc0;

            for (int x = x0; x <= x1; x++) {
                int shade = x == x0 || x == x1 ? 192 : (255 - wallshade);

                // calculate progress along x-axis via tx{0,1} so that walls
                // which are partially cut off due to portal edges still have
                // proper heights
                const f32 xp = ifnan((x - tx0) / (f32) txd, 0);

                // get y coordinates for this x
                const int
                    tyf = (int) (xp * yfd) + yf0,
                    tyc = (int) (xp * ycd) + yc0,
                    yf = clamp(tyf, state.y_lo[x], state.y_hi[x]),
                    yc = clamp(tyc, state.y_lo[x], state.y_hi[x]);

                // floor
                if (yf > state.y_lo[x]) {
                    verline(
                        x,
                        state.y_lo[x],
                        yf,
                        0xFFFF0000);
                }

                // ceiling
                if (yc < state.y_hi[x]) {
                    verline(
                        x,
                        yc,
                        state.y_hi[x],
                        0xFF00FFFF);
                }

                if (wall->portal) {
                    const int
                        tnyf = (int) (xp * nyfd) + nyf0,
                        tnyc = (int) (xp * nycd) + nyc0,
                        nyf = clamp(tnyf, state.y_lo[x], state.y_hi[x]),
                        nyc = clamp(tnyc, state.y_lo[x], state.y_hi[x]);

                    verline(
                        x,
                        nyc,
                        yc,
                        abgr_mul(0xFF00FF00, shade));

                    verline(
                        x,
                        yf,
                        nyf,
                        abgr_mul(0xFF0000FF, shade));

                    state.y_hi[x] =
                        clamp(
                            min(min(yc, nyc), state.y_hi[x]),
                            0, SCREEN_HEIGHT - 1);

                    state.y_lo[x] =
                        clamp(
                            max(max(yf, nyf), state.y_lo[x]),
                            0, SCREEN_HEIGHT - 1);
                } else {
                    verline(
                        x,
                        yf,
                        yc,
                        abgr_mul(0xFFD0D0D0, shade));
                }

                if (state.sleepy) {
                    present();
                    SDL_Delay(10);
                }
            }

            if (wall->portal) {
                ASSERT(queue.n != QUEUE_MAX, "out of queue space");
                queue.arr[queue.n++] = (struct queue_entry) {
                    .id = wall->portal,
                    .x0 = x0,
                    .x1 = x1
                };
            }
        }
    }

    state.sleepy = false;
}

static void present() {
    void *px;
    int pitch;
    SDL_LockTexture(state.texture, NULL, &px, &pitch);
    {
        for (usize y = 0; y < SCREEN_HEIGHT; y++) {
            memcpy(
                &((u8*) px)[y * pitch],
                &state.pixels[y * SCREEN_WIDTH],
                SCREEN_WIDTH * 4);
        }
    }
    SDL_UnlockTexture(state.texture);

    SDL_SetRenderTarget(state.renderer, NULL);
    SDL_SetRenderDrawColor(state.renderer, 0, 0, 0, 0xFF);
    SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_NONE);

    SDL_RenderClear(state.renderer);
    SDL_RenderCopyEx(
        state.renderer,
        state.texture,
        NULL,
        NULL,
        0.0,
        NULL,
        SDL_FLIP_VERTICAL);

    SDL_SetTextureBlendMode(state.debug, SDL_BLENDMODE_BLEND);
    SDL_RenderCopy(state.renderer, state.debug, NULL, &((SDL_Rect) { 0, 0, 512, 512 }));
    SDL_RenderPresent(state.renderer);
}

int main(int argc, char *argv[]) {
    ASSERT(
        !SDL_Init(SDL_INIT_VIDEO),
        "SDL failed to initialize: %s",
        SDL_GetError());

    state.window =
        SDL_CreateWindow(
            "raycast",
            SDL_WINDOWPOS_CENTERED_DISPLAY(0),
            SDL_WINDOWPOS_CENTERED_DISPLAY(0),
            1280,
            720,
            0);

    ASSERT(state.window, "failed to create SDL window: %s\n", SDL_GetError());

    state.renderer =
        SDL_CreateRenderer(
            state.window,
            -1,
            SDL_RENDERER_ACCELERATED
            | SDL_RENDERER_PRESENTVSYNC);

    state.texture =
        SDL_CreateTexture(
            state.renderer,
            SDL_PIXELFORMAT_ABGR8888,
            SDL_TEXTUREACCESS_STREAMING,
            SCREEN_WIDTH,
            SCREEN_HEIGHT);
    state.debug =
        SDL_CreateTexture(
            state.renderer,
            SDL_PIXELFORMAT_ABGR8888,
            SDL_TEXTUREACCESS_TARGET,
            128,
            128);

    state.pixels = malloc(SCREEN_WIDTH * SCREEN_HEIGHT * 4);

    state.camera.pos = (v2) { 3, 3 };
    state.camera.angle = 0.0;
    state.camera.sector = 1;

    int ret = 0;
    ASSERT(
        !(ret = load_sectors("res/level.txt")),
        "error while loading sectors: %d",
        ret);
    printf(
        "loaded %zu sectors with %zu walls",
        state.sectors.n,
        state.walls.n);

    while (!state.quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT:
                    state.quit = true;
                    break;
                default:
                    break;
            }
        }

        if (state.quit) {
            break;
        }

        const f32 rot_speed = 3.0f * 0.016f, move_speed = 3.0f * 0.016f;

        const u8 *keystate = SDL_GetKeyboardState(NULL);

        if (keystate[SDLK_RIGHT & 0xFFFF]) {
            state.camera.angle -= rot_speed;
        }

        if (keystate[SDLK_LEFT & 0xFFFF]) {
            state.camera.angle += rot_speed;
        }

        state.camera.anglecos = cos(state.camera.angle);
        state.camera.anglesin = sin(state.camera.angle);

        if (keystate[SDLK_UP & 0xFFFF]) {
            state.camera.pos = (v2) {
                state.camera.pos.x + (move_speed * state.camera.anglecos),
                state.camera.pos.y + (move_speed * state.camera.anglesin),
            };
        }

        if (keystate[SDLK_DOWN & 0xFFFF]) {
            state.camera.pos = (v2) {
                state.camera.pos.x - (move_speed * state.camera.anglecos),
                state.camera.pos.y - (move_speed * state.camera.anglesin),
            };
        }

        if (keystate[SDLK_F1 & 0xFFFF]) {
            state.sleepy = true;
        }

        // update player sector
        {
            // BFS neighbors in a circular queue, player is likely to be in one
            // of the neighboring sectors
            enum { QUEUE_MAX = 64 };
            int
                queue[QUEUE_MAX] = { state.camera.sector },
                i = 0,
                n = 1,
                found = SECTOR_NONE;

            while (n != 0) {
                // get front of queue and advance to next
                const int id = queue[i];
                i = (i + 1) % (QUEUE_MAX);
                n--;

                const struct sector *sector = &state.sectors.arr[id];

                if (point_in_sector(sector, state.camera.pos)) {
                    found = id;
                    break;
                }

                // check neighbors
                for (usize j = 0; j < sector->nwalls; j++) {
                    const struct wall *wall =
                        &state.walls.arr[sector->firstwall + j];

                    if (wall->portal) {
                        if (n == QUEUE_MAX) {
                            fprintf(stderr, "out of queue space!");
                            goto done;
                        }

                        queue[(i + n) % QUEUE_MAX] = wall->portal;
                        n++;
                    }
                }
            }


done:
            if (!found) {
                fprintf(stderr, "player is not in a sector!");
                state.camera.sector = 1;
            } else {
                state.camera.sector = found;
            }
        }

        memset(state.pixels, 0, SCREEN_WIDTH * SCREEN_HEIGHT * 4);
        render();
        if (!state.sleepy) { present(); }
    }

    SDL_DestroyTexture(state.debug);
    SDL_DestroyTexture(state.texture);
    SDL_DestroyRenderer(state.renderer);
    SDL_DestroyWindow(state.window);
    return 0;
}
