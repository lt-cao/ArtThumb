#include "Decoders/ThumbnailEffects.h"

#include "Settings/UserSettings.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <vector>

namespace artthumb {

HRESULT ApplyThumbnailSharpness(HBITMAP bitmap, int amount) noexcept {
    if (!bitmap) return E_INVALIDARG;
    amount = std::clamp(amount, kMinimumSharpness, kMaximumSharpness);
    if (amount == 0) return S_FALSE;

    DIBSECTION dib{};
    if (GetObjectW(bitmap, sizeof(dib), &dib) != sizeof(dib) ||
        !dib.dsBm.bmBits || dib.dsBm.bmBitsPixel != 32 ||
        dib.dsBm.bmWidth < 3 || dib.dsBm.bmHeight < 3 ||
        dib.dsBm.bmWidthBytes <= 0) return E_FAIL;

    try {
        const int width = dib.dsBm.bmWidth;
        const int height = dib.dsBm.bmHeight;
        const size_t stride = static_cast<size_t>(dib.dsBm.bmWidthBytes);
        if (stride > SIZE_MAX / static_cast<size_t>(height)) return E_OUTOFMEMORY;
        const size_t byteCount = stride * static_cast<size_t>(height);
        std::vector<unsigned char> source(byteCount);
        std::memcpy(source.data(), dib.dsBm.bmBits, byteCount);
        auto* destination = static_cast<unsigned char*>(dib.dsBm.bmBits);

        // A bounded four-neighbour unsharp mask. At 100%, one quarter of the
        // local high-frequency detail is added; alpha is preserved.
        for (int y = 1; y + 1 < height; ++y) {
            for (int x = 1; x + 1 < width; ++x) {
                const size_t center = static_cast<size_t>(y) * stride +
                                      static_cast<size_t>(x) * 4;
                const size_t left = center - 4;
                const size_t right = center + 4;
                const size_t up = center - stride;
                const size_t down = center + stride;
                const int alpha = source[center + 3];
                for (size_t channel = 0; channel < 3; ++channel) {
                    const int value = source[center + channel];
                    const int detail = value * 4 - source[left + channel] -
                                       source[right + channel] - source[up + channel] -
                                       source[down + channel];
                    const int sharpened = value + detail * amount / 400;
                    destination[center + channel] = static_cast<unsigned char>(
                        std::clamp(sharpened, 0, alpha));
                }
            }
        }
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

} // namespace artthumb
