/*
 * listener.c is part of the Scorched End Audio Library (SEAL) and is licensed
 * under the terms of the GNU Lesser General Public License. See COPYING
 * attached with the library.
 */

#include <assert.h>
#include <al/al.h>
#include <seal/listener.h>
#include <seal/core.h>
#include <seal/err.h>

static seal_err_t
setf(int key, float value)
{
    _seal_lock_openal();
    alListenerf(key, value);

    return _seal_get_openal_err();
}

static seal_err_t
set3f(int key, float x, float y, float z)
{
    _seal_lock_openal();
    alListener3f(key, x, y, z);

    return _seal_get_openal_err();
}

static seal_err_t
setfv(int key, float* vector)
{
    assert(vector != 0);

    _seal_lock_openal();
    alListenerfv(key, vector);

    return _seal_get_openal_err();
}

static seal_err_t
getf(int key, float* pvalue)
{
    _seal_lock_openal();
    alGetListenerf(key, pvalue);

    return _seal_get_openal_err();
}

static seal_err_t
get3f(int key, float* px, float* py, float* pz)
{
    assert(px != 0 && py != 0 && pz != 0);

    _seal_lock_openal();
    alGetListener3f(key, px, py, pz);

    return _seal_get_openal_err();
}

static seal_err_t
getfv(int key, float* vector)
{
    assert(vector != 0);

    _seal_lock_openal();
    alGetListenerfv(key, vector);
    if (_seal_get_openal_err() == 0)
        return 0;

    return 1;
}

seal_err_t
seal_set_listener_gain(float gain)
{
    return setf(AL_GAIN, gain);
}

seal_err_t
seal_set_listener_pos(float x, float y, float z)
{
    return set3f(AL_POSITION, x, y, z);
}

seal_err_t
seal_set_listener_vel(float x, float y, float z)
{
    return set3f(AL_VELOCITY, x, y, z);
}

seal_err_t
seal_set_listener_orien(float* orien)
{
    return setfv(AL_ORIENTATION, orien);
}

seal_err_t
seal_get_listener_gain(float* pgain)
{
    return getf(AL_GAIN, pgain);
}

seal_err_t
seal_get_listener_pos(float* px, float* py, float* pz)
{
    return get3f(AL_POSITION, px, py, pz);
}

seal_err_t
seal_get_listener_vel(float* px, float* py, float* pz)
{
    return get3f(AL_VELOCITY, px, py, pz);
}

seal_err_t
seal_get_listener_orien(float* orien)
{
    return getfv(AL_ORIENTATION, orien);
}