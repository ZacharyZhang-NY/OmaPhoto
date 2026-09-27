#pragma once

// Sharpening and noise reduction; Amount runs 0 to 150.
struct CameraRawDetailSettings {
    double sharpenAmount = 0;
    double sharpenRadius = 10;
    double sharpenDetail = 25;
    double sharpenMasking = 0;
    double noiseLuminance = 0;
    double noiseLuminanceDetail = 50;
    double noiseLuminanceContrast = 0;
    double noiseColor = 0;
    double noiseColorDetail = 50;
    double noiseColorSmoothness = 50;
    bool adjustsSharpening() const;
    bool adjustsNoise() const;
    bool adjusts() const;
    CameraRawDetailSettings normalized() const;
    friend bool operator==(const CameraRawDetailSettings &, const CameraRawDetailSettings &) = default;
};

// Generic lens corrections: a rendered layer carries no profile.
struct CameraRawOpticsSettings {
    bool removeChromaticAberration = false;
    bool enableLensProfile = false;
    double profileDistortion = 100;
    double profileVignetting = 100;
    // −100 to 100, the Lens Correction filter's sign.
    double distortion = 0;
    double purpleAmount = 0;
    // Degrees, 0 to 360; the low handle stays lower.
    double purpleHueLow = 270;
    double purpleHueHigh = 310;
    double greenAmount = 0;
    double greenHueLow = 60;
    double greenHueHigh = 120;
    // Brightens the corners against lens falloff.
    double vignetteAmount = 0;
    double vignetteMidpoint = 50;
    bool adjusts() const;
    CameraRawOpticsSettings normalized() const;
    // The radial distortion handed to `lens_distort`.
    double distortionK(double profileStrength) const;
    friend bool operator==(const CameraRawOpticsSettings &, const CameraRawOpticsSettings &) = default;
};
