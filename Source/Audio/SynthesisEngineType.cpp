#include "SynthesisEngineType.h"
#include "../Utils/OnnxRuntime.h"

#if defined(_WIN32) && defined(USE_DIRECTML) && defined(HAVE_ONNXRUNTIME)
#include <onnxruntime_cxx_api.h>
#endif

#ifdef _WIN32
#include <d3d12.h>
#include <dxgi1_2.h>
#include <windows.h>

#ifdef _MSC_VER
#pragma comment(lib, "dxgi.lib")
#endif
#endif

namespace
{
#ifdef _WIN32
/**
 * True when the adapter is an integrated GPU, i.e. D3D12 reports it as a UMA
 * (unified memory) architecture. d3d12.dll is loaded at runtime rather than
 * linked, so a machine without it simply reports "not integrated" and the
 * caller's other checks decide.
 *
 * Returns false via @p ok when the question could not be answered at all
 * (no D3D12 runtime, or the adapter cannot create a device) - an adapter
 * DirectML itself could not use.
 */
bool isIntegratedAdapter(IDXGIAdapter1 *adapter, bool &ok)
{
  ok = false;

  static const auto createDevice = []() -> PFN_D3D12_CREATE_DEVICE
  {
    if (auto *module = LoadLibraryW(L"d3d12.dll"))
      return reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(
          GetProcAddress(module, "D3D12CreateDevice"));
    return nullptr;
  }();

  if (createDevice == nullptr)
    return false;

  ID3D12Device *device = nullptr;
  if (FAILED(createDevice(adapter, D3D_FEATURE_LEVEL_11_0,
                          __uuidof(ID3D12Device),
                          reinterpret_cast<void **>(&device))) ||
      device == nullptr)
    return false;

  D3D12_FEATURE_DATA_ARCHITECTURE arch{};
  arch.NodeIndex = 0;
  const bool gotArch = SUCCEEDED(device->CheckFeatureSupport(
      D3D12_FEATURE_ARCHITECTURE, &arch, sizeof(arch)));
  device->Release();

  if (!gotArch)
    return false;

  ok = true;
  return arch.UMA != FALSE;
}

struct WindowsGpuCapabilities
{
  bool vocoderCapable = false;
  bool singleIntelAdapter = false;
  bool hasHardwareAdapter = false;
};

WindowsGpuCapabilities detectWindowsGpuCapabilities()
{
  IDXGIFactory1 *factory = nullptr;
  if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                reinterpret_cast<void **>(&factory))) ||
      factory == nullptr)
    return {};

  int hardwareAdapters = 0;
  bool loneAdapterNeedsClassic = false;
  bool loneAdapterIsIntel = false;

  for (UINT i = 0;; ++i)
  {
    IDXGIAdapter1 *adapter = nullptr;
    const auto hr = factory->EnumAdapters1(i, &adapter);
    if (hr == DXGI_ERROR_NOT_FOUND)
      break;
    if (FAILED(hr) || adapter == nullptr)
      continue;

    DXGI_ADAPTER_DESC1 desc{};
    if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
        (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0)
    {
      ++hardwareAdapters;
      if (hardwareAdapters == 1)
      {
        bool ok = false;
        const bool integrated = isIntegratedAdapter(adapter, ok);
        loneAdapterIsIntel =
            juce::String(desc.Description).containsIgnoreCase("Intel");
        // An adapter D3D12 cannot open is no better for the vocoder than an
        // integrated one: DirectML would fall back to the CPU either way.
        // Also prefer Classic for a lone Intel adapter, even if its driver
        // does not report UMA. Match the display name without case sensitivity.
        loneAdapterNeedsClassic =
            !ok || integrated || loneAdapterIsIntel;
      }
    }
    adapter->Release();

    if (hardwareAdapters > 1)
      break;
  }

  factory->Release();

  return {hardwareAdapters > 1 ||
              (hardwareAdapters == 1 && !loneAdapterNeedsClassic),
          hardwareAdapters == 1 && loneAdapterIsIntel,
          hardwareAdapters > 0};
}

const WindowsGpuCapabilities &getWindowsGpuCapabilities()
{
  static const auto cached = detectWindowsGpuCapabilities();
  return cached;
}
#endif
} // namespace

SynthesisEngineType defaultSynthesisEngineType()
{
#if JUCE_LINUX
  return SynthesisEngineType::Psola;
#elif defined(_WIN32)
  return getWindowsGpuCapabilities().vocoderCapable
             ? SynthesisEngineType::Vocoder
             : SynthesisEngineType::Psola;
#else
  return SynthesisEngineType::Vocoder;
#endif
}

SynthesisEngineType synthesisEngineTypeForSavedPreference(SynthesisEngineType type)
{
#ifdef _WIN32
  if (getWindowsGpuCapabilities().singleIntelAdapter)
    return SynthesisEngineType::Psola;
#endif
  return type;
}

AiResynthesisSupport getAiResynthesisSupport()
{
#if JUCE_MAC
  return AiResynthesisSupport::Accelerated;
#elif defined(_WIN32) && defined(HAVE_ONNXRUNTIME)
  if (!OnnxRuntime::isAvailable())
    return AiResynthesisSupport::Unavailable;

#if defined(USE_DIRECTML)
  // A DirectML runtime can be installed on a machine with no hardware adapter
  // to run it on; the vocoder then falls back to the CPU.
  try
  {
    for (const auto &provider : Ort::GetAvailableProviders())
      if (provider == "DmlExecutionProvider" &&
          getWindowsGpuCapabilities().hasHardwareAdapter)
        return AiResynthesisSupport::Accelerated;
  }
  catch (const std::exception &)
  {
  }
#endif
  return AiResynthesisSupport::CpuOnly;
#else
  return AiResynthesisSupport::Unavailable;
#endif
}
