/*
 * core.c is part of the Scorched End Audio Library (SEAL) and is licensed
 * under the terms of the GNU Lesser General Public License. See COPYING
 * attached with the library.
 */

#include <stddef.h>
#include <malloc.h>
#include <al/al.h>
#include <al/alc.h>
#include <al/efx.h>
#include <mpg123/mpg123.h>
#include <seal/core.h>
#include <seal/threading.h>
#include <seal/mid.h>
#include <seal/err.h>

/* Defined in err.c. */
extern _seal_tls_t _seal_err;
/* Global lock on OpenAL functions. */
static _seal_lock_t openal_lock;
/* Number of auxiliary sends per source. */
static int neffects_per_src = 1;

LPALGENEFFECTS alGenEffects = 0;
LPALDELETEEFFECTS alDeleteEffects = 0;
LPALISEFFECT alIsEffect = 0;
LPALEFFECTF alEffectf = 0;
LPALEFFECTI alEffecti = 0;
LPALGETEFFECTF alGetEffectf = 0;
LPALGETEFFECTI alGetEffecti = 0;

static int init_ext_proc(void);


const char*
seal_get_verion(void)
{
    return "0.2.0";
}

/*
 * Initializes the specified device and creates a single context. SEAL
 * currently does not make use of multiple contexts.
 */
int
seal_startup(const char* device_name)
{
    ALCdevice* device;
    ALCcontext* context;
    ALint attr[] = { ALC_MAX_AUXILIARY_SENDS, 4, 0, 0 };

    device = alcOpenDevice(device_name);
    SEAL_CHK(device != 0, SEAL_OPEN_DEVICE_FAILED, 0);
    SEAL_CHK_S(alcIsExtensionPresent(device, ALC_EXT_EFX_NAME),
               SEAL_NO_EFX, clean_device);

    context = alcCreateContext(device, attr);
    switch (alcGetError(device)) {
    case ALC_INVALID_VALUE:
        SEAL_ABORT_S(SEAL_CREATE_CONTEXT_FAILED, clean_device);
    case ALC_INVALID_DEVICE:
        SEAL_ABORT_S(SEAL_BAD_DEVICE, clean_device);
    }
    alcMakeContextCurrent(context);

    if (init_ext_proc() == 0)
        goto clean_all;

    /* `mpg123_init' is thread-unsafe. */
    SEAL_CHK_S(mpg123_init() == MPG123_OK && seal_midi_startup() != 0,
               SEAL_INIT_MPG_FAILED, clean_all);

    /* Reset OpenAL's error state. */
    alGetError();
    alcGetError(device);

    alcGetIntegerv(device, ALC_MAX_AUXILIARY_SENDS, 1, &neffects_per_src);

    openal_lock = _seal_create_lock();
    _seal_err = _seal_alloc_tls();
    _seal_set_tls(_seal_err, (void*) SEAL_OK);

    return 1;

clean_all:
    alcMakeContextCurrent(0);
    alcDestroyContext(context);
clean_device:
    alcCloseDevice(device);

    return 0;
}

/* Finalizes the current device and context. */
void
seal_cleanup(void)
{
    ALCdevice* device;
    ALCcontext* context;

    _seal_free_tls(_seal_err);
    _seal_destroy_lock(openal_lock);

    mpg123_exit();

    context = alcGetCurrentContext();
    device = alcGetContextsDevice(context);
    alcMakeContextCurrent(0);
    alcDestroyContext(context);
    alcCloseDevice(device);
}

int
seal_get_neffects_per_src(void)
{
    return neffects_per_src;
}

void
_seal_lock_openal(void)
{
    _seal_lock(openal_lock);
}

void
_seal_unlock_openal(void)
{
    _seal_unlock(openal_lock);
}

void*
_seal_malloc(size_t size)
{
    void* mem;

    mem = malloc(size);
    SEAL_CHK(mem != 0, SEAL_MEM_ALLOC_FAILED, 0);

    return mem;
}

void*
_seal_calloc(size_t count, size_t size)
{
    void* mem;

    mem = calloc(count, size);
    SEAL_CHK(mem != 0, SEAL_MEM_ALLOC_FAILED, 0);

    return mem;
}

void*
_seal_realloc(void* mem, size_t size)
{
    mem = realloc(mem, size);
    SEAL_CHK(mem != 0, SEAL_MEM_ALLOC_FAILED, 0);

    return mem;
}

void
_seal_free(void* mem)
{
    free(mem);
}

static int
init_ext_proc(void)
{
    alGenEffects = (LPALGENEFFECTS) alGetProcAddress("alGenEffects");
    alDeleteEffects = (LPALDELETEEFFECTS) alGetProcAddress("alDeleteEffects");
    alIsEffect = (LPALISEFFECT) alGetProcAddress("alIsEffect");
    alEffectf = (LPALEFFECTF) alGetProcAddress("alEffectf");
    alEffecti = (LPALEFFECTI) alGetProcAddress("alEffecti");
    alGetEffectf = (LPALGETEFFECTF) alGetProcAddress("alGetEffectf");
    alGetEffecti = (LPALGETEFFECTI) alGetProcAddress("alGetEffecti");
    SEAL_CHK(alGenEffects && alDeleteEffects && alIsEffect && alGetEffectf
             && alGetEffecti && alEffectf,
             SEAL_NO_EXT_FUNC, 0);

    return 1;
}
