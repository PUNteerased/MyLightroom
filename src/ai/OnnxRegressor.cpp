#include "OnnxRegressor.hpp"
#include <QtMath>

#if defined(MYLR_HAS_ONNX) && MYLR_HAS_ONNX
#include <onnxruntime_cxx_api.h>
#endif

namespace mylr {

#if defined(MYLR_HAS_ONNX) && MYLR_HAS_ONNX
struct OnnxRegressor::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "MyLightroom"};
    std::unique_ptr<Ort::Session> session;
};
#endif

OnnxRegressor::OnnxRegressor() {
#if defined(MYLR_HAS_ONNX) && MYLR_HAS_ONNX
    m_impl = new Impl();
#endif
}

OnnxRegressor::~OnnxRegressor() {
#if defined(MYLR_HAS_ONNX) && MYLR_HAS_ONNX
    delete m_impl;
#endif
}

bool OnnxRegressor::loadModel(const QString& path) {
#if defined(MYLR_HAS_ONNX) && MYLR_HAS_ONNX
    try {
        Ort::SessionOptions opts;
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        opts.AppendExecutionProvider_CUDA(OrtCUDAProviderOptions{});
        m_impl->session = std::make_unique<Ort::Session>(m_impl->env, path.toStdWString().c_str(), opts);
        m_loaded = true;
        return true;
    } catch (...) {
        m_loaded = false;
        return false;
    }
#else
    Q_UNUSED(path);
    m_loaded = false;
    return false;
#endif
}

QVector<float> OnnxRegressor::heuristicPredict(const QVector<float>& input) const {
    QVector<float> delta(OutputSize, 0.f);
    if (input.size() < 256) return delta;
    float cdfDiff = 0.f;
    for (int i = 0; i < 128; ++i)
        cdfDiff += input[256 + i];
    delta[2] = cdfDiff * 0.01f;
    delta[3] = cdfDiff * 0.005f;
    return delta;
}

QVector<float> OnnxRegressor::predict(const QVector<float>& input) const {
#if defined(MYLR_HAS_ONNX) && MYLR_HAS_ONNX
    if (!m_loaded || !m_impl->session) return heuristicPredict(input);

    try {
        std::vector<float> in(InputSize, 0.f);
        for (int i = 0; i < qMin(input.size(), InputSize); ++i) in[i] = input[i];

        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<int64_t> shape = {1, InputSize};
        Ort::Value tensor = Ort::Value::CreateTensor<float>(mem, in.data(), in.size(), shape.data(), shape.size());

        const char* inNames[] = {"input"};
        const char* outNames[] = {"output"};
        auto outs = m_impl->session->Run(Ort::RunOptions{nullptr}, inNames, &tensor, 1, outNames, 1);
        float* data = outs[0].GetTensorMutableData<float>();
        const size_t count = outs[0].GetTensorTypeAndShapeInfo().GetElementCount();
        QVector<float> result(static_cast<int>(count));
        for (size_t i = 0; i < count; ++i) result[static_cast<int>(i)] = data[i];
        return result;
    } catch (...) {
        return heuristicPredict(input);
    }
#else
    Q_UNUSED(this);
    return heuristicPredict(input);
#endif
}

void OnnxRegressor::applyDelta(DevelopSettings& s, const QVector<float>& d) const {
    if (d.size() < 12) return;
    s.basic.temp += d[0] * 500.f;
    s.basic.tint += d[1] * 30.f;
    s.basic.exposure += d[2];
    s.basic.contrast += d[3] * 50.f;
    s.basic.highlights += d[4] * 50.f;
    s.basic.shadows += d[5] * 50.f;
    s.basic.whites += d[6] * 50.f;
    s.basic.blacks += d[7] * 50.f;
    s.basic.texture += d[8] * 30.f;
    s.basic.clarity += d[9] * 30.f;
    s.basic.dehaze += d[10] * 30.f;
    s.basic.vibrance += d[11] * 30.f;
    s.basic.temp = qBound(2000.f, s.basic.temp, 50000.f);
    s.basic.exposure = qBound(-5.f, s.basic.exposure, 5.f);
}

} // namespace mylr
