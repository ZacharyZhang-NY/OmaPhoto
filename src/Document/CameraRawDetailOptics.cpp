#include "Document/CameraRawDetailOptics.h"
#include "Document/CameraRaw.h"
#include "Document/ImageAdjustments.h"
#include <utility>
extern "C" {
#include "AdjustPixels.h"
}

using ImageAdjustmentPixels::clamp;

bool CameraRawDetailSettings::adjustsSharpening() const
{
    return sharpenAmount != 0;
}

bool CameraRawDetailSettings::adjustsNoise() const
{
    return noiseLuminance != 0 || noiseColor != 0;
}

bool CameraRawDetailSettings::adjusts() const
{
    return adjustsSharpening() || adjustsNoise();
}

CameraRawDetailSettings CameraRawDetailSettings::normalized() const
{
    return {clamp(sharpenAmount, 0, 150, 0),        clamp(sharpenRadius, 0, 100, 10),      clamp(sharpenDetail, 0, 100, 25),
            clamp(sharpenMasking, 0, 100, 0),       clamp(noiseLuminance, 0, 100, 0),      clamp(noiseLuminanceDetail, 0, 100, 50),
            clamp(noiseLuminanceContrast, 0, 100, 0), clamp(noiseColor, 0, 100, 0),        clamp(noiseColorDetail, 0, 100, 50),
            clamp(noiseColorSmoothness, 0, 100, 50)};
}

bool CameraRawOpticsSettings::adjusts() const
{
    return removeChromaticAberration || enableLensProfile || distortion != 0 || purpleAmount != 0 || greenAmount != 0 || vignetteAmount != 0;
}

CameraRawOpticsSettings CameraRawOpticsSettings::normalized() const
{
    CameraRawOpticsSettings result = *this;
    result.profileDistortion = clamp(profileDistortion, 0, 100, 100);
    result.profileVignetting = clamp(profileVignetting, 0, 100, 100);
    result.distortion = clamp(distortion, -100, 100, 0);
    result.purpleAmount = clamp(purpleAmount, 0, 100, 0);
    result.greenAmount = clamp(greenAmount, 0, 100, 0);
    result.vignetteAmount = clamp(vignetteAmount, -100, 100, 0);
    result.vignetteMidpoint = clamp(vignetteMidpoint, 0, 100, 50);
    result.purpleHueLow = clamp(purpleHueLow, 0, 360, 270);
    result.purpleHueHigh = clamp(purpleHueHigh, 0, 360, 310);
    result.greenHueLow = clamp(greenHueLow, 0, 360, 60);
    result.greenHueHigh = clamp(greenHueHigh, 0, 360, 120);
    if (result.purpleHueLow > result.purpleHueHigh)
        std::swap(result.purpleHueLow, result.purpleHueHigh);
    if (result.greenHueLow > result.greenHueHigh)
        std::swap(result.greenHueLow, result.greenHueHigh);
    return result;
}

double CameraRawOpticsSettings::distortionK(double profileStrength) const
{
    const double manual = distortion / 100 * profileStrength;
    const double profile = enableLensProfile ? profileDistortion / 100 * profileStrength : 0;
    return manual + profile;
}

void CameraRawSettings::applyDetailOptics(uchar *pixels, int width, int height, qsizetype stride, double scale, double profileStrength,
                                          bool sharpenMask) const
{
    const CameraRawDetailSettings sharpening = detail.normalized();
    const CameraRawOpticsSettings lens = optics.normalized();
    const size_t w = size_t(width), h = size_t(height), s = size_t(stride);
    if (sharpenMask) {
        adjust_camera_raw_sharpen_mask_overlay(pixels, w, h, s, sharpening.sharpenRadius, sharpening.sharpenDetail, sharpening.sharpenMasking, scale);
        return;
    }
    if (lens.adjusts())
        adjust_camera_raw_optics(pixels, w, h, s, lens.removeChromaticAberration ? 1 : 0, lens.enableLensProfile ? 1 : 0, lens.profileDistortion,
                                 lens.profileVignetting, lens.distortionK(profileStrength), lens.purpleAmount, lens.purpleHueLow, lens.purpleHueHigh,
                                 lens.greenAmount, lens.greenHueLow, lens.greenHueHigh, lens.vignetteAmount, lens.vignetteMidpoint, scale);
    if (sharpening.adjusts())
        adjust_camera_raw_detail(pixels, w, h, s, sharpening.sharpenAmount, sharpening.sharpenRadius, sharpening.sharpenDetail,
                                 sharpening.sharpenMasking, sharpening.noiseLuminance, sharpening.noiseLuminanceDetail,
                                 sharpening.noiseLuminanceContrast, sharpening.noiseColor, sharpening.noiseColorDetail,
                                 sharpening.noiseColorSmoothness, scale);
}
