#pragma once

#include "../core/DevelopSettings.hpp"
#include <QVector>
#include <QString>

namespace mylr {

class OnnxRegressor {
public:
    static constexpr int InputSize = 520;
    static constexpr int OutputSize = 32;

    OnnxRegressor();
    ~OnnxRegressor();

    bool loadModel(const QString& path);
    QVector<float> predict(const QVector<float>& input) const;
    void applyDelta(DevelopSettings& settings, const QVector<float>& delta) const;

private:
    QVector<float> heuristicPredict(const QVector<float>& input) const;

    struct Impl;
    struct Impl* m_impl = nullptr;
    bool m_loaded = false;
};

} // namespace mylr
