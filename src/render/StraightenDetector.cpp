#include "StraightenDetector.hpp"
#include <QtMath>
#include <algorithm>
#include <vector>

namespace mylr {

float StraightenDetector::detectAngle(const QImage& image) const {
    if (image.isNull()) return 0.f;

    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    const int w = gray.width(), h = gray.height();
    std::vector<float> angles;

    for (int y = 1; y < h - 1; y += 4) {
        for (int x = 1; x < w - 1; x += 4) {
            const int gx = qGray(gray.pixel(x + 1, y)) - qGray(gray.pixel(x - 1, y));
            const int gy = qGray(gray.pixel(x, y + 1)) - qGray(gray.pixel(x, y - 1));
            const float mag = std::sqrt(float(gx * gx + gy * gy));
            if (mag > 30.f)
                angles.push_back(std::atan2(float(gy), float(gx)) * 180.f / float(M_PI));
        }
    }

    if (angles.empty()) return 0.f;

    std::vector<int> bins(36, 0);
    for (float a : angles) {
        float norm = std::fmod(a + 180.f, 180.f);
        bins[static_cast<int>(norm / 5.f) % 36]++;
    }
    const auto it = std::max_element(bins.begin(), bins.end());
    const int peak = static_cast<int>(std::distance(bins.begin(), it));
    return static_cast<float>(peak * 5 - 90);
}

} // namespace mylr
