#include <al/al.h>
#include <al/efx.h>
#include <seal/reverb.h>
#include <seal/core.h>
#include <seal/err.h>
#include <assert.h>

struct seal_reverb_t
{
    unsigned int id;
};

static float get_attr(seal_reverb_t*, int);

seal_reverb_t*
seal_alloc_reverb(void)
{
    seal_reverb_t* reverb;

    reverb = _seal_malloc(sizeof (seal_reverb_t));
    if (reverb == 0)
        return 0;

    _seal_lock_openal();
    alGenEffects(1, &reverb->id);
    SEAL_CHK_AL3_S(AL_OUT_OF_MEMORY, SEAL_ALLOC_EFEFCT_FAILED,
                   AL_INVALID_OPERATION, SEAL_ALLOC_EFEFCT_FAILED,
                   AL_INVALID_VALUE, SEAL_ALLOC_EFEFCT_FAILED, cleanup);

    return reverb;

cleanup:
    _seal_free(reverb);

    return 0;
}

int
seal_free_reverb(seal_reverb_t* reverb)
{
    assert(reverb != 0);

    if (alIsEffect(reverb->id)) {
        _seal_lock_openal();
        alDeleteEffects(1, &reverb->id);
        SEAL_CHK_AL3(AL_OUT_OF_MEMORY, SEAL_FREE_EFFECT_FAILED,
                     AL_INVALID_OPERATION, SEAL_FREE_EFFECT_FAILED,
                     AL_INVALID_VALUE, SEAL_FREE_EFFECT_FAILED, 0);
    }

    _seal_free(reverb);

    return 1;
}

int
seal_set_reverb_density(seal_reverb_t* reverb, float density);

int
seal_set_reverb_diffusion(seal_reverb_t* reverb, float diffusion);

int
seal_set_reverb_gain(seal_reverb_t* reverb, float gain);

int
seal_set_reverb_hfgain(seal_reverb_t* reverb, float hfgain);

int
seal_set_reverb_decay_time(seal_reverb_t* reverb, float decay_time);

int
seal_set_reverb_hfdecay_ratio(seal_reverb_t* reverb, float decay_hfratio);

int
seal_set_reverb_reflections_gain(seal_reverb_t* reverb, float gain);

int
seal_set_reverb_reflections_delay(seal_reverb_t* reverb, float delay);

int
seal_set_reverb_late_gain(seal_reverb_t* reverb, float gain);

int
seal_set_reverb_late_delay(seal_reverb_t* reverb, float delay);

int
seal_set_reverb_air_absorbtion_hfgain(seal_reverb_t* reverb, float hfgain);

int
seal_set_reverb_room_rolloff_factor(seal_reverb_t* reverb, float factor);

int
seal_set_reverb_hfdecay_limited(seal_reverb_t* reverb, int limited);

float
seal_get_reverb_density(seal_reverb_t* reverb)
{
    return get_attr(reverb, AL_REVERB_DENSITY);
}

float
seal_get_reverb_diffusion(seal_reverb_t* reverb)
{
    return get_attr(reverb,  AL_REVERB_DIFFUSION);
}

float
seal_get_reverb_gain(seal_reverb_t* reverb)
{
    return get_attr(reverb,  AL_REVERB_GAIN);
}

float
seal_get_reverb_hfgain(seal_reverb_t* reverb)
{
    return get_attr(reverb,  AL_REVERB_GAINHF);
}

float
seal_get_reverb_decay_time(seal_reverb_t* reverb)
{
    return get_attr(reverb,  AL_REVERB_DECAY_TIME);
}

float
seal_get_reverb_hfdecay_ratio(seal_reverb_t* reverb)
{
    return get_attr(reverb,  AL_REVERB_DECAY_HFRATIO);
}

float
seal_get_reverb_reflections_gain(seal_reverb_t* reverb)
{
    return get_attr(reverb,  AL_REVERB_REFLECTIONS_GAIN);
}

float
seal_get_reverb_reflections_delay(seal_reverb_t* reverb)
{
    return get_attr(reverb,  AL_REVERB_REFLECTIONS_DELAY);
}

float
seal_get_reverb_late_gain(seal_reverb_t* reverb)
{
    return get_attr(reverb,  AL_REVERB_LATE_REVERB_GAIN);
}

float
seal_get_reverb_late_delay(seal_reverb_t* reverb)
{
    return get_attr(reverb,  AL_REVERB_LATE_REVERB_DELAY);
}

float
seal_get_reverb_air_absorbtion_hfgain(seal_reverb_t* reverb)
{
    return get_attr(reverb,  AL_REVERB_ROOM_ROLLOFF_FACTOR);
}

float
seal_get_reverb_room_rolloff_factor(seal_reverb_t* reverb)
{
    return get_attr(reverb,  AL_REVERB_AIR_ABSORPTION_GAINHF);
}

int
seal_is_reverb_hfdecay_limited(seal_reverb_t* reverb)
{
    int attr_value;

    assert(reverb != 0 && alIsEffect(reverb->id));

    alGetEffecti(reverb->id, AL_REVERB_DECAY_HFLIMIT, &attr_value);

    return attr_value;
}

float
get_attr(seal_reverb_t* reverb, int attr_key)
{
    float attr_value;

    assert(reverb != 0 && alIsEffect(reverb->id));

    alGetEffectf(reverb->id, attr_key, &attr_value);

    return attr_value;
}
