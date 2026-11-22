#include "mod/sound.h"

#include <windows.h>

#include <intrin.h>
#include <mmsystem.h>

#include <dsound.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

#include <unordered_map>

#include "common/log.h"
#include "mod/game.h"
#include "mod/patch.h"
#include "mod/sim.h"

namespace sound {
namespace {

Config g_cfg;
CRITICAL_SECTION g_lock;

// A buffer of the game as the game's clock sees it.
struct Buf {
  DWORD bytes = 0;     // length
  DWORD block = 1;     // bytes per sample frame
  DWORD rate = 0;      // bytes per second now (SetFrequency changes it)
  DWORD base_rate = 0; // bytes per second of the format
  bool playing = false;
  bool looping = false;
  uint64_t t0 = 0;     // clock at pos0
  uint64_t pos0 = 0;   // position at t0, bytes
};
std::unordered_map<void*, Buf> g_bufs;

struct Lock {
  Lock() { EnterCriticalSection(&g_lock); }
  ~Lock() { LeaveCriticalSection(&g_lock); }
};

// Position now; ends a one-shot buffer that has played out.
DWORD position(Buf& b) {
  if (!b.bytes) return 0;
  uint64_t pos = b.pos0;
  if (b.playing) {
    pos += (sim::now_ms() - b.t0) * b.rate / 1000;
    if (b.looping) {
      pos %= b.bytes;
    } else if (pos >= b.bytes) {
      b.playing = false;
      b.pos0 = 0;
      pos = 0;
    }
  }
  if (pos >= b.bytes) pos %= b.bytes;
  return static_cast<DWORD>(pos - pos % b.block);
}

void rebase(Buf& b) {
  b.pos0 = position(b);
  b.t0 = sim::now_ms();
}

bool from_game(uintptr_t ra) { return ra >= game::kImageBase && ra < game::kImageBase + 0x00EA7000; }

// --- IDirectSoundBuffer ------------------------------------------------------------
using Release_t = ULONG(WINAPI*)(IDirectSoundBuffer*);
using GetCurrentPosition_t = HRESULT(WINAPI*)(IDirectSoundBuffer*, DWORD*, DWORD*);
using GetStatus_t = HRESULT(WINAPI*)(IDirectSoundBuffer*, DWORD*);
using Play_t = HRESULT(WINAPI*)(IDirectSoundBuffer*, DWORD, DWORD, DWORD);
using SetCurrentPosition_t = HRESULT(WINAPI*)(IDirectSoundBuffer*, DWORD);
using SetFrequency_t = HRESULT(WINAPI*)(IDirectSoundBuffer*, DWORD);
using Stop_t = HRESULT(WINAPI*)(IDirectSoundBuffer*);
Release_t oRelease = nullptr;
GetCurrentPosition_t oGetCurrentPosition = nullptr;
GetStatus_t oGetStatus = nullptr;
Play_t oPlay = nullptr;
SetCurrentPosition_t oSetCurrentPosition = nullptr;
SetFrequency_t oSetFrequency = nullptr;
Stop_t oStop = nullptr;

ULONG WINAPI hkRelease(IDirectSoundBuffer* self) {
  ULONG left = oRelease(self);
  if (left == 0) {
    Lock l;
    g_bufs.erase(self);
  }
  return left;
}

HRESULT WINAPI hkGetCurrentPosition(IDirectSoundBuffer* self, DWORD* play, DWORD* write) {
  {
    Lock l;
    auto it = g_bufs.find(self);
    if (it != g_bufs.end()) {
      Buf& b = it->second;
      DWORD p = position(b);
      if (play) *play = p;
      if (write) {
        DWORD lead = b.rate / 50;  // 20 ms ahead of the play cursor
        DWORD w = b.bytes ? (p + lead - lead % b.block) % b.bytes : 0;
        *write = w;
      }
      return DS_OK;
    }
  }
  return oGetCurrentPosition(self, play, write);
}

HRESULT WINAPI hkGetStatus(IDirectSoundBuffer* self, DWORD* status) {
  HRESULT hr = oGetStatus(self, status);
  if (FAILED(hr) || !status) return hr;
  Lock l;
  auto it = g_bufs.find(self);
  if (it != g_bufs.end()) {
    Buf& b = it->second;
    position(b);
    *status &= ~static_cast<DWORD>(DSBSTATUS_PLAYING | DSBSTATUS_LOOPING);
    if (b.playing) *status |= DSBSTATUS_PLAYING | (b.looping ? DSBSTATUS_LOOPING : 0);
  }
  return hr;
}

HRESULT WINAPI hkPlay(IDirectSoundBuffer* self, DWORD reserved, DWORD priority, DWORD flags) {
  HRESULT hr = oPlay(self, reserved, priority, flags);
  Lock l;
  auto it = g_bufs.find(self);
  if (it != g_bufs.end()) {
    Buf& b = it->second;
    rebase(b);  // (also ends a one-shot that had played out: it starts from 0 again)
    b.playing = true;
    b.looping = (flags & DSBPLAY_LOOPING) != 0;
  }
  return hr;
}

HRESULT WINAPI hkStop(IDirectSoundBuffer* self) {
  {
    Lock l;
    auto it = g_bufs.find(self);
    if (it != g_bufs.end()) {
      Buf& b = it->second;
      rebase(b);
      b.playing = false;
    }
  }
  return oStop(self);
}

HRESULT WINAPI hkSetCurrentPosition(IDirectSoundBuffer* self, DWORD pos) {
  {
    Lock l;
    auto it = g_bufs.find(self);
    if (it != g_bufs.end()) {
      Buf& b = it->second;
      b.pos0 = b.bytes ? pos % b.bytes : 0;
      b.t0 = sim::now_ms();
    }
  }
  return oSetCurrentPosition(self, pos);
}

HRESULT WINAPI hkSetFrequency(IDirectSoundBuffer* self, DWORD freq) {
  {
    Lock l;
    auto it = g_bufs.find(self);
    if (it != g_bufs.end()) {
      Buf& b = it->second;
      rebase(b);
      b.rate = freq ? freq * b.block : b.base_rate;  // 0 = the format's own
    }
  }
  return oSetFrequency(self, freq);
}

void hook_buffer_class(IDirectSoundBuffer* buf) {
  if (oGetStatus) return;
  void** vt = *reinterpret_cast<void***>(buf);
  patch::hook("IDirectSoundBuffer::Release", reinterpret_cast<uintptr_t>(vt[2]), (void*)&hkRelease, (void**)&oRelease);
  patch::hook("IDirectSoundBuffer::GetCurrentPosition", reinterpret_cast<uintptr_t>(vt[4]), (void*)&hkGetCurrentPosition,
              (void**)&oGetCurrentPosition);
  patch::hook("IDirectSoundBuffer::GetStatus", reinterpret_cast<uintptr_t>(vt[9]), (void*)&hkGetStatus, (void**)&oGetStatus);
  patch::hook("IDirectSoundBuffer::Play", reinterpret_cast<uintptr_t>(vt[12]), (void*)&hkPlay, (void**)&oPlay);
  patch::hook("IDirectSoundBuffer::SetCurrentPosition", reinterpret_cast<uintptr_t>(vt[13]), (void*)&hkSetCurrentPosition,
              (void**)&oSetCurrentPosition);
  patch::hook("IDirectSoundBuffer::SetFrequency", reinterpret_cast<uintptr_t>(vt[17]), (void*)&hkSetFrequency,
              (void**)&oSetFrequency);
  patch::hook("IDirectSoundBuffer::Stop", reinterpret_cast<uintptr_t>(vt[18]), (void*)&hkStop, (void**)&oStop);
}

// --- IDirectSound8 -------------------------------------------------------------------
using CreateSoundBuffer_t = HRESULT(WINAPI*)(IDirectSound8*, const DSBUFFERDESC*, IDirectSoundBuffer**, IUnknown*);
using DuplicateSoundBuffer_t = HRESULT(WINAPI*)(IDirectSound8*, IDirectSoundBuffer*, IDirectSoundBuffer**);
CreateSoundBuffer_t oCreateSoundBuffer = nullptr;
DuplicateSoundBuffer_t oDuplicateSoundBuffer = nullptr;
unsigned g_made = 0;

HRESULT WINAPI hkCreateSoundBuffer(IDirectSound8* ds, const DSBUFFERDESC* desc, IDirectSoundBuffer** out, IUnknown* outer) {
  uintptr_t ra = reinterpret_cast<uintptr_t>(_ReturnAddress());
  HRESULT hr = oCreateSoundBuffer(ds, desc, out, outer);
  // Only the game's own buffers (Bink's movie sound keeps real time), never the primary one.
  if (SUCCEEDED(hr) && out && *out && desc && !(desc->dwFlags & DSBCAPS_PRIMARYBUFFER) && desc->lpwfxFormat && from_game(ra)) {
    hook_buffer_class(*out);
    Buf b;
    b.bytes = desc->dwBufferBytes;
    b.block = desc->lpwfxFormat->nBlockAlign ? desc->lpwfxFormat->nBlockAlign : 1;
    b.base_rate = b.rate = desc->lpwfxFormat->nSamplesPerSec * b.block;
    Lock l;
    g_bufs[*out] = b;
    if (++g_made <= 3 || g_made % 200 == 0)
      LOGD("sound: buffer %u of the game: %u bytes, %u Hz, %u bytes a frame", g_made, b.bytes,
           (unsigned)desc->lpwfxFormat->nSamplesPerSec, b.block);
  }
  return hr;
}

HRESULT WINAPI hkDuplicateSoundBuffer(IDirectSound8* ds, IDirectSoundBuffer* from, IDirectSoundBuffer** out) {
  HRESULT hr = oDuplicateSoundBuffer(ds, from, out);
  if (SUCCEEDED(hr) && out && *out) {
    Lock l;
    auto it = g_bufs.find(from);
    if (it != g_bufs.end()) {
      Buf b = it->second;
      b.rate = b.base_rate;
      b.playing = b.looping = false;
      b.pos0 = 0;
      g_bufs[*out] = b;
    }
  }
  return hr;
}

using DirectSoundCreate8_t = HRESULT(WINAPI*)(const GUID*, IDirectSound8**, IUnknown*);
DirectSoundCreate8_t oDirectSoundCreate8 = nullptr;

HRESULT WINAPI hkDirectSoundCreate8(const GUID* device, IDirectSound8** out, IUnknown* outer) {
  if (g_cfg.no_sound) {
    if (out) *out = nullptr;
    return DSERR_NODRIVER;
  }
  HRESULT hr = oDirectSoundCreate8(device, out, outer);
  uintptr_t ra = reinterpret_cast<uintptr_t>(_ReturnAddress());
  LOGI("sound: DirectSoundCreate8 -> 0x%08X, asked from 0x%08X", (unsigned)hr, (unsigned)ra);
  if (SUCCEEDED(hr) && out && *out && !oCreateSoundBuffer && g_cfg.sound_clock) {
    void** vt = *reinterpret_cast<void***>(*out);
    patch::hook("IDirectSound8::CreateSoundBuffer", reinterpret_cast<uintptr_t>(vt[3]), (void*)&hkCreateSoundBuffer,
                (void**)&oCreateSoundBuffer);
    patch::hook("IDirectSound8::DuplicateSoundBuffer", reinterpret_cast<uintptr_t>(vt[5]), (void*)&hkDuplicateSoundBuffer,
                (void**)&oDuplicateSoundBuffer);
  }
  return hr;
}

}  // namespace

void on_frame() {
  if (g_cfg.fast || !oGetCurrentPosition) return;
  // A looping buffer the game keeps filling (music, speech): what is heard is kept within
  // 150 ms of where the game's clock says the buffer is (a game that waited, a card's clock
  // that runs a little off).
  Lock l;
  for (auto& kv : g_bufs) {
    Buf& b = kv.second;
    if (!b.playing || !b.looping || !b.bytes) continue;
    DWORD real = 0;
    auto self = static_cast<IDirectSoundBuffer*>(kv.first);
    if (FAILED(oGetCurrentPosition(self, &real, nullptr))) continue;
    DWORD want = position(b);
    DWORD ahead = (real + b.bytes - want) % b.bytes;
    DWORD off = ahead < b.bytes - ahead ? ahead : b.bytes - ahead;
    if (off > b.rate * 150 / 1000) oSetCurrentPosition(self, want);
  }
}

// [debug] mute: this process's audio session is muted (Windows keeps that per exe path:
// a lab copy's, never the player's game).
void mute_process() {
  CoInitialize(nullptr);
  IMMDeviceEnumerator* devices = nullptr;
  IMMDevice* device = nullptr;
  IAudioSessionManager* manager = nullptr;
  ISimpleAudioVolume* volume = nullptr;
  bool ok = SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                       reinterpret_cast<void**>(&devices))) &&
            SUCCEEDED(devices->GetDefaultAudioEndpoint(eRender, eConsole, &device)) &&
            SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL, nullptr,
                                       reinterpret_cast<void**>(&manager))) &&
            SUCCEEDED(manager->GetSimpleAudioVolume(nullptr, FALSE, &volume)) && SUCCEEDED(volume->SetMute(TRUE, nullptr));
  LOGI("sound: this copy is %s", ok ? "muted ([debug] mute)" : "NOT muted: the audio session could not be reached");
  if (volume) volume->Release();
  if (manager) manager->Release();
  if (device) device->Release();
  if (devices) devices->Release();
}

void install(const Config& cfg) {
  g_cfg = cfg;
  InitializeCriticalSection(&g_lock);
  if (cfg.mute) mute_process();
  patch::hook_api("dsound.dll", "DirectSoundCreate8", (void*)&hkDirectSoundCreate8, (void**)&oDirectSoundCreate8);
}

}  // namespace sound
